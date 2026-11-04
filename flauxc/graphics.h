/* ===================================================================
 * Flaux graphics: a window, shapes, text, keyboard, mouse and beeps.
 * Built on SDL2. Only added to programs that use graphics.
 *
 * Programs that use 3D or shaders get the OpenGL runtime (gl.h) instead of
 * the 2D drawing part at the end of this file. Everything else here (colours,
 * keys, the mouse and mouse look, game controllers, window events, `wait`,
 * sound and reading pictures) is shared. Fullscreen and sharp screens work
 * the same way for both (as below), but each draws its own way: the 2D part
 * here with a canvas texture, gl.h at the size the picture is shown at.
 *
 * For automated tests, these environment variables help:
 *   FLAUX_FRAMES=N        stop after N frames
 *   FLAUX_FAST=1          don't wait between frames (run as fast as possible)
 *   FLAUX_SCREENSHOT=f    save the last frame to f (a .bmp file) when stopping
 *   FLAUX_HOLD_KEYS=a,b   pretend these keys are held down the whole time
 *   FLAUX_MOUSE=x,y       pretend the mouse is at x, y in the real window
 *                         (window pixels, before fullscreen scaling)
 *   FLAUX_MOUSE_MOVE=dx,dy pretend the mouse moves this far every frame
 *                         (screen pixels: what mouse_moved_x/y() give)
 *   FLAUX_SCREEN=WxH      pretend the screen is W by H pixels: going fullscreen
 *                         makes the window that big (the test screen is 1024x768)
 *   FLAUX_SCREENSHOT_FULL=f  also save the last frame at the canvas's real size
 *   FLAUX_NO_GEOMETRY=1   draw triangles in rows of pixels, as without SDL_RenderGeometry
 *
 * Fullscreen: everything is drawn on a canvas texture, which show() copies to
 * the real window, letterboxed with black bars, so the program keeps its own
 * coordinates whatever the window's size. The canvas has as many pixels as it's
 * shown at (fx_cw by fx_ch): the size of window(...) in a normal window, more in
 * fullscreen or on a sharp (HiDPI) screen. Then everything is drawn at that full
 * resolution, fx_sx by fx_sy canvas pixels per program pixel: rects, dots, text
 * and pictures on whole program pixels (sharp and blocky, edges on whole canvas
 * pixels), triangles with SDL_RenderGeometry (SDL 2.0.18 and newer; otherwise
 * rows of canvas pixels), circles and lines in rows of canvas pixels. In a
 * normal window everything is drawn exactly as it always was. Switching
 * fullscreen makes a new canvas and copies the old picture onto it, scaled.
 * F11 or Alt+Enter switch fullscreen on and off (they're kept for that, so the
 * program doesn't see F11, or Enter while Alt is held), and fullscreen() asks
 * for it. Mouse positions are turned back into the program's pixels, and
 * screenshot() saves the program's pixels (each the colour at its middle).
 *
 * Mouse look: lock_mouse() hides the pointer and uses SDL's relative mouse
 * mode, so the mouse can move forever without stopping at the screen's edge.
 * mouse_moved_x/y() add up every SDL_MOUSEMOTION's xrel/yrel between frames,
 * locked or not, in window coordinates as SDL gives them (screen pixels). They
 * are NOT scaled like mouse_x/y(): the same hand movement gives the same
 * numbers in a window and fullscreen, so a game's turn speed feels the same.
 * Esc lets go of the mouse (the
 * program still sees Esc), and while the program wants it locked, a click on
 * the window locks it again; unlock_mouse() lets go for good.
 * =================================================================== */
#define FX_USES_GRAPHICS 1
#define SDL_MAIN_HANDLED
#include <SDL.h>

/* stb_image (public domain) reads .png, .jpg, .bmp and .gif pictures */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#ifdef __TINYC__
#define STBI_NO_SIMD   /* (TinyCC, for Flaux Studio on the web's Export, has no SSE headers) */
#endif
#ifdef _WIN32
#define STBI_WINDOWS_UTF8
#endif
#include "stb_image.h"

#ifndef FX_PROGRAM_DIR
#define FX_PROGRAM_DIR ""
#endif

static SDL_Window *fx_win;
static SDL_Renderer *fx_ren;
static SDL_Texture *fx_font_tex;
static SDL_Texture *fx_canvas;     /* what the program draws on (NULL: draws on the window itself) */
static SDL_Surface *fx_canvas_copy;/* the canvas, saved when fullscreen was last switched */
static bool fx_full;               /* fullscreen now */
static int fx_cw, fx_ch;           /* the canvas's real size in pixels (see "Fullscreen" at the top) */
static double fx_sx = 1, fx_sy = 1;/* canvas pixels per program pixel: 1 in a normal window */
static bool fx_geometry = true;    /* SDL_RenderGeometry works (until it says it doesn't) */
static int fx_win_w, fx_win_h;
static bool fx_dirty;              /* something drawn since the last show() */
static uint8_t fx_keys_now[SDL_NUM_SCANCODES], fx_keys_prev[SDL_NUM_SCANCODES];
static bool fx_mouse_now, fx_mouse_prev;
static int fx_mouse_xv, fx_mouse_yv;
static double fx_move_x, fx_move_y;        /* mouse movement since the last frame, screen pixels */
static int64_t fx_moved_xv, fx_moved_yv;   /* ... as the program sees it this frame */
static bool fx_lock_wanted;                /* lock_mouse() was called (and not unlock_mouse()) */
static uint64_t fx_next_frame;
static int64_t fx_frames, fx_frame_limit = -1;
static const char *fx_shot_path;
static bool fx_fast;
static double fx_start_time;      /* when the window opened, for time in shaders */
static SDL_AudioDeviceID fx_audio;
static bool fx_audio_failed;       /* no sound on this computer: don't keep trying */

typedef struct { uint8_t r, g, b; } fx_rgb_t;

static const struct { const char *name; uint32_t rgb; } fx_colors[] = {
/*@COLORS@*/
};

/*@FONT@*/

/* the Flaux logo, shown in the corner of game windows */
/*@ICON@*/

/* ------------------------------------------------------------ helpers */

static void fx_need_window(int line) {
    if (!fx_win)
        fx_fail(line, "Open a window first, at the top of your program, like this:\n\n"
                      "      window(800, 600)",
                "There's no window to draw in yet.");
}

/* lower-case, without spaces, dashes or underscores: "Dark Green" -> "darkgreen" */
static void fx_squash(fx_text t, char *out, size_t size) {
    size_t n = 0;
    for (int64_t i = 0; i < t.n && n + 1 < size; i++) {
        char c = t.p[i];
        if (c == ' ' || c == '_' || c == '-') continue;
        out[n++] = (char)tolower((unsigned char)c);
    }
    out[n] = 0;
}

static int fx_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Colours already worked out, by where their text is (so the same colour
   name in a loop is only looked up once) */
static struct { const char *p; int64_t n; fx_rgb_t c; } fx_color_cache[256];

static fx_rgb_t fx_color(fx_text t, int line) {
    unsigned slot = (unsigned)(((uintptr_t)t.p >> 3) ^ ((uintptr_t)t.p >> 11) ^ (unsigned)t.n) & 255u;
    if (fx_color_cache[slot].p == t.p && fx_color_cache[slot].n == t.n && t.p) return fx_color_cache[slot].c;
    char key[40];
    fx_squash(t, key, sizeof key);
    fx_rgb_t c = {0, 0, 0};
    bool found = false;
    size_t n = strlen(key);
    if (key[0] == '#' && (n == 4 || n == 7)) {
        int v[6], digits = (int)n - 1;
        found = true;
        for (int i = 0; i < digits; i++) if ((v[i] = fx_hex(key[i + 1])) < 0) found = false;
        if (found && digits == 3) c = (fx_rgb_t){ (uint8_t)(v[0] * 17), (uint8_t)(v[1] * 17), (uint8_t)(v[2] * 17) };
        if (found && digits == 6) c = (fx_rgb_t){ (uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]), (uint8_t)(v[4] * 16 + v[5]) };
    } else {
        for (size_t i = 0; i < sizeof fx_colors / sizeof fx_colors[0]; i++) {
            if (strcmp(fx_colors[i].name, key) == 0) {
                uint32_t rgb = fx_colors[i].rgb;
                c = (fx_rgb_t){ (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb };
                found = true;
                break;
            }
        }
    }
    if (!found)
        fx_fail(line, "Use a colour name like \"red\", \"blue\", \"dark green\" or \"sky\", a code like "
                      "\"#ff8800\",\n  or make one with rgb(255, 136, 0).",
                "I don't know the colour \"%.*s\".", (int)t.n, t.p);
    /* "#rrggbb" codes (and everything rgb(...) makes) are quick to read
       again, so only names are remembered */
    if (key[0] != '#') {
        fx_color_cache[slot].p = t.p;
        fx_color_cache[slot].n = t.n;
        fx_color_cache[slot].c = c;
    }
    return c;
}

static int fx_px(double v) { /* a position on screen, rounded to the nearest pixel */
    if (v > 1e6) return 1000000;
    if (v < -1e6) return -1000000;
    return (int)lround(v);
}

/* Drawing at more than one canvas pixel per program pixel (fullscreen, a sharp screen). */
static bool fx_hires(void) { return fx_cw != fx_win_w || fx_ch != fx_win_h; }

/* The edge of whole program pixel x (or y) on the canvas. */
static int fx_cx(int x) { return (int)lround(x * fx_sx); }
static int fx_cy(int y) { return (int)lround(y * fx_sy); }

/* Whole program pixels [x0, x1) by [y0, y1), with edges on whole canvas pixels: sharp, and
   boxes side by side meet exactly. In a normal window it's just that rectangle. */
static SDL_Rect fx_box(int x0, int y0, int x1, int y1) {
    int a = fx_cx(x0), b = fx_cy(y0);
    SDL_Rect r = { a, b, fx_cx(x1) - a, fx_cy(y1) - b };
    return r;
}

/* ------------------------------------------------------------- keys */

static int fx_scancodes(fx_text name, int line, SDL_Scancode out[2]) {
    char key[40];
    fx_squash(name, key, sizeof key);
    out[1] = SDL_SCANCODE_UNKNOWN;
    if (!strcmp(key, "shift")) { out[0] = SDL_SCANCODE_LSHIFT; out[1] = SDL_SCANCODE_RSHIFT; return 2; }
    if (!strcmp(key, "ctrl") || !strcmp(key, "control")) { out[0] = SDL_SCANCODE_LCTRL; out[1] = SDL_SCANCODE_RCTRL; return 2; }
    if (!strcmp(key, "alt")) { out[0] = SDL_SCANCODE_LALT; out[1] = SDL_SCANCODE_RALT; return 2; }
    const char *sdl = key;
    if (!strcmp(key, "enter")) sdl = "return";
    else if (!strcmp(key, "esc")) sdl = "escape";
    else if (!strcmp(key, "pageup")) sdl = "pageup";
    out[0] = SDL_GetScancodeFromName(sdl);
    if (out[0] == SDL_SCANCODE_UNKNOWN)
        fx_fail(line, "Key names are letters (\"a\" to \"z\"), numbers (\"0\" to \"9\"), arrows (\"left\", "
                      "\"right\", \"up\", \"down\"),\n  and \"space\", \"enter\", \"escape\", \"shift\", \"ctrl\", "
                      "\"alt\", \"tab\", \"backspace\" and \"f1\" to \"f12\".",
                "I don't know the key \"%.*s\".", (int)name.n, name.p);
    return 1;
}

static bool fx_key_down(fx_text name, int line) {
    SDL_Scancode sc[2];
    int n = fx_scancodes(name, line, sc);
    for (int i = 0; i < n; i++) if (fx_keys_now[sc[i]]) return true;
    return false;
}

static bool fx_key_pressed(fx_text name, int line) {
    SDL_Scancode sc[2];
    int n = fx_scancodes(name, line, sc);
    for (int i = 0; i < n; i++) if (fx_keys_now[sc[i]] && !fx_keys_prev[sc[i]]) return true;
    return false;
}

/* ------------------------------------------------------ game controllers */

/* Xbox, PlayStation, Switch and most other game controllers (SDL knows
   them all). Button names: "a", "b", "x", "y", "start", "back", "lb", "rb",
   "lt", "rt", "l3", "r3" and the D-pad "up", "down", "left", "right".
   VR controllers (when a program runs in a headset) use the same names. */
#define FX_PAD_BUTTONS 20
static SDL_GameController *fx_pads[4];
static uint8_t fx_pad_now[FX_PAD_BUTTONS], fx_pad_prev[FX_PAD_BUTTONS];
static double fx_stick[2][2];          /* [left/right][x/y], -1 to 1 */
static double fx_xr_buttons[FX_PAD_BUTTONS]; /* from VR controllers, if any */
static bool fx_xr_running;                    /* in a VR headset right now */
static double fx_xr_stick[2][2];

enum { FX_PAD_A, FX_PAD_B, FX_PAD_X, FX_PAD_Y, FX_PAD_START, FX_PAD_BACK, FX_PAD_LB, FX_PAD_RB,
       FX_PAD_LT, FX_PAD_RT, FX_PAD_L3, FX_PAD_R3, FX_PAD_UP, FX_PAD_DOWN, FX_PAD_LEFT, FX_PAD_RIGHT };

static int fx_pad_button(fx_text name, int line) {
    static const char *const names[] = { "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt",
                                         "l3", "r3", "up", "down", "left", "right" };
    char key[40];
    fx_squash(name, key, sizeof key);
    if (!strcmp(key, "select") || !strcmp(key, "view")) strcpy(key, "back");
    if (!strcmp(key, "menu") || !strcmp(key, "options")) strcpy(key, "start");
    if (!strcmp(key, "leftbumper") || !strcmp(key, "l1") || !strcmp(key, "grip")) strcpy(key, "lb");
    if (!strcmp(key, "rightbumper") || !strcmp(key, "r1")) strcpy(key, "rb");
    if (!strcmp(key, "lefttrigger") || !strcmp(key, "l2")) strcpy(key, "lt");
    if (!strcmp(key, "righttrigger") || !strcmp(key, "r2") || !strcmp(key, "trigger")) strcpy(key, "rt");
    for (int i = 0; i < 16; i++) if (!strcmp(names[i], key)) return i;
    fx_fail(line, "Controller buttons are \"a\", \"b\", \"x\", \"y\", \"start\", \"back\", \"lb\", \"rb\" "
                  "(bumpers),\n  \"lt\", \"rt\" (triggers), \"l3\", \"r3\" (pressing a stick) and \"up\", "
                  "\"down\", \"left\", \"right\" (the D-pad).",
            "I don't know the controller button \"%.*s\".", (int)name.n, name.p);
    return 0;
}

static void fx_pad_connect(int index) {
    for (int i = 0; i < 4; i++) {
        if (!fx_pads[i]) {
            fx_pads[i] = SDL_GameControllerOpen(index);
            return;
        }
    }
}

static void fx_read_pads(void) {
    memcpy(fx_pad_prev, fx_pad_now, sizeof fx_pad_now);
    memset(fx_pad_now, 0, sizeof fx_pad_now);
    memset(fx_stick, 0, sizeof fx_stick);
    static const SDL_GameControllerButton map[16] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
        SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_DPAD_UP,
        SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    for (int p = 0; p < 4; p++) {
        SDL_GameController *c = fx_pads[p];
        if (!c) continue;
        if (!SDL_GameControllerGetAttached(c)) {
            SDL_GameControllerClose(c);
            fx_pads[p] = NULL;
            continue;
        }
        for (int i = 0; i < 16; i++)
            if (map[i] != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(c, map[i])) fx_pad_now[i] = 1;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) fx_pad_now[FX_PAD_LT] = 1;
        if (SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) fx_pad_now[FX_PAD_RT] = 1;
        const SDL_GameControllerAxis axes[2][2] = { { SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY },
                                                    { SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY } };
        for (int s = 0; s < 2; s++)
            for (int a = 0; a < 2; a++) {
                double v = SDL_GameControllerGetAxis(c, axes[s][a]) / 32767.0;
                if (v < -1) v = -1;
                if (fabs(v) < 0.15) v = 0;          /* sticks never rest exactly in the middle */
                if (a == 1) v = -v;                  /* up is positive, like y in 3D */
                if (fabs(v) > fabs(fx_stick[s][a])) fx_stick[s][a] = v;
            }
    }
    for (int i = 0; i < FX_PAD_BUTTONS; i++) if (fx_xr_buttons[i] > 0.5) fx_pad_now[i] = 1;
    for (int s = 0; s < 2; s++)
        for (int a = 0; a < 2; a++)
            if (fabs(fx_xr_stick[s][a]) > fabs(fx_stick[s][a])) fx_stick[s][a] = fx_xr_stick[s][a];
    const char *held = getenv("FLAUX_HOLD_BUTTONS"); /* for tests */
    if (held && *held) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s", held);
        for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            if (!strncmp(tok, "stick=", 6)) { fx_stick[0][0] = atof(tok + 6); continue; }
            fx_pad_now[fx_pad_button((fx_text){ tok, (int64_t)strlen(tok) }, 0)] = 1;
        }
    }
}

static bool fx_button_down(fx_text name, int line) { return fx_pad_now[fx_pad_button(name, line)] != 0; }
static bool fx_button_pressed(fx_text name, int line) {
    int b = fx_pad_button(name, line);
    return fx_pad_now[b] && !fx_pad_prev[b];
}

static int fx_stick_side(fx_text side, int line) {
    char key[40];
    fx_squash(side, key, sizeof key);
    if (!strcmp(key, "left") || !key[0]) return 0;
    if (!strcmp(key, "right")) return 1;
    fx_fail(line, "Use stick_x(\"left\") or stick_x(\"right\").", "A controller has a \"left\" and a \"right\" stick, not \"%.*s\".",
            (int)side.n, side.p);
    return 0;
}
static double fx_stick_x(fx_text side, int line) { return fx_stick[fx_stick_side(side, line)][0]; }
static double fx_stick_y(fx_text side, int line) { return fx_stick[fx_stick_side(side, line)][1]; }
/* ------------------------------------------------------- fullscreen */

/* (See "Fullscreen" at the top.) fx_out_w by fx_out_h: how many pixels the real window has
   (more than its size on a sharp screen), measured by fx_measure() at each show(). */
static int fx_out_w, fx_out_h;

/* Where a w-by-h picture goes in an out_w-by-out_h space: as big as fits,
   same shape, in the middle (the rest is black bars). */
static SDL_Rect fx_fit(int out_w, int out_h, int w, int h) {
    SDL_Rect r = { 0, 0, out_w, out_h };
    if (out_w <= 0 || out_h <= 0 || w <= 0 || h <= 0) return r;
    if ((int64_t)out_w * h <= (int64_t)out_h * w) {      /* bars at the top and bottom */
        r.h = (int)((int64_t)out_w * h / w);
        r.y = (out_h - r.h) / 2;
    } else {                                             /* bars at the sides */
        r.w = (int)((int64_t)out_h * w / h);
        r.x = (out_w - r.w) / 2;
    }
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    return r;
}

/* A mouse position in the real window -> canvas pixels, kept inside the
   canvas (so the black bars give the nearest edge, never a position off it). */
static void fx_window_to_canvas(int wx, int wy, int *cx, int *cy) {
    int ww = fx_win_w, wh = fx_win_h, ow = fx_out_w, oh = fx_out_h;
    if (fx_win) SDL_GetWindowSize(fx_win, &ww, &wh);
    double px = ww > 0 ? (double)wx * ow / ww : wx, py = wh > 0 ? (double)wy * oh / wh : wy;
    SDL_Rect v = fx_fit(ow, oh, fx_win_w, fx_win_h);
    int x = (int)floor((px - v.x) * fx_win_w / v.w), y = (int)floor((py - v.y) * fx_win_h / v.h);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > fx_win_w - 1) x = fx_win_w - 1;
    if (y > fx_win_h - 1) y = fx_win_h - 1;
    *cx = x;
    *cy = y;
}

/* Where the mouse is, in canvas pixels. Gives the mouse buttons. */
static Uint32 fx_read_mouse(void) {
    int wx = 0, wy = 0;
    Uint32 buttons = SDL_GetMouseState(&wx, &wy);
    const char *fake = getenv("FLAUX_MOUSE"); /* for tests */
    if (fake && *fake) sscanf(fake, "%d,%d", &wx, &wy);
    fx_window_to_canvas(wx, wy, &fx_mouse_xv, &fx_mouse_yv);
    return buttons;
}

/* Each way of drawing (2D here, 3D in gl.h) has its own: */
static void fx_set_fullscreen(bool on);   /* switch fullscreen on or off */
static void fx_redisplay(void);           /* show the last frame again (the window changed) */
static void fx_targets_reset(void);       /* the graphics card lost what was drawn */

/* ------------------------------------------------------- mouse look */

static void fx_set_mouse_lock(bool on) {
    if (!fx_win || on == (bool)SDL_GetRelativeMouseMode()) return;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);   /* fails quietly if this computer can't */
}

static void fx_g_lock_mouse(bool on, int line) {
    if (on) fx_need_window(line);
    fx_lock_wanted = on;
    fx_set_mouse_lock(on);
}

static bool fx_mouse_locked(void) { return fx_win && SDL_GetRelativeMouseMode(); }

/* Handle one event from the window. Gives true when the window was closed. */
static bool fx_handle_event(const SDL_Event *ev) {
    if (ev->type == SDL_QUIT) return true;
    if (ev->type == SDL_MOUSEMOTION) {      /* as SDL gives it: not scaled to the canvas */
        fx_move_x += ev->motion.xrel;
        fx_move_y += ev->motion.yrel;
    }
    /* Esc lets go of a locked mouse; a click locks it again while the program wants that */
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat && ev->key.keysym.scancode == SDL_SCANCODE_ESCAPE)
        fx_set_mouse_lock(false);
    if (ev->type == SDL_MOUSEBUTTONDOWN && fx_lock_wanted) fx_set_mouse_lock(true);
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat &&
        (ev->key.keysym.scancode == SDL_SCANCODE_F11 ||
         ((ev->key.keysym.scancode == SDL_SCANCODE_RETURN || ev->key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
          (ev->key.keysym.mod & KMOD_ALT))))
        fx_set_fullscreen(!fx_full);
    if (ev->type == SDL_RENDER_TARGETS_RESET) fx_targets_reset();
    if (ev->type == SDL_WINDOWEVENT &&
        (ev->window.event == SDL_WINDOWEVENT_EXPOSED || ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
        fx_redisplay();   /* (and the picture gets as many pixels as the window has now) */
    if (ev->type == SDL_CONTROLLERDEVICEADDED) fx_pad_connect(ev->cdevice.which);
    return false;
}

static void fx_g_fullscreen(int line) {
    fx_need_window(line);
    fx_set_fullscreen(true);
}

static void fx_read_input(void) {
    memcpy(fx_keys_prev, fx_keys_now, sizeof fx_keys_now);
    int count = 0;
    const Uint8 *state = SDL_GetKeyboardState(&count);
    if (count > SDL_NUM_SCANCODES) count = SDL_NUM_SCANCODES;
    memcpy(fx_keys_now, state, (size_t)count);
    const char *held = getenv("FLAUX_HOLD_KEYS"); /* for tests */
    if (held && *held) {
        char buf[256];
        snprintf(buf, sizeof buf, "%s", held);
        for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            SDL_Scancode sc[2];
            int n = fx_scancodes((fx_text){ tok, (int64_t)strlen(tok) }, 0, sc);
            for (int i = 0; i < n; i++) fx_keys_now[sc[i]] = 1;
        }
    }
    /* F11 and Alt+Enter switch fullscreen (see fx_handle_event), so the program doesn't get them */
    fx_keys_now[SDL_SCANCODE_F11] = 0;
    if (fx_keys_now[SDL_SCANCODE_LALT] || fx_keys_now[SDL_SCANCODE_RALT])
        fx_keys_now[SDL_SCANCODE_RETURN] = fx_keys_now[SDL_SCANCODE_KP_ENTER] = 0;
    fx_mouse_prev = fx_mouse_now;
    fx_mouse_now = (fx_read_mouse() & SDL_BUTTON_LMASK) != 0;
    /* mouse movement: whole pixels this frame, the rest is kept for the next one */
    const char *move = getenv("FLAUX_MOUSE_MOVE"); /* for tests */
    if (move && *move) {
        double dx = 0, dy = 0;
        sscanf(move, "%lf,%lf", &dx, &dy);
        fx_move_x += dx;
        fx_move_y += dy;
    }
    fx_moved_xv = (int64_t)fx_move_x;
    fx_moved_yv = (int64_t)fx_move_y;
    fx_move_x -= (double)fx_moved_xv;
    fx_move_y -= (double)fx_moved_yv;
    fx_read_pads();
}

static int64_t fx_mouse_x(void) { return fx_mouse_xv; }
static int64_t fx_mouse_y(void) { return fx_mouse_yv; }
static bool fx_mouse_down(void) { return fx_mouse_now; }
static bool fx_mouse_clicked(void) { return fx_mouse_now && !fx_mouse_prev; }
static int64_t fx_mouse_moved_x(void) { return fx_moved_xv; }
static int64_t fx_mouse_moved_y(void) { return fx_moved_yv; }

/* ----------------------------------------------------------- window */

static void fx_g_wait_events(void);

/* Everything before the window itself: checks, and starting SDL. */
static void fx_start_window(int64_t w, int64_t h, int line) {
    if (fx_win)
        fx_fail(line, "Call window(...) just once, at the start of your program.",
                "There's already a window open.");
    if (w < 50 || h < 50 || w > 8000 || h > 8000)
        fx_fail(line, "Pick a size between 50 and 8000, like window(800, 600).",
                "A window can't be %" PRId64 " by %" PRId64 " pixels.", w, h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
        fx_fail(line, "Make sure this computer can show windows. (On WSL you need Windows 11, or an X server.\n"
                      "  On a computer you reach over the internet, windows can't be shown.)",
                "Flaux couldn't open a window: %s", SDL_GetError());
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER); /* fine if this computer has none */
    /* SDL can fall back to drawing nowhere when there's no screen. Unless a
       test asked for that on purpose, say so instead of running invisibly. */
    const char *driver = SDL_GetCurrentVideoDriver();
    const char *wanted = getenv("SDL_VIDEODRIVER");
    if (driver && (!strcmp(driver, "offscreen") || !strcmp(driver, "dummy") || !strcmp(driver, "evdev")) &&
        !(wanted && *wanted))
        fx_fail(line, "Run your program on a computer with a screen. (On WSL you need Windows 11, or an X server.\n"
                      "  On a computer you reach over the internet, windows can't be shown.)",
                "Flaux couldn't find a screen to open the window on.");
}

/* The window itself, with the Flaux logo as its icon. */
static void fx_make_window(int64_t w, int64_t h, fx_text title, Uint32 flags, int line) {
    char *name = (char *)fx_text_make(title.p, title.n).p;
    /* ALLOW_HIGHDPI: on sharp screens (like a Mac's) the window has more pixels than its size,
       and the canvas gets them all (see "Fullscreen" at the top) */
    fx_win = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (int)w, (int)h, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | flags);
    if (!fx_win) fx_fail(line, NULL, "Flaux couldn't open a window: %s", SDL_GetError());
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom((void *)fx_icon, FX_ICON_SIZE, FX_ICON_SIZE, 32,
                                                           FX_ICON_SIZE * 4, SDL_PIXELFORMAT_RGBA32);
    if (icon) { SDL_SetWindowIcon(fx_win, icon); SDL_FreeSurface(icon); }
    fx_win_w = fx_cw = fx_out_w = (int)w;
    fx_win_h = fx_ch = fx_out_h = (int)h;
    const char *frames = getenv("FLAUX_FRAMES");
    if (frames && *frames) fx_frame_limit = atoll(frames);
    fx_shot_path = getenv("FLAUX_SCREENSHOT");
    fx_fast = getenv("FLAUX_FAST") && *getenv("FLAUX_FAST");
    fx_start_time = SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) fx_pad_connect(i);
    fx_wait_events = fx_g_wait_events;   /* (see `wait` in runtime.h) */
}

/* Seconds since the window opened. In tests (FLAUX_FAST) it counts frames
   instead, so pictures come out the same every time. */
static double fx_seconds(void) {
    if (fx_fast) return fx_frames / 60.0;
    return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency() - fx_start_time;
}

/* Frames shown in the last second, so you can see how fast a game runs. */
static double fx_fps_value;
static int64_t fx_fps_frames;
static double fx_fps_since;
static void fx_count_frame(void) {
    double now = SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    if (fx_fps_since == 0) fx_fps_since = now;
    fx_fps_frames++;
    if (now - fx_fps_since >= 0.5) {
        fx_fps_value = fx_fps_frames / (now - fx_fps_since);
        fx_fps_frames = 0;
        fx_fps_since = now;
    }
}
static int64_t fx_fps(void) { return (int64_t)llround(fx_fps_value); }

static void fx_g_close(void) {
    if (fx_audio) {
        /* let the last beeps finish */
        for (int i = 0; i < 300 && SDL_GetQueuedAudioSize(fx_audio) > 0; i++) SDL_Delay(10);
    }
    SDL_Quit();
}

/* After a frame is shown: wait so games run at 60 frames a second, then
   read the keys, the mouse and the controllers. */
static void fx_next_frame_wait(void) {
    fx_count_frame();
    if (!fx_fast && !fx_xr_running) {   /* (in VR, the headset says when the next frame is) */
        uint64_t freq = SDL_GetPerformanceFrequency();
        fx_next_frame += freq / 60;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < fx_next_frame) SDL_Delay((Uint32)((fx_next_frame - now) * 1000 / freq));
        else if (now - fx_next_frame > freq / 4) fx_next_frame = now; /* fell far behind: catch up */
    }
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (fx_handle_event(&ev)) { /* the window's close button */
            fx_g_close();
            exit(0);
        }
    }
    fx_read_input();
}

/* While the program waits (`wait 2s`): handle the window's events, so it
 * doesn't go "Not responding" and its close button still ends the program. */
static void fx_g_wait_events(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (fx_handle_event(&ev)) {
            fx_g_close();
            exit(0);
        }
    }
}

/* ------------------------------------------------------------ pictures */

static bool fx_is_absolute(const char *p) {
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

/* Read a picture file's pixels (as red, green, blue, alpha). Pictures are
   looked for next to the program first, then in the current folder. */
static unsigned char *fx_read_picture(const char *file, int *w, int *h, int line) {
    char path[4096];
    int comp = 0;
    unsigned char *px = NULL;
    if (!fx_is_absolute(file) && FX_PROGRAM_DIR[0]) {
        snprintf(path, sizeof path, "%s/%s", FX_PROGRAM_DIR, file);
        px = stbi_load(path, w, h, &comp, 4);
    }
    if (!px) {
        snprintf(path, sizeof path, "%s", file);
        px = stbi_load(path, w, h, &comp, 4);
    }
    if (!px && !fx_is_absolute(file)) {   /* next to the program itself (a game someone shared) */
        char *base = SDL_GetBasePath();
        if (base) {
            char here[4096];
            snprintf(here, sizeof here, "%s%s", base, file);
            SDL_free(base);
            px = stbi_load(here, w, h, &comp, 4);
        }
    }
    if (px) return px;
    FILE *f = fopen(path, "rb");
    if (!f && FX_PROGRAM_DIR[0]) {
        char there[4096];
        snprintf(there, sizeof there, "%s/%s", FX_PROGRAM_DIR, file);
        f = fopen(there, "rb");
    }
    if (!f) {
        char fix[4400];
        snprintf(fix, sizeof fix, "Put the picture in the same folder as your program%s%s,\n"
                 "  and check the name is exactly right (including .png at the end).",
                 FX_PROGRAM_DIR[0] ? ":\n\n      " : "", FX_PROGRAM_DIR[0] ? FX_PROGRAM_DIR : "");
        fx_fail(line, fix, "I can't find the picture \"%s\".", file);
    }
    fclose(f);
    fx_fail(line, "Flaux can read .png, .jpg, .bmp and .gif pictures. Try opening it in a paint\n"
                  "  program and saving it again as a .png.",
            "I couldn't read the picture \"%s\" (%s).", file, stbi_failure_reason());
    return NULL;
}

/* How wide text is on screen, to help centre it. */
static int64_t fx_text_width(fx_text t, int64_t size) {
    if (size < 1) size = 1;
    int64_t widest = 0, col = 0;
    for (int64_t i = 0; i < t.n; i++) {
        unsigned char ch = (unsigned char)t.p[i];
        if (ch == '\n') { col = 0; continue; }
        if (ch >= 0x80 && ch < 0xC0) continue;
        if (++col > widest) widest = col;
    }
    return widest ? (widest * 6 - 1) * size : 0;
}

/* -------------------------------------------------------------- sound */

static void fx_g_beep(double pitch, double seconds, int line) {
    (void)line;
    if (fx_audio_failed) return;
    if (!fx_audio) {
        fx_audio_failed = true; /* until it works */
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return; /* no sound on this computer: stay quiet */
        SDL_AudioSpec want;
        memset(&want, 0, sizeof want);
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        fx_audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (!fx_audio) return;
        fx_audio_failed = false;
        SDL_PauseAudioDevice(fx_audio, 0);
    }
    if (pitch <= 0 || seconds <= 0) return;
    if (seconds > 10) seconds = 10;
    int n = (int)(seconds * 44100), fade = 441;
    int16_t *buf = fx_alloc(sizeof(int16_t) * (size_t)n);
    for (int i = 0; i < n; i++) {
        double phase = fmod(i * pitch / 44100.0, 1.0);
        double amp = 2500;
        if (i < fade) amp *= (double)i / fade;                 /* soft start */
        if (n - i < fade) amp *= (double)(n - i) / fade;       /* soft end */
        buf[i] = (int16_t)(phase < 0.5 ? amp : -amp);
    }
    SDL_QueueAudio(fx_audio, buf, (Uint32)(sizeof(int16_t) * (size_t)n));
    free(buf);
}

#ifndef FX_USES_3D
/* ====================================================================
 * 2D drawing with SDL's renderer (programs without 3D or shaders)
 * ==================================================================== */

static void fx_use_color(fx_text color, int line) {
    fx_rgb_t c = fx_color(color, line);
    SDL_SetRenderDrawColor(fx_ren, c.r, c.g, c.b, 255);
    fx_dirty = true;
}

/* ------------------------------------------- fullscreen and sharp screens */

/* How many pixels the real window has (more than its size on a sharp screen), measured at
   each show(). (Some renderers give the canvas's size while drawing on it, so not then.) */
static void fx_measure(void) {
    if (!fx_ren) return;
    SDL_Texture *target = SDL_GetRenderTarget(fx_ren);
    if (target) SDL_SetRenderTarget(fx_ren, NULL);
    if (SDL_GetRendererOutputSize(fx_ren, &fx_out_w, &fx_out_h) != 0 || fx_out_w < 1 || fx_out_h < 1) {
        fx_out_w = fx_win_w;
        fx_out_h = fx_win_h;
    }
    if (target) SDL_SetRenderTarget(fx_ren, target);
}

/* The part of the real window the canvas is shown in, in renderer pixels. */
static SDL_Rect fx_view(void) {
    return fx_fit(fx_out_w, fx_out_h, fx_win_w, fx_win_h);
}

static void fx_sharp(SDL_Texture *t, bool sharp) { /* how a texture is scaled: sharp pixels, or smooth */
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(t, sharp ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#else
    (void)t; (void)sharp;
#endif
}

/* Give the canvas as many pixels as it's shown at (fullscreen, a sharp screen), but never fewer
   than the program's own window(...) size, keeping what's drawn on it (scaled). */
static void fx_fit_canvas(void) {
    if (!fx_canvas) return;
    SDL_Rect v = fx_view();
    int cw = v.w, ch = v.h;
    SDL_RendererInfo info;
    if (cw < fx_win_w || ch < fx_win_h ||
        (SDL_GetRendererInfo(fx_ren, &info) == 0 && info.max_texture_width > 0 && info.max_texture_height > 0 &&
         (cw > info.max_texture_width || ch > info.max_texture_height))) {
        cw = fx_win_w;
        ch = fx_win_h;
    }
    if (cw == fx_cw && ch == fx_ch) return;
    SDL_Texture *t = SDL_CreateTexture(fx_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, cw, ch);
    if (!t) return;                     /* (not enough memory: keep drawing on the one there is) */
    if (SDL_SetRenderTarget(fx_ren, t) != 0) {
        SDL_DestroyTexture(t);
        SDL_SetRenderTarget(fx_ren, fx_canvas);
        return;
    }
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(fx_ren, &r, &g, &b, &a);
    SDL_SetRenderDrawColor(fx_ren, 0, 0, 0, 255);
    SDL_RenderClear(fx_ren);
    /* the picture so far: sharp if it's a whole number of times bigger, otherwise smooth */
    fx_sharp(fx_canvas, cw % fx_cw == 0 && ch % fx_ch == 0);
    SDL_SetTextureBlendMode(fx_canvas, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(fx_ren, fx_canvas, NULL, NULL);
    SDL_DestroyTexture(fx_canvas);
    SDL_SetRenderDrawColor(fx_ren, r, g, b, a);
    fx_canvas = t;
    fx_cw = cw;
    fx_ch = ch;
    fx_sx = (double)cw / fx_win_w;
    fx_sy = (double)ch / fx_win_h;
}

/* Put the canvas on the screen. */
static void fx_present(void) {
    fx_measure();
    if (!fx_canvas) { SDL_RenderPresent(fx_ren); return; }
    fx_fit_canvas();      /* (the window may have changed: moved to a sharper screen, say) */
    SDL_Rect v = fx_view();
    /* pixel for pixel (or a whole number of times bigger): sharp; otherwise smooth, so pixels don't come out uneven */
    fx_sharp(fx_canvas, v.w % fx_cw == 0 && v.h % fx_ch == 0);
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor(fx_ren, &r, &g, &b, &a);
    SDL_SetRenderTarget(fx_ren, NULL);
    SDL_SetRenderDrawColor(fx_ren, 0, 0, 0, 255);
    SDL_RenderClear(fx_ren);
    SDL_RenderCopy(fx_ren, fx_canvas, NULL, &v);
    SDL_RenderPresent(fx_ren);
    SDL_SetRenderTarget(fx_ren, fx_canvas);
    SDL_SetRenderDrawColor(fx_ren, r, g, b, a);
}

/* Put back the canvas saved at the last switch (some computers lose it then). */
static void fx_restore_canvas(void) {
    if (!fx_canvas || !fx_canvas_copy) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(fx_ren, fx_canvas_copy);
    if (!t) return;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(fx_ren, fx_canvas);
    SDL_RenderCopy(fx_ren, t, NULL, NULL);
    SDL_DestroyTexture(t);
}

static void fx_set_fullscreen(bool on) {
    if (!fx_win || on == fx_full) return;
    if (fx_canvas) {
        if (fx_canvas_copy && (fx_canvas_copy->w != fx_cw || fx_canvas_copy->h != fx_ch)) {
            SDL_FreeSurface(fx_canvas_copy);
            fx_canvas_copy = NULL;
        }
        if (!fx_canvas_copy)
            fx_canvas_copy = SDL_CreateRGBSurfaceWithFormat(0, fx_cw, fx_ch, 32, SDL_PIXELFORMAT_ARGB8888);
        if (fx_canvas_copy && SDL_RenderReadPixels(fx_ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                                                   fx_canvas_copy->pixels, fx_canvas_copy->pitch) != 0) {
            SDL_FreeSurface(fx_canvas_copy);
            fx_canvas_copy = NULL;
        }
    } else if (on) {
        SDL_RenderSetLogicalSize(fx_ren, fx_win_w, fx_win_h); /* no canvas: let SDL scale the drawing */
    }
    int sw = 0, sh = 0;
    const char *screen = getenv("FLAUX_SCREEN"); /* for tests: a pretend screen of that size */
    if (screen && sscanf(screen, "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0)
        SDL_SetWindowSize(fx_win, on ? sw : fx_win_w, on ? sh : fx_win_h);
    else if (SDL_SetWindowFullscreen(fx_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
        return;
    fx_full = on;
    if (!on) SDL_SetWindowSize(fx_win, fx_win_w, fx_win_h);
    fx_restore_canvas();
    fx_present();         /* (this also gives the canvas the screen's resolution) */
    SDL_PumpEvents();
    fx_read_mouse();    /* the same spot on the screen is somewhere else on the canvas now */
}


static void fx_redisplay(void) { fx_present(); }
static void fx_targets_reset(void) { fx_restore_canvas(); fx_present(); }

static void fx_g_window(int64_t w, int64_t h, fx_text title, int line) {
    fx_start_window(w, h, line);
    fx_make_window(w, h, title, 0, line);
    fx_ren = SDL_CreateRenderer(fx_win, -1, SDL_RENDERER_ACCELERATED);
    if (!fx_ren) fx_ren = SDL_CreateRenderer(fx_win, -1, SDL_RENDERER_SOFTWARE);
    if (!fx_ren) fx_fail(line, NULL, "Flaux couldn't draw in the window: %s", SDL_GetError());
    SDL_SetRenderDrawBlendMode(fx_ren, SDL_BLENDMODE_BLEND);
    /* the canvas the program draws on (see "Fullscreen" at the top) */
    if (SDL_RenderTargetSupported(fx_ren)) {
        fx_canvas = SDL_CreateTexture(fx_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, (int)w, (int)h);
        if (fx_canvas && SDL_SetRenderTarget(fx_ren, fx_canvas) != 0) {
            SDL_DestroyTexture(fx_canvas);
            fx_canvas = NULL;
        }
    }
    if (!fx_canvas) SDL_RenderSetLogicalSize(fx_ren, fx_win_w, fx_win_h); /* (a sharp screen: SDL scales it) */

    /* the font, as white letters on a see-through background */
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 95 * 6, 8, 32, SDL_PIXELFORMAT_RGBA8888);
    if (s) {
        Uint32 *px = (Uint32 *)s->pixels;
        int pitch = s->pitch / 4;
        for (int c = 0; c < 95; c++)
            for (int x = 0; x < 5; x++)
                for (int y = 0; y < 7; y++)
                    px[y * pitch + c * 6 + x] = (fx_font[c][x] >> y & 1) ? 0xFFFFFFFFu : 0x00000000u;
        fx_font_tex = SDL_CreateTextureFromSurface(fx_ren, s);
        SDL_SetTextureBlendMode(fx_font_tex, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(s);
    }

    if (getenv("FLAUX_NO_GEOMETRY") && *getenv("FLAUX_NO_GEOMETRY")) fx_geometry = false;

    SDL_SetRenderDrawColor(fx_ren, 0, 0, 0, 255);
    SDL_RenderClear(fx_ren);
    fx_measure();
    fx_fit_canvas();      /* a sharp screen: more pixels */
    if (getenv("FLAUX_FULLSCREEN") && *getenv("FLAUX_FULLSCREEN")) fx_set_fullscreen(true); /* for tests */
    SDL_PumpEvents();
    fx_read_input();
    fx_next_frame = SDL_GetPerformanceCounter();
}

/* Save what's drawn as a .bmp picture, in the program's own pixels (each one the colour at its
   middle on the canvas), or at the canvas's real size if `full`. */
static bool fx_save_bmp(const char *path, bool full) {
    int cw = fx_canvas ? fx_cw : fx_win_w, ch = fx_canvas ? fx_ch : fx_win_h;
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, cw, ch, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s || SDL_RenderReadPixels(fx_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) != 0) {
        if (s) SDL_FreeSurface(s);
        return false;
    }
    if (!full && (cw != fx_win_w || ch != fx_win_h)) {
        SDL_Surface *small = SDL_CreateRGBSurfaceWithFormat(0, fx_win_w, fx_win_h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (!small) { SDL_FreeSurface(s); return false; }
        for (int y = 0; y < fx_win_h; y++) {
            int from_y = (int)((y + 0.5) * fx_sy);
            const Uint32 *src = (const Uint32 *)((const Uint8 *)s->pixels + (size_t)(from_y < ch ? from_y : ch - 1) * s->pitch);
            Uint32 *dst = (Uint32 *)((Uint8 *)small->pixels + (size_t)y * small->pitch);
            for (int x = 0; x < fx_win_w; x++) {
                int from_x = (int)((x + 0.5) * fx_sx);
                dst[x] = src[from_x < cw ? from_x : cw - 1];
            }
        }
        SDL_FreeSurface(s);
        s = small;
    }
    bool ok = SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

static void fx_g_screenshot(fx_text path, int line) {
    fx_need_window(line);
    if (!fx_save_bmp(fx_text_make(path.p, path.n).p, false))
        fx_fail(line, "Check the file name, and that you're allowed to save files in that folder.",
                "Flaux couldn't save the screenshot: %s", SDL_GetError());
}

/* For tests: the screenshots asked for with FLAUX_SCREENSHOT and FLAUX_SCREENSHOT_FULL. */
static void fx_test_shots(int line) {
    if (fx_shot_path) fx_g_screenshot((fx_text){ fx_shot_path, (int64_t)strlen(fx_shot_path) }, line);
    const char *full = getenv("FLAUX_SCREENSHOT_FULL");
    if (full && *full) fx_save_bmp(full, true);
}

/* Show what was drawn, then wait so the game runs at 60 frames a second. */
static void fx_g_show(int line) {
    fx_need_window(line);
    fx_frames++;
    bool last = fx_frame_limit >= 0 && fx_frames >= fx_frame_limit;
    if (last) fx_test_shots(line);
    fx_present();
    fx_dirty = false;
    if (last) {
        fx_g_close();
        exit(0);
    }
    fx_next_frame_wait();
}

/* At the end of the program, keep the window open until it's closed. */
static void fx_g_finish(void) {
    if (!fx_win) {
        if (fx_audio) fx_g_close();
        return;
    }
    if (fx_frame_limit >= 0) { /* test mode */
        fx_test_shots(0);
        fx_g_close();
        return;
    }
    if (fx_dirty) fx_present();
    SDL_Event ev;
    while (SDL_WaitEvent(&ev))
        if (fx_handle_event(&ev)) break;
    fx_g_close();
}

/* ------------------------------------------------------------ drawing */

static void fx_g_clear(fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    SDL_RenderClear(fx_ren);
}

static void fx_g_rect(double x, double y, double w, double h, fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    SDL_Rect r = fx_box(fx_px(x), fx_px(y), fx_px(x + w), fx_px(y + h));
    SDL_RenderFillRect(fx_ren, &r);
}

/* A filled ellipse in canvas pixels (centre and radii not rounded): in each row, the pixels whose
   middles are inside. For drawing at more than one canvas pixel per program pixel. */
static void fx_fill_ellipse(double cx, double cy, double rx, double ry) {
    if (rx <= 0 || ry <= 0) return;
    double top = ceil(cy - ry - 0.5), bottom = floor(cy + ry - 0.5);
    if (top < 0) top = 0;
    if (bottom > fx_ch - 1) bottom = fx_ch - 1;
    if (top > bottom) return;            /* (off the canvas) */
    for (int y = (int)top; y <= (int)bottom; y++) {
        double t = (y + 0.5 - cy) / ry, half = 1 - t * t;
        if (half < 0) continue;
        half = rx * sqrt(half);
        double xs = ceil(cx - half - 0.5), xe = floor(cx + half - 0.5);
        if (xs < 0) xs = 0;
        if (xe > fx_cw - 1) xe = fx_cw - 1;
        if (xe >= xs) SDL_RenderDrawLine(fx_ren, (int)xs, y, (int)xe, y);
    }
}

static void fx_g_circle(double cx, double cy, double radius, fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    if (radius <= 0) return;
    if (radius > 20000) radius = 20000;
    if (fx_hires()) { /* the pixels within `radius` of the middle of pixel (cx, cy), like below */
        fx_fill_ellipse((cx + 0.5) * fx_sx, (cy + 0.5) * fx_sy, (radius + 0.5) * fx_sx, (radius + 0.5) * fx_sy);
        return;
    }
    int top = (int)ceil(cy - radius), bottom = (int)floor(cy + radius);
    if (top < -1) top = -1;
    if (bottom > fx_win_h) bottom = fx_win_h;
    for (int y = top; y <= bottom; y++) {
        double dy = y - cy;
        double dx = sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(fx_ren, fx_px(cx - dx), y, fx_px(cx + dx), y);
    }
}

/* Fill a triangle one row of pixels at a time (the pixels whose middles are inside), on a
   surface `width` by `height` pixels. */
static void fx_fill_rows(double x1, double y1, double x2, double y2, double x3, double y3, int width, int height) {
    double t;
#define FX_SWAP(a, b) (t = a, a = b, b = t)
    if (y1 > y2) { FX_SWAP(x1, x2); FX_SWAP(y1, y2); }
    if (y1 > y3) { FX_SWAP(x1, x3); FX_SWAP(y1, y3); }
    if (y2 > y3) { FX_SWAP(x2, x3); FX_SWAP(y2, y3); }
#undef FX_SWAP
    double top = ceil(y1 - 0.5), bottom = floor(y3 - 0.5);
    if (top < 0) top = 0;
    if (bottom > height - 1) bottom = height - 1;
    if (top > bottom) return;            /* (off the canvas) */
    for (int y = (int)top; y <= (int)bottom; y++) {
        double yc = y + 0.5;
        double xa = y3 == y1 ? x1 : x1 + (x3 - x1) * (yc - y1) / (y3 - y1);
        double xb = yc < y2 ? (y2 == y1 ? x1 : x1 + (x2 - x1) * (yc - y1) / (y2 - y1))
                            : (y3 == y2 ? x2 : x2 + (x3 - x2) * (yc - y2) / (y3 - y2));
        if (xa > xb) { double s = xa; xa = xb; xb = s; }
        if (xb < -1 || xa > width + 1) continue;     /* (this row is off the canvas) */
        if (xa < -1) xa = -1;
        if (xb > width + 1) xb = width + 1;
        int xs = (int)ceil(xa - 0.5), xe = (int)floor(xb - 0.5);
        if (xe >= xs) SDL_RenderDrawLine(fx_ren, xs, y, xe, y);
    }
}

/* A filled triangle in program pixels: exactly the pixels whose middles are inside. Drawing at
   more canvas pixels than that, it's one SDL_RenderGeometry triangle at the canvas's resolution
   (sharp and quick), or rows of canvas pixels where SDL can't do that. */
static void fx_fill_triangle(double x1, double y1, double x2, double y2, double x3, double y3) {
    if (!fx_hires()) { fx_fill_rows(x1, y1, x2, y2, x3, y3, fx_win_w, fx_win_h); return; }
    x1 *= fx_sx; x2 *= fx_sx; x3 *= fx_sx;
    y1 *= fx_sy; y2 *= fx_sy; y3 *= fx_sy;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    /* (far off the canvas, rows of pixels work it out exactly) */
    if (fx_geometry && fabs(x1) < 1e6 && fabs(x2) < 1e6 && fabs(x3) < 1e6 &&
        fabs(y1) < 1e6 && fabs(y2) < 1e6 && fabs(y3) < 1e6) {
        Uint8 r, g, b, a;
        SDL_GetRenderDrawColor(fx_ren, &r, &g, &b, &a);
        SDL_Color c = { r, g, b, a };
        SDL_Vertex v[3] = { { { (float)x1, (float)y1 }, c, { 0, 0 } },
                            { { (float)x2, (float)y2 }, c, { 0, 0 } },
                            { { (float)x3, (float)y3 }, c, { 0, 0 } } };
        if (SDL_RenderGeometry(fx_ren, NULL, v, 3, NULL, 0) == 0) return;
        fx_geometry = false;    /* this computer can't: rows of pixels from now on */
    }
#endif
    fx_fill_rows(x1, y1, x2, y2, x3, y3, fx_cw, fx_ch);
}

static void fx_g_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    fx_fill_triangle(x1, y1, x2, y2, x3, y3);
}

/* For drawing at more canvas pixels than program pixels: a line through the middles of the program
   pixels at its ends, `half` program pixels each side, as two triangles. `ends` makes it that
   much longer at both ends too (so a thin line covers the pixels at its ends, like in a normal window). */
static void fx_line_quad(double x1, double y1, double x2, double y2, double half, bool ends) {
    double dx = x2 - x1, dy = y2 - y1, len = sqrt(dx * dx + dy * dy);
    double ux = len > 0 ? dx / len : 1, uy = len > 0 ? dy / len : 0;   /* along the line */
    double nx = -uy * half, ny = ux * half, ex = ends ? ux * half : 0, ey = ends ? uy * half : 0;
    x1 += 0.5 - ex; y1 += 0.5 - ey;
    x2 += 0.5 + ex; y2 += 0.5 + ey;
    fx_fill_triangle(x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 - nx, y2 - ny);
    fx_fill_triangle(x1 + nx, y1 + ny, x2 - nx, y2 - ny, x2 + nx, y2 + ny);
}

static void fx_g_line(double x1, double y1, double x2, double y2, fx_text color, double thickness, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    if (fx_hires()) {     /* at the canvas's resolution: a thin line is one program pixel wide */
        if (thickness <= 1.5) { fx_line_quad(x1, y1, x2, y2, 0.5, true); return; }
        double r = thickness / 2 < 20000 ? thickness / 2 : 20000;
        fx_line_quad(x1, y1, x2, y2, r, false);      /* with round ends */
        fx_fill_ellipse((x1 + 0.5) * fx_sx, (y1 + 0.5) * fx_sy, r * fx_sx, r * fx_sy);
        fx_fill_ellipse((x2 + 0.5) * fx_sx, (y2 + 0.5) * fx_sy, r * fx_sx, r * fx_sy);
        return;
    }
    if (thickness <= 1.5) {
        SDL_RenderDrawLine(fx_ren, fx_px(x1), fx_px(y1), fx_px(x2), fx_px(y2));
        return;
    }
    /* a thick line is a long rectangle (two triangles) with round ends */
    double dx = x2 - x1, dy = y2 - y1, len = sqrt(dx * dx + dy * dy), r = thickness / 2;
    if (len > 0) {
        double nx = -dy / len * r, ny = dx / len * r;
        fx_fill_triangle(x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 + nx, y2 + ny);
        fx_fill_triangle(x1 - nx, y1 - ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny);
    }
    fx_g_circle(x1, y1, r, color, line);
    fx_g_circle(x2, y2, r, color, line);
}

static void fx_g_dot(double x, double y, fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    if (fx_hires()) {
        SDL_Rect r = fx_box(fx_px(x), fx_px(y), fx_px(x) + 1, fx_px(y) + 1);
        SDL_RenderFillRect(fx_ren, &r);
        return;
    }
    SDL_RenderDrawPoint(fx_ren, fx_px(x), fx_px(y));
}

static void fx_g_write(fx_text t, double x, double y, fx_text color, int64_t size, int line) {
    fx_need_window(line);
    fx_rgb_t c = fx_color(color, line);
    fx_dirty = true;
    if (!fx_font_tex) return;
    if (size < 1) size = 1;
    if (size > 50) size = 50;
    SDL_SetTextureColorMod(fx_font_tex, c.r, c.g, c.b);
    int px = fx_px(x), py = fx_px(y), col = 0, row = 0, s = (int)size;
    for (int64_t i = 0; i < t.n; i++) {
        unsigned char ch = (unsigned char)t.p[i];
        if (ch == '\n') { row++; col = 0; continue; }
        if (ch >= 0x80 && ch < 0xC0) continue;     /* rest of a non-English letter */
        if (ch < 32 || ch > 126) ch = '?';
        SDL_Rect src = { (ch - 32) * 6, 0, 5, 7 };
        int left = px + col * 6 * s, top = py + row * 9 * s;
        SDL_Rect dst = fx_box(left, top, left + 5 * s, top + 7 * s);
        SDL_RenderCopy(fx_ren, fx_font_tex, &src, &dst);
        col++;
    }
}

/* ------------------------------------------------------------ pictures */

typedef struct { char *name; SDL_Texture *tex; int w, h; } fx_image_t;
static fx_image_t *fx_images;
static int fx_image_count, fx_image_cap;

/* Load a picture (once; after that it comes from the cache). */
static fx_image_t *fx_image(fx_text name, int line) {
    static const char *last_p;
    static int64_t last_n = -1;
    static int last_i = -1;
    if (name.p == last_p && name.n == last_n && last_i >= 0) return &fx_images[last_i];
    fx_need_window(line);
    int found = -1;
    for (int i = 0; i < fx_image_count && found < 0; i++)
        if ((int64_t)strlen(fx_images[i].name) == name.n && memcmp(fx_images[i].name, name.p, (size_t)name.n) == 0)
            found = i;
    if (found < 0) {
        char *file = (char *)fx_text_make(name.p, name.n).p;
        int w = 0, h = 0;
        unsigned char *px = fx_read_picture(file, &w, &h, line);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
        SDL_Texture *tex = s ? SDL_CreateTextureFromSurface(fx_ren, s) : NULL;
        if (s) SDL_FreeSurface(s);
        stbi_image_free(px);
        if (!tex) fx_fail(line, NULL, "Flaux couldn't use the picture \"%s\": %s", file, SDL_GetError());
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        if (fx_image_count == fx_image_cap) {
            fx_image_cap = fx_image_cap ? fx_image_cap * 2 : 8;
            fx_images = fx_realloc(fx_images, sizeof(fx_image_t) * (size_t)fx_image_cap);
        }
        fx_images[fx_image_count] = (fx_image_t){ file, tex, w, h };
        found = fx_image_count++;
    }
    last_p = name.p;
    last_n = name.n;
    last_i = found;
    return &fx_images[found];
}

static void fx_g_image(fx_text name, double x, double y, double w, double h, double angle,
                       bool sized, int line) {
    fx_image_t *im = fx_image(name, line);
    fx_dirty = true;
    double dw = sized ? w : im->w, dh = sized ? h : im->h;
    int flip = SDL_FLIP_NONE;
    if (dw < 0) { dw = -dw; flip |= SDL_FLIP_HORIZONTAL; }  /* a negative width flips it */
    if (dh < 0) { dh = -dh; flip |= SDL_FLIP_VERTICAL; }
    SDL_Rect dst = fx_box(fx_px(x), fx_px(y), fx_px(x + dw), fx_px(y + dh));
    if (angle == 0 && flip == SDL_FLIP_NONE) SDL_RenderCopy(fx_ren, im->tex, NULL, &dst);
    else SDL_RenderCopyEx(fx_ren, im->tex, NULL, &dst, angle, NULL, (SDL_RendererFlip)flip);
}

static int64_t fx_image_width(fx_text name, int line) { return fx_image(name, line)->w; }
static int64_t fx_image_height(fx_text name, int line) { return fx_image(name, line)->h; }

#endif /* not FX_USES_3D */

/* ======================== end of graphics runtime ========================== */
