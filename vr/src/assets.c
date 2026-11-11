/* Flaux VR: the Assets page.  A library of ready-made things: the six shapes, patterns and whole
 * models.  Pick one and it turns slowly in the viewer on the right; drag to look at it from any
 * side, then place it on the plate in front of you.
 *
 * Every asset is a sentence the co-pilot understands ("a castle"), built into a list of shapes of
 * its own for the pictures, and into the scene when you place it. */
#include "editor.h"

#define CX 280.0f
#define CW 1360.0f
#define CR (CX + CW)
#define ASSET_N 16
#define ASSET_MAX 160
#define THUMB 224

typedef struct { const char *name, *prompt, *kind, *about; } Asset;
static const Asset ASSETS[ASSET_N] = {
    {"Cube", "a cube", "Shape", "A box. The start of most things."},
    {"Ball", "a ball", "Shape", "Round all over."},
    {"Cylinder", "a cylinder", "Shape", "A pillar, a wheel, a tree trunk."},
    {"Cone", "a cone", "Shape", "A roof, a spike, a party hat."},
    {"Ramp", "a ramp", "Shape", "A slope to go up or down."},
    {"Ring", "a ring", "Shape", "A hoop lying flat."},
    {"Tree", "a tree", "Model", "A trunk and two layers of leaves."},
    {"House", "a house", "Model", "Walls, a roof, a door, two windows and a chimney."},
    {"Castle", "a castle", "Model", "Four corner towers, walls, a gate and a keep."},
    {"Snowman", "a snowman", "Model", "Three snowballs, a carrot nose and a hat."},
    {"Rocket", "a rocket", "Model", "A body, a nose cone, a window and four fins."},
    {"Forest", "a forest", "Model", "Seven trees, a different size each."},
    {"Tower", "a tower of 6 cubes", "Pattern", "Six cubes, one on top of another."},
    {"Stairs", "8 stairs", "Pattern", "Eight steps, going up and away from you."},
    {"Pyramid", "a pyramid of 4", "Pattern", "Four layers of cubes, each smaller."},
    {"Spiral", "a spiral of 24 balls", "Pattern", "Twenty-four balls winding upwards."}};

static Obj objs[ASSET_N][ASSET_MAX];
static int count[ASSET_N];
static V3 lo[ASSET_N], hi[ASSET_N];
static RT rt_thumbs, rt_view;
static int ready, made_for_color = -1, sel = 8;
static float spin = 0.7f, tilt = 0.38f;
static double touched = -10;

static void generate(void) {
    for (int i = 0; i < ASSET_N; i++) {
        count[i] = copilot_preview(ASSETS[i].prompt, objs[i], ASSET_MAX);
        lo[i] = v3(1e9f, 1e9f, 1e9f); hi[i] = v3(-1e9f, -1e9f, -1e9f);
        for (int k = 0; k < count[i]; k++) {
            const Obj *o = &objs[i][k];
            float side = 0.5f * fmaxf(o->scale.x, o->scale.z), up = 0.5f * o->scale.y * (o->shape == SH_TORUS ? 0.28f : 1.0f);
            if (fabsf(o->rot.x) > 0.3f || fabsf(o->rot.z) > 0.3f) side = up = 0.5f * fmaxf(o->scale.y, fmaxf(o->scale.x, o->scale.z));
            lo[i] = v3(fminf(lo[i].x, o->pos.x - side), fminf(lo[i].y, o->pos.y - up), fminf(lo[i].z, o->pos.z - side));
            hi[i] = v3(fmaxf(hi[i].x, o->pos.x + side), fmaxf(hi[i].y, o->pos.y + up), fmaxf(hi[i].z, o->pos.z + side));
        }
        if (!count[i]) lo[i] = hi[i] = v3(0, 0, 0);
    }
}

/* one asset, on a round stand, filling the given part of the picture */
static void draw_asset(int i, int vx, int vy, int vw, int vh, float yaw, float pitch, float t, int stand) {
    V3 mid = v3mul(v3add(lo[i], hi[i]), 0.5f);
    float r = fmaxf(0.5f * v3len(v3sub(hi[i], lo[i])), 0.2f);
    float half = 0.30f, dist = r / sinf(half) * 1.08f;
    Pose p;
    p.p = v3add(mid, v3mul(v3(cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)), dist));
    p.q = q4mul(q4axis(v3(0, 1, 0), yaw), q4axis(v3(1, 0, 0), -pitch));
    float hfov = atanf(tanf(half) * (float)vw / (float)vh);
    Cam c = cam_make(p, -hfov, hfov, half, -half, vw, vh);
    glViewport(vx, vy, vw, vh);
    gfx_shapes_begin(&c, t);
    if (stand) {
        float w = fmaxf(hi[i].x - lo[i].x, hi[i].z - lo[i].z) * 1.35f + 0.3f;
        gfx_shape(SH_CYLINDER, v3(mid.x, lo[i].y - 0.035f * w, mid.z), q4ident(), v3(w, 0.07f * w, w), v3(0.16f, 0.19f, 0.42f), 1, 0.35f);
    }
    for (int k = 0; k < count[i]; k++) {
        const Obj *o = &objs[i][k];
        gfx_shape(o->shape, o->pos, o->rot, o->scale, v3(o->r / 255.0f, o->g / 255.0f, o->b / 255.0f), 1, 0);
    }
}

static void clear_rt(void) {
    glDepthMask(GL_TRUE); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void assets_prerender(void) {
    if (!ed.main.visible || ed.tab != TAB_ASSETS) return;
    if (!ready) { rt_create(&rt_thumbs, THUMB * 4, THUMB * 4, 4); rt_create(&rt_view, 978, 720, 4); ready = 1; }
    if (made_for_color != ed.color) {            /* the plain shapes take the colour you're building with */
        generate();
        rt_begin(&rt_thumbs); clear_rt();
        for (int i = 0; i < ASSET_N; i++) draw_asset(i, (i % 4) * THUMB, (i / 4) * THUMB, THUMB, THUMB, 0.7f, 0.42f, 0, 0);
        rt_end(&rt_thumbs);
        made_for_color = ed.color;
    }
    if (ed.time - touched > 2.5) spin += ed.dt * 0.5f;           /* left alone, it turns by itself */
    rt_begin(&rt_view); clear_rt();
    draw_asset(sel, 0, 0, rt_view.w, rt_view.h, spin, tilt, (float)fmod(ed.time, 3600.0), 1);
    rt_end(&rt_view);
}

void screen_assets(void) {
    static const Col NONE = {0, 0, 0, 0};
    ui_title(CX, 36, "Assets", "Ready-made shapes, patterns and models. Pick one, look it over, then place it.");

    ui_card(CX, 150, 640, 774);
    ui_text_bold(CX + 28, 168, 28, C_TEXT, "Library");
    char buf[96]; snprintf(buf, sizeof buf, "%d assets", ASSET_N);
    ui_text_right(CX + 612, 172, 22, C_DIM, buf);
    for (int i = 0; i < ASSET_N; i++) {
        float x = CX + 22 + (i % 4) * 150.0f, y = 214 + (i / 4) * 174.0f, w = 146, h = 166;
        int on = sel == i;
        int hand = ui_hover(x, y, w, h);
        if (ui_press(x, y, w, h)) { sel = i; on = 1; spin = 0.7f; tilt = 0.38f; touched = -10; plat_sound(SND_CLICK); }
        float hv = ui_anim(ui_id(ASSETS[i].name, 41), hand >= 0 ? 1.0f : 0.0f, 14.0f);
        float act = ui_anim(ui_id(ASSETS[i].name, 42), on ? 1.0f : 0.0f, 12.0f);
        if (hand >= 0 && hv < 0.2f) ui_buzz(hand, 0.12f);
        ui_glow(x, y, w, h, 22, 20, rgba(0x4FE3F5, 0.22f * hv + 0.42f * act));
        ui_frame_rect(x, y, w, h, 22, col_mix(rgba(0x10173C, 0.9f), rgba(0x26307A, 1), fmaxf(hv * 0.6f, act)), 2.5f,
                      col_mix(rgba(0xFFFFFF, 0.10f + 0.3f * hv), rgba(0x4FE3F5, 1), act));
        float s = 112 + 8 * hv;
        if (ready) ui_image(x + (w - s) * 0.5f, y + 4 - 3 * hv, s, s, 0, rt_thumbs.tex, (i % 4) * 0.25f, (i / 4 + 1) * 0.25f,
                            (i % 4 + 1) * 0.25f, (i / 4) * 0.25f, rgba(0xFFFFFF, 1));
        ui_text_center(x + w * 0.5f, y + 116, 23, col_mix(C_DIM, C_TEXT, fmaxf(hv, act)), ASSETS[i].name);
        ui_text_center(x + w * 0.5f, y + 142, 17, i < 6 ? C_CYAN : i < 12 ? C_PINK : C_AMBER, ASSETS[i].kind);
    }

    /* the asset viewer */
    float x = CX + 660, w = 700, vx = x + 24, vy = 174, vw = w - 48, vh = 480;
    ui_card(x, 150, w, 774);
    float px = 0, py = 0;
    static float lx, ly; static int dragging;
    if (ui_drag(ui_id("asset-view", 0), vx, vy, vw, vh, &px, &py)) {
        if (dragging) { spin -= (px - lx) * 0.006f; tilt = fclamp(tilt + (py - ly) * 0.005f, -0.25f, 1.45f); }
        dragging = 1; lx = px; ly = py; touched = ed.time;
    } else dragging = 0;
    int hover = ui_hover(vx, vy, vw, vh);
    if (hover >= 0 && fabsf(ui_ptr[hover].sx) > 0.2f) { spin -= ui_ptr[hover].sx * ui_dt * 2.0f; touched = ed.time; }
    ui_glow(vx, vy, vw, vh, 24, 26, rgba(0x7C3AED, 0.28f));
    ui_grad(vx, vy, vw, vh, 24, rgba(0x1B2460, 1), rgba(0x070A1E, 1), 0);
    if (ready) ui_image(vx, vy, vw, vh, 24, rt_view.tex, 0.0f, 1, 1.0f, 0, rgba(0xFFFFFF, 1));
    ui_frame_rect(vx, vy, vw, vh, 24, NONE, 2.5f, rgba(0x8B6CFF, hover >= 0 ? 1.0f : 0.6f));
    float kw = ui_text_width(21, ASSETS[sel].kind) + 36;
    ui_frame_rect(vx + 18, vy + 18, kw, 42, 21, rgba(0x070B22, 0.8f), 1.5f, rgba(0x4FE3F5, 0.8f));
    ui_text(vx + 36, vy + 28, 21, C_CYAN, ASSETS[sel].kind);
    ui_text_right(vx + vw - 20, vy + vh - 38, 19, C_FAINT, "Drag, or push the stick, to turn it");

    V3 size = v3sub(hi[sel], lo[sel]);
    ui_text_bold(vx, 676, 40, C_TEXT, ASSETS[sel].name);
    ui_text(vx, 728, 23, C_DIM, ASSETS[sel].about);
    const char *label[3] = {"Made of", "Size", "Co-pilot words"};
    char v0[32], v1[48], v2[48];
    snprintf(v0, sizeof v0, "%d shape%s", count[sel], count[sel] == 1 ? "" : "s");
    snprintf(v1, sizeof v1, "%.1f x %.1f x %.1f m", (double)size.x, (double)size.y, (double)size.z);
    snprintf(v2, sizeof v2, "\"%s\"", ASSETS[sel].prompt);
    const char *value[3] = {v0, v1, v2};
    for (int i = 0; i < 3; i++) {
        float cx = vx + i * (vw / 3);
        ui_text(cx, 772, 19, C_FAINT, label[i]);
        ui_text_fit(cx, 798, 23, vw / 3 - 12, C_TEXT, value[i]);
    }
    if (ui_icon_button(vx, 842, vw, 64, "\xE2\x9C\x9A", "Place it in front of me", BTN_PRIMARY)) copilot_run(ASSETS[sel].prompt);
}
