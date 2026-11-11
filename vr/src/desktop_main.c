/* Flaux VR: a test rig for a computer with no headset.  It runs the real app off-screen, moves a
 * pretend head and pretend controllers through a script, and saves pictures of what you'd see.
 *     ./flauxvr_test <assets dir> <output dir> [script]
 * Scripts: "tour" (pictures of every screen), "host" and "join <ip>" (two copies building together). */
#define EGL_EGLEXT_PROTOTYPES
#include "editor.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>

static const char *asset_dir = "assets", *out_dir = ".";
static FrameIn fi;
static RT eye_rt;
static EyeView eye;
static int sounds[SND_COUNT];

void *plat_asset(const char *name, size_t *size) {
    char path[512]; snprintf(path, sizeof path, "%s/%s", asset_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (size) *size = (size_t)n;
    return buf;
}
void plat_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }
void plat_sound(int id) { if (id >= 0 && id < SND_COUNT) sounds[id]++; }

static Q4 look(V3 d) {
    d = v3norm(d);
    return q4mul(q4axis(v3(0, 1, 0), atan2f(-d.x, -d.z)), q4axis(v3(1, 0, 0), asinf(d.y)));
}

static Pose to_stage(Pose world) {
    Pose rig = {ed.rig_pos, q4axis(v3(0, 1, 0), ed.rig_yaw)};
    return pose_mul(pose_inv(rig), world);
}

static void aim_world(int h, V3 target) {          /* point a controller at a spot in the world */
    Pose head_w = pose_mul((Pose){ed.rig_pos, q4axis(v3(0, 1, 0), ed.rig_yaw)}, fi.head);
    Pose w; w.p = pose_pt(head_w, v3(h ? 0.22f : -0.22f, -0.32f, -0.18f));
    w.q = look(v3sub(target, w.p));
    fi.hand[h].active = 1;
    fi.hand[h].aim = to_stage(w);
    Pose g = w; g.p = pose_pt(w, v3(0, -0.03f, 0.06f));
    fi.hand[h].grip = to_stage(g);
}
static void aim_panel(int h, float px, float py) { aim_world(h, ui_panel_point(&ed.main, px, py)); }
static void aim_kb(int h, float px, float py) { aim_world(h, ui_panel_point(&ed.kb, px, py)); }

static void frames(int n) {
    for (int i = 0; i < n; i++) {
        fi.dt = 1.0f / 72; fi.time += fi.dt;
        app_update(&fi);
        app_prerender();
        rt_begin(&eye_rt);
        app_render(&eye, eye_rt.fbo_ms);
        rt_end(&eye_rt);
        for (int h = 0; h < 2; h++) app_haptic(h);
    }
}

static void click(int h) { fi.hand[h].trigger = 1; frames(2); fi.hand[h].trigger = 0; frames(3); }
static void tap(float px, float py) { aim_panel(1, px, py); frames(2); click(1); }
static void tap_kb(float px, float py) { aim_kb(1, px, py); frames(2); click(1); }

static void shot(const char *name) {
    frames(2);
    int w = eye_rt.w, h = eye_rt.h;
    unsigned char *px = malloc((size_t)w * h * 4), *row = malloc((size_t)w * 3);
    glBindFramebuffer(GL_FRAMEBUFFER, eye_rt.fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char path[512]; snprintf(path, sizeof path, "%s/%s.ppm", out_dir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) memcpy(row + x * 3, px + ((size_t)y * w + x) * 4, 3);
        fwrite(row, 1, (size_t)w * 3, f);
    }
    fclose(f); free(px); free(row);
    fprintf(stderr, "saved %s  (%d shapes, GL error 0x%x)\n", path, g_obj_count, glGetError());
}

static void head(float x, float y, float z, V3 look_at) {
    fi.head.p = v3(x, y, z);                       /* x, y, z in the play space; look_at in the world */
    fi.head.q = look(v3sub(look_at, v3add(fi.head.p, ed.rig_pos)));
    eye.pose = fi.head;
}

static void look_panel(float py) { head(fi.head.p.x, fi.head.p.y, fi.head.p.z, ui_panel_point(&ed.main, PW / 2, py)); }

static void type(const char *text) {                /* on the on-screen keyboard */
    static const char *rows[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (; *text; text++) {
        if (*text == ' ') { tap_kb(620, 447); continue; }
        for (int r = 0; r < 4; r++) {
            const char *at = strchr(rows[r], *text);
            if (!at) continue;
            float x = 66 + (r == 2 ? 55.4f : r == 3 ? 166.2f : 0) + (float)(at - rows[r]) * 110.8f + 50;
            tap_kb(x, 92 + r * 80 + 35);
        }
    }
}

#define TAB_Y(i) (132 + (i) * 80 + 33)

static void tour(void) {
    V3 fwd = v3(0, 1.45f, 0);
    head(0, 1.62f, 0, fwd);
    frames(3);
    look_panel(PH / 2 + 30);
    aim_panel(0, 150, 600); aim_panel(1, 520, 215);
    frames(30); shot("01_home");

    tap(132, TAB_Y(1));                       /* Build */
    tap(366, 290);                       /* arm the cube */
    aim_panel(1, 1290, 250); frames(20); shot("02_build");

    /* place a few shapes out on the plate */
    ed.main.visible = 0;
    head(0, 1.62f, 0, v3(0, 0.4f, -1.5f));
    float spots[5][2] = {{-1.0f, -1.0f}, {-0.5f, -1.0f}, {0, -1.0f}, {0.5f, -1.0f}, {-0.5f, -1.5f}};
    for (int i = 0; i < 5; i++) { aim_world(1, v3(spots[i][0], 0, spots[i][1])); frames(2); click(1); }
    aim_world(1, v3(-0.5f, 0.5f, -0.8f)); frames(2); click(1);            /* one on top of another */
    ed.tool = SH_SPHERE; ed.color = 13;
    aim_world(1, v3(1.25f, 0, -0.75f)); frames(2); click(1);
    ed.tool = SH_CONE; ed.color = 2;
    aim_world(1, v3(2.0f, 0, -1.5f)); aim_world(0, v3(-1.6f, 0.2f, -1.5f)); frames(20);
    shot("03_world_placing");
    click(1);
    ed.tool = -1;

    /* grab the ball with the left hand and stretch it with both */
    aim_world(0, v3(1.25f, 0.25f, -0.75f)); frames(3);
    fi.hand[0].squeeze = 1; frames(3);
    aim_world(0, v3(0.8f, 1.6f, -1.2f)); frames(5);
    fi.hand[1].squeeze = 1; frames(2);
    aim_world(1, v3(3.0f, 1.0f, -0.5f)); frames(5);
    shot("04_world_grab_scale");
    fi.hand[0].squeeze = 0; fi.hand[1].squeeze = 0; frames(3);

    ed.main.visible = 1;
    head(0, 1.62f, 0, fwd);
    frames(2);
    look_panel(PH / 2 + 30);
    tap(132, TAB_Y(6));                   /* Co-pilot */
    tap(600, 185);                                 /* the prompt box: opens the keyboard */
    type("a castle");
    aim_kb(1, 400, 250); frames(20);
    look_panel(PH + 40);
    shot("05_copilot_keyboard");
    tap_kb(1090, 447);                             /* Enter */
    look_panel(PH / 2 + 30);
    aim_panel(1, 700, 280); frames(20); shot("06_copilot_built");

    tap(132, TAB_Y(4));                   /* Viewer */
    aim_panel(1, 800, 500); frames(20); shot("07_viewer");
    /* drag to orbit, then tap a shape */
    fi.hand[1].trigger = 1; frames(2); aim_panel(1, 1000, 420); frames(6); fi.hand[1].trigger = 0; frames(2);
    tap(1500, 252 + 30);                           /* Top */
    aim_panel(1, 900, 600); frames(15); shot("08_viewer_top");
    tap(1500 + 60, 252 + 30);                      /* Corner */

    tap(132, TAB_Y(2));                            /* Assets */
    aim_panel(1, 1300, 420); frames(30); shot("08b_assets_castle");
    tap(280 + 22 + 150 * 2 + 73, 214 + 174 * 2 + 60);      /* the rocket */
    aim_panel(0, 500, 300);
    fi.hand[1].trigger = 1; aim_panel(1, 1250, 400); frames(2); aim_panel(1, 1380, 360); frames(6); fi.hand[1].trigger = 0;   /* drag to turn it */
    frames(20); shot("08c_assets_rocket");
    tap(132, TAB_Y(3));                   /* Objects */
    tap(500, 200);                                 /* first row */
    aim_panel(1, 1100, 560); frames(20); shot("09_objects");
    tap(132, TAB_Y(5));
    snprintf(ed.server, sizeof ed.server, "flaux-vr.onrender.com");
    aim_panel(1, 1500, 559); aim_panel(0, 600, 429); frames(20); shot("10_rooms");
    tap(132, TAB_Y(7)); aim_panel(1, 560, 426); frames(20); shot("11_projects");
    tap(560, 426); frames(5);                      /* Save */
    tap(132, TAB_Y(8)); aim_panel(1, 700, 285); frames(20); shot("12_settings");

    /* out in the world: the castle, the stars, the edge of the plate */
    ed.main.visible = 0; fi.hand[0].active = 0;
    head(-5.5f, 3.2f, 1.5f, v3(0, 1.2f, -3));
    aim_world(1, v3(0, 1.5f, -4.5f)); frames(10); shot("13_world_castle");
    {   /* hold both controllers up in front of the face */
        head(0, 1.6f, 0, v3(0, 1.45f, -1));
        Pose rig = {ed.rig_pos, q4axis(v3(0, 1, 0), ed.rig_yaw)};
        for (int h = 0; h < 2; h++) {
            Pose w; w.p = pose_pt(pose_mul(rig, fi.head), v3(h ? 0.11f : -0.11f, -0.07f, -0.30f));
            w.q = look(v3(h ? -0.25f : 0.25f, 0.45f, -1));
            fi.hand[h].active = 1; fi.hand[h].grip = to_stage(w);
            Pose a = w; a.p = pose_pt(w, v3(0, -0.01f, -0.05f)); fi.hand[h].aim = to_stage(a);
        }
        frames(10); shot("14_controllers");
    }
    head(0, 1.7f, 0, v3(3, 8, -8)); fi.hand[0].active = 0; fi.hand[1].active = 0; frames(5); shot("14_sky");
    head(0, 26.0f, 36.0f, v3(0, 0, 0)); frames(5); shot("15_plate_from_above");
    fprintf(stderr, "sounds:"); for (int i = 0; i < SND_COUNT; i++) fprintf(stderr, " %d", sounds[i]);
    fprintf(stderr, "\n");
    app_pause();
}

/* two copies of the app building together over the network */
static void room(int host, const char *ip) {
    head(0, 1.62f, 0, v3(0, 1.0f, -3));
    ed.main.visible = 0;
    frames(3);
    ed.main.visible = 0;
    if (host) {
        scene_clear(); scene_commit("clear");
        net_host("Test room");
        copilot_run("a tower of 3 red cubes");
    } else {
        usleep(600000);
        net_join(ip);
    }
    for (int i = 0; i < 400; i++) {
        frames(1); usleep(10000);
        if (!host && i == 150) copilot_run("a ring of 6 gold balls");
        if (host && i == 300) { scene_remove(g_obj[0].id); scene_commit("del"); }
    }
    fprintf(stderr, "%s: mode %d, %d shapes, %d other player(s), status \"%s\"\n", host ? "host" : "join", net_mode(), g_obj_count,
            net_peer_count(), g_net_status);
    shot(host ? "room_host" : "room_join");
    net_leave();
}

/* two copies meeting in a room on a server:  flauxvr_test <assets> <out> net <address> <room> <a|b> */
static void server_room(const char *address, const char *room_code, int first) {
    head(0, 1.62f, 0, v3(0, 1.0f, -3));
    frames(3);
    ed.main.visible = 0;
    scene_clear(); scene_commit("clear");
    if (!first) usleep(700000);
    net_join_server(address, room_code);
    int built = 0;
    for (int i = 0; i < 600; i++) {
        frames(1); usleep(10000);
        if (net_mode() == 2 && !built && i > (first ? 60 : 260)) { copilot_run(first ? "a tower of 3 red cubes" : "a ring of 6 gold balls"); built = 1; }
        if (first && i == 480 && g_obj_count) { scene_remove(g_obj[0].id); scene_commit("del"); }
    }
    fprintf(stderr, "%s: mode %d, %d shapes, %d other player(s), status \"%s\"\n", first ? "first" : "second", net_mode(), g_obj_count,
            net_peer_count(), g_net_status);
    net_leave();
}

int main(int argc, char **argv) {
    if (argc > 1) asset_dir = argv[1];
    if (argc > 2) out_dir = argv[2];
    const char *script = argc > 3 ? argv[3] : "tour";
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "no EGL\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig config; EGLint n = 0;
    eglChooseConfig(dpy, cfg_attr, &config, 1, &n);
    EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "no GL context\n"); return 1; }
    fprintf(stderr, "GL: %s\n", (const char *)glGetString(GL_VERSION));

    g_encode = getenv("FLAUX_PLAIN") != NULL;      /* by default, draw the way the headset does: into sRGB pictures */
    char data[512]; snprintf(data, sizeof data, "%s/data_%s%s", out_dir, script, argc > 6 ? argv[6] : "");
    app_init(data);
    rt_create(&eye_rt, 1600, 1200, 4);
    eye.l = -0.84f; eye.r = 0.84f; eye.u = 0.68f; eye.d = -0.68f; eye.w = eye_rt.w; eye.h = eye_rt.h;
    fi.head.q = q4ident(); fi.hand[0].aim.q = fi.hand[0].grip.q = fi.hand[1].aim.q = fi.hand[1].grip.q = q4ident();
    if (!strcmp(script, "host")) room(1, NULL);
    else if (!strcmp(script, "join")) room(0, argc > 4 ? argv[4] : "127.0.0.1");
    else if (!strcmp(script, "net") && argc > 6) server_room(argv[4], argv[5], argv[6][0] == 'a');
    else tour();
    return 0;
}
