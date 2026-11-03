/* ===================================================================
 * Flaux graphics: a window, shapes, text, keyboard, mouse and beeps.
 * Built on SDL2. Only added to programs that use graphics.
 *
 * For automated tests, two environment variables help:
 *   FLAUX_FRAMES=N        stop after N frames
 *   FLAUX_FAST=1          don't wait between frames (run as fast as possible)
 *   FLAUX_SCREENSHOT=f    save the last frame to f (a .bmp file) when stopping
 *   FLAUX_HOLD_KEYS=a,b   pretend these keys are held down the whole time
 *   FLAUX_MOUSE=x,y       pretend the mouse is at x, y in the real window
 *                         (window pixels, before fullscreen scaling)
 *   FLAUX_MOUSE_MOVE=dx,dy pretend the mouse moves this far every frame
 *                         (screen pixels: what mouse_moved_x/y() give)
 *
 * Fullscreen: everything is drawn on a canvas texture the size of window(...).
 * show() copies it to the real window, scaled to fit and letterboxed with black
 * bars, so the program keeps its own coordinates whatever the window's size.
 * F11 or Alt+Enter switch fullscreen on and off (they're kept for that, so the
 * program doesn't see F11, or Enter while Alt is held), and fullscreen() asks
 * for it. Mouse positions are turned back into canvas pixels.
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
    if (!fx_ren)
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

static fx_rgb_t fx_color(fx_text t, int line) {
    static const char *last_p;
    static int64_t last_n = -1;
    static fx_rgb_t last;
    if (t.p == last_p && t.n == last_n) return last; /* same colour as last time: fast */
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
    last_p = t.p;
    last_n = t.n;
    last = c;
    return c;
}

static void fx_use_color(fx_text color, int line) {
    fx_rgb_t c = fx_color(color, line);
    SDL_SetRenderDrawColor(fx_ren, c.r, c.g, c.b, 255);
    fx_dirty = true;
}

static int fx_px(double v) { /* a position on screen, rounded to the nearest pixel */
    if (v > 1e6) return 1000000;
    if (v < -1e6) return -1000000;
    return (int)lround(v);
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

/* ------------------------------------------------------- fullscreen */

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

/* The part of the real window the canvas is shown in, in renderer pixels. */
static SDL_Rect fx_view(void) {
    int ow = fx_win_w, oh = fx_win_h;
    if (fx_ren) SDL_GetRendererOutputSize(fx_ren, &ow, &oh);
    return fx_fit(ow, oh, fx_win_w, fx_win_h);
}

/* A mouse position in the real window -> canvas pixels, kept inside the
   canvas (so the black bars give the nearest edge, never a position off it). */
static void fx_window_to_canvas(int wx, int wy, int *cx, int *cy) {
    int ww = fx_win_w, wh = fx_win_h, ow = fx_win_w, oh = fx_win_h;
    if (fx_win) SDL_GetWindowSize(fx_win, &ww, &wh);
    if (fx_ren) SDL_GetRendererOutputSize(fx_ren, &ow, &oh);
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

/* Put the canvas on the screen. */
static void fx_present(void) {
    if (!fx_canvas) { SDL_RenderPresent(fx_ren); return; }
    SDL_Rect v = fx_view();
#if SDL_VERSION_ATLEAST(2, 0, 12)
    /* a whole number of times bigger: sharp pixels; otherwise smooth, so pixels don't come out uneven */
    bool whole = v.w % fx_win_w == 0 && v.h % fx_win_h == 0;
    SDL_SetTextureScaleMode(fx_canvas, whole ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
#endif
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
        if (!fx_canvas_copy)
            fx_canvas_copy = SDL_CreateRGBSurfaceWithFormat(0, fx_win_w, fx_win_h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (fx_canvas_copy && SDL_RenderReadPixels(fx_ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                                                   fx_canvas_copy->pixels, fx_canvas_copy->pitch) != 0) {
            SDL_FreeSurface(fx_canvas_copy);
            fx_canvas_copy = NULL;
        }
    } else if (on) {
        SDL_RenderSetLogicalSize(fx_ren, fx_win_w, fx_win_h); /* no canvas: let SDL scale the drawing */
    }
    if (SDL_SetWindowFullscreen(fx_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) return;
    fx_full = on;
    if (!on) SDL_SetWindowSize(fx_win, fx_win_w, fx_win_h);
    fx_restore_canvas();
    fx_present();
    SDL_PumpEvents();
    fx_read_mouse();    /* the same spot on the screen is somewhere else on the canvas now */
}

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
    if (ev->type == SDL_RENDER_TARGETS_RESET) { fx_restore_canvas(); fx_present(); }
    if (ev->type == SDL_WINDOWEVENT &&
        (ev->window.event == SDL_WINDOWEVENT_EXPOSED || ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED))
        fx_present();
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
}

static int64_t fx_mouse_x(void) { return fx_mouse_xv; }
static int64_t fx_mouse_y(void) { return fx_mouse_yv; }
static bool fx_mouse_down(void) { return fx_mouse_now; }
static bool fx_mouse_clicked(void) { return fx_mouse_now && !fx_mouse_prev; }
static int64_t fx_mouse_moved_x(void) { return fx_moved_xv; }
static int64_t fx_mouse_moved_y(void) { return fx_moved_yv; }

/* ----------------------------------------------------------- window */

static void fx_g_wait_events(void);

static void fx_g_window(int64_t w, int64_t h, fx_text title, int line) {
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
    /* SDL can fall back to drawing nowhere when there's no screen. Unless a
       test asked for that on purpose, say so instead of running invisibly. */
    const char *driver = SDL_GetCurrentVideoDriver();
    const char *wanted = getenv("SDL_VIDEODRIVER");
    if (driver && (!strcmp(driver, "offscreen") || !strcmp(driver, "dummy") || !strcmp(driver, "evdev")) &&
        !(wanted && *wanted))
        fx_fail(line, "Run your program on a computer with a screen. (On WSL you need Windows 11, or an X server.\n"
                      "  On a computer you reach over the internet, windows can't be shown.)",
                "Flaux couldn't find a screen to open the window on.");
    char *name = (char *)fx_text_make(title.p, title.n).p;
    fx_win = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (int)w, (int)h, SDL_WINDOW_SHOWN);
    if (!fx_win) fx_fail(line, NULL, "Flaux couldn't open a window: %s", SDL_GetError());
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom((void *)fx_icon, FX_ICON_SIZE, FX_ICON_SIZE, 32,
                                                           FX_ICON_SIZE * 4, SDL_PIXELFORMAT_RGBA32);
    if (icon) { SDL_SetWindowIcon(fx_win, icon); SDL_FreeSurface(icon); }
    fx_ren = SDL_CreateRenderer(fx_win, -1, SDL_RENDERER_ACCELERATED);
    if (!fx_ren) fx_ren = SDL_CreateRenderer(fx_win, -1, SDL_RENDERER_SOFTWARE);
    if (!fx_ren) fx_fail(line, NULL, "Flaux couldn't draw in the window: %s", SDL_GetError());
    SDL_SetRenderDrawBlendMode(fx_ren, SDL_BLENDMODE_BLEND);
    fx_win_w = (int)w;
    fx_win_h = (int)h;
    /* the canvas the program draws on (see "Fullscreen" at the top) */
    if (SDL_RenderTargetSupported(fx_ren)) {
        fx_canvas = SDL_CreateTexture(fx_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, (int)w, (int)h);
        if (fx_canvas && SDL_SetRenderTarget(fx_ren, fx_canvas) != 0) {
            SDL_DestroyTexture(fx_canvas);
            fx_canvas = NULL;
        }
    }

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

    const char *frames = getenv("FLAUX_FRAMES");
    if (frames && *frames) fx_frame_limit = atoll(frames);
    fx_shot_path = getenv("FLAUX_SCREENSHOT");
    fx_fast = getenv("FLAUX_FAST") && *getenv("FLAUX_FAST");

    SDL_SetRenderDrawColor(fx_ren, 0, 0, 0, 255);
    SDL_RenderClear(fx_ren);
    if (getenv("FLAUX_FULLSCREEN") && *getenv("FLAUX_FULLSCREEN")) fx_set_fullscreen(true); /* for tests */
    SDL_PumpEvents();
    fx_read_input();
    fx_next_frame = SDL_GetPerformanceCounter();
    fx_wait_events = fx_g_wait_events;
}

static void fx_g_screenshot(fx_text path, int line) {
    fx_need_window(line);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, fx_win_w, fx_win_h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s || SDL_RenderReadPixels(fx_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) != 0 ||
        SDL_SaveBMP(s, fx_text_make(path.p, path.n).p) != 0)
        fx_fail(line, "Check the file name, and that you're allowed to save files in that folder.",
                "Flaux couldn't save the screenshot: %s", SDL_GetError());
    SDL_FreeSurface(s);
}

static void fx_g_close(void) {
    if (fx_audio) {
        /* let the last beeps finish */
        for (int i = 0; i < 300 && SDL_GetQueuedAudioSize(fx_audio) > 0; i++) SDL_Delay(10);
    }
    SDL_Quit();
}

/* Show what was drawn, then wait so the game runs at 60 frames a second. */
static void fx_g_show(int line) {
    fx_need_window(line);
    fx_frames++;
    bool last = fx_frame_limit >= 0 && fx_frames >= fx_frame_limit;
    if (last && fx_shot_path) fx_g_screenshot((fx_text){ fx_shot_path, (int64_t)strlen(fx_shot_path) }, line);
    fx_present();
    fx_dirty = false;
    if (last) {
        fx_g_close();
        exit(0);
    }
    if (!fx_fast) {
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

/* At the end of the program, keep the window open until it's closed. */
static void fx_g_finish(void) {
    if (!fx_win) {
        if (fx_audio) fx_g_close();
        return;
    }
    if (fx_frame_limit >= 0) { /* test mode */
        if (fx_shot_path) fx_g_screenshot((fx_text){ fx_shot_path, (int64_t)strlen(fx_shot_path) }, 0);
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
    SDL_Rect r = { fx_px(x), fx_px(y), fx_px(x + w) - fx_px(x), fx_px(y + h) - fx_px(y) };
    SDL_RenderFillRect(fx_ren, &r);
}

static void fx_g_circle(double cx, double cy, double radius, fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    if (radius <= 0) return;
    if (radius > 20000) radius = 20000;
    int top = (int)ceil(cy - radius), bottom = (int)floor(cy + radius);
    if (top < -1) top = -1;
    if (bottom > fx_win_h) bottom = fx_win_h;
    for (int y = top; y <= bottom; y++) {
        double dy = y - cy;
        double dx = sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(fx_ren, fx_px(cx - dx), y, fx_px(cx + dx), y);
    }
}

/* Fill a triangle one row of pixels at a time. */
static void fx_fill_triangle(double x1, double y1, double x2, double y2, double x3, double y3) {
    double t;
#define FX_SWAP(a, b) (t = a, a = b, b = t)
    if (y1 > y2) { FX_SWAP(x1, x2); FX_SWAP(y1, y2); }
    if (y1 > y3) { FX_SWAP(x1, x3); FX_SWAP(y1, y3); }
    if (y2 > y3) { FX_SWAP(x2, x3); FX_SWAP(y2, y3); }
#undef FX_SWAP
    int top = (int)ceil(y1 - 0.5), bottom = (int)floor(y3 - 0.5);
    if (top < 0) top = 0;
    if (bottom > fx_win_h - 1) bottom = fx_win_h - 1;
    for (int y = top; y <= bottom; y++) {
        double yc = y + 0.5;
        double xa = y3 == y1 ? x1 : x1 + (x3 - x1) * (yc - y1) / (y3 - y1);
        double xb = yc < y2 ? (y2 == y1 ? x1 : x1 + (x2 - x1) * (yc - y1) / (y2 - y1))
                            : (y3 == y2 ? x2 : x2 + (x3 - x2) * (yc - y2) / (y3 - y2));
        if (xa > xb) { double s = xa; xa = xb; xb = s; }
        int xs = (int)ceil(xa - 0.5), xe = (int)floor(xb - 0.5);
        if (xe >= xs) SDL_RenderDrawLine(fx_ren, xs, y, xe, y);
    }
}

static void fx_g_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          fx_text color, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
    fx_fill_triangle(x1, y1, x2, y2, x3, y3);
}

static void fx_g_line(double x1, double y1, double x2, double y2, fx_text color, double thickness, int line) {
    fx_need_window(line);
    fx_use_color(color, line);
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
        SDL_Rect dst = { px + col * 6 * s, py + row * 9 * s, 5 * s, 7 * s };
        SDL_RenderCopy(fx_ren, fx_font_tex, &src, &dst);
        col++;
    }
}

/* ------------------------------------------------------------ pictures */

typedef struct { char *name; SDL_Texture *tex; int w, h; } fx_image_t;
static fx_image_t *fx_images;
static int fx_image_count, fx_image_cap;

static bool fx_is_absolute(const char *p) {
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

/* Load a picture (once; after that it comes from the cache). Pictures are
   looked for next to the program first, then in the current folder. */
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
        char path[4096];
        int w = 0, h = 0, comp = 0;
        unsigned char *px = NULL;
        if (!fx_is_absolute(file) && FX_PROGRAM_DIR[0]) {
            snprintf(path, sizeof path, "%s/%s", FX_PROGRAM_DIR, file);
            px = stbi_load(path, &w, &h, &comp, 4);
        }
        if (!px) {
            snprintf(path, sizeof path, "%s", file);
            px = stbi_load(path, &w, &h, &comp, 4);
        }
        if (!px) {
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
        }
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
    SDL_Rect dst = { fx_px(x), fx_px(y), fx_px(x + dw) - fx_px(x), fx_px(y + dh) - fx_px(y) };
    if (angle == 0 && flip == SDL_FLIP_NONE) SDL_RenderCopy(fx_ren, im->tex, NULL, &dst);
    else SDL_RenderCopyEx(fx_ren, im->tex, NULL, &dst, angle, NULL, (SDL_RendererFlip)flip);
}

static int64_t fx_image_width(fx_text name, int line) { return fx_image(name, line)->w; }
static int64_t fx_image_height(fx_text name, int line) { return fx_image(name, line)->h; }

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

/* ======================== end of graphics runtime ========================== */
