/* ===================================================================
 * Flaux graphics: a window, shapes, text, keyboard, mouse and beeps.
 * Built on SDL2. Only added to programs that use graphics.
 *
 * Programs that use 3D or shaders get the OpenGL runtime (gl.h) instead of
 * the 2D drawing part at the end of this file. Everything else here (colours,
 * keys, the mouse, game controllers, sound and reading pictures) is shared.
 *
 * For automated tests, these environment variables help:
 *   FLAUX_FRAMES=N        stop after N frames
 *   FLAUX_FAST=1          don't wait between frames (run as fast as possible)
 *   FLAUX_SCREENSHOT=f    save the last frame to f (a .bmp file) when stopping
 *   FLAUX_HOLD_KEYS=a,b   pretend these keys are held down the whole time
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
static int fx_win_w, fx_win_h;
static bool fx_dirty;              /* something drawn since the last show() */
static uint8_t fx_keys_now[SDL_NUM_SCANCODES], fx_keys_prev[SDL_NUM_SCANCODES];
static bool fx_mouse_now, fx_mouse_prev;
static int fx_mouse_xv, fx_mouse_yv;
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
    fx_mouse_prev = fx_mouse_now;
    Uint32 buttons = SDL_GetMouseState(&fx_mouse_xv, &fx_mouse_yv);
    fx_mouse_now = (buttons & SDL_BUTTON_LMASK) != 0;
    fx_read_pads();
}

static int64_t fx_mouse_x(void) { return fx_mouse_xv; }
static int64_t fx_mouse_y(void) { return fx_mouse_yv; }
static bool fx_mouse_down(void) { return fx_mouse_now; }
static bool fx_mouse_clicked(void) { return fx_mouse_now && !fx_mouse_prev; }

/* ----------------------------------------------------------- window */

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
    fx_win = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (int)w, (int)h, SDL_WINDOW_SHOWN | flags);
    if (!fx_win) fx_fail(line, NULL, "Flaux couldn't open a window: %s", SDL_GetError());
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom((void *)fx_icon, FX_ICON_SIZE, FX_ICON_SIZE, 32,
                                                           FX_ICON_SIZE * 4, SDL_PIXELFORMAT_RGBA32);
    if (icon) { SDL_SetWindowIcon(fx_win, icon); SDL_FreeSurface(icon); }
    fx_win_w = (int)w;
    fx_win_h = (int)h;
    const char *frames = getenv("FLAUX_FRAMES");
    if (frames && *frames) fx_frame_limit = atoll(frames);
    fx_shot_path = getenv("FLAUX_SCREENSHOT");
    fx_fast = getenv("FLAUX_FAST") && *getenv("FLAUX_FAST");
    fx_start_time = SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) fx_pad_connect(i);
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
        if (ev.type == SDL_QUIT) { /* the window's close button */
            fx_g_close();
            exit(0);
        }
        if (ev.type == SDL_CONTROLLERDEVICEADDED) fx_pad_connect(ev.cdevice.which);
    }
    fx_read_input();
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

static void fx_g_window(int64_t w, int64_t h, fx_text title, int line) {
    fx_start_window(w, h, line);
    fx_make_window(w, h, title, 0, line);
    fx_ren = SDL_CreateRenderer(fx_win, -1, SDL_RENDERER_ACCELERATED);
    if (!fx_ren) fx_ren = SDL_CreateRenderer(fx_win, -1, SDL_RENDERER_SOFTWARE);
    if (!fx_ren) fx_fail(line, NULL, "Flaux couldn't draw in the window: %s", SDL_GetError());
    SDL_SetRenderDrawBlendMode(fx_ren, SDL_BLENDMODE_BLEND);

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

    SDL_SetRenderDrawColor(fx_ren, 0, 0, 0, 255);
    SDL_RenderClear(fx_ren);
    SDL_PumpEvents();
    fx_read_input();
    fx_next_frame = SDL_GetPerformanceCounter();
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

/* Show what was drawn, then wait so the game runs at 60 frames a second. */
static void fx_g_show(int line) {
    fx_need_window(line);
    fx_frames++;
    bool last = fx_frame_limit >= 0 && fx_frames >= fx_frame_limit;
    if (last && fx_shot_path) fx_g_screenshot((fx_text){ fx_shot_path, (int64_t)strlen(fx_shot_path) }, line);
    SDL_RenderPresent(fx_ren);
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
        if (fx_shot_path) fx_g_screenshot((fx_text){ fx_shot_path, (int64_t)strlen(fx_shot_path) }, 0);
        fx_g_close();
        return;
    }
    if (fx_dirty) SDL_RenderPresent(fx_ren);
    SDL_Event ev;
    while (SDL_WaitEvent(&ev)) {
        if (ev.type == SDL_QUIT) break;
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_EXPOSED) SDL_RenderPresent(fx_ren);
    }
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
    SDL_Rect dst = { fx_px(x), fx_px(y), fx_px(x + dw) - fx_px(x), fx_px(y + dh) - fx_px(y) };
    if (angle == 0 && flip == SDL_FLIP_NONE) SDL_RenderCopy(fx_ren, im->tex, NULL, &dst);
    else SDL_RenderCopyEx(fx_ren, im->tex, NULL, &dst, angle, NULL, (SDL_RendererFlip)flip);
}

static int64_t fx_image_width(fx_text name, int line) { return fx_image(name, line)->w; }
static int64_t fx_image_height(fx_text name, int line) { return fx_image(name, line)->h; }

#endif /* not FX_USES_3D */

/* ======================== end of graphics runtime ========================== */
