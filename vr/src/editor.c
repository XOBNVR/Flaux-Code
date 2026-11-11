/* Flaux VR: the editor.  Reads your head and hands each frame, works out what you're pointing at,
 * places, grabs and stretches shapes, moves you around, and draws the world. */
#include "editor.h"
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

Cfg cfg = {0.70f, 1.0f, 2.5f, 1.0f, 0.25f, 0.5f, 1, 1, 1, 1, 1, 1};
Ed ed;

const unsigned PALETTE[PALETTE_N] = {
    0xF43F5E, 0xFB923C, 0xFBBF24, 0xFDE047, 0xA3E635, 0x22C55E, 0x14B8A6, 0x22D3EE, 0x38BDF8, 0x3B82F6,
    0x6366F1, 0x8B5CF6, 0xC084FC, 0xF472B6, 0xFDA4AF, 0xF8FAFC, 0xCBD5E1, 0x64748B, 0x1E293B, 0x92400E};
const char *const PALETTE_NAMES[PALETTE_N] = {
    "red", "orange", "gold", "yellow", "lime", "green", "teal", "cyan", "sky", "blue",
    "indigo", "violet", "purple", "pink", "rose", "white", "silver", "grey", "black", "brown"};

#define PLATE 48.0f
#define TRIG_ON 0.6f
#define TRIG_OFF 0.35f

static FrameIn in;
static int trig_down[2], trig_pressed[2], grip_down[2], grip_pressed[2];
static int btn_prev[2][4], btn_hit[2][4];        /* a, b, stick, menu */
static uint32_t grab_id[2], hover_id[2];
static Pose grab_rel[2];
static float grab_spin[2];
static int scaling; static float scale_d0; static V3 scale_s0;
static int panel_grab = -1; static Pose panel_rel;
static Pose panel_stage;                      /* the panel, measured from the play space: it comes along when you walk with the stick */
static int panel_placed, turn_latch;
static float world_buzz[2];
static int ghost_on; static V3 ghost_pos;
static float laser_len[2]; static int laser_hit[2];
static double last_autosave; static unsigned autosave_hash;
static int frames; static double fps_t0;
static int controller_model[2] = {-1, -1};
static int climbing[2]; static V3 climb_from[2];
static int giz_hover[2] = {-1, -1}, giz_hand = -1, giz_axis;
static float giz_start; static V3 giz_pos0, giz_scale0; static Q4 giz_rot0;
static const V3 AXES[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

/* ---------- small helpers ---------- */

void ed_toast(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(ed.toast, sizeof ed.toast, fmt, ap);
    va_end(ap);
    ed.toast_until = ed.time + 3.5;
}

void ed_select(uint32_t id) { ed.selected = id; }

static Pose rig(void) { Pose r = {ed.rig_pos, q4axis(v3(0, 1, 0), ed.rig_yaw)}; return r; }

float ed_front_yaw(void) { return q4yaw(ed.head.q); }

V3 ed_front_point(float dist) {
    float yaw = ed_front_yaw();
    return v3(ed.head.p.x - sinf(yaw) * dist, 0, ed.head.p.z - cosf(yaw) * dist);
}

void ed_summon_panel(void) {
    float yaw = ed_front_yaw();
    V3 fwd = v3(-sinf(yaw), 0, -cosf(yaw));
    ed.main.pose.p = v3add(ed.head.p, v3add(v3mul(fwd, cfg.dist), v3(0, -0.10f, 0)));
    ed.main.pose.q = q4mul(q4axis(v3(0, 1, 0), yaw), q4axis(v3(1, 0, 0), -0.20f));
    ed.main.visible = 1;
    panel_placed = 1;
    panel_stage = pose_mul(pose_inv(rig()), ed.main.pose);
}

void ed_teleport(V3 p) {
    ed.rig_pos.x += p.x - ed.head.p.x;
    ed.rig_pos.z += p.z - ed.head.p.z;
    ed.rig_pos.y = p.y;
}

static V3 obj_color(const Obj *o) { return v3(o->r / 255.0f, o->g / 255.0f, o->b / 255.0f); }
static float shape_height(int shape) { return shape == SH_TORUS ? 0.28f : 1.0f; }
static float snapf(float v, float step) { return roundf(v / step) * step; }

/* ---------- pointing at shapes ---------- */

static int ray_obj(const Obj *o, V3 ro, V3 rd, float *t_out, V3 *n_out) {
    Q4 inv = q4conj(o->rot);
    V3 lo = q4rot(inv, v3sub(ro, o->pos)), ld = q4rot(inv, rd);
    lo = v3(lo.x / o->scale.x, lo.y / o->scale.y, lo.z / o->scale.z);
    ld = v3(ld.x / o->scale.x, ld.y / o->scale.y, ld.z / o->scale.z);
    V3 nl;
    float t;
    if (o->shape == SH_SPHERE) {
        float a = v3dot(ld, ld), b = 2 * v3dot(lo, ld), c = v3dot(lo, lo) - 0.25f;
        float disc = b * b - 4 * a * c;
        if (disc < 0 || a < 1e-12f) return 0;
        t = (-b - sqrtf(disc)) / (2 * a);
        if (t < 0) return 0;
        nl = v3add(lo, v3mul(ld, t));
    } else {
        float h[3] = {0.5f, 0.5f * shape_height(o->shape), 0.5f};
        float o3[3] = {lo.x, lo.y, lo.z}, d3[3] = {ld.x, ld.y, ld.z};
        float tmin = -1e9f, tmax = 1e9f; int axis = 0; float sign = 1;
        for (int i = 0; i < 3; i++) {
            if (fabsf(d3[i]) < 1e-9f) { if (fabsf(o3[i]) > h[i]) return 0; continue; }
            float t1 = (-h[i] - o3[i]) / d3[i], t2 = (h[i] - o3[i]) / d3[i], s = -1;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; s = 1; }
            if (t1 > tmin) { tmin = t1; axis = i; sign = s; }
            if (t2 < tmax) tmax = t2;
        }
        if (tmin > tmax || tmin < 0) return 0;
        t = tmin;
        nl = v3(axis == 0 ? sign : 0, axis == 1 ? sign : 0, axis == 2 ? sign : 0);
    }
    *t_out = t;
    *n_out = v3norm(q4rot(o->rot, v3(nl.x / o->scale.x, nl.y / o->scale.y, nl.z / o->scale.z)));
    return 1;
}

static Obj *pick(V3 ro, V3 rd, float max_t, float *t_out, V3 *n_out) {
    Obj *best = NULL; float bt = max_t; V3 bn = v3(0, 1, 0);
    for (int i = 0; i < g_obj_count; i++) {
        float t; V3 n;
        if (ray_obj(&g_obj[i], ro, rd, &t, &n) && t < bt) { best = &g_obj[i]; bt = t; bn = n; }
    }
    if (best) { *t_out = bt; *n_out = bn; }
    return best;
}

static int ray_plate(V3 ro, V3 rd, float *t) {
    if (rd.y > -1e-4f || ro.y <= 0) return 0;
    *t = -ro.y / rd.y;
    V3 p = v3add(ro, v3mul(rd, *t));
    return fabsf(p.x) <= PLATE * 0.5f && fabsf(p.z) <= PLATE * 0.5f && *t < 60.0f;
}

/* where a new shape would go if this hand pulled the trigger now */
static V3 placement(int h, V3 scale, int shape) {
    V3 ro = ed.aim[h].p, rd = q4rot(ed.aim[h].q, v3(0, 0, -1));
    V3 ext = v3(scale.x, scale.y * shape_height(shape), scale.z);
    float t, tp; V3 n, pos;
    Obj *o = pick(ro, rd, 20.0f, &t, &n);
    int plate = ray_plate(ro, rd, &tp);
    if (o && (!plate || t < tp)) {
        float out = 0.5f * (fabsf(n.x) * ext.x + fabsf(n.y) * ext.y + fabsf(n.z) * ext.z);
        pos = v3add(v3add(ro, v3mul(rd, t)), v3mul(n, out));
    } else if (plate) {
        pos = v3add(ro, v3mul(rd, tp)); pos.y = ext.y * 0.5f;
    } else {
        pos = v3add(ro, v3mul(rd, 1.5f));
    }
    if (cfg.snap) {
        pos.x = snapf(pos.x, cfg.snap_size); pos.z = snapf(pos.z, cfg.snap_size);
        pos.y = ext.y * 0.5f + snapf(pos.y - ext.y * 0.5f, cfg.snap_size);
    }
    return pos;
}

/* ---------- things you can do to the selection ---------- */

void ed_delete_selected(void) {
    Obj *o = scene_find(ed.selected);
    if (!o) { ed_toast("Nothing is selected."); return; }
    if (o->flags & OBJ_LOCKED) { ed_toast("%s is locked. Unlock it first.", o->name); plat_sound(SND_ERROR); return; }
    char name[24]; snprintf(name, sizeof name, "%s", o->name);
    scene_remove(o->id);
    ed.selected = 0;
    scene_commit("Delete");
    ed_toast("Deleted %s. Press A to undo.", name);
    plat_sound(SND_DELETE);
}

void ed_duplicate_selected(void) {
    Obj *o = scene_find(ed.selected);
    if (!o) { ed_toast("Nothing is selected."); return; }
    Obj src = *o;
    Obj *d = scene_add(src.shape, v3add(src.pos, v3(src.scale.x, 0, 0)), src.rot, src.scale, src.r, src.g, src.b);
    if (!d) { ed_toast("The scene is full (%d shapes).", MAX_OBJ); return; }
    ed.selected = d->id;
    scene_commit("Duplicate");
    ed_toast("Made a copy: %s.", d->name);
    plat_sound(SND_PLACE);
}

/* ---------- projects and settings ---------- */

static void project_path(char *out, size_t n, const char *name) { snprintf(out, n, "%s/projects/%s.fvr", ed.data_dir, name); }

static void clean_name(char *out, size_t n, const char *name) {
    size_t k = 0;
    for (; *name && k + 1 < n; name++)
        if ((*name >= 'a' && *name <= 'z') || (*name >= 'A' && *name <= 'Z') || (*name >= '0' && *name <= '9') || *name == ' ' ||
            *name == '-' || *name == '_')
            out[k++] = *name;
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}

int ed_project_save(const char *name) {
    char clean[32], path[400];
    clean_name(clean, sizeof clean, name);
    if (!clean[0]) { ed_toast("Give the project a name first."); plat_sound(SND_ERROR); return 0; }
    project_path(path, sizeof path, clean);
    if (!scene_save(path)) { ed_toast("Couldn't save %s.", clean); plat_sound(SND_ERROR); return 0; }
    snprintf(ed.project, sizeof ed.project, "%s", clean);
    g_scene_dirty = 0;
    ed_save_settings();
    ed_toast("Saved %s (%d shapes).", clean, g_obj_count);
    plat_sound(SND_OPEN);
    return 1;
}

int ed_project_open(const char *name) {
    char path[400];
    project_path(path, sizeof path, name);
    if (!scene_load(path)) { ed_toast("Couldn't open %s.", name); plat_sound(SND_ERROR); return 0; }
    snprintf(ed.project, sizeof ed.project, "%s", name);
    ed.selected = 0;
    ed_save_settings();
    ed_toast("Opened %s (%d shapes).", name, g_obj_count);
    plat_sound(SND_OPEN);
    return 1;
}

void ed_project_new(void) {
    scene_clear();
    scene_commit("New project");
    g_scene_dirty = 0;
    ed.selected = 0;
    snprintf(ed.project, sizeof ed.project, "My world");
    ed_toast("Started a new, empty project.");
}

int ed_project_delete(const char *name) {
    char path[400];
    project_path(path, sizeof path, name);
    if (remove(path) != 0) { ed_toast("Couldn't delete %s.", name); return 0; }
    ed_toast("Deleted the saved project %s.", name);
    plat_sound(SND_DELETE);
    return 1;
}

int ed_project_list(char names[][32], int max) {
    char dir[320];
    snprintf(dir, sizeof dir, "%s/projects", ed.data_dir);
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t len = strlen(e->d_name);
        if (len < 5 || len > 35 || strcmp(e->d_name + len - 4, ".fvr") != 0) continue;
        memcpy(names[n], e->d_name, len - 4); names[n][len - 4] = 0;
        n++;
    }
    closedir(d);
    for (int i = 1; i < n; i++)                       /* A to Z */
        for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) {
            char t[32]; memcpy(t, names[j], 32); memcpy(names[j], names[j - 1], 32); memcpy(names[j - 1], t, 32);
        }
    return n;
}

int ed_project_count(const char *name) {      /* how many shapes a saved project holds */
    char path[400], line[256];
    project_path(path, sizeof path, name);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "obj ", 4)) n++;
    fclose(f);
    return n;
}

int ed_project_export(void) {
    char clean[32], path[400];
    clean_name(clean, sizeof clean, ed.project);
    if (!clean[0]) snprintf(clean, sizeof clean, "My world");
    snprintf(path, sizeof path, "%s/exports/%s.flx", ed.data_dir, clean);
    if (!scene_export_flx(path, clean)) { ed_toast("Couldn't write the Flaux program."); plat_sound(SND_ERROR); return 0; }
    ed_toast("Wrote exports/%s.flx: open it in Flaux Studio.", clean);
    plat_sound(SND_OPEN);
    return 1;
}

void ed_save_settings(void) {
    char path[320];
    snprintf(path, sizeof path, "%s/settings.txt", ed.data_dir);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "dist %.3f\nscale %.3f\nspeed %.3f\nsky %.3f\nsnap_size %.3f\nbuild_size %.3f\n", (double)cfg.dist, (double)cfg.scale,
            (double)cfg.speed, (double)cfg.sky, (double)cfg.snap_size, (double)cfg.build_size);
    fprintf(f, "snap_turn %d\ngrid %d\nsnap %d\nhaptics %d\nsound %d\nshadows %d\ncolor %d\n", cfg.snap_turn, cfg.grid, cfg.snap,
            cfg.haptics, cfg.sound, cfg.shadows, ed.color);
    fprintf(f, "project %s\nplayer %s\n", ed.project, ed.player);
    if (ed.server[0]) fprintf(f, "server %s\n", ed.server);
    if (ed.room_code[0]) fprintf(f, "room %s\n", ed.room_code);
    fclose(f);
}

static void load_settings(void) {
    char path[320], line[160];
    snprintf(path, sizeof path, "%s/settings.txt", ed.data_dir);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *v = strchr(line, ' ');
        if (!v) continue;
        *v++ = 0;
        float x = strtof(v, NULL);
        if (!strcmp(line, "dist")) cfg.dist = fclamp(x, 0.35f, 1.3f);
        else if (!strcmp(line, "scale")) cfg.scale = fclamp(x, 0.6f, 1.6f);
        else if (!strcmp(line, "speed")) cfg.speed = fclamp(x, 0.5f, 8.0f);
        else if (!strcmp(line, "sky")) cfg.sky = fclamp(x, 0.2f, 2.0f);
        else if (!strcmp(line, "snap_size")) cfg.snap_size = fclamp(x, 0.05f, 1.0f);
        else if (!strcmp(line, "build_size")) cfg.build_size = fclamp(x, 0.1f, 3.0f);
        else if (!strcmp(line, "snap_turn")) cfg.snap_turn = x != 0;
        else if (!strcmp(line, "grid")) cfg.grid = x != 0;
        else if (!strcmp(line, "snap")) cfg.snap = x != 0;
        else if (!strcmp(line, "haptics")) cfg.haptics = x != 0;
        else if (!strcmp(line, "sound")) cfg.sound = x != 0;
        else if (!strcmp(line, "shadows")) cfg.shadows = x != 0;
        else if (!strcmp(line, "color")) ed.color = (int)fclamp(x, 0, PALETTE_N - 1);
        else if (!strcmp(line, "project")) snprintf(ed.project, sizeof ed.project, "%s", v);
        else if (!strcmp(line, "player")) snprintf(ed.player, sizeof ed.player, "%s", v);
        else if (!strcmp(line, "server")) snprintf(ed.server, sizeof ed.server, "%s", v);
        else if (!strcmp(line, "room")) snprintf(ed.room_code, sizeof ed.room_code, "%s", v);
    }
    fclose(f);
}

static unsigned scene_hash(void) {
    unsigned h = 2166136261u;
    const unsigned char *p = (const unsigned char *)g_obj;
    for (size_t i = 0; i < sizeof(Obj) * (size_t)g_obj_count; i++) h = (h ^ p[i]) * 16777619u;
    return h ^ (unsigned)g_obj_count;
}

static void autosave(void) {
    unsigned h = scene_hash();
    if (h == autosave_hash) return;
    char path[320];
    snprintf(path, sizeof path, "%s/autosave.fvr", ed.data_dir);
    int dirty = g_scene_dirty;
    if (scene_save(path)) autosave_hash = h;
    g_scene_dirty = dirty;
}

void app_pause(void) { autosave(); ed_save_settings(); }

/* ---------- start ---------- */

void app_init(const char *data_dir) {
    memset(&ed, 0, sizeof ed);
    snprintf(ed.data_dir, sizeof ed.data_dir, "%s", data_dir);
    snprintf(ed.project, sizeof ed.project, "My world");
    snprintf(ed.player, sizeof ed.player, "Player");
    snprintf(ed.room_code, sizeof ed.room_code, "FLAUX");
    ed.tool = -1; ed.color = 7;
    ed.rig_pos = v3(0, 0, 2.0f);
    ed.v_yaw = 0.6f; ed.v_pitch = 0.45f; ed.v_dist = 9.0f; ed.v_target = v3(0, 0.8f, 0);
    char dir[320];
    mkdir(ed.data_dir, 0775);
    snprintf(dir, sizeof dir, "%s/projects", ed.data_dir); mkdir(dir, 0775);
    snprintf(dir, sizeof dir, "%s/exports", ed.data_dir); mkdir(dir, 0775);
    load_settings();

    gfx_init();
    ui_init();
    scene_init();
    net_init(ed.player);
    rt_create(&ed.viewer, 1308, 816, 4);
    gfx_thumbnails(&ed.thumbs);
    controller_model[0] = gfx_model_load("controllers/left.fxm");
    controller_model[1] = gfx_model_load("controllers/right.fxm");
    if (controller_model[0] < 0 || controller_model[1] < 0) plat_log("controller models missing: drawing simple ones");

    ed.main.pw = PW; ed.main.ph = PH; ed.kb.pw = KW; ed.kb.ph = KH;
    ed.side.pw = SW; ed.side.ph = SH; ed.side.pose = pose_ident();
    ed.main.pose = pose_ident(); ed.kb.pose = pose_ident();

    snprintf(dir, sizeof dir, "%s/autosave.fvr", ed.data_dir);
    if (scene_load(dir)) { g_scene_dirty = 0; plat_log("picked up where you left off: %d shapes", g_obj_count); }
    autosave_hash = scene_hash();
    copilot_say(0, "Hi! Tell me what to build: \"a tower of 6 red cubes\", \"a ring of 12 balls\", \"a castle\".");
}

/* ---------- each frame ---------- */

static void read_input(const FrameIn *fi) {
    in = *fi;
    ed.dt = fclamp(in.dt, 0.0005f, 0.1f);
    ed.time = in.time;
    Pose r = rig();
    ed.head = pose_mul(r, in.head);
    for (int h = 0; h < 2; h++) {
        HandIn *hi = &in.hand[h];
        ed.aim[h] = pose_mul(r, hi->aim);
        ed.grip[h] = pose_mul(r, hi->grip);
        int was = trig_down[h];
        if (!hi->active) trig_down[h] = 0;
        else if (hi->trigger > TRIG_ON) trig_down[h] = 1;
        else if (hi->trigger < TRIG_OFF) trig_down[h] = 0;
        trig_pressed[h] = trig_down[h] && !was;
        int gwas = grip_down[h];
        if (!hi->active) grip_down[h] = 0;
        else if (hi->squeeze > TRIG_ON) grip_down[h] = 1;
        else if (hi->squeeze < TRIG_OFF) grip_down[h] = 0;
        grip_pressed[h] = grip_down[h] && !gwas;
        int now[4] = {hi->a, hi->b, hi->stick, hi->menu};
        for (int i = 0; i < 4; i++) { btn_hit[h][i] = hi->active && now[i] && !btn_prev[h][i]; btn_prev[h][i] = now[i]; }

        Pointer *p = &ui_ptr[h];
        p->active = hi->active;
        p->origin = ed.aim[h].p; p->dir = q4rot(ed.aim[h].q, v3(0, 0, -1));
        p->released = was && !trig_down[h];
        p->down = trig_down[h]; p->pressed = trig_pressed[h];
        p->sx = hi->sx; p->sy = hi->sy;
    }
}

static void place_panels(void) {
    if (!panel_placed && ed.head.p.y - ed.rig_pos.y > 0.3f) ed_summon_panel();
    ed.main.w_m = 0.86f * cfg.scale; ed.main.h_m = ed.main.w_m * PH / PW;
    if (panel_placed) ed.main.pose = pose_mul(rig(), panel_stage);
    if (panel_grab >= 0) {
        if (!grip_down[panel_grab] || !in.hand[panel_grab].active) panel_grab = -1;
        else { ed.main.pose = pose_mul(ed.aim[panel_grab], panel_rel); panel_stage = pose_mul(pose_inv(rig()), ed.main.pose); }
    }
    /* the keyboard hangs below the panel, tipped up towards you */
    /* the objects list stands at the panel's right edge, turned in towards you */
    ed.side.w_m = ed.main.w_m * SW / PW; ed.side.h_m = ed.main.h_m;
    ed.side.visible = ed.main.visible;
    {
        float turn = 0.50f;
        Pose rel = {{ed.main.w_m * 0.5f + 0.012f + ed.side.w_m * 0.5f * cosf(turn), 0, ed.side.w_m * 0.5f * sinf(turn)}, q4axis(v3(0, 1, 0), -turn)};
        ed.side.pose = pose_mul(ed.main.pose, rel);
    }
    ed.kb.w_m = 0.66f * cfg.scale; ed.kb.h_m = ed.kb.w_m * KH / KW;
    ed.kb.visible = ed.main.visible && ed.edit != NULL;
    Pose rel = {{0, -(ed.main.h_m * 0.5f + ed.kb.h_m * 0.42f + 0.012f), 0.075f}, q4axis(v3(1, 0, 0), -0.62f)};
    ed.kb.pose = pose_mul(ed.main.pose, rel);
}

/* ---------- the handles on the selected shape: move, turn or size it along one axis ---------- */

static V3 giz_dir(const Obj *o, int a) { return ed.xform == 2 ? q4rot(o->rot, AXES[a]) : AXES[a]; }
static float giz_far(const Obj *o) { return v3len(v3sub(o->pos, ed.head.p)); }
static float giz_radius(const Obj *o) { return 0.028f + 0.02f * giz_far(o); }

static float giz_len(const Obj *o, int a) {
    V3 e = v3(o->scale.x, o->scale.y * shape_height(o->shape), o->scale.z);
    float half = ed.xform == 2 ? 0.5f * (a == 0 ? e.x : a == 1 ? e.y : e.z) : 0.5f * fmaxf(e.x, fmaxf(e.y, e.z));
    return half + 0.16f + 0.07f * giz_far(o);
}

static V3 giz_handle(const Obj *o, int a) {
    if (ed.xform == 1) return v3add(o->pos, v3mul(AXES[(a + 2) % 3], giz_len(o, a)));     /* a bead on the ring round axis a */
    return v3add(o->pos, v3mul(giz_dir(o, a), giz_len(o, a)));
}

/* how far along the line through c in direction a the laser comes closest */
static int axis_param(V3 c, V3 a, V3 ro, V3 rd, float *out) {
    V3 w = v3sub(c, ro);
    float b = v3dot(a, rd), den = 1 - b * b;
    if (den < 1e-4f) return 0;
    *out = (b * v3dot(rd, w) - v3dot(a, w)) / den;
    return 1;
}

static int ring_angle(V3 c, int a, V3 ro, V3 rd, float *out) {
    float dn = v3dot(rd, AXES[a]);
    if (fabsf(dn) < 1e-4f) return 0;
    float t = v3dot(v3sub(c, ro), AXES[a]) / dn;
    if (t <= 0) return 0;
    V3 p = v3sub(v3add(ro, v3mul(rd, t)), c);
    *out = atan2f(v3dot(p, AXES[(a + 2) % 3]), v3dot(p, AXES[(a + 1) % 3]));
    return 1;
}

/* says 1 if this hand is busy with a handle, so nothing else should react to it */
static int gizmo_update(int h, V3 ro, V3 rd) {
    Obj *o = scene_find(ed.selected);
    if (giz_hand == h) {
        if (!o || !trig_down[h]) {
            if (o) scene_commit(ed.xform == 0 ? "Move" : ed.xform == 1 ? "Turn" : "Resize");
            giz_hand = -1;
            return 1;
        }
        float v;
        V3 a = ed.xform == 2 ? q4rot(giz_rot0, AXES[giz_axis]) : AXES[giz_axis];
        if (ed.xform == 1) {
            if (ring_angle(giz_pos0, giz_axis, ro, rd, &v)) {
                float d = v - giz_start;
                if (cfg.snap) d = snapf(d, FX_PI / 12);
                o->rot = q4norm(q4mul(q4axis(AXES[giz_axis], d), giz_rot0));
            }
        } else if (axis_param(giz_pos0, a, ro, rd, &v)) {
            float d = v - giz_start;
            if (ed.xform == 0) {
                if (cfg.snap) d = snapf(d, cfg.snap_size);
                o->pos = v3add(giz_pos0, v3mul(a, d));
            } else {
                float s0 = giz_axis == 0 ? giz_scale0.x : giz_axis == 1 ? giz_scale0.y : giz_scale0.z;
                float k = giz_axis == 1 ? shape_height(o->shape) : 1.0f;
                float sn = fclamp(s0 + 2 * d / k, 0.05f, 60.0f);
                if (cfg.snap) sn = fmaxf(snapf(sn, 0.05f), 0.05f);
                if (giz_axis == 0) o->scale.x = sn; else if (giz_axis == 1) o->scale.y = sn; else o->scale.z = sn;
            }
        }
        laser_len[h] = v3len(v3sub(giz_handle(o, giz_axis), ro)); laser_hit[h] = 1;
        return 1;
    }
    giz_hover[h] = -1;
    if (!o || (o->flags & OBJ_LOCKED) || ed.tool >= 0 || giz_hand >= 0 || grab_id[0] == o->id || grab_id[1] == o->id) return 0;
    float best = 1e9f, r = giz_radius(o) * 1.7f;
    for (int a = 0; a < 3; a++) {
        V3 oc = v3sub(giz_handle(o, a), ro);
        float along = v3dot(oc, rd);
        if (along > 0 && v3dot(oc, oc) - along * along < r * r && along < best) { best = along; giz_hover[h] = a; }
    }
    if (giz_hover[h] < 0) return 0;
    laser_len[h] = best; laser_hit[h] = 1;
    if (trig_pressed[h]) {
        giz_hand = h; giz_axis = giz_hover[h];
        giz_pos0 = o->pos; giz_scale0 = o->scale; giz_rot0 = o->rot; giz_start = 0;
        if (ed.xform == 1) ring_angle(giz_pos0, giz_axis, ro, rd, &giz_start);
        else axis_param(giz_pos0, giz_dir(o, giz_axis), ro, rd, &giz_start);
        world_buzz[h] = 0.5f; plat_sound(SND_GRAB);
    }
    return 1;
}

static void hands_in_world(void) {
    ghost_on = 0;
    for (int h = 0; h < 2; h++) {
        hover_id[h] = 0; laser_len[h] = 0; laser_hit[h] = 0;
        if (!in.hand[h].active) { grab_id[h] = 0; ed.hand_on[h] = 0; climbing[h] = 0; giz_hover[h] = -1; if (giz_hand == h) giz_hand = -1; continue; }
        V3 ro = ed.aim[h].p, rd = q4rot(ed.aim[h].q, v3(0, 0, -1));
        ed.hand_on[h] = ui_ptr[h].panel >= 0 || ui_dragging(h);

        /* pulling yourself along: hold a grip on nothing and move your hand, and the world comes with it */
        if (climbing[h]) {
            if (!grip_down[h]) climbing[h] = 0;
            else {
                V3 moved = q4rot(q4axis(v3(0, 1, 0), ed.rig_yaw), v3sub(in.hand[h].grip.p, climb_from[h]));
                ed.rig_pos = v3sub(ed.rig_pos, moved);
                ed.rig_pos.y = fclamp(ed.rig_pos.y, 0.0f, 60.0f);
                climb_from[h] = in.hand[h].grip.p;
                laser_len[h] = 0;
                continue;
            }
        }

        if (ed.hand_on[h]) {
            laser_len[h] = ui_ptr[h].dist < 1e8f ? ui_ptr[h].dist : 0.5f; laser_hit[h] = 1;
            if (grip_pressed[h] && (ui_ptr[h].panel == 0 || ui_ptr[h].panel == 2) && panel_grab < 0 && !grab_id[h]) {
                panel_grab = h; panel_rel = pose_mul(pose_inv(ed.aim[h]), ed.main.pose);
                world_buzz[h] = 0.4f; plat_sound(SND_GRAB);
            }
            if (!grab_id[h]) continue;
        }

        /* holding a shape */
        if (grab_id[h]) {
            Obj *o = scene_find(grab_id[h]);
            if (!o || !grip_down[h]) {
                if (o) {
                    if (cfg.snap) {
                        V3 up = q4rot(o->rot, v3(0, 1, 0));
                        if (up.y > 0.94f) o->rot = q4axis(v3(0, 1, 0), snapf(q4yaw(o->rot), FX_PI / 12));   /* nearly upright: square it up */
                    }
                    scene_commit("Move");
                }
                grab_id[h] = 0; scaling = 0;
                continue;
            }
            int other = 1 - h;
            if (grip_down[other] && !grab_id[other] && panel_grab != other && in.hand[other].active) {
                float d = v3len(v3sub(ed.grip[0].p, ed.grip[1].p));
                if (!scaling) { scaling = 1; scale_d0 = d > 0.02f ? d : 0.02f; scale_s0 = o->scale; }
                float k = fclamp(d / scale_d0, 0.05f, 40.0f);
                o->scale = v3(fclamp(scale_s0.x * k, 0.02f, 60.0f), fclamp(scale_s0.y * k, 0.02f, 60.0f), fclamp(scale_s0.z * k, 0.02f, 60.0f));
            } else {
                scaling = 0;
                if (fabsf(in.hand[h].sy) > 0.25f) {         /* push it away or pull it in */
                    float reach = -grab_rel[h].p.z;
                    reach = fclamp(reach + in.hand[h].sy * ed.dt * (1.0f + reach) * 1.8f, 0.05f, 40.0f);
                    grab_rel[h].p.z = -reach;
                }
                if (fabsf(in.hand[h].sx) > 0.35f) grab_spin[h] -= in.hand[h].sx * ed.dt * 2.2f;
            }
            Pose p = pose_mul(ed.aim[h], grab_rel[h]);
            o->rot = q4norm(q4mul(q4axis(v3(0, 1, 0), grab_spin[h]), p.q));
            o->pos = p.p;
            if (cfg.snap) o->pos = v3(snapf(p.p.x, cfg.snap_size), snapf(p.p.y, cfg.snap_size), snapf(p.p.z, cfg.snap_size));
            laser_len[h] = v3len(v3sub(o->pos, ro)); laser_hit[h] = 1;
            continue;
        }

        int locked_out = net_locked_out();          /* the room's owner has stopped guests editing */
        if (!locked_out && gizmo_update(h, ro, rd)) continue;

        float t = 0, tp = 0; V3 n;
        Obj *o = pick(ro, rd, 30.0f, &t, &n);
        int plate = ray_plate(ro, rd, &tp);
        if (o && (!plate || t < tp)) { hover_id[h] = o->id; laser_len[h] = t; laser_hit[h] = 1; }
        else if (plate) { laser_len[h] = tp; laser_hit[h] = 1; o = NULL; }
        else { laser_len[h] = 5.0f; o = NULL; }

        if (locked_out) {
            if (trig_pressed[h] || (grip_pressed[h] && o)) { ed_toast("The room's owner has locked editing."); plat_sound(SND_ERROR); }
            if (grip_pressed[h] && !o) { climbing[h] = 1; climb_from[h] = in.hand[h].grip.p; }
            continue;
        }
        if (ed.tool >= 0) {                     /* placing: show where it would go */
            V3 s = v3(cfg.build_size, cfg.build_size, cfg.build_size);
            V3 pos = placement(h, s, ed.tool);
            if (h == 1 || !ghost_on) { ghost_on = 1; ghost_pos = pos; }
            if (trig_pressed[h]) {
                unsigned c = PALETTE[ed.color];
                Obj *made = scene_add(ed.tool, pos, q4ident(), s, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
                if (made) {
                    ed.selected = made->id;
                    scene_commit("Place");
                    world_buzz[h] = 0.6f; plat_sound(SND_PLACE);
                } else { ed_toast("The scene is full (%d shapes).", MAX_OBJ); plat_sound(SND_ERROR); }
            }
        } else if (trig_pressed[h]) {
            ed.selected = o ? o->id : 0;
            if (o) { world_buzz[h] = 0.3f; plat_sound(SND_CLICK); }
        }

        if (grip_pressed[h]) {
            Obj *g = o;
            if (!g)                                 /* or one you're touching */
                for (int i = 0; i < g_obj_count; i++) {
                    float reach = 0.12f + 0.5f * fmaxf(g_obj[i].scale.x, fmaxf(g_obj[i].scale.y, g_obj[i].scale.z));
                    if (v3len(v3sub(g_obj[i].pos, ed.grip[h].p)) < fminf(reach, 0.45f)) { g = &g_obj[i]; break; }
                }
            if (g && (g->flags & OBJ_LOCKED)) { ed_toast("%s is locked.", g->name); plat_sound(SND_ERROR); }
            else if (g && grab_id[1 - h] != g->id) {
                Pose op = {g->pos, g->rot};
                grab_id[h] = g->id; grab_rel[h] = pose_mul(pose_inv(ed.aim[h]), op); grab_spin[h] = 0;
                ed.selected = g->id;
                world_buzz[h] = 0.5f; plat_sound(SND_GRAB);
            } else if (!g) {
                climbing[h] = 1; climb_from[h] = in.hand[h].grip.p;
                world_buzz[h] = 0.25f;
            }
        }
    }
    for (int h = 0; h < 2; h++) {                  /* what the others in the room see of your lasers */
        int on = in.hand[h].active && !ed.hand_on[h] && laser_len[h] > 0 && !net_locked_out();
        V3 from = ed.aim[h].p;
        net_set_aim(h, on, from, v3add(from, v3mul(q4rot(ed.aim[h].q, v3(0, 0, -1)), fminf(laser_len[h], 30.0f))));
    }
}

static void move_around(void) {
    float yaw = ed_front_yaw();
    HandIn *l = &in.hand[0], *r = &in.hand[1];
    if (l->active && !ed.hand_on[0] && !grab_id[0]) {
        float sx = fabsf(l->sx) > 0.15f ? l->sx : 0, sy = fabsf(l->sy) > 0.15f ? l->sy : 0;
        V3 fwd = v3(-sinf(yaw), 0, -cosf(yaw)), right = v3(cosf(yaw), 0, -sinf(yaw));
        ed.rig_pos = v3add(ed.rig_pos, v3mul(v3add(v3mul(fwd, sy), v3mul(right, sx)), cfg.speed * ed.dt));
    }
    if (r->active && !ed.hand_on[1] && !grab_id[1]) {
        float turn = 0;
        if (cfg.snap_turn) {
            if (!turn_latch && fabsf(r->sx) > 0.7f) { turn = r->sx > 0 ? -FX_PI / 6 : FX_PI / 6; turn_latch = 1; }
            if (fabsf(r->sx) < 0.3f) turn_latch = 0;
        } else if (fabsf(r->sx) > 0.2f) {
            turn = -r->sx * 1.8f * ed.dt;
        }
        if (turn != 0) {                         /* turn about where you're standing, not the room's middle */
            V3 before = ed.head.p;
            ed.rig_yaw += turn;
            V3 after = pose_pt(rig(), in.head.p);
            ed.rig_pos.x += before.x - after.x; ed.rig_pos.z += before.z - after.z;
        }
        if (fabsf(r->sy) > 0.6f && fabsf(r->sx) < 0.5f)
            ed.rig_pos.y = fclamp(ed.rig_pos.y + r->sy * cfg.speed * 0.7f * ed.dt, 0.0f, 60.0f);
    }
    ed.rig_pos.x = fclamp(ed.rig_pos.x, -200, 200); ed.rig_pos.z = fclamp(ed.rig_pos.z, -200, 200);
}

static void buttons(void) {
    if (btn_hit[1][0]) {                         /* A */
        if (scene_undo()) { ed_toast("Undone."); plat_sound(SND_CLICK); if (!scene_find(ed.selected)) ed.selected = 0; }
        else ed_toast("Nothing to undo.");
    }
    if (btn_hit[1][1]) {                         /* B */
        if (scene_redo()) { ed_toast("Redone."); plat_sound(SND_CLICK); }
        else ed_toast("Nothing to redo.");
    }
    if (btn_hit[0][0]) { ed_summon_panel(); plat_sound(SND_OPEN); }                 /* X */
    if (btn_hit[0][1] || btn_hit[0][3]) {                                           /* Y or menu */
        ed.main.visible = !ed.main.visible;
        if (ed.main.visible) ed_summon_panel();
        else { ed.edit = NULL; ed_toast("Panel hidden. Press Y to bring it back."); }
        plat_sound(SND_OPEN);
    }
    if (btn_hit[1][2]) {                         /* right stick click */
        if (ed.tool >= 0) { ed.tool = -1; ed_toast("Stopped placing."); }
        else { ed.xform = (ed.xform + 1) % 3; ed_toast(ed.xform == 0 ? "Handles: move" : ed.xform == 1 ? "Handles: turn" : "Handles: size"); plat_sound(SND_CLICK); }
    }
}

static void queue_lines(void) {
    static const float CYAN[4] = {0.31f, 0.89f, 0.96f, 0.95f}, VIOLET[4] = {0.62f, 0.45f, 1.0f, 0.95f};
    gfx_ribbons_clear();
    gfx_ribbon_layer(1);                           /* your lasers are drawn over the panels, never behind them */
    for (int h = 0; h < 2; h++) {
        if (!in.hand[h].active || laser_len[h] <= 0) continue;
        const float *c = h ? CYAN : VIOLET;
        float end[4] = {c[0], c[1], c[2], laser_hit[h] ? 0.55f : 0.0f};
        V3 a = ed.aim[h].p, b = v3add(a, v3mul(q4rot(ed.aim[h].q, v3(0, 0, -1)), laser_len[h]));
        gfx_ribbon(a, b, laser_hit[h] ? 0.006f : 0.004f, c, end);
    }
    gfx_ribbon_layer(0);
    for (int i = 0; i < MAX_PEERS; i++) {          /* the other players' lasers, each in their own colour */
        Peer *p = &g_peers[i];
        if (!p->used) continue;
        unsigned pc = PALETTE[(p->id * 5 + 3) % 15];
        float c0[4] = {((pc >> 16) & 255) / 255.0f, ((pc >> 8) & 255) / 255.0f, (pc & 255) / 255.0f, 0.9f};
        float c1[4] = {c0[0], c0[1], c0[2], 0.5f};
        for (int h = 0; h < 2; h++) if (p->aim_on[h]) gfx_ribbon(p->aim_from[h], p->aim_to[h], 0.014f, c1, c0);
    }
    Obj *s = scene_find(ed.selected);
    if (s && !(s->flags & OBJ_LOCKED) && ed.tool < 0 && !net_locked_out()) {       /* the handles' lines */
        static const float GC[3][4] = {{0.96f, 0.25f, 0.37f, 0.95f}, {0.29f, 0.87f, 0.50f, 0.95f}, {0.23f, 0.51f, 0.96f, 0.95f}};
        float w = 0.005f + 0.004f * giz_far(s);
        for (int a = 0; a < 3; a++) {
            int hot = giz_hover[0] == a || giz_hover[1] == a || (giz_hand >= 0 && giz_axis == a);
            float c[4] = {GC[a][0], GC[a][1], GC[a][2], hot ? 1.0f : 0.75f};
            if (ed.xform == 1) {
                float r = giz_len(s, a);
                V3 u = AXES[(a + 1) % 3], v = AXES[(a + 2) % 3], prev = v3add(s->pos, v3mul(u, r));
                for (int k = 1; k <= 48; k++) {
                    float ang = k / 48.0f * 2 * FX_PI;
                    V3 pt = v3add(s->pos, v3add(v3mul(u, cosf(ang) * r), v3mul(v, sinf(ang) * r)));
                    gfx_ribbon(prev, pt, hot ? w * 1.6f : w, c, c); prev = pt;
                }
            } else {
                gfx_ribbon(s->pos, giz_handle(s, a), hot ? w * 1.6f : w, c, c);
            }
        }
    }
    if (s) {
        float c[4] = {0.31f, 0.89f, 0.96f, 0.9f};
        V3 sc = v3(s->scale.x * 1.04f + 0.01f, s->scale.y * shape_height(s->shape) * 1.04f + 0.01f, s->scale.z * 1.04f + 0.01f);
        gfx_wire_box(s->pos, s->rot, sc, 0.008f + 0.004f * v3len(v3sub(s->pos, ed.head.p)), c);
    }
}

void app_update(const FrameIn *fi) {
    read_input(fi);
    frames++;
    if (ed.time - fps_t0 >= 0.5) { ed.fps = (float)(frames / (ed.time - fps_t0)); frames = 0; fps_t0 = ed.time; }
    place_panels();

    Panel *panels[3] = {&ed.main, &ed.kb, &ed.side};
    ui_frame(panels, 3, ed.dt, ed.time);
    if (ed.main.visible) {
        ui_begin(0); screens_draw(); ui_end();
        ui_begin(2); side_draw(); ui_end();
        if (ed.kb.visible) { ui_begin(1); keyboard_draw(); ui_end(); }
    }
    net_listen_rooms(ed.main.visible && ed.tab == TAB_ROOMS);

    hands_in_world();
    move_around();
    buttons();
    if (ed.v_follow) ed.v_target = v3lerp(ed.v_target, v3(ed.head.p.x, ed.head.p.y - 0.5f, ed.head.p.z), fclamp(ed.dt * 4, 0, 1));
    if (ed.selected && !scene_find(ed.selected)) ed.selected = 0;

    net_tick(ed.time, ed.head, ed.grip[0], ed.grip[1]);
    if (ed.time - last_autosave > 15.0) { last_autosave = ed.time; autosave(); }
    queue_lines();
}

float app_haptic(int hand) {
    float b = fmaxf(ui_take_buzz(hand), world_buzz[hand]);
    world_buzz[hand] = 0;
    return cfg.haptics ? b : 0;
}

/* ---------- drawing ---------- */

static void draw_person(Pose head, Pose lh, Pose rh, V3 color) {
    gfx_shape(SH_SPHERE, head.p, head.q, v3(0.21f, 0.24f, 0.23f), color, 1, 0.15f);
    gfx_shape(SH_CUBE, pose_pt(head, v3(0, 0.02f, -0.095f)), head.q, v3(0.17f, 0.075f, 0.06f), v3(0.08f, 0.09f, 0.16f), 1, 0.9f);
    gfx_shape(SH_SPHERE, lh.p, lh.q, v3(0.075f, 0.075f, 0.075f), color, 1, 0.3f);
    gfx_shape(SH_SPHERE, rh.p, rh.q, v3(0.075f, 0.075f, 0.075f), color, 1, 0.3f);
    V3 chest = v3(head.p.x, head.p.y - 0.42f, head.p.z);
    gfx_shape(SH_CONE, chest, q4axis(v3(0, 1, 0), q4yaw(head.q)), v3(0.30f, 0.46f, 0.20f), color, 1, 0.1f);
}

static void draw_controller(int h) {
    Pose g = ed.grip[h];
    V3 body = v3(0.13f, 0.14f, 0.24f), accent = h ? v3(0.25f, 0.85f, 0.95f) : v3(0.62f, 0.45f, 1.0f);
    if (controller_model[h] >= 0) {             /* the real controller, where your hand is */
        gfx_model(controller_model[h], g.p, g.q, accent, trig_down[h] || grip_down[h] ? 1.0f : 0.0f);
        return;
    }
    gfx_shape(SH_CYLINDER, pose_pt(g, v3(0, 0, 0.02f)), q4mul(g.q, q4axis(v3(1, 0, 0), FX_PI / 2)), v3(0.034f, 0.115f, 0.034f), body, 1, 0);
    gfx_shape(SH_TORUS, pose_pt(g, v3(0, 0.012f, -0.05f)), q4mul(g.q, q4axis(v3(1, 0, 0), 0.9f)), v3(0.085f, 0.06f, 0.085f), body, 1, 0.25f);
    gfx_shape(SH_SPHERE, ed.aim[h].p, ed.aim[h].q, v3(0.018f, 0.018f, 0.018f), accent, 1, 1.0f);
}

static void draw_world(const Cam *c, int viewer) {
    float t = (float)fmod(ed.time, 3600.0);
    glDepthMask(GL_TRUE); glDisable(GL_SCISSOR_TEST); glStencilMask(0xFF);
    glClearColor(0, 0, 0, 1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    gfx_sky(c, t, cfg.sky);
    gfx_plate(c, PLATE, cfg.grid, t);
    if (cfg.shadows) {
        gfx_shadows_begin(c);
        for (int i = 0; i < g_obj_count; i++) gfx_shadow(g_obj[i].shape, g_obj[i].pos, g_obj[i].rot, g_obj[i].scale);
        gfx_shadows_end();
    }
    gfx_shapes_begin(c, t);
    for (int i = 0; i < g_obj_count; i++) {
        const Obj *o = &g_obj[i];
        float glow = o->id == ed.selected ? 0.75f : (o->id == hover_id[0] || o->id == hover_id[1]) ? 0.3f : 0.0f;
        gfx_shape(o->shape, o->pos, o->rot, o->scale, obj_color(o), 1, glow);
    }
    for (int i = 0; i < MAX_PEERS; i++)
        if (g_peers[i].used) {
            unsigned pc = PALETTE[(g_peers[i].id * 5 + 3) % 15];
            draw_person(g_peers[i].head, g_peers[i].hand[0], g_peers[i].hand[1],
                        v3(((pc >> 16) & 255) / 255.0f, ((pc >> 8) & 255) / 255.0f, (pc & 255) / 255.0f));
        }
    if (viewer) draw_person(ed.head, ed.grip[0], ed.grip[1], v3(0.31f, 0.89f, 0.96f));
    else for (int h = 0; h < 2; h++) if (in.hand[h].active) draw_controller(h);
    if (ghost_on && ed.tool >= 0) {
        unsigned pc = PALETTE[ed.color];
        float s = cfg.build_size;
        gfx_shape(ed.tool, ghost_pos, q4ident(), v3(s, s, s), v3(((pc >> 16) & 255) / 255.0f, ((pc >> 8) & 255) / 255.0f, (pc & 255) / 255.0f),
                  0.45f, 0.6f);
    }
    Obj *sel = scene_find(ed.selected);
    if (sel && !viewer && !(sel->flags & OBJ_LOCKED) && ed.tool < 0 && !net_locked_out()) {          /* the handles themselves */
        static const V3 GC[3] = {{0.96f, 0.25f, 0.37f}, {0.29f, 0.87f, 0.50f}, {0.23f, 0.51f, 0.96f}};
        float r = giz_radius(sel) * 2;
        for (int a = 0; a < 3; a++) {
            int hot = giz_hover[0] == a || giz_hover[1] == a || (giz_hand >= 0 && giz_axis == a);
            float k = hot ? 1.35f : 1.0f;
            V3 at = giz_handle(sel, a);
            if (ed.xform == 0) {
                Q4 q = a == 0 ? q4axis(v3(0, 0, 1), -FX_PI / 2) : a == 1 ? q4ident() : q4axis(v3(1, 0, 0), FX_PI / 2);
                gfx_shape(SH_CONE, at, q, v3(r * k, r * 1.6f * k, r * k), GC[a], 1, hot ? 1.0f : 0.35f);
            } else if (ed.xform == 1) {
                gfx_shape(SH_SPHERE, at, q4ident(), v3(r * k, r * k, r * k), GC[a], 1, hot ? 1.0f : 0.35f);
            } else {
                gfx_shape(SH_CUBE, at, sel->rot, v3(r * 0.9f * k, r * 0.9f * k, r * 0.9f * k), GC[a], 1, hot ? 1.0f : 0.35f);
            }
        }
    }
    for (int i = 0; i < MAX_PEERS; i++)                 /* where each other player's laser lands */
        if (g_peers[i].used) {
            unsigned pc = PALETTE[(g_peers[i].id * 5 + 3) % 15];
            V3 col = v3(((pc >> 16) & 255) / 255.0f, ((pc >> 8) & 255) / 255.0f, (pc & 255) / 255.0f);
            for (int h = 0; h < 2; h++)
                if (g_peers[i].aim_on[h]) {
                    float d = 0.035f + 0.02f * v3len(v3sub(g_peers[i].aim_to[h], c->pos));
                    gfx_shape(SH_SPHERE, g_peers[i].aim_to[h], q4ident(), v3(d, d, d), col, 1, 1.0f);
                }
        }
    gfx_ribbons_flush(c, viewer ? -1 : 0);
    if (!viewer) { ui_render(c); gfx_ribbons_flush(c, 1); }
}

Cam ed_viewer_cam(void) {
    float cp = cosf(ed.v_pitch), sp = sinf(ed.v_pitch);
    Pose p;
    p.p = v3add(ed.v_target, v3mul(v3(cp * sinf(ed.v_yaw), sp, cp * cosf(ed.v_yaw)), ed.v_dist));
    p.q = q4mul(q4axis(v3(0, 1, 0), ed.v_yaw), q4axis(v3(1, 0, 0), -ed.v_pitch));
    float v = 0.42f, hfov = atanf(tanf(v) * (float)ed.viewer.w / (float)ed.viewer.h);
    return cam_make(p, -hfov, hfov, v, -v, ed.viewer.w, ed.viewer.h);
}

void ed_viewer_pick(float u, float v) {
    Cam c = ed_viewer_cam();
    V3 d = v3norm(q4rot(c.rot, v3(flerp(c.tl, c.tr, u), flerp(c.tu, c.td, v), -1)));
    float t; V3 n;
    Obj *o = pick(c.pos, d, 400.0f, &t, &n);
    ed.selected = o ? o->id : 0;
    if (o) { ed_toast("Selected %s.", o->name); plat_sound(SND_CLICK); }
}

void app_prerender(void) {
    assets_prerender();
    if (!ed.main.visible || ed.tab != TAB_VIEWER) return;
    Cam c = ed_viewer_cam();
    rt_begin(&ed.viewer);
    draw_world(&c, 1);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);     /* the picture is solid, even where shapes were see-through */
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    rt_end(&ed.viewer);
}

void app_render(const EyeView *eye, GLuint fbo) {
    Cam c = cam_make(pose_mul(rig(), eye->pose), eye->l, eye->r, eye->u, eye->d, eye->w, eye->h);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, eye->w, eye->h);
    draw_world(&c, 0);
}
