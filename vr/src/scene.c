/* Flaux VR: the scene.  A flat list of shapes, with undo/redo (a snapshot of the whole list per
 * step: scenes are small), and saving to a plain text file with one object per line. */
#include "fx.h"
#include <time.h>

Obj g_obj[MAX_OBJ];
int g_obj_count;
int g_scene_dirty;

#define HISTORY 64
typedef struct { Obj *objs; int count; char label[32]; } Snap;
static Snap hist[HISTORY];
static int hist_count, hist_pos;      /* hist[hist_pos - 1] is the scene as it is now */
static uint32_t id_base, id_next = 1;

uint32_t scene_new_id(void) {
    if (!id_base) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        id_base = ((uint32_t)(ts.tv_nsec ^ ts.tv_sec * 2654435761u) & 0x7FFu) + 1;     /* different on every headset */
    }
    return (id_base << 20) | (id_next++ & 0xFFFFFu);
}

static void snap_free(Snap *s) { free(s->objs); s->objs = NULL; s->count = 0; }

static void snap_take(Snap *s, const char *label) {
    s->count = g_obj_count;
    s->objs = malloc(sizeof(Obj) * (size_t)(g_obj_count ? g_obj_count : 1));
    memcpy(s->objs, g_obj, sizeof(Obj) * (size_t)g_obj_count);
    snprintf(s->label, sizeof s->label, "%s", label);
}

static void snap_restore(const Snap *s) {
    g_obj_count = s->count;
    memcpy(g_obj, s->objs, sizeof(Obj) * (size_t)s->count);
    g_scene_dirty = 1;
}

static void history_reset(const char *label) {
    for (int i = 0; i < hist_count; i++) snap_free(&hist[i]);
    hist_count = hist_pos = 0;
    snap_take(&hist[0], label);
    hist_count = hist_pos = 1;
}

void scene_init(void) { g_obj_count = 0; g_scene_dirty = 0; history_reset("New scene"); }

void scene_commit(const char *what) {
    for (int i = hist_pos; i < hist_count; i++) snap_free(&hist[i]);     /* a new change forgets the redos */
    hist_count = hist_pos;
    if (hist_count == HISTORY) {
        snap_free(&hist[0]);
        memmove(hist, hist + 1, sizeof(Snap) * (HISTORY - 1));
        hist_count--; hist_pos--;
    }
    snap_take(&hist[hist_count], what);
    hist_count++; hist_pos++;
    g_scene_dirty = 1;
}

int scene_can_undo(void) { return hist_pos > 1; }
int scene_can_redo(void) { return hist_pos < hist_count; }
int scene_undo(void) { if (!scene_can_undo()) return 0; hist_pos--; snap_restore(&hist[hist_pos - 1]); return 1; }
int scene_redo(void) { if (!scene_can_redo()) return 0; hist_pos++; snap_restore(&hist[hist_pos - 1]); return 1; }
const char *scene_history_label(int back) {
    int i = hist_pos - 1 - back;
    return i >= 1 ? hist[i].label : NULL;
}

Obj *scene_add(int shape, V3 pos, Q4 rot, V3 scale, uint8_t r, uint8_t g, uint8_t b) {
    if (g_obj_count >= MAX_OBJ) return NULL;
    Obj *o = &g_obj[g_obj_count++];
    memset(o, 0, sizeof *o);
    o->id = scene_new_id();
    o->shape = shape; o->pos = pos; o->rot = rot; o->scale = scale;
    o->r = r; o->g = g; o->b = b;
    int n = 1;
    for (int i = 0; i < g_obj_count - 1; i++) if (g_obj[i].shape == shape) n++;
    snprintf(o->name, sizeof o->name, "%s %d", SHAPE_NAMES[shape], n);
    g_scene_dirty = 1;
    return o;
}

Obj *scene_find(uint32_t id) {
    if (!id) return NULL;
    for (int i = 0; i < g_obj_count; i++) if (g_obj[i].id == id) return &g_obj[i];
    return NULL;
}

void scene_remove(uint32_t id) {
    for (int i = 0; i < g_obj_count; i++)
        if (g_obj[i].id == id) {
            memmove(&g_obj[i], &g_obj[i + 1], sizeof(Obj) * (size_t)(g_obj_count - i - 1));
            g_obj_count--; g_scene_dirty = 1;
            return;
        }
}

void scene_clear(void) { g_obj_count = 0; g_scene_dirty = 1; }

/* ---------- text ---------- */

int scene_write_obj(const Obj *o, char *buf, size_t n) {
    return snprintf(buf, n, "obj %u %d %.4f %.4f %.4f %.5f %.5f %.5f %.5f %.4f %.4f %.4f %d %d %d %d %s\n",
                    o->id, o->shape, (double)o->pos.x, (double)o->pos.y, (double)o->pos.z,
                    (double)o->rot.x, (double)o->rot.y, (double)o->rot.z, (double)o->rot.w,
                    (double)o->scale.x, (double)o->scale.y, (double)o->scale.z, o->r, o->g, o->b, o->flags, o->name);
}

int scene_parse_obj(const char *line, Obj *o) {
    unsigned id; int shape, r, g, b, flags, used = 0;
    float f[10];
    memset(o, 0, sizeof *o);
    if (sscanf(line, "obj %u %d %f %f %f %f %f %f %f %f %f %f %d %d %d %d %n", &id, &shape, &f[0], &f[1], &f[2], &f[3], &f[4],
               &f[5], &f[6], &f[7], &f[8], &f[9], &r, &g, &b, &flags, &used) < 16)
        return 0;
    if (shape < 0 || shape >= SH_COUNT || !id) return 0;
    for (int i = 0; i < 10; i++) if (!isfinite(f[i])) return 0;
    o->id = id; o->shape = shape;
    o->pos = v3(f[0], f[1], f[2]);
    o->rot = q4norm(q4(f[3], f[4], f[5], f[6]));
    o->scale = v3(fclamp(fabsf(f[7]), 0.01f, 200.0f), fclamp(fabsf(f[8]), 0.01f, 200.0f), fclamp(fabsf(f[9]), 0.01f, 200.0f));
    o->r = (uint8_t)r; o->g = (uint8_t)g; o->b = (uint8_t)b; o->flags = (uint8_t)flags;
    if (used > 0) {
        snprintf(o->name, sizeof o->name, "%s", line + used);
        o->name[strcspn(o->name, "\r\n")] = 0;
    }
    if (!o->name[0]) snprintf(o->name, sizeof o->name, "%s", SHAPE_NAMES[shape]);
    return 1;
}

int scene_save(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "flauxvr 2\n");
    char line[256];
    for (int i = 0; i < g_obj_count; i++) { scene_write_obj(&g_obj[i], line, sizeof line); fputs(line, f); }
    int ok = !ferror(f);
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(tmp, path) != 0) ok = 0;       /* never leave a half-written project behind */
    if (!ok) remove(tmp);
    return ok;
}

int scene_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    if (!fgets(line, sizeof line, f) || strncmp(line, "flauxvr", 7) != 0) { fclose(f); return 0; }
    g_obj_count = 0;
    while (fgets(line, sizeof line, f) && g_obj_count < MAX_OBJ) {
        Obj o;
        if (scene_parse_obj(line, &o) && !scene_find(o.id)) g_obj[g_obj_count++] = o;
    }
    fclose(f);
    history_reset("Opened project");
    g_scene_dirty = 0;
    return 1;
}

/* Writes the scene as a Flaux program, to open in Flaux Studio.  Flaux shapes stand upright, so a
 * shape's tilt is left out, and ramps and rings (which Flaux doesn't have yet) become notes. */
int scene_export_flx(const char *path, const char *title) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# %s: built in Flaux VR.\n# Open this file in Flaux Studio and press Run.\n\n", title);
    fprintf(f, "window(1280, 720, \"%s\")\nset angle to 0\n\nrepeat forever\n", title);
    fprintf(f, "    sky(\"#05060f\", \"#1a1440\")\n");
    fprintf(f, "    camera(sin(angle) * 9, 4, cos(angle) * 9, 0, 1, 0)\n");
    fprintf(f, "    plane(0, 0, 0, 48, 48, \"#0d1026\")\n");
    for (int i = 0; i < g_obj_count; i++) {
        const Obj *o = &g_obj[i];
        char col[16];
        snprintf(col, sizeof col, "#%02x%02x%02x", o->r, o->g, o->b);
        double x = o->pos.x, y = o->pos.y, z = o->pos.z;
        double size = (o->scale.x + o->scale.y + o->scale.z) / 3.0, rad = (o->scale.x + o->scale.z) / 4.0;
        switch (o->shape) {
        case SH_CUBE:
            fprintf(f, "    box(%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, \"%s\")\n", x, y, z, (double)o->scale.x, (double)o->scale.y,
                    (double)o->scale.z, col);
            break;
        case SH_SPHERE: fprintf(f, "    ball(%.2f, %.2f, %.2f, %.2f, \"%s\")\n", x, y, z, size / 2, col); break;
        case SH_CYLINDER: fprintf(f, "    cylinder(%.2f, %.2f, %.2f, %.2f, %.2f, \"%s\")\n", x, y, z, rad, (double)o->scale.y, col); break;
        case SH_CONE: fprintf(f, "    cone(%.2f, %.2f, %.2f, %.2f, %.2f, \"%s\")\n", x, y, z, rad, (double)o->scale.y, col); break;
        default: fprintf(f, "    # %s (a %s) at %.2f, %.2f, %.2f\n", o->name, o->shape == SH_WEDGE ? "ramp" : "ring", x, y, z); break;
        }
    }
    fprintf(f, "    add 0.01 to angle\n    show()\nend\n");
    return fclose(f) == 0;
}
