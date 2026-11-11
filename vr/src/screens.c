/* Flaux VR: what's on the panel.  A sidebar of tabs, one screen per tab, and the keyboard. */
#include "editor.h"

#define CX 280.0f
#define CW 1360.0f
#define CR (CX + CW)

static const Col NONE = {0, 0, 0, 0};

/* ---------- little widgets ---------- */

/* a box you can type in; says 1 when Enter is pressed */
static int field(float x, float y, float w, float h, char *buf, int max, const char *placeholder) {
    int focused = ed.edit == buf;
    int hand = ui_hover(x, y, w, h);
    if (ui_press(x, y, w, h)) {
        ed.edit = buf; ed.edit_max = max; ed.kb_shift = buf[0] == 0; ed.kb_sym = 0; ed.edit_submit = 0;
        focused = 1;
        plat_sound(SND_OPEN);
    }
    float hv = ui_anim(ui_id(placeholder, (int)(x + y * 31)), (hand >= 0 || focused) ? 1.0f : 0.0f, 14.0f);
    if (focused) ui_glow(x, y, w, h, 18, 20, rgba(0x4FE3F5, 0.30f));
    ui_frame_rect(x, y, w, h, 18, rgba(0x070B22, 0.92f), 2, col_mix(rgba(0xFFFFFF, 0.16f), rgba(0x4FE3F5, 1.0f), hv));
    float size = fclamp(h * 0.42f, 22.0f, 32.0f), ty = y + (h - size) * 0.5f;
    ui_clip(x + 8, y, w - 16, h);
    float tw = 0;
    if (buf[0]) {
        tw = ui_text_width(size, buf);
        float shift = tw > w - 60 ? tw - (w - 60) : 0;       /* long text: keep the end in view */
        ui_text(x + 24 - shift, ty, size, C_TEXT, buf);
        tw -= shift;
    } else if (!focused) {
        ui_text(x + 24, ty, size, C_FAINT, placeholder);
    }
    if (focused && fmod(ui_time, 1.0) < 0.55) ui_rect(x + 26 + tw, y + h * 0.24f, 3, h * 0.52f, 1.5f, C_CYAN);
    ui_unclip();
    if (focused && ed.edit_submit) { ed.edit_submit = 0; ed.edit = NULL; return 1; }
    return 0;
}

static int stepper(float x, float y, float w, const char *label, float *v, float step, float lo, float hi, const char *fmt) {
    const float h = 58, bw = 62, lw = 54;
    int changed = 0;
    char minus[40], plus[40], buf[32];
    snprintf(minus, sizeof minus, "\xE2\x88\x92"); snprintf(plus, sizeof plus, "+");
    ui_text_bold(x, y + 15, 27, C_DIM, label);
    unsigned id = ui_id(label, (int)(x * 3 + y * 17));
    (void)id;
    if (ui_button(x + lw, y, bw, h, minus, BTN_NORMAL)) { *v = fclamp(*v - step, lo, hi); changed = 1; }
    float vx = x + lw + bw + 8, vw = w - lw - bw * 2 - 16;
    ui_frame_rect(vx, y, vw, h, 16, rgba(0x070B22, 0.9f), 1.5f, rgba(0xFFFFFF, 0.10f));
    snprintf(buf, sizeof buf, fmt, (double)*v);
    ui_text_center(vx + vw * 0.5f, y + 15, 27, C_TEXT, buf);
    if (ui_button(vx + vw + 8, y, bw, h, plus, BTN_NORMAL)) { *v = fclamp(*v + step, lo, hi); changed = 1; }
    return changed;
}

static int swatch(float x, float y, float s, unsigned hex, int selected) {
    int hand = ui_hover(x, y, s, s);
    int clicked = ui_press(x, y, s, s) != 0;
    float hv = ui_anim(ui_id("sw", (int)(x * 13 + y * 7)), hand >= 0 ? 1.0f : 0.0f, 14.0f);
    float g = 4 * hv;
    if (selected) ui_glow(x - 4, y - 4, s + 8, s + 8, (s + 8) * 0.5f, 18, rgba(hex, 0.75f));
    if (selected) ui_frame_rect(x - 7, y - 7, s + 14, s + 14, (s + 14) * 0.5f, NONE, 3.5f, rgba(0xFFFFFF, 1));
    ui_frame_rect(x - g, y - g, s + 2 * g, s + 2 * g, (s + 2 * g) * 0.5f, rgba(hex, 1), 2, rgba(0xFFFFFF, 0.25f + 0.5f * hv));
    if (clicked) plat_sound(SND_CLICK);
    return clicked;
}

static float list_x, list_y, list_w, list_h, list_content, *list_scroll;

static void list_begin(float x, float y, float w, float h, float content_h, float *scroll) {
    list_x = x; list_y = y; list_w = w; list_h = h; list_content = content_h; list_scroll = scroll;
    float max = content_h > h ? content_h - h : 0;
    *scroll = fclamp(*scroll - ui_scroll_y(x, y, w, h) * 1100.0f, 0, max);
    ui_clip(x, y, w, h);
}

static void list_end(void) {
    ui_unclip();
    if (list_content <= list_h) return;
    float max = list_content - list_h;
    float bh = fmaxf(list_h * list_h / list_content, 60), bx = list_x + list_w - 10;
    float px = 0, py = 0;
    if (ui_drag(ui_id("scroll", (int)(list_x + list_y)), bx - 16, list_y, 36, list_h, &px, &py))
        *list_scroll = fclamp((py - list_y - bh * 0.5f) / (list_h - bh) * max, 0, max);
    ui_rect(bx, list_y, 8, list_h, 4, rgba(0xFFFFFF, 0.06f));
    ui_rect(bx, list_y + (list_h - bh) * (*list_scroll / max), 8, bh, 4, rgba(0x4FE3F5, 0.75f));
}

static void go(int tab) { ed.tab = tab; ed.edit = NULL; }

/* ---------- the frame around every screen ---------- */

static void chrome(void) {
    static const char *icons[TAB_COUNT] = {"\xE2\x8C\x82", "\xE2\x9C\x9A", "\xE2\x97\x86", "\xE2\x98\xB0", "\xE2\x97\x89",
                                           "\xE2\x87\x84", "\xE2\x9C\xA6", "\xE2\x96\xA4", "\xE2\x9A\x99"};
    static const char *names[TAB_COUNT] = {"Home", "Build", "Assets", "Objects", "Viewer", "Rooms", "Co-pilot", "Projects", "Settings"};
    ui_glow(0, 0, PW, PH, 52, 70, rgba(0x7C3AED, 0.30f));
    ui_grad(0, 0, PW, PH, 52, rgba(0x0D1236, 0.99f), rgba(0x070A1E, 0.99f), 0);
    ui_frame_rect(0, 0, PW, PH, 52, NONE, 2.5f, rgba(0x8B6CFF, 0.65f));
    ui_grad(60, 0, PW - 120, 3, 1.5f, rgba(0x4FE3F5, 0.0f), rgba(0x4FE3F5, 0.9f), 1);
    ui_rect(16, 16, 232, PH - 32, 40, rgba(0x04061A, 0.72f));

    ui_text(40, 40, 46, C_CYAN, "\xE2\x9C\xA6");
    ui_text_bold(92, 38, 44, C_TEXT, "FLAUX");
    ui_text_bold(92, 84, 22, C_VIOLET, "VR  EDITOR");

    for (int i = 0; i < TAB_COUNT; i++) {
        float x = 30, y = 132 + i * 80.0f, w = 204, h = 66;
        int active = ed.tab == i;
        int hand = ui_hover(x, y, w, h);
        if (ui_press(x, y, w, h)) { go(i); plat_sound(SND_CLICK); active = 1; }
        float hv = ui_anim(ui_id(names[i], 7), hand >= 0 ? 1.0f : 0.0f, 14.0f);
        float on = ui_anim(ui_id(names[i], 8), active ? 1.0f : 0.0f, 12.0f);
        if (hand >= 0 && hv < 0.2f) ui_buzz(hand, 0.12f);
        if (on > 0.02f) {
            ui_glow(x, y, w, h, 22, 22, rgba(0x4FE3F5, 0.22f * on));
            ui_grad(x, y, w, h, 22, rgba(0x7C3AED, on), rgba(0x22B8EE, on), 1);
        }
        ui_rect(x, y, w, h, 22, rgba(0xFFFFFF, 0.08f * hv * (1 - on)));
        Col c = col_mix(col_mix(C_DIM, C_TEXT, hv), rgba(0xFFFFFF, 1), on);
        ui_text(x + 20, y + 16, 32, c, icons[i]);
        ui_text(x + 70, y + 19, 28, c, names[i]);
    }
    if (ui_icon_button(30, PH - 104, 98, 60, "\xE2\x86\xB6", NULL, scene_can_undo() ? BTN_NORMAL : BTN_GHOST)) {
        if (scene_undo()) ed_toast("Undone."); else ed_toast("Nothing to undo.");
    }
    if (ui_icon_button(136, PH - 104, 98, 60, "\xE2\x86\xB7", NULL, scene_can_redo() ? BTN_NORMAL : BTN_GHOST)) {
        if (scene_redo()) ed_toast("Redone."); else ed_toast("Nothing to redo.");
    }

    /* at the top right of every page: the project, the scene and the room, at a glance */
    {
        char chip[3][64]; Col dot[3] = {C_VIOLET, C_CYAN, C_FAINT};
        int m = net_mode();
        snprintf(chip[0], sizeof chip[0], "%s%s", ed.project, g_scene_dirty ? " *" : "");
        snprintf(chip[1], sizeof chip[1], "%d shape%s", g_obj_count, g_obj_count == 1 ? "" : "s");
        if (m == 1) { snprintf(chip[2], sizeof chip[2], "Hosting, %d here", net_peer_count() + 1); dot[2] = C_GREEN; }
        else if (m == 2) { snprintf(chip[2], sizeof chip[2], "%s, %d here", net_on_server() ? "Online" : "In a room", net_peer_count() + 1); dot[2] = C_GREEN; }
        else if (m == 3) { snprintf(chip[2], sizeof chip[2], "Joining"); dot[2] = C_AMBER; }
        else snprintf(chip[2], sizeof chip[2], "Solo");
        float x = CR;
        for (int i = 2; i >= 0; i--) {
            float w = fminf(ui_text_width(21, chip[i]), 220) + 56;
            x -= w;
            ui_frame_rect(x, 40, w, 42, 21, rgba(0x18204A, 0.85f), 1.5f, rgba(0xFFFFFF, 0.12f));
            ui_rect(x + 16, 54, 14, 14, 7, dot[i]);
            ui_text_fit(x + 40, 50, 21, 220, C_TEXT, chip[i]);
            x -= 10;
        }
    }

    /* the strip along the bottom */
    float y = PH - 66;
    ui_rect(CX, y - 12, CW, 2, 1, rgba(0xFFFFFF, 0.08f));
    float left = (float)(ed.toast_until - ui_time);
    if (left > 0) {
        float a = fclamp(left * 2.0f, 0, 1);
        ui_rect(CX, y + 6, 10, 28, 5, rgba(0x4FE3F5, a));
        ui_text_fit(CX + 24, y + 6, 26, 860, rgba(0xF4F6FF, a), ed.toast);
    } else {
        ui_text(CX, y + 6, 24, C_FAINT, "Hold a grip on this panel to move it.  X brings it to you.");
    }
    char buf[96];
    int mode = net_mode();
    snprintf(buf, sizeof buf, "%d shape%s    %s    %.0f fps", g_obj_count, g_obj_count == 1 ? "" : "s",
             mode == 1 ? "Hosting" : mode == 2 ? "In a room" : "Solo", (double)(ed.fps > 1 ? ed.fps : 72));
    ui_text_right(CR, y + 6, 24, C_DIM, buf);
}

/* ---------- Home ---------- */

static int big_card(float x, float y, float w, float h, const char *icon, const char *title, const char *text, unsigned hex) {
    int hand = ui_hover(x, y, w, h);
    int clicked = ui_press(x, y, w, h) != 0;
    float hv = ui_anim(ui_id(title, 3), hand >= 0 ? 1.0f : 0.0f, 12.0f);
    if (hand >= 0 && hv < 0.2f) ui_buzz(hand, 0.12f);
    ui_glow(x, y, w, h, 28, 26, rgba(hex, 0.35f * hv));
    ui_frame_rect(x, y - 6 * hv, w, h, 28, col_mix(rgba(0x18204A, 0.85f), rgba(0x242E66, 0.95f), hv), 2, col_mix(rgba(0xFFFFFF, 0.10f), rgba(hex, 1), hv));
    ui_rect(x + 28, y + 28 - 6 * hv, 76, 76, 22, rgba(hex, 0.20f));
    ui_text_center(x + 66, y + 44 - 6 * hv, 44, rgba(hex, 1), icon);
    ui_text_bold(x + 124, y + 34 - 6 * hv, 32, C_TEXT, title);
    ui_text(x + 124, y + 76 - 6 * hv, 22, C_DIM, text);
    if (clicked) plat_sound(SND_CLICK);
    return clicked;
}

static void key_line(float x, float y, const char *key, const char *what) {
    float kw = ui_text_width(23, key) + 34;
    if (kw < 150) kw = 150;
    ui_frame_rect(x, y, kw, 46, 14, rgba(0x2A3470, 0.9f), 1.5f, rgba(0x4FE3F5, 0.45f));
    ui_text_center(x + kw * 0.5f, y + 11, 23, C_CYAN, key);
    ui_text(x + 172, y + 10, 25, C_TEXT, what);
}

static void screen_home(void) {
    ui_title(CX, 36, "Welcome to Flaux VR", "Build in 3D with your hands.");
    float w = (CW - 40) / 3;
    if (big_card(CX, 150, w, 120, "\xE2\x9C\x9A", "Start building", "Pick a shape and place it", 0x4FE3F5)) go(TAB_BUILD);
    if (big_card(CX + w + 20, 150, w, 120, "\xE2\x97\x86", "Assets", "Ready-made models to place", 0xA78BFA)) go(TAB_ASSETS);
    if (big_card(CX + (w + 20) * 2, 150, w, 120, "\xE2\x87\x84", "Build together", "On your Wi-Fi or a server", 0xF472B6)) go(TAB_ROOMS);

    ui_card(CX, 290, CW, 404);
    ui_text_bold(CX + 32, 308, 30, C_TEXT, "Controls");
    ui_text_right(CR - 32, 314, 21, C_FAINT, "Saved by itself every few seconds, and when you take the headset off.");
    float x1 = CX + 32, x2 = CX + CW * 0.5f + 12, y = 366;
    key_line(x1, y, "Trigger", "Click, select, place a shape");
    key_line(x2, y, "Left stick", "Walk around");
    key_line(x1, y + 64, "Grip", "Grab a shape, or this panel");
    key_line(x2, y + 64, "Right stick", "Turn, and fly up or down");
    key_line(x1, y + 128, "Both grips", "Stretch a shape bigger or smaller");
    key_line(x2, y + 128, "Stick + grab", "Push, pull and spin what you hold");
    key_line(x1, y + 192, "X", "Bring this panel to you");
    key_line(x2, y + 192, "Y", "Hide or show the panel");
    key_line(x1, y + 256, "A", "Undo");
    key_line(x2, y + 256, "B", "Redo");

    /* what's on the plate, shape by shape */
    float pw = 840;
    ui_card(CX, 714, pw, 210);
    ui_text_bold(CX + 28, 730, 26, C_TEXT, "On the plate");
    int counts[SH_COUNT] = {0}, most = 1;
    for (int i = 0; i < g_obj_count; i++) counts[g_obj[i].shape]++;
    for (int i = 0; i < SH_COUNT; i++) if (counts[i] > most) most = counts[i];
    char buf[64]; snprintf(buf, sizeof buf, "%d shape%s in all", g_obj_count, g_obj_count == 1 ? "" : "s");
    ui_text_right(CX + pw - 28, 734, 21, C_DIM, buf);
    for (int i = 0; i < SH_COUNT; i++) {
        float x = CX + 24 + i * 134.0f;
        ui_image(x + 4, 770, 62, 62, 0, ed.thumbs, (float)i / SH_COUNT, 1, (float)(i + 1) / SH_COUNT, 0, counts[i] ? rgba(PALETTE[ed.color], 1) : rgba(0x55608F, 1));
        snprintf(buf, sizeof buf, "%d", counts[i]);
        ui_text_bold(x + 72, 778, 34, counts[i] ? C_TEXT : C_FAINT, buf);
        ui_text(x + 6, 842, 20, C_DIM, SHAPE_NAMES[i]);
        ui_rect(x + 6, 876, 112, 8, 4, rgba(0x2A3366, 1));
        if (counts[i]) ui_grad(x + 6, 876, fmaxf(112.0f * counts[i] / most, 8), 8, 4, rgba(0x7C3AED, 1), rgba(0x22B8EE, 1), 1);
    }

    /* what you did last */
    float hx = CX + pw + 20, hw = CW - pw - 20;
    ui_card(hx, 714, hw, 210);
    ui_text_bold(hx + 28, 730, 26, C_TEXT, "History");
    int shown = 0;
    for (int i = 0; i < 4; i++) {
        const char *label = scene_history_label(i);
        if (!label) break;
        ui_rect(hx + 30, 784 + i * 34.0f, 10, 10, 5, i == 0 ? C_CYAN : C_FAINT);
        ui_text(hx + 54, 774 + i * 34.0f, 23, i == 0 ? C_TEXT : C_DIM, label);
        if (i == 0) ui_text_right(hx + hw - 28, 776, 19, C_FAINT, "A undoes this");
        shown++;
    }
    if (!shown) ui_text(hx + 30, 778, 22, C_FAINT, "Nothing yet. Place your first shape.");
}

/* ---------- Build ---------- */

static void screen_build(void) {
    ui_title(CX, 36, "Build", "Pick a shape and a colour, then pull the trigger out in the world.");
    unsigned hex = PALETTE[ed.color];

    ui_card(CX, 150, 836, 372);
    ui_text_bold(CX + 28, 168, 28, C_TEXT, "Shapes");
    for (int i = 0; i < SH_COUNT; i++) {
        float x = CX + 24 + i * 132.0f, y = 216, w = 124, h = 158;
        int armed = ed.tool == i;
        int hand = ui_hover(x, y, w, h);
        if (ui_press(x, y, w, h)) {
            ed.tool = armed ? -1 : i; armed = !armed;
            plat_sound(SND_CLICK);
            if (armed) ed_toast("%s ready. Point at the plate and pull the trigger.", SHAPE_NAMES[i]);
        }
        float hv = ui_anim(ui_id(SHAPE_NAMES[i], 1), hand >= 0 ? 1.0f : 0.0f, 14.0f);
        float on = ui_anim(ui_id(SHAPE_NAMES[i], 2), armed ? 1.0f : 0.0f, 12.0f);
        if (hand >= 0 && hv < 0.2f) ui_buzz(hand, 0.12f);
        ui_glow(x, y, w, h, 22, 22, rgba(0x4FE3F5, 0.25f * hv + 0.45f * on));
        ui_frame_rect(x, y, w, h, 22, col_mix(rgba(0x10173C, 0.9f), rgba(0x26307A, 1), fmaxf(hv * 0.6f, on)), 2.5f,
                      col_mix(rgba(0xFFFFFF, 0.10f + 0.3f * hv), rgba(0x4FE3F5, 1), on));
        float s = 104 + 8 * hv;
        ui_image(x + (w - s) * 0.5f, y + 6 - 4 * hv, s, s, 0, ed.thumbs, (float)i / SH_COUNT, 1, (float)(i + 1) / SH_COUNT, 0, rgba(hex, 1));
        ui_text_center(x + w * 0.5f, y + 118, 23, col_mix(C_DIM, C_TEXT, fmaxf(hv, on)), SHAPE_NAMES[i]);
    }
    ui_slider(CX + 32, 404, 420, "Size", &cfg.build_size, 0.1f, 3.0f, "%.2f m");
    if (ed.tool >= 0) {
        char buf[64]; snprintf(buf, sizeof buf, "Placing: %s", SHAPE_NAMES[ed.tool]);
        ui_text(CX + 500, 410, 24, C_CYAN, buf);
        if (ui_button(CX + 500, 446, 300, 56, "Stop placing", BTN_NORMAL)) ed.tool = -1;
    } else {
        ui_text(CX + 500, 410, 24, C_DIM, "Nothing picked.");
        ui_text(CX + 500, 446, 22, C_FAINT, "Tap a shape to start placing.");
    }

    ui_card(CX + 856, 150, 504, 372);
    ui_text_bold(CX + 884, 168, 28, C_TEXT, "Colour");
    ui_text_right(CR - 28, 172, 24, rgba(hex, 1), PALETTE_NAMES[ed.color]);
    for (int i = 0; i < PALETTE_N; i++) {
        float x = CX + 890 + (i % 5) * 92.0f, y = 222 + (i / 5) * 72.0f;
        if (swatch(x, y, 58, PALETTE[i], ed.color == i)) ed.color = i;
    }

    ui_card(CX, 542, 836, 382);
    ui_text_bold(CX + 28, 560, 28, C_TEXT, "Helpers");
    ui_toggle(CX + 32, 610, 772, "Snap to the grid", &cfg.snap);
    ui_slider(CX + 32, 682, 772, "Grid step", &cfg.snap_size, 0.05f, 1.0f, "%.2f m");
    ui_toggle(CX + 32, 778, 772, "Show grid lines on the plate", &cfg.grid);
    ui_toggle(CX + 32, 846, 772, "Shadows", &cfg.shadows);

    ui_card(CX + 856, 542, 504, 382);
    Obj *o = scene_find(ed.selected);
    ui_text_bold(CX + 884, 560, 28, C_TEXT, "Selected");
    if (o) {
        ui_rect(CX + 884, 612, 44, 44, 22, rgba((unsigned)(o->r << 16 | o->g << 8 | o->b), 1));
        ui_text_fit(CX + 944, 618, 30, 380, C_TEXT, o->name);
        if (ui_icon_button(CX + 884, 680, 448, 62, "\xE2\x97\x8F", "Paint with this colour", BTN_NORMAL)) {
            o->r = (uint8_t)(hex >> 16); o->g = (uint8_t)(hex >> 8); o->b = (uint8_t)hex;
            scene_commit("Paint");
        }
        if (ui_icon_button(CX + 884, 756, 218, 62, "\xE2\x9D\x90", "Copy", BTN_NORMAL)) ed_duplicate_selected();
        if (ui_icon_button(CX + 1114, 756, 218, 62, "\xE2\x9C\x95", "Delete", BTN_DANGER)) ed_delete_selected();
        if (ui_button(CX + 884, 832, 448, 62, "More in Objects", BTN_GHOST)) go(TAB_SCENE);
    } else {
        ui_text(CX + 884, 618, 24, C_DIM, "Nothing selected.");
        ui_text(CX + 884, 656, 22, C_FAINT, "Point at a shape and pull the trigger");
        ui_text(CX + 884, 686, 22, C_FAINT, "to select it, or hold a grip to grab it.");
    }
}

/* ---------- Objects ---------- */

static float scroll_objects;
static double clear_confirm;

static void turn_selected(Obj *o, V3 axis, float angle, const char *what) {
    o->rot = q4norm(q4mul(q4axis(axis, angle), o->rot));
    scene_commit(what);
}

static void screen_scene(void) {
    char sub[64];
    snprintf(sub, sizeof sub, "%d shape%s in this scene.", g_obj_count, g_obj_count == 1 ? "" : "s");
    ui_title(CX, 36, "Objects", sub);

    ui_card(CX, 150, 540, 774);
    const float row = 64;
    if (g_obj_count == 0) {
        ui_text_center(CX + 270, 470, 28, C_DIM, "The plate is empty.");
        if (ui_button(CX + 130, 524, 280, 62, "Start building", BTN_PRIMARY)) go(TAB_BUILD);
    }
    list_begin(CX + 12, 164, 516, 746, g_obj_count * row, &scroll_objects);
    for (int i = 0; i < g_obj_count; i++) {
        Obj *o = &g_obj[i];
        float y = 164 + i * row - scroll_objects;
        if (y < 164 - row || y > 164 + 746) continue;
        int sel = o->id == ed.selected;
        int hand = ui_hover(CX + 12, y, 490, row - 6);
        if (ui_press(CX + 12, y, 490, row - 6)) { ed.selected = sel ? 0 : o->id; sel = !sel; plat_sound(SND_CLICK); }
        if (sel) ui_grad(CX + 16, y, 486, row - 6, 16, rgba(0x7C3AED, 0.85f), rgba(0x22B8EE, 0.85f), 1);
        else if (hand >= 0) ui_rect(CX + 16, y, 486, row - 6, 16, rgba(0xFFFFFF, 0.08f));
        ui_image(CX + 22, y + 3, 52, 52, 0, ed.thumbs, (float)o->shape / SH_COUNT, 1, (float)(o->shape + 1) / SH_COUNT, 0,
                 rgba((unsigned)(o->r << 16 | o->g << 8 | o->b), 1));
        ui_text_fit(CX + 84, y + 15, 26, 244, C_TEXT, o->name);
        ui_text_right(CX + 486, y + 18, 21, sel ? C_TEXT : C_FAINT, (o->flags & OBJ_LOCKED) ? "locked" : SHAPE_NAMES[o->shape]);
    }
    list_end();

    float x = CX + 560, w = 800;
    ui_card(x, 150, w, 774);
    Obj *o = scene_find(ed.selected);
    if (!o) {
        ui_text_center(x + w * 0.5f, 420, 30, C_DIM, "Nothing selected");
        ui_text_center(x + w * 0.5f, 468, 23, C_FAINT, "Tap a shape in the list, or point at one in the world.");
        if (g_obj_count > 0) {
            int sure = ui_time < clear_confirm;
            if (ui_button(x + w * 0.5f - 210, 820, 420, 62, sure ? "Tap again to delete them all" : "Delete everything", BTN_DANGER)) {
                if (sure) { scene_clear(); scene_commit("Delete everything"); clear_confirm = 0; ed_toast("Everything deleted. Press A to undo."); plat_sound(SND_DELETE); }
                else clear_confirm = ui_time + 3.0;
            }
        }
        return;
    }
    int locked = o->flags & OBJ_LOCKED;
    float fx = x + 28, fw = w - 56;
    ui_text(fx, 170, 22, C_DIM, "Name");
    static char name_buf[24]; static uint32_t name_for;
    if (name_for != o->id || ed.edit != name_buf) { snprintf(name_buf, sizeof name_buf, "%s", o->name); name_for = o->id; }
    if (field(fx, 200, fw - 220, 62, name_buf, (int)sizeof name_buf - 1, "Name this shape")) {
        if (name_buf[0]) { snprintf(o->name, sizeof o->name, "%s", name_buf); scene_commit("Rename"); }
    } else if (ed.edit == name_buf && name_buf[0]) {
        snprintf(o->name, sizeof o->name, "%s", name_buf);
    }
    if (ui_button(fx + fw - 204, 200, 204, 62, locked ? "Unlock" : "Lock", locked ? BTN_ON : BTN_NORMAL)) {
        o->flags ^= OBJ_LOCKED; scene_commit(locked ? "Unlock" : "Lock");
    }

    float cw = (fw - 32) / 3, step = cfg.snap ? cfg.snap_size : 0.1f;
    int moved = 0;
    ui_text(fx, 282, 22, C_DIM, "Position (metres)");
    moved |= stepper(fx, 312, cw, "X", &o->pos.x, step, -100, 100, "%.2f");
    moved |= stepper(fx + cw + 16, 312, cw, "Y", &o->pos.y, step, -20, 100, "%.2f");
    moved |= stepper(fx + (cw + 16) * 2, 312, cw, "Z", &o->pos.z, step, -100, 100, "%.2f");
    if (moved) scene_commit("Move");

    int sized = 0;
    ui_text(fx, 392, 22, C_DIM, "Size (metres)");
    sized |= stepper(fx, 422, cw, "W", &o->scale.x, 0.1f, 0.05f, 60, "%.2f");
    sized |= stepper(fx + cw + 16, 422, cw, "H", &o->scale.y, 0.1f, 0.05f, 60, "%.2f");
    sized |= stepper(fx + (cw + 16) * 2, 422, cw, "D", &o->scale.z, 0.1f, 0.05f, 60, "%.2f");
    if (sized) scene_commit("Resize");

    ui_text(fx, 502, 22, C_DIM, "Turn");
    float bw = (fw - 48) / 4;
    if (ui_icon_button(fx, 532, bw, 60, "\xE2\x86\xB6", "Left", BTN_NORMAL)) turn_selected(o, v3(0, 1, 0), FX_PI / 12, "Turn");
    if (ui_icon_button(fx + bw + 16, 532, bw, 60, "\xE2\x86\xB7", "Right", BTN_NORMAL)) turn_selected(o, v3(0, 1, 0), -FX_PI / 12, "Turn");
    if (ui_icon_button(fx + (bw + 16) * 2, 532, bw, 60, "\xE2\x86\x91", "Tip", BTN_NORMAL)) turn_selected(o, v3(1, 0, 0), -FX_PI / 12, "Tip");
    if (ui_button(fx + (bw + 16) * 3, 532, bw, 60, "Stand up", BTN_NORMAL)) { o->rot = q4ident(); scene_commit("Stand up"); }

    ui_text(fx, 612, 22, C_DIM, "Colour");
    for (int i = 0; i < PALETTE_N; i++) {
        unsigned hex = PALETTE[i];
        int same = o->r == (hex >> 16) && o->g == ((hex >> 8) & 255) && o->b == (hex & 255);
        if (swatch(fx + 2 + (i % 10) * 74.4f, 646 + (i / 10) * 58.0f, 44, hex, same)) {
            o->r = (uint8_t)(hex >> 16); o->g = (uint8_t)(hex >> 8); o->b = (uint8_t)hex;
            scene_commit("Paint");
        }
    }

    float aw = (fw - 32) / 3;
    if (ui_icon_button(fx, 838, aw, 64, "\xE2\x9D\x90", "Copy", BTN_NORMAL)) ed_duplicate_selected();
    if (ui_icon_button(fx + aw + 16, 838, aw, 64, "\xE2\x97\x89", "Look at it", BTN_NORMAL)) {
        ed.v_target = o->pos; ed.v_follow = 0;
        ed.v_dist = fclamp(4.0f * fmaxf(o->scale.x, fmaxf(o->scale.y, o->scale.z)), 2.0f, 60.0f);
        go(TAB_VIEWER);
    }
    if (ui_icon_button(fx + (aw + 16) * 2, 838, aw, 64, "\xE2\x9C\x95", "Delete", BTN_DANGER)) ed_delete_selected();
}

/* ---------- Scene viewer ---------- */

static void screen_viewer(void) {
    ui_title(CX, 36, "Scene viewer", "Your whole build from outside, without moving. The cyan figure is you.");
    const float x = CX, y = 150, w = 1090, h = 680;
    static int dragging, orbiting; static float sx, sy, lx, ly;
    float px = 0, py = 0;
    int hover = ui_hover(x, y, w, h);
    if (hover >= 0) {                              /* thumbstick: zoom and spin */
        if (fabsf(ui_ptr[hover].sy) > 0.2f) ed.v_dist = fclamp(ed.v_dist * expf(-ui_ptr[hover].sy * ui_dt * 1.6f), 1.0f, 90.0f);
        if (fabsf(ui_ptr[hover].sx) > 0.2f) ed.v_yaw -= ui_ptr[hover].sx * ui_dt * 1.6f;
    }
    if (ui_drag(ui_id("viewer", 0), x, y, w, h, &px, &py)) {
        if (!dragging) { dragging = 1; orbiting = 0; sx = lx = px; sy = ly = py; }
        if (!orbiting && fabsf(px - sx) + fabsf(py - sy) > 22) { orbiting = 1; lx = px; ly = py; }
        if (orbiting) {
            ed.v_yaw -= (px - lx) * 0.0055f;
            ed.v_pitch = fclamp(ed.v_pitch + (py - ly) * 0.0045f, -0.05f, 1.52f);
            lx = px; ly = py;
        }
    } else if (dragging) {
        dragging = 0;
        if (!orbiting) ed_viewer_pick((sx - x) / w, (sy - y) / h);       /* a click, not a drag */
    }
    ui_glow(x, y, w, h, 28, 30, rgba(0x7C3AED, 0.30f));
    ui_image(x, y, w, h, 28, ed.viewer.tex, 0, 1, 1, 0, rgba(0xFFFFFF, 1));
    ui_frame_rect(x, y, w, h, 28, NONE, 2.5f, rgba(0x8B6CFF, hover >= 0 ? 1.0f : 0.6f));
    ui_text(x + 4, y + h + 20, 23, C_FAINT, "Drag to look around.  Thumbstick to zoom and spin.  Tap a shape to select it.");
    {
        char info[96];
        snprintf(info, sizeof info, "Camera %.1f m away, %.0f\xC2\xB0 up", (double)ed.v_dist, (double)(ed.v_pitch * 57.2958f));
        float iw = ui_text_width(21, info) + 36;
        ui_frame_rect(x + 20, y + h - 62, iw, 42, 21, rgba(0x070B22, 0.78f), 1.5f, rgba(0xFFFFFF, 0.14f));
        ui_text(x + 38, y + h - 52, 21, C_DIM, info);
        snprintf(info, sizeof info, "Aimed at  x %.1f   y %.1f   z %.1f", (double)ed.v_target.x, (double)ed.v_target.y, (double)ed.v_target.z);
        float aw = ui_text_width(21, info) + 36;
        ui_frame_rect(x + w - aw - 20, y + h - 62, aw, 42, 21, rgba(0x070B22, 0.78f), 1.5f, rgba(0xFFFFFF, 0.14f));
        ui_text(x + w - aw - 2, y + h - 52, 21, C_DIM, info);
        snprintf(info, sizeof info, "%d shape%s, %d player%s", g_obj_count, g_obj_count == 1 ? "" : "s", net_peer_count() + 1, net_peer_count() ? "s" : "");
        float sw = ui_text_width(21, info) + 36;
        ui_frame_rect(x + w - sw - 20, y + 20, sw, 42, 21, rgba(0x070B22, 0.78f), 1.5f, rgba(0xFFFFFF, 0.14f));
        ui_text(x + w - sw - 2, y + 30, 21, C_DIM, info);
    }
    Obj *sel = scene_find(ed.selected);
    if (sel) {
        char buf[64]; snprintf(buf, sizeof buf, "Selected: %s", sel->name);
        float tw = ui_text_width(24, buf) + 40;
        ui_frame_rect(x + 20, y + 20, tw, 50, 25, rgba(0x070B22, 0.8f), 1.5f, rgba(0x4FE3F5, 0.8f));
        ui_text(x + 40, y + 33, 24, C_TEXT, buf);
    }

    float bx = x + w + 20, bw = CR - bx, by = 150;
    ui_text(bx, by, 22, C_DIM, "Look from");
    float hw = (bw - 12) * 0.5f;
    if (ui_button(bx, by + 32, hw, 58, "Front", BTN_NORMAL)) { ed.v_yaw = 0; ed.v_pitch = 0.12f; }
    if (ui_button(bx + hw + 12, by + 32, hw, 58, "Side", BTN_NORMAL)) { ed.v_yaw = FX_PI / 2; ed.v_pitch = 0.12f; }
    if (ui_button(bx, by + 102, hw, 58, "Top", BTN_NORMAL)) { ed.v_pitch = 1.52f; }
    if (ui_button(bx + hw + 12, by + 102, hw, 58, "Corner", BTN_NORMAL)) { ed.v_yaw = 0.7f; ed.v_pitch = 0.55f; }
    ui_text(bx, by + 184, 22, C_DIM, "Zoom");
    if (ui_button(bx, by + 216, hw, 58, "+", BTN_NORMAL)) ed.v_dist = fclamp(ed.v_dist * 0.75f, 1.0f, 90.0f);
    if (ui_button(bx + hw + 12, by + 216, hw, 58, "\xE2\x88\x92", BTN_NORMAL)) ed.v_dist = fclamp(ed.v_dist * 1.33f, 1.0f, 90.0f);
    ui_text(bx, by + 298, 22, C_DIM, "Aim at");
    if (ui_button(bx, by + 330, bw, 58, "The middle", BTN_NORMAL)) { ed.v_target = v3(0, 0.8f, 0); ed.v_follow = 0; }
    if (ui_button(bx, by + 400, bw, 58, "My selection", sel ? BTN_NORMAL : BTN_GHOST)) {
        if (sel) { ed.v_target = sel->pos; ed.v_follow = 0; } else ed_toast("Select a shape first.");
    }
    if (ui_button(bx, by + 470, bw, 58, ed.v_follow ? "Following me" : "Follow me", ed.v_follow ? BTN_ON : BTN_NORMAL)) ed.v_follow = !ed.v_follow;
    if (ui_icon_button(bx, by + 564, bw, 64, "\xE2\x9C\xA6", "Go there", BTN_PRIMARY)) {
        ed_teleport(v3(ed.v_target.x, 0, ed.v_target.z + 1.5f));
        ed_summon_panel();
        ed_toast("Moved you to the spot the viewer is aimed at.");
    }
    ui_text(bx, by + 640, 20, C_FAINT, "Moves you to where");
    ui_text(bx, by + 666, 20, C_FAINT, "the viewer is aimed.");
}

/* ---------- Rooms ---------- */

static void screen_rooms(void) {
    ui_title(CX, 36, "Build together", "Share one scene with friends: on the same Wi-Fi, or anywhere through a server.");
    int mode = net_mode();
    char buf[128];
    ui_card(CX, 150, CW, 130);
    ui_rect(CX + 32, 190, 20, 20, 10, mode == 1 || mode == 2 ? C_GREEN : mode == 3 ? C_AMBER : C_FAINT);
    ui_text_fit(CX + 68, 172, 34, CW - 460, C_TEXT, g_net_status);
    const char *ip = net_local_ip();
    snprintf(buf, sizeof buf, ip[0] ? "This headset's address on the Wi-Fi: %s" : "This headset isn't on a network.", ip);
    ui_text(CX + 68, 224, 22, C_DIM, buf);
    if (mode != 0 && ui_button(CR - 352, 182, 320, 66, mode == 1 ? "Close the room" : mode == 3 ? "Stop" : "Leave the room", BTN_DANGER))
        net_leave();

    /* on this Wi-Fi */
    ui_card(CX, 300, 660, 624);
    ui_text_bold(CX + 28, 318, 28, C_TEXT, "On this Wi-Fi");
    ui_text(CX + 28, 356, 21, C_FAINT, "One headset opens a room. The others see it here.");
    if (mode == 0 && ui_icon_button(CX + 28, 396, 604, 66, "\xE2\x87\x84", "Open a room on this headset", BTN_NORMAL)) {
        char name[40]; snprintf(name, sizeof name, "%s's room", ed.player);
        net_host(name);
        ed_toast("%s", g_net_status);
    }
    int n = 0;
    for (int i = 0; i < MAX_ROOMS; i++) {
        Room *r = &g_rooms[i];
        if (!r->ip[0]) continue;
        float y = 482 + n * 88.0f;
        ui_frame_rect(CX + 24, y, 612, 78, 20, rgba(0x10173C, 0.9f), 1.5f, rgba(0xFFFFFF, 0.10f));
        ui_text_fit(CX + 48, y + 11, 27, 360, C_TEXT, r->name);
        snprintf(buf, sizeof buf, "%s   %d player%s", r->ip, r->players, r->players == 1 ? "" : "s");
        ui_text(CX + 48, y + 45, 21, C_DIM, buf);
        if (ui_button(CX + 470, y + 11, 150, 56, "Join", BTN_PRIMARY)) net_join(r->ip);
        if (++n >= 4) break;
    }
    if (!n) {
        int dots = (int)(ui_time * 2.5) % 4;
        snprintf(buf, sizeof buf, mode == 1 ? "You're hosting. Others will see your room here." : "Looking for rooms nearby%.*s", dots, "...");
        ui_text_center(CX + 330, 640, 25, C_DIM, buf);
        if (mode != 1) ui_text_center(CX + 330, 682, 21, C_FAINT, "Nothing yet. Ask a friend to open a room.");
    }

    /* through a server */
    float x = CX + 680, w = 680;
    ui_card(x, 300, w, 624);
    ui_text_bold(x + 28, 318, 28, C_TEXT, "Over the internet");
    ui_text(x + 28, 356, 21, C_FAINT, "Everyone types the same server and the same room code.");
    ui_text(x + 28, 396, 21, C_DIM, "Server address");
    int go_now = field(x + 28, 424, w - 56, 62, ed.server, (int)sizeof ed.server - 1, "my-server.onrender.com");
    ui_text(x + 28, 500, 21, C_DIM, "Room code");
    go_now |= field(x + 28, 528, w - 252, 62, ed.room_code, (int)sizeof ed.room_code - 1, "FLAUX");
    if (ui_icon_button(x + w - 208, 528, 180, 62, "\xE2\x87\x84", "Join", BTN_PRIMARY)) go_now = 1;
    if (go_now) {
        ed.edit = NULL;
        net_join_server(ed.server, ed.room_code);
        ed_save_settings();
        ed_toast("%s", g_net_status);
    }
    ui_text(x + 28, 604, 21, C_DIM, "Your name");
    static char name_buf[20];
    if (ed.edit != name_buf) snprintf(name_buf, sizeof name_buf, "%s", ed.player);
    if (field(x + 28, 632, w - 56, 62, name_buf, (int)sizeof name_buf - 1, "Player")) {
        if (name_buf[0]) { snprintf(ed.player, sizeof ed.player, "%s", name_buf); net_init(ed.player); ed_save_settings(); }
    }
    ui_text_bold(x + 28, 716, 26, C_TEXT, "Players here");
    snprintf(buf, sizeof buf, "%s (you)", ed.player);
    ui_rect(x + 30, 764, 24, 24, 12, C_CYAN);
    ui_text(x + 68, 761, 24, C_TEXT, buf);
    int k = 1;
    for (int i = 0; i < MAX_PEERS && k < 8; i++)
        if (g_peers[i].used) {
            float px = x + 30 + (k % 2) * 320.0f, py = 764 + (k / 2) * 38.0f;
            ui_rect(px, py, 24, 24, 12, rgba(PALETTE[(g_peers[i].id * 5 + 3) % 15], 1));
            ui_text_fit(px + 38, py - 3, 24, 260, C_TEXT, g_peers[i].name);
            k++;
        }
}

/* ---------- Co-pilot ---------- */

void copilot_say(int user, const char *fmt, ...) {
    if (ed.log_n == LOG_N) {
        memmove(ed.log[0], ed.log[1], sizeof ed.log[0] * (LOG_N - 1));
        memmove(&ed.log_user[0], &ed.log_user[1], sizeof(int) * (LOG_N - 1));
        ed.log_n--;
    }
    va_list ap; va_start(ap, fmt);
    vsnprintf(ed.log[ed.log_n], sizeof ed.log[0], fmt, ap);
    va_end(ap);
    ed.log_user[ed.log_n++] = user;
}

static void screen_copilot(void) {
    ui_title(CX, 36, "Co-pilot", "Type what you want, and it's built on the plate in front of you.");
    int send = field(CX, 150, CW - 220, 76, ed.prompt, (int)sizeof ed.prompt - 1, "a tower of 6 red cubes");
    if (ui_icon_button(CR - 200, 150, 200, 76, "\xE2\x9C\xA6", "Build", BTN_PRIMARY)) send = 1;
    static const char *ideas[10] = {"a tower of 8 blue cubes", "a ring of 12 gold balls", "rainbow stairs", "a castle", "a house",
                                    "a forest", "a snowman", "a pyramid of 5", "a spiral of 24 pink balls", "a rocket"};
    float x = CX, y = 248;
    for (int i = 0; i < 10; i++) {
        float w = ui_text_width(23, ideas[i]) + 44;
        if (x + w > CR) { x = CX; y += 64; }
        if (ui_button(x, y, w, 52, ideas[i], BTN_NORMAL)) { snprintf(ed.prompt, sizeof ed.prompt, "%s", ideas[i]); send = 1; }
        x += w + 12;
    }
    if (send && ed.prompt[0]) {
        ed.edit = NULL;
        copilot_say(1, "%s", ed.prompt);
        copilot_run(ed.prompt);
        ed.prompt[0] = 0;
    }
    ui_card(CX, 390, CW, 534);
    for (int i = 0; i < ed.log_n; i++) {
        float ly = 412 + i * 55.0f;
        if (ed.log_user[i]) {
            float w = ui_text_width(25, ed.log[i]) + 48;
            if (w > CW - 80) w = CW - 80;
            ui_grad(CR - 28 - w, ly, w, 46, 23, rgba(0x7C3AED, 0.9f), rgba(0x22B8EE, 0.9f), 1);
            ui_text_fit(CR - 4 - w, ly + 10, 25, w - 40, C_TEXT, ed.log[i]);
        } else {
            ui_text(CX + 28, ly + 8, 28, C_CYAN, "\xE2\x9C\xA6");
            ui_text_fit(CX + 70, ly + 10, 25, CW - 110, C_TEXT, ed.log[i]);
        }
    }
}

/* ---------- Projects ---------- */

static float scroll_projects;
static char proj_names[48][32];
static int proj_shapes[48];
static int proj_count; static double proj_listed = -10;
static char delete_name[32]; static double delete_confirm;

static void screen_projects(void) {
    ui_title(CX, 36, "Projects", "Saved on this headset, in Android/data/com.xobnvr.flaux/files/projects.");
    if (ui_time - proj_listed > 1.0) {
        proj_count = ed_project_list(proj_names, 48); proj_listed = ui_time;
        for (int i = 0; i < proj_count; i++) proj_shapes[i] = ed_project_count(proj_names[i]);
    }

    ui_card(CX, 150, 620, 774);
    ui_text_bold(CX + 28, 170, 28, C_TEXT, "This project");
    ui_text(CX + 28, 224, 22, C_DIM, "Name");
    static char name_buf[32];
    if (ed.edit != name_buf) snprintf(name_buf, sizeof name_buf, "%s", ed.project);
    if (field(CX + 28, 254, 564, 66, name_buf, 28, "My world")) {
        if (name_buf[0]) snprintf(ed.project, sizeof ed.project, "%s", name_buf);
    } else if (ed.edit == name_buf && name_buf[0]) {
        snprintf(ed.project, sizeof ed.project, "%s", name_buf);
    }
    char buf[96];
    snprintf(buf, sizeof buf, "%d shape%s.  %s", g_obj_count, g_obj_count == 1 ? "" : "s",
             g_scene_dirty ? "There are changes you haven't saved." : "Everything is saved.");
    ui_text(CX + 28, 338, 22, g_scene_dirty ? C_AMBER : C_GREEN, buf);
    if (ui_icon_button(CX + 28, 390, 564, 72, "\xE2\x9C\x93", "Save", BTN_PRIMARY)) { ed.edit = NULL; ed_project_save(ed.project); proj_listed = -10; }
    if (ui_icon_button(CX + 28, 478, 564, 64, "\xE2\x9C\x9A", "New empty project", BTN_NORMAL)) { ed.edit = NULL; ed_project_new(); }
    if (ui_icon_button(CX + 28, 556, 564, 64, "\xE2\x9C\x8E", "Write it as a Flaux program", BTN_NORMAL)) ed_project_export();
    ui_text(CX + 28, 644, 21, C_FAINT, "\"Write it as a Flaux program\" makes a .flx file in the");
    ui_text(CX + 28, 672, 21, C_FAINT, "exports folder. Copy it to a computer and open it in");
    ui_text(CX + 28, 700, 21, C_FAINT, "Flaux Studio to see your build as code.");
    ui_text(CX + 28, 760, 21, C_FAINT, "Your work is also kept by itself every few seconds,");
    ui_text(CX + 28, 788, 21, C_FAINT, "so it's still here next time, even if you don't save.");

    float x = CX + 640, w = 720;
    ui_card(x, 150, w, 774);
    snprintf(buf, sizeof buf, "Saved projects (%d)", proj_count);
    ui_text_bold(x + 28, 170, 28, C_TEXT, buf);
    if (!proj_count) ui_text_center(x + w * 0.5f, 500, 25, C_DIM, "Nothing saved yet. Press Save to keep this one.");
    const float row = 84;
    list_begin(x + 12, 226, w - 24, 684, proj_count * row, &scroll_projects);
    for (int i = 0; i < proj_count; i++) {
        float y = 226 + i * row - scroll_projects;
        if (y < 226 - row || y > 226 + 684) continue;
        int current = !strcmp(proj_names[i], ed.project);
        ui_frame_rect(x + 20, y, w - 64, row - 10, 20, rgba(0x10173C, 0.9f), 1.5f, current ? rgba(0x4FE3F5, 0.8f) : rgba(0xFFFFFF, 0.10f));
        ui_text_fit(x + 44, y + 10, 27, w - 400, C_TEXT, proj_names[i]);
        snprintf(buf, sizeof buf, "%d shape%s%s", proj_shapes[i], proj_shapes[i] == 1 ? "" : "s", current ? "  (open now)" : "");
        ui_text(x + 44, y + 44, 20, current ? C_CYAN : C_FAINT, buf);
        if (ui_button(x + w - 320, y + 9, 150, 56, "Open", BTN_NORMAL)) ed_project_open(proj_names[i]);
        int sure = !strcmp(delete_name, proj_names[i]) && ui_time < delete_confirm;
        if (ui_button(x + w - 160, y + 9, 104, 56, sure ? "Sure?" : "\xE2\x9C\x95", BTN_DANGER)) {
            if (sure) { ed_project_delete(proj_names[i]); proj_listed = -10; delete_name[0] = 0; }
            else { snprintf(delete_name, sizeof delete_name, "%s", proj_names[i]); delete_confirm = ui_time + 3.0; }
        }
    }
    list_end();
}

/* ---------- Settings ---------- */

static void screen_settings(void) {
    ui_title(CX, 36, "Settings", "Make Flaux VR comfortable for you.");
    float w = (CW - 20) * 0.5f, x2 = CX + w + 20;
    int changed = 0;

    ui_card(CX, 150, w, 380);
    ui_text_bold(CX + 28, 170, 28, C_TEXT, "This panel");
    changed |= ui_slider(CX + 32, 226, w - 64, "Distance from you", &cfg.dist, 0.35f, 1.3f, "%.2f m");
    changed |= ui_slider(CX + 32, 322, w - 64, "Size", &cfg.scale, 0.6f, 1.6f, "%.2fx");
    if (ui_icon_button(CX + 32, 432, w - 64, 66, "\xE2\x9C\xA6", "Bring the panel to me", BTN_PRIMARY)) ed_summon_panel();

    ui_card(x2, 150, w, 380);
    ui_text_bold(x2 + 28, 170, 28, C_TEXT, "Moving");
    changed |= ui_slider(x2 + 32, 226, w - 64, "Walking speed", &cfg.speed, 0.5f, 8.0f, "%.1f m/s");
    changed |= ui_toggle(x2 + 32, 330, w - 64, "Turn in steps (more comfortable)", &cfg.snap_turn);
    if (ui_button(x2 + 32, 432, w - 64, 66, "Go back to the middle of the plate", BTN_NORMAL)) {
        ed_teleport(v3(0, 0, 2)); ed_summon_panel();
    }

    ui_card(CX, 550, w, 374);
    ui_text_bold(CX + 28, 570, 28, C_TEXT, "The world");
    changed |= ui_slider(CX + 32, 626, w - 64, "Star brightness", &cfg.sky, 0.2f, 2.0f, "%.1fx");
    changed |= ui_toggle(CX + 32, 726, w - 64, "Grid lines on the plate", &cfg.grid);
    changed |= ui_toggle(CX + 32, 794, w - 64, "Shadows", &cfg.shadows);

    ui_card(x2, 550, w, 374);
    ui_text_bold(x2 + 28, 570, 28, C_TEXT, "Feel");
    changed |= ui_toggle(x2 + 32, 622, w - 64, "Controller buzz", &cfg.haptics);
    changed |= ui_toggle(x2 + 32, 690, w - 64, "Sounds", &cfg.sound);
    ui_rect(x2 + 32, 776, w - 64, 2, 1, rgba(0xFFFFFF, 0.08f));
    ui_text_bold(x2 + 32, 800, 26, C_TEXT, "Flaux VR " FX_VERSION);
    ui_text(x2 + 32, 838, 21, C_DIM, "A native OpenXR editor for Meta Quest.");
    ui_text(x2 + 32, 868, 21, C_FAINT, "Part of Flaux, the coding language that reads like English.");
    static double save_at;
    if (changed) save_at = ui_time + 1.0;
    if (save_at > 0 && ui_time > save_at) { save_at = 0; ed_save_settings(); }
}

void screens_draw(void) {
    chrome();
    switch (ed.tab) {
    case TAB_HOME: screen_home(); break;
    case TAB_BUILD: screen_build(); break;
    case TAB_ASSETS: screen_assets(); break;
    case TAB_SCENE: screen_scene(); break;
    case TAB_VIEWER: screen_viewer(); break;
    case TAB_ROOMS: screen_rooms(); break;
    case TAB_COPILOT: screen_copilot(); break;
    case TAB_PROJECTS: screen_projects(); break;
    default: screen_settings(); break;
    }
    if (ed.edit_submit) { ed.edit_submit = 0; ed.edit = NULL; }       /* Enter with nothing listening */
}

/* ---------- the keyboard ---------- */

static void type_char(const char *s) {
    if (!ed.edit) return;
    size_t n = strlen(ed.edit), k = strlen(s);
    if ((int)(n + k) > ed.edit_max) { plat_sound(SND_ERROR); return; }
    memcpy(ed.edit + n, s, k + 1);
}

static void backspace(void) {
    if (!ed.edit) return;
    size_t n = strlen(ed.edit);
    while (n > 0 && ((unsigned char)ed.edit[n - 1] & 0xC0) == 0x80) n--;
    if (n > 0) n--;
    ed.edit[n] = 0;
}

void keyboard_draw(void) {
    static const char *rows[2][4] = {{"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"}, {"1234567890", "!@#$%&*()?", "+=/\\:;\"'_", "<>[]^~|"}};
    ui_glow(0, 0, KW, KH, 40, 50, rgba(0x22B8EE, 0.25f));
    ui_grad(0, 0, KW, KH, 40, rgba(0x0D1236, 0.99f), rgba(0x070A1E, 0.99f), 0);
    ui_frame_rect(0, 0, KW, KH, 40, NONE, 2.5f, rgba(0x4FE3F5, 0.55f));
    ui_frame_rect(66, 16, KW - 132, 60, 16, rgba(0x04061A, 0.9f), 1.5f, rgba(0xFFFFFF, 0.12f));
    ui_clip(76, 16, KW - 152, 60);
    float tw = ui_text_width(30, ed.edit ? ed.edit : "");
    float shift = tw > KW - 200 ? tw - (KW - 200) : 0;
    ui_text(90 - shift, 31, 30, C_TEXT, ed.edit ? ed.edit : "");
    if (fmod(ui_time, 1.0) < 0.55) ui_rect(93 + tw - shift, 28, 3, 36, 1.5f, C_CYAN);
    ui_unclip();

    const float x0 = 66, kw = 100, gap = 10.8f, kh = 70, y0 = 92, step = 80;
    for (int r = 0; r < 4; r++) {
        const char *keys = rows[ed.kb_sym][r];
        int n = (int)strlen(keys);
        float x = x0 + (r == 2 ? (kw + gap) * 0.5f : r == 3 ? (kw + gap) * 1.5f : 0);
        for (int i = 0; i < n; i++) {
            char label[2] = {keys[i], 0};
            if (ed.kb_shift && label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 32);
            if (ui_button(x + i * (kw + gap), y0 + r * step, kw, kh, label, BTN_NORMAL)) { type_char(label); ed.kb_shift = 0; }
        }
    }
    float wide = kw * 1.5f + gap * 0.5f, y3 = y0 + 3 * step, y4 = y0 + 4 * step;
    if (ui_button(x0, y3, wide, kh, "\xE2\x87\xA7", ed.kb_shift ? BTN_ON : BTN_NORMAL)) ed.kb_shift = !ed.kb_shift;
    float bx = x0 + (kw + gap) * 8.5f;
    static double held_since, next_repeat;
    int hand = ui_hover(bx, y3, wide, kh);
    if (ui_button(bx, y3, wide, kh, "\xE2\x8C\xAB", BTN_NORMAL)) { backspace(); held_since = ui_time; next_repeat = ui_time + 0.45; }
    else if (hand >= 0 && ui_ptr[hand].down && held_since > 0 && ui_time > next_repeat) { backspace(); next_repeat = ui_time + 0.07; }
    else if (hand < 0 || !ui_ptr[hand].down) held_since = 0;

    float x = x0;
    if (ui_button(x, y4, wide, kh, ed.kb_sym ? "abc" : "?123", ed.kb_sym ? BTN_ON : BTN_NORMAL)) ed.kb_sym = !ed.kb_sym;
    x += wide + gap;
    if (ui_button(x, y4, kw * 0.8f, kh, ",", BTN_NORMAL)) type_char(",");
    x += kw * 0.8f + gap;
    float space = (kw + gap) * 10 - gap - wide * 2 - kw * 3.4f - gap * 6;
    if (ui_button(x, y4, space, kh, "space", BTN_NORMAL)) type_char(" ");
    x += space + gap;
    if (ui_button(x, y4, kw * 0.8f, kh, ".", BTN_NORMAL)) type_char(".");
    x += kw * 0.8f + gap;
    if (ui_button(x, y4, kw * 0.8f, kh, "-", BTN_NORMAL)) type_char("-");
    x += kw * 0.8f + gap;
    if (ui_button(x, y4, kw, kh, "\xE2\x96\xBC", BTN_GHOST)) ed.edit = NULL;           /* put the keyboard away */
    x += kw + gap;
    if (ui_icon_button(x, y4, wide, kh, "\xE2\x8F\x8E", NULL, BTN_PRIMARY)) ed.edit_submit = 1;
}
