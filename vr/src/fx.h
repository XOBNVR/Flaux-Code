/* Flaux VR: shared types, maths and the interfaces between the parts of the app.
 *
 *   gfx.c     the world renderer: starry sky, baseplate, shapes, shadows, lasers
 *   ui.c      the panels: rounded rectangles, text, buttons, sliders, the keyboard
 *   scene.c   the objects you build, undo/redo, saving and loading
 *   net.c     rooms: building together on the same Wi-Fi
 *   editor.c  everything you do: pointing, grabbing, the tabs, the scene viewer
 *   android_main.c / desktop_main.c   the headset, or an off-screen test rig
 */
#ifndef FX_H
#define FX_H

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES3/gl3.h>

#define FX_VERSION "2.3.0"
#define FX_PI 3.14159265358979f

/* ---------- maths ---------- */

typedef struct { float x, y, z; } V3;
typedef struct { float x, y, z, w; } Q4;
typedef struct { float m[16]; } M4;          /* column-major, like OpenGL */
typedef struct { V3 p; Q4 q; } Pose;

static inline V3 v3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
static inline V3 v3add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline V3 v3scale(V3 a, V3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline float v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3len(V3 a) { return sqrtf(v3dot(a, a)); }
static inline V3 v3norm(V3 a) { float l = v3len(a); return l > 1e-9f ? v3mul(a, 1.0f / l) : v3(0, 0, 0); }
static inline V3 v3lerp(V3 a, V3 b, float t) { return v3add(a, v3mul(v3sub(b, a), t)); }
static inline float fclamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float flerp(float a, float b, float t) { return a + (b - a) * t; }

static inline Q4 q4(float x, float y, float z, float w) { Q4 q = {x, y, z, w}; return q; }
static inline Q4 q4ident(void) { return q4(0, 0, 0, 1); }
static inline Q4 q4mul(Q4 a, Q4 b) {
    return q4(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
              a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
static inline Q4 q4conj(Q4 q) { return q4(-q.x, -q.y, -q.z, q.w); }
static inline Q4 q4norm(Q4 q) {
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-9f) return q4ident();
    return q4(q.x / l, q.y / l, q.z / l, q.w / l);
}
static inline Q4 q4axis(V3 axis, float angle) {
    V3 a = v3norm(axis); float s = sinf(angle * 0.5f);
    return q4(a.x * s, a.y * s, a.z * s, cosf(angle * 0.5f));
}
static inline V3 q4rot(Q4 q, V3 v) {
    V3 u = v3(q.x, q.y, q.z);
    V3 t = v3mul(v3cross(u, v), 2.0f);
    return v3add(v3add(v, v3mul(t, q.w)), v3cross(u, t));
}
/* how far round the up axis a rotation faces (0 = looking down -z) */
static inline float q4yaw(Q4 q) { V3 f = q4rot(q, v3(0, 0, -1)); return atan2f(-f.x, -f.z); }

static inline Pose pose_ident(void) { Pose p = {{0, 0, 0}, {0, 0, 0, 1}}; return p; }
static inline Pose pose_mul(Pose a, Pose b) {   /* b first, then a */
    Pose r; r.q = q4norm(q4mul(a.q, b.q)); r.p = v3add(a.p, q4rot(a.q, b.p)); return r;
}
static inline Pose pose_inv(Pose a) { Pose r; r.q = q4conj(a.q); r.p = v3mul(q4rot(r.q, a.p), -1.0f); return r; }
static inline V3 pose_pt(Pose a, V3 p) { return v3add(a.p, q4rot(a.q, p)); }

static inline M4 m4ident(void) { M4 r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
static inline M4 m4mul(M4 a, M4 b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int w = 0; w < 4; w++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + w] * b.m[c * 4 + k];
            r.m[c * 4 + w] = s;
        }
    return r;
}
static inline M4 m4trs(V3 p, Q4 q, V3 s) {
    V3 x = q4rot(q, v3(s.x, 0, 0)), y = q4rot(q, v3(0, s.y, 0)), z = q4rot(q, v3(0, 0, s.z));
    M4 r = m4ident();
    r.m[0] = x.x; r.m[1] = x.y; r.m[2] = x.z;
    r.m[4] = y.x; r.m[5] = y.y; r.m[6] = y.z;
    r.m[8] = z.x; r.m[9] = z.y; r.m[10] = z.z;
    r.m[12] = p.x; r.m[13] = p.y; r.m[14] = p.z;
    return r;
}
static inline M4 m4view(Pose cam) { Pose i = pose_inv(cam); return m4trs(i.p, i.q, v3(1, 1, 1)); }
/* a projection from the tangents of the four field-of-view angles */
static inline M4 m4proj(float tl, float tr, float tu, float td, float n, float f) {
    M4 r; memset(&r, 0, sizeof r);
    r.m[0] = 2.0f / (tr - tl); r.m[5] = 2.0f / (tu - td);
    r.m[8] = (tr + tl) / (tr - tl); r.m[9] = (tu + td) / (tu - td);
    r.m[10] = -(f + n) / (f - n); r.m[11] = -1.0f; r.m[14] = -2.0f * f * n / (f - n);
    return r;
}

/* ---------- the platform (android_main.c or desktop_main.c) ---------- */

typedef struct {
    int active;
    Pose aim, grip;            /* in the play space: floor at y = 0 */
    float trigger, squeeze, sx, sy;
    int a, b, stick, menu;     /* a = A or X (lower button), b = B or Y (upper button) */
} HandIn;

typedef struct {
    float dt; double time;
    Pose head;
    HandIn hand[2];            /* 0 = left, 1 = right */
} FrameIn;

typedef struct { Pose pose; float l, r, u, d; int w, h; } EyeView;   /* fov angles in radians */

void *plat_asset(const char *name, size_t *size);   /* malloc'd; NULL if missing */
void plat_log(const char *fmt, ...);
void plat_sound(int id);
enum { SND_CLICK, SND_PLACE, SND_DELETE, SND_GRAB, SND_ERROR, SND_OPEN, SND_COUNT };

/* ---------- the app (editor.c) ---------- */

void app_init(const char *data_dir);
void app_update(const FrameIn *in);
void app_prerender(void);                               /* off-screen pictures, once a frame */
void app_render(const EyeView *eye, GLuint fbo);       /* one eye, into fbo */
float app_haptic(int hand);                             /* 0..1 buzz wanted this frame */
void app_pause(void);                                   /* save before the app sleeps */

/* ---------- gfx.c ---------- */

enum { SH_CUBE, SH_SPHERE, SH_CYLINDER, SH_CONE, SH_WEDGE, SH_TORUS, SH_COUNT };
extern const char *const SHAPE_NAMES[SH_COUNT];

typedef struct {
    M4 view, proj, vp;
    V3 pos; Q4 rot;
    float tl, tr, tu, td;
    int w, h;
} Cam;

typedef struct { GLuint fbo_ms, rb_color, rb_depth, fbo, tex; int w, h; } RT;

extern int g_encode;          /* 1: shaders write display colours themselves (plain RGBA8 targets) */

void gfx_init(void);
Cam cam_make(Pose pose, float l, float r, float u, float d, int w, int h);
void rt_create(RT *rt, int w, int h, int samples);
void rt_begin(RT *rt);
void rt_end(RT *rt);          /* resolves into rt->tex */
GLuint gfx_program(const char *vs, const char *fs, const char *name);

void gfx_sky(const Cam *c, float time, float brightness);
void gfx_plate(const Cam *c, float size, int grid, float time);
void gfx_shadows_begin(const Cam *c);
void gfx_shadow(int shape, V3 pos, Q4 rot, V3 scale);
void gfx_shadows_end(void);
void gfx_shapes_begin(const Cam *c, float time);
void gfx_shape(int shape, V3 pos, Q4 rot, V3 scale, V3 color, float alpha, float glow);
void gfx_ribbon(V3 a, V3 b, float width, const float ca[4], const float cb[4]);
void gfx_ribbon_layer(int top);
void gfx_ribbons_flush(const Cam *c, int layer);     /* layer -1: everything */
void gfx_ribbons_clear(void);
void gfx_wire_box(V3 pos, Q4 rot, V3 scale, float width, const float col[4]);
int gfx_model_load(const char *asset);          /* -1 if it isn't there */
void gfx_model(int id, V3 pos, Q4 rot, V3 tint, float glow);
void gfx_thumbnails(GLuint *tex_out);     /* one picture holding every shape, for the Build tab */

/* ---------- ui.c ---------- */

typedef struct { float r, g, b, a; } Col;
static inline Col rgba(unsigned hex, float a) {
    Col c = {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, a}; return c;
}
static inline Col col_mix(Col a, Col b, float t) {
    Col c = {flerp(a.r, b.r, t), flerp(a.g, b.g, t), flerp(a.b, b.b, t), flerp(a.a, b.a, t)}; return c;
}

typedef struct {
    Pose pose;                 /* centre of the panel in the world; faces +z of the pose */
    float w_m, h_m;            /* size in metres */
    float pw, ph;              /* size in panel pixels */
    int visible;
    int passive;               /* only for show: lasers pass through it */
} Panel;

typedef struct {
    V3 origin, dir;            /* the laser, in the world */
    int active, down, pressed, released;
    float sx, sy;              /* that hand's thumbstick */
    /* filled in by the UI: */
    int panel;                 /* which panel the laser lands on, or -1 */
    float px, py, dist;
} Pointer;

#define UI_MAX_PANELS 24
extern Pointer ui_ptr[2];
extern float ui_dt;
extern double ui_time;
extern int ui_clicked_any;    /* a widget was clicked this frame */

void ui_init(void);
void ui_frame(Panel **panels, int count, float dt, double time);   /* after filling ui_ptr[] */
void ui_begin(int panel);
void ui_end(void);
void ui_render(const Cam *c);
V3 ui_panel_point(const Panel *p, float px, float py);

void ui_clip(float x, float y, float w, float h);
void ui_unclip(void);
void ui_rect(float x, float y, float w, float h, float radius, Col c);
void ui_grad(float x, float y, float w, float h, float radius, Col c0, Col c1, int horizontal);
void ui_frame_rect(float x, float y, float w, float h, float radius, Col fill, float border, Col bcol);
void ui_glow(float x, float y, float w, float h, float radius, float blur, Col c);
void ui_arrow(float x, float y, float k, Col outline);     /* the arrow cursor, k panel pixels per square */
void ui_image(float x, float y, float w, float h, float radius, GLuint tex, float u0, float v0, float u1, float v1, Col tint);
float ui_text(float x, float y, float size, Col c, const char *s);     /* returns the width */
float ui_text_bold(float x, float y, float size, Col c, const char *s);
float ui_text_width(float size, const char *s);
void ui_text_center(float cx, float y, float size, Col c, const char *s);
void ui_text_right(float rx, float y, float size, Col c, const char *s);
void ui_text_fit(float x, float y, float size, float maxw, Col c, const char *s);

int ui_hover(float x, float y, float w, float h);            /* -1, or the hand pointing there */
int ui_press(float x, float y, float w, float h);            /* 1 on the frame a trigger goes down there */
float ui_anim(unsigned id, float target, float speed);
unsigned ui_id(const char *s, int n);
int ui_drag(unsigned id, float x, float y, float w, float h, float *px, float *py);   /* held from inside */
float ui_scroll_y(float x, float y, float w, float h);       /* thumbstick scrolling over an area */
void ui_buzz(int hand, float amount);
float ui_take_buzz(int hand);
int ui_dragging(int hand);

enum { BTN_NORMAL, BTN_PRIMARY, BTN_DANGER, BTN_GHOST, BTN_ON };
int ui_button(float x, float y, float w, float h, const char *label, int style);
int ui_icon_button(float x, float y, float w, float h, const char *icon, const char *label, int style);
int ui_toggle(float x, float y, float w, const char *label, int *value);
int ui_slider(float x, float y, float w, const char *label, float *value, float lo, float hi, const char *fmt);
void ui_card(float x, float y, float w, float h);
void ui_title(float x, float y, const char *title, const char *sub);

/* the colours of the app */
#define C_TEXT   rgba(0xF4F6FF, 1.0f)
#define C_DIM    rgba(0x9AA6D6, 1.0f)
#define C_FAINT  rgba(0x6B77A8, 1.0f)
#define C_CYAN   rgba(0x4FE3F5, 1.0f)
#define C_VIOLET rgba(0x8B5CF6, 1.0f)
#define C_PINK   rgba(0xF472B6, 1.0f)
#define C_GREEN  rgba(0x4ADE80, 1.0f)
#define C_AMBER  rgba(0xFBBF24, 1.0f)
#define C_RED    rgba(0xF43F5E, 1.0f)

/* ---------- scene.c ---------- */

#define MAX_OBJ 2048
#define OBJ_LOCKED 1

typedef struct {
    uint32_t id;
    int shape;
    V3 pos; Q4 rot; V3 scale;
    uint8_t r, g, b, flags;
    char name[24];
} Obj;

extern Obj g_obj[MAX_OBJ];
extern int g_obj_count;
extern int g_scene_dirty;      /* changed since the last save */

void scene_init(void);
Obj *scene_add(int shape, V3 pos, Q4 rot, V3 scale, uint8_t r, uint8_t g, uint8_t b);
Obj *scene_find(uint32_t id);
void scene_remove(uint32_t id);
void scene_clear(void);
void scene_commit(const char *what);     /* remember this moment for undo */
int scene_undo(void);
int scene_redo(void);
int scene_can_undo(void);
int scene_can_redo(void);
const char *scene_history_label(int back);   /* 0 = newest; NULL past the end */
int scene_write_obj(const Obj *o, char *buf, size_t n);
int scene_parse_obj(const char *line, Obj *o);
int scene_save(const char *path);
int scene_load(const char *path);
int scene_export_flx(const char *path, const char *title);
uint32_t scene_new_id(void);

/* ---------- net.c ---------- */

typedef struct {
    int used; uint32_t id; char name[20]; Pose head, hand[2]; double seen;
    int aim_on[2]; V3 aim_from[2], aim_to[2];       /* their lasers, while they point into the world */
} Peer;
typedef struct { char code[24]; int players, shapes; } PublicRoom;
#define MAX_PUBLIC 12
extern PublicRoom g_public[MAX_PUBLIC];
extern int g_public_count, g_public_state;      /* 0 not asked, 1 asking, 2 here they are, 3 couldn't ask */
typedef struct { char ip[20]; char name[24]; int players; double seen; } Room;
#define MAX_PEERS 8
#define MAX_ROOMS 8
extern Peer g_peers[MAX_PEERS];
extern Room g_rooms[MAX_ROOMS];
extern char g_net_status[96];

void net_init(const char *player_name);
void net_tick(double time, Pose head, Pose lh, Pose rh);
int net_host(const char *room_name);
int net_join(const char *ip);
int net_join_server(const char *address, const char *room, int make_public);   /* a server, or a hosting headset's address */
void net_list_public(const char *address);     /* fills g_public, a moment later */
void net_set_aim(int hand, int on, V3 from, V3 to);
int net_is_owner(void);        /* you opened this room */
int net_locked(void);          /* the owner has stopped guests editing */
int net_locked_out(void);      /* ...and you're a guest */
void net_set_lock(int on);
int net_on_server(void);
void net_leave(void);
int net_mode(void);            /* 0 alone, 1 hosting, 2 joined, 3 joining */
int net_peer_count(void);
const char *net_local_ip(void);
void net_listen_rooms(int on);

#endif
