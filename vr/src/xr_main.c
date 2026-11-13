/* Flaux VR on the headset: an OpenGL ES context, and OpenXR for the head, the controllers and the
 * two eye pictures.  Everything you see is drawn by editor.c; this file only feeds it input and
 * gives it somewhere to draw.
 *
 * It's built two ways.  On Android (a Meta Quest) it's the app.  On a Linux PC (-DFLAUX_PC_XR,
 * see test_xr.sh) the very same OpenXR code runs against the Monado runtime with the OpenXR
 * validation layer switched on, so mistakes in how OpenXR is used show up without a headset. */
#include "editor.h"

#include <EGL/egl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#define XR_USE_PLATFORM_ANDROID
#else
#include <EGL/eglext.h>
#define XR_USE_PLATFORM_EGL
#endif
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

static int g_quit;
static FILE *g_log;                  /* everything logged also goes to log.txt next to your projects */

static void log_line(int error, const char *fmt, va_list ap) {
    char msg[1024];
    vsnprintf(msg, sizeof msg, fmt, ap);
#ifdef __ANDROID__
    __android_log_write(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "FlauxVR", msg);
#else
    fprintf(stderr, "%s%s\n", error ? "ERROR: " : "", msg);
#endif
    if (g_log) { fprintf(g_log, "%s%s\n", error ? "ERROR: " : "", msg); fflush(g_log); }
}
static void LOGI(const char *fmt, ...) { va_list ap; va_start(ap, fmt); log_line(0, fmt, ap); va_end(ap); }
static void LOGE(const char *fmt, ...) { va_list ap; va_start(ap, fmt); log_line(1, fmt, ap); va_end(ap); }
void plat_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); log_line(0, fmt, ap); va_end(ap); }

#ifdef __ANDROID__
static struct android_app *g_app;

/* If the app crashes, say where in log.txt (only calls that are safe inside a signal handler). */
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
static int crash_fd = -1, crash_note_fd = -1;
static uintptr_t lib_base;

static void crash_hex(const char *label, uintptr_t v) {
    char buf[40]; int n = 0;
    while (label[n]) { buf[n] = label[n]; n++; }
    for (int i = 60; i >= 0; i -= 4) buf[n++] = "0123456789abcdef"[(v >> i) & 15];
    buf[n++] = '\n';
    if (write(crash_fd, buf, (size_t)n) < 0) return;
}

static void crash_note(const char *label, uintptr_t v) {       /* short: it gets shown as text in the headset */
    char buf[32]; int n = 0, started = 0;
    while (label[n]) { buf[n] = label[n]; n++; }
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 15);
        if (d || started || i == 0) { buf[n++] = "0123456789ABCDEF"[d]; started = 1; }
    }
    buf[n++] = ' ';
    if (crash_note_fd >= 0 && write(crash_note_fd, buf, (size_t)n) < 0) crash_note_fd = -1;
}

static void on_crash(int sig, siginfo_t *info, void *context) {
    if (crash_fd >= 0) {
        static const char head[] = "CRASH\n";
        if (write(crash_fd, head, sizeof head - 1) < 0) crash_fd = -1;
        crash_hex("signal  ", (uintptr_t)sig);
        crash_hex("address ", (uintptr_t)info->si_addr);
#ifdef __aarch64__
        ucontext_t *uc = context;
        crash_hex("pc      ", (uintptr_t)uc->uc_mcontext.pc);
        crash_hex("lr      ", (uintptr_t)uc->uc_mcontext.regs[30]);
#endif
        crash_hex("libbase ", lib_base);
        fsync(crash_fd);
    }
    if (crash_note_fd >= 0) {
        crash_note("SIG ", (uintptr_t)sig);
#ifdef __aarch64__
        ucontext_t *uc2 = context;                 /* where in libflauxvr.so (or outside it: then the number is huge) */
        crash_note("PC ", (uintptr_t)uc2->uc_mcontext.pc - lib_base);
        crash_note("LR ", (uintptr_t)uc2->uc_mcontext.regs[30] - lib_base);
#endif
        fsync(crash_note_fd);
    }
    signal(sig, SIG_DFL);        /* then let Android report it the usual way */
    raise(sig);
}

static void watch_for_crashes(void) {
    Dl_info di;
    if (dladdr((void *)&watch_for_crashes, &di)) lib_base = (uintptr_t)di.dli_fbase;
    if (g_log) crash_fd = fileno(g_log);
    static char stack[32768];
    stack_t ss = {.ss_sp = stack, .ss_size = sizeof stack};
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash; sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    const int sigs[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL};
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, NULL);
}

void *plat_asset(const char *name, size_t *size) {
    AAsset *a = AAssetManager_open(g_app->activity->assetManager, name, AASSET_MODE_BUFFER);
    if (!a) return NULL;
    size_t n = (size_t)AAsset_getLength(a);
    void *buf = malloc(n ? n : 1);
    if (AAsset_read(a, buf, n) != (int)n) { free(buf); buf = NULL; }
    AAsset_close(a);
    if (size) *size = n;
    return buf;
}

/* ---------- sound: short beeps made here, played through OpenSL ES ---------- */

#define SND_RATE 24000
#define SND_VOICES 4
static SLObjectItf sl_engine, sl_mix, sl_player[SND_VOICES];
static SLAndroidSimpleBufferQueueItf sl_queue[SND_VOICES];
static int16_t *snd_data[SND_COUNT]; static int snd_len[SND_COUNT];
static int snd_next;

static void make_sound(int id, float seconds, float f0, float f1, float volume, float noise) {
    int n = (int)(seconds * SND_RATE);
    int16_t *d = malloc(sizeof(int16_t) * (size_t)n);
    float phase = 0; unsigned seed = 99u + (unsigned)id;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        phase += 2 * FX_PI * flerp(f0, f1, t) / SND_RATE;
        seed = seed * 1664525u + 1013904223u;
        float w = sinf(phase) * (1 - noise) + ((float)(seed >> 8) / 8388608.0f - 1.0f) * noise;
        float env = fminf(t * 30.0f, 1.0f) * (1 - t) * (1 - t);
        d[i] = (int16_t)(w * env * volume * 32000.0f);
    }
    snd_data[id] = d; snd_len[id] = n;
}

static void sound_init(void) {
    make_sound(SND_CLICK, 0.045f, 1400, 900, 0.30f, 0.15f);
    make_sound(SND_PLACE, 0.14f, 520, 880, 0.40f, 0.05f);
    make_sound(SND_DELETE, 0.16f, 520, 180, 0.40f, 0.10f);
    make_sound(SND_GRAB, 0.06f, 300, 420, 0.35f, 0.20f);
    make_sound(SND_ERROR, 0.20f, 220, 200, 0.35f, 0.25f);
    make_sound(SND_OPEN, 0.18f, 660, 1320, 0.30f, 0.02f);
    if (slCreateEngine(&sl_engine, 0, NULL, 0, NULL, NULL) != SL_RESULT_SUCCESS) { sl_engine = NULL; return; }
    (*sl_engine)->Realize(sl_engine, SL_BOOLEAN_FALSE);
    SLEngineItf eng;
    (*sl_engine)->GetInterface(sl_engine, SL_IID_ENGINE, &eng);
    if ((*eng)->CreateOutputMix(eng, &sl_mix, 0, NULL, NULL) != SL_RESULT_SUCCESS) return;
    (*sl_mix)->Realize(sl_mix, SL_BOOLEAN_FALSE);
    for (int i = 0; i < SND_VOICES; i++) {
        SLDataLocator_AndroidSimpleBufferQueue loc = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
        SLDataFormat_PCM fmt = {SL_DATAFORMAT_PCM, 1, SND_RATE * 1000, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};
        SLDataSource src = {&loc, &fmt};
        SLDataLocator_OutputMix out = {SL_DATALOCATOR_OUTPUTMIX, sl_mix};
        SLDataSink sink = {&out, NULL};
        const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
        const SLboolean req[1] = {SL_BOOLEAN_TRUE};
        if ((*eng)->CreateAudioPlayer(eng, &sl_player[i], &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS) { sl_player[i] = NULL; continue; }
        (*sl_player[i])->Realize(sl_player[i], SL_BOOLEAN_FALSE);
        (*sl_player[i])->GetInterface(sl_player[i], SL_IID_BUFFERQUEUE, &sl_queue[i]);
        SLPlayItf play;
        (*sl_player[i])->GetInterface(sl_player[i], SL_IID_PLAY, &play);
        (*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING);
    }
}

void plat_sound(int id) {
    if (!cfg.sound || id < 0 || id >= SND_COUNT || !snd_data[id]) return;
    int v = snd_next++ % SND_VOICES;
    if (!sl_player[v] || !sl_queue[v]) return;
    (*sl_queue[v])->Clear(sl_queue[v]);
    (*sl_queue[v])->Enqueue(sl_queue[v], snd_data[id], (SLuint32)(sizeof(int16_t) * (size_t)snd_len[id]));
}

#else   /* a PC: files instead of assets, and no sound */

void *plat_asset(const char *name, size_t *size) {
    char path[512]; snprintf(path, sizeof path, "%s/%s", getenv("FLAUX_ASSETS") ? getenv("FLAUX_ASSETS") : "assets", name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (size) *size = (size_t)n;
    return buf;
}
void plat_sound(int id) { (void)id; }
static void sound_init(void) {}
#endif

/* ---------- starting in steps ----------
 * Each step of starting up is written to boot.txt before it's done.  If the app dies, the next
 * start reads which step it was: a step the app can live without (sound, smooth edges, the
 * controller models...) is left out from then on, and either way the headset shows, in big plain
 * letters drawn without any shaders, where it stopped, so it can simply be read out. */

static char boot_dir[400], boot_last[64], boot_crash[96], boot_skips[256];
static int boot_tries;                /* starts in a row that stopped at the same step (0: last time was fine) */
static int boot_hold;                 /* frames left to show the notice; -1: stay on it */
static const char *boot_lines[5]; static int boot_line_count;
static char boot_text[5][48];

static int boot_optional(const char *stage) {
    static const char *const names[] = {"sound", "smooth-edges", "nebula", "thumbnails", "controllers"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) if (!strcmp(stage, names[i])) return 1;
    return 0;
}

static void boot_write(const char *name, const char *text) {
    if (!boot_dir[0]) return;
    char path[480]; snprintf(path, sizeof path, "%s/%s", boot_dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fflush(f); fsync(fileno(f));
    fclose(f);
}

static void boot_read(const char *name, char *out, size_t n) {
    out[0] = 0;
    char path[480]; snprintf(path, sizeof path, "%s/%s", boot_dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t k = fread(out, 1, n - 1, f);
    out[k] = 0;
    fclose(f);
    for (char *c = out; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
    while (k > 0 && out[k - 1] == ' ') out[--k] = 0;
}

void plat_stage(const char *name) {
    LOGI("step: %s", name);
    boot_write("boot.txt", name);
}

int plat_skip(const char *name) {
    char key[40]; snprintf(key, sizeof key, " %s ", name);
    return strstr(boot_skips, key) != NULL;
}

static void boot_begin(const char *dir) {
    snprintf(boot_dir, sizeof boot_dir, "%s", dir ? dir : "");
    if (!boot_dir[0]) return;
    char skips[240];
    boot_read("boot.txt", boot_last, sizeof boot_last);
    boot_read("crash.txt", boot_crash, sizeof boot_crash);
    boot_read("skip.txt", skips, sizeof skips);
    snprintf(boot_skips, sizeof boot_skips, " %s ", skips);
    if (!strcmp(boot_last, "done") || !strcmp(boot_last, "notice")) boot_last[0] = 0;       /* last time went fine */
    if (boot_last[0]) {
        char tries[96], was[64] = ""; int n = 0;
        boot_read("tries.txt", tries, sizeof tries);
        sscanf(tries, "%d %63s", &n, was);
        boot_tries = strcmp(was, boot_last) == 0 ? n + 1 : 1;
        snprintf(tries, sizeof tries, "%d %s", boot_tries, boot_last);
        boot_write("tries.txt", tries);
        LOGI("last start stopped at \"%s\" %s (%d in a row)", boot_last, boot_crash, boot_tries);
        if (boot_optional(boot_last) && !plat_skip(boot_last)) {
            snprintf(skips + strlen(skips), sizeof skips - strlen(skips), " %s", boot_last);
            boot_write("skip.txt", skips);
            snprintf(boot_skips, sizeof boot_skips, " %s ", skips);
        }
    }
    else boot_write("tries.txt", "0 -");
    boot_write("crash.txt", "");
#ifdef __ANDROID__
    char path[480]; snprintf(path, sizeof path, "%s/crash.txt", boot_dir);
    crash_note_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
    plat_stage("egl");
}

/* what to show before the editor itself is up */
static void boot_notice(void) {
    static const char *const core_after[] = {"shaders", "meshes", "stars", "font", "scene", "viewer", "project", "first-update", "first-draw"};
    int app_step = 0;
    for (size_t i = 0; i < sizeof core_after / sizeof core_after[0]; i++) if (!strcmp(boot_last, core_after[i])) app_step = 1;
    snprintf(boot_text[0], sizeof boot_text[0], "FLAUX VR " FX_VERSION);
    boot_line_count = 2;
    if (!boot_last[0]) {
        snprintf(boot_text[1], sizeof boot_text[1], "STARTING");
        boot_hold = 12;
    } else {
        snprintf(boot_text[1], sizeof boot_text[1], "LAST START STOPPED AT");
        snprintf(boot_text[2], sizeof boot_text[2], "%.40s", boot_last);
        snprintf(boot_text[3], sizeof boot_text[3], "%.44s", boot_crash[0] ? boot_crash : "NO CRASH NOTE");
        if (boot_optional(boot_last)) { snprintf(boot_text[4], sizeof boot_text[4], "LEAVING THAT OUT NOW"); boot_hold = 320; }
        else if (app_step) { snprintf(boot_text[4], sizeof boot_text[4], "TELL CLAUDE THESE LINES"); boot_hold = -1; }
        else { snprintf(boot_text[4], sizeof boot_text[4], "TRYING AGAIN"); boot_hold = 320; }
        boot_line_count = 5;
        if (boot_hold < 0) boot_write("boot.txt", "notice");      /* the start after this one tries afresh */
    }
    for (int i = 0; i < 5; i++) boot_lines[i] = boot_text[i];
}

/* five-by-seven letters, one byte per column, lowest bit at the top */
static const unsigned char *letter(char c) {
    static const unsigned char digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}};
    static const unsigned char letters[26][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
    static const unsigned char dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08}, dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00},
                               colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00}, blank[5] = {0, 0, 0, 0, 0};
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    return c == '-' || c == '_' ? dash : c == '.' ? dot : c == ':' ? colon : blank;
}

/* the notice, into one eye's picture: nothing but clearing little squares */
static void boot_draw(GLuint fbo, int w, int h) {
    int cell = h / 230 < 1 ? 1 : h / 230;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.010f, 0.014f, 0.050f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    for (int l = 0; l < boot_line_count; l++) {
        const char *text = boot_lines[l];
        int n = (int)strlen(text);
        int x0 = (w - n * 6 * cell) / 2, y0 = h / 2 + (boot_line_count * 5 - l * 10) * cell;
        if (l == 2 || l == 3) glClearColor(0.25f, 0.85f, 0.95f, 1.0f); else glClearColor(0.9f, 0.9f, 1.0f, 1.0f);
        for (int i = 0; i < n; i++) {
            const unsigned char *g = letter(text[i]);
            for (int cx = 0; cx < 5; cx++)
                for (int cy = 0; cy < 7; cy++)
                    if (g[cx] & (1 << cy)) {
                        glScissor(x0 + (i * 6 + cx) * cell, y0 - (cy + 1) * cell, cell, cell);
                        glClear(GL_COLOR_BUFFER_BIT);
                    }
        }
    }
    glDisable(GL_SCISSOR_TEST);
}

/* ---------- OpenGL ES ---------- */

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLConfig egl_cfg;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static EGLSurface egl_surf = EGL_NO_SURFACE;

static int egl_init(void) {
#ifdef __ANDROID__
    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    egl_dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
#endif
    if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL)) { LOGE("eglInitialize failed 0x%x", eglGetError()); return 0; }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint want[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
                           EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, 0x0040 /* ES3 */, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(egl_dpy, want, &egl_cfg, 1, &n) || n < 1) {
        LOGI("no exact EGL config (0x%x); taking any OpenGL ES 3 one", eglGetError());
        const EGLint any[] = {EGL_RENDERABLE_TYPE, 0x0040, EGL_NONE};
        if (!eglChooseConfig(egl_dpy, any, &egl_cfg, 1, &n) || n < 1) { LOGE("no EGL config 0x%x", eglGetError()); return 0; }
    }
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    egl_ctx = eglCreateContext(egl_dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed 0x%x", eglGetError()); return 0; }
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    egl_surf = eglCreatePbufferSurface(egl_dpy, egl_cfg, pb);      /* the eyes get their own pictures from OpenXR */
    if (egl_surf == EGL_NO_SURFACE) LOGI("no pbuffer surface (0x%x); going without one", eglGetError());
    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) { LOGE("eglMakeCurrent failed 0x%x", eglGetError()); return 0; }
    LOGI("OpenGL: %s on %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
    return 1;
}

/* ---------- OpenXR ---------- */

#define MAX_IMAGES 32
typedef struct {
    XrSwapchain handle; int w, h; uint32_t count;
    XrSwapchainImageOpenGLESKHR images[MAX_IMAGES];
    GLuint fbo[MAX_IMAGES];
} Eye;

static XrInstance xr_instance;
static XrSystemId xr_system;
static XrSession xr_session;
static XrSpace xr_stage, xr_view, xr_aim[2], xr_grip[2];
static XrSessionState xr_state = XR_SESSION_STATE_UNKNOWN;
static int xr_running, xr_focused, g_ready, g_resumed, frames_drawn;
static uint32_t last_image;
static Eye eyes[2];
static GLuint ms_fbo, ms_color, ms_depth;
static XrActionSet act_set;
static XrAction act_aim, act_grip, act_trigger, act_squeeze, act_stick, act_a, act_b, act_stick_click, act_menu, act_buzz;
static XrPath hand_path[2];

#define XR_OK(call) xr_ok((call), #call)
static int xr_ok(XrResult r, const char *what) {
    if (XR_SUCCEEDED(r)) return 1;
    char msg[XR_MAX_RESULT_STRING_SIZE] = "";
    if (xr_instance) xrResultToString(xr_instance, r, msg);
    LOGE("%s failed: %d %s", what, (int)r, msg);
    return 0;
}

static XrPath path(const char *s) { XrPath p = XR_NULL_PATH; xrStringToPath(xr_instance, s, &p); return p; }

static XrAction make_action(XrActionType type, const char *name, const char *nice) {
    XrActionCreateInfo ci = {XR_TYPE_ACTION_CREATE_INFO};
    ci.actionType = type;
    snprintf(ci.actionName, sizeof ci.actionName, "%s", name);
    snprintf(ci.localizedActionName, sizeof ci.localizedActionName, "%s", nice);
    ci.countSubactionPaths = 2; ci.subactionPaths = hand_path;
    XrAction a = XR_NULL_HANDLE;
    XR_OK(xrCreateAction(act_set, &ci, &a));
    return a;
}

static void suggest(const char *profile, const XrActionSuggestedBinding *b, uint32_t n) {
    XrInteractionProfileSuggestedBinding s = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    s.interactionProfile = path(profile); s.suggestedBindings = b; s.countSuggestedBindings = n;
    XR_OK(xrSuggestInteractionProfileBindings(xr_instance, &s));
}

static int xr_make_actions(void) {
    hand_path[0] = path("/user/hand/left"); hand_path[1] = path("/user/hand/right");
    XrActionSetCreateInfo si = {XR_TYPE_ACTION_SET_CREATE_INFO};
    snprintf(si.actionSetName, sizeof si.actionSetName, "editor");
    snprintf(si.localizedActionSetName, sizeof si.localizedActionSetName, "Flaux VR editor");
    if (!XR_OK(xrCreateActionSet(xr_instance, &si, &act_set))) return 0;
    act_aim = make_action(XR_ACTION_TYPE_POSE_INPUT, "aim", "Pointing");
    act_grip = make_action(XR_ACTION_TYPE_POSE_INPUT, "grip", "Hand");
    act_trigger = make_action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Click, select, place");
    act_squeeze = make_action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Grab");
    act_stick = make_action(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick", "Move and turn");
    act_a = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "lower_button", "A or X");
    act_b = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "upper_button", "B or Y");
    act_stick_click = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click", "Stick click");
    act_menu = make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu");
    act_buzz = make_action(XR_ACTION_TYPE_VIBRATION_OUTPUT, "buzz", "Buzz");

    const XrActionSuggestedBinding touch[] = {
        {act_aim, path("/user/hand/left/input/aim/pose")}, {act_aim, path("/user/hand/right/input/aim/pose")},
        {act_grip, path("/user/hand/left/input/grip/pose")}, {act_grip, path("/user/hand/right/input/grip/pose")},
        {act_trigger, path("/user/hand/left/input/trigger/value")}, {act_trigger, path("/user/hand/right/input/trigger/value")},
        {act_squeeze, path("/user/hand/left/input/squeeze/value")}, {act_squeeze, path("/user/hand/right/input/squeeze/value")},
        {act_stick, path("/user/hand/left/input/thumbstick")}, {act_stick, path("/user/hand/right/input/thumbstick")},
        {act_stick_click, path("/user/hand/left/input/thumbstick/click")}, {act_stick_click, path("/user/hand/right/input/thumbstick/click")},
        {act_a, path("/user/hand/left/input/x/click")}, {act_a, path("/user/hand/right/input/a/click")},
        {act_b, path("/user/hand/left/input/y/click")}, {act_b, path("/user/hand/right/input/b/click")},
        {act_menu, path("/user/hand/left/input/menu/click")},
        {act_buzz, path("/user/hand/left/output/haptic")}, {act_buzz, path("/user/hand/right/output/haptic")}};
    suggest("/interaction_profiles/oculus/touch_controller", touch, sizeof touch / sizeof touch[0]);
    const XrActionSuggestedBinding simple[] = {
        {act_aim, path("/user/hand/left/input/aim/pose")}, {act_aim, path("/user/hand/right/input/aim/pose")},
        {act_grip, path("/user/hand/left/input/grip/pose")}, {act_grip, path("/user/hand/right/input/grip/pose")},
        {act_trigger, path("/user/hand/left/input/select/click")}, {act_trigger, path("/user/hand/right/input/select/click")},
        {act_menu, path("/user/hand/left/input/menu/click")}, {act_menu, path("/user/hand/right/input/menu/click")},
        {act_buzz, path("/user/hand/left/output/haptic")}, {act_buzz, path("/user/hand/right/output/haptic")}};
    suggest("/interaction_profiles/khr/simple_controller", simple, sizeof simple / sizeof simple[0]);

    XrSessionActionSetsAttachInfo at = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1; at.actionSets = &act_set;
    if (!XR_OK(xrAttachSessionActionSets(xr_session, &at))) return 0;
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo sp = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sp.subactionPath = hand_path[h]; sp.poseInActionSpace.orientation.w = 1;
        sp.action = act_aim; XR_OK(xrCreateActionSpace(xr_session, &sp, &xr_aim[h]));
        sp.action = act_grip; XR_OK(xrCreateActionSpace(xr_session, &sp, &xr_grip[h]));
    }
    return 1;
}

static int xr_init(void) {
    LOGI("OpenXR: starting");
    plat_stage("xr-loader");
    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
#ifdef __ANDROID__
    /* the loader has to be told about Android before anything else */
    XrResult (*init_loader)(const XrLoaderInitInfoBaseHeaderKHR *) = NULL;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init_loader);
    if (!init_loader) { LOGE("this OpenXR loader has no xrInitializeLoaderKHR"); return 0; }
    XrLoaderInitInfoAndroidKHR li = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    li.applicationVM = g_app->activity->vm; li.applicationContext = g_app->activity->clazz;
    if (!XR_OK(init_loader((const XrLoaderInitInfoBaseHeaderKHR *)&li))) return 0;
    const char *exts[] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfoAndroidKHR ai = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ai.applicationVM = g_app->activity->vm; ai.applicationActivity = g_app->activity->clazz;
    ci.next = &ai;
#else
    const char *exts[] = {XR_MNDX_EGL_ENABLE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    static const char *const layers[] = {"XR_APILAYER_LUNARG_core_validation"};
    if (!getenv("FLAUX_NO_VALIDATION")) { ci.enabledApiLayerCount = 1; ci.enabledApiLayerNames = layers; }
#endif
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Flaux VR");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Flaux");
    ci.applicationInfo.applicationVersion = 2; ci.applicationInfo.engineVersion = 2;
    ci.enabledExtensionCount = 2; ci.enabledExtensionNames = exts;
    plat_stage("xr-instance");
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    if (!XR_OK(xrCreateInstance(&ci, &xr_instance))) {
        ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        if (!XR_OK(xrCreateInstance(&ci, &xr_instance))) return 0;
    }
    LOGI("OpenXR: instance made");

    XrSystemGetInfo gi = {XR_TYPE_SYSTEM_GET_INFO};
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XR_OK(xrGetSystem(xr_instance, &gi, &xr_system))) return 0;
    LOGI("OpenXR: headset found");
    plat_stage("xr-session");

    /* OpenXR insists on being asked this before a session is made (extension calls are looked up by name) */
    XrResult (*get_requirements)(XrInstance, XrSystemId, XrGraphicsRequirementsOpenGLESKHR *) = NULL;
    xrGetInstanceProcAddr(xr_instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&get_requirements);
    XrGraphicsRequirementsOpenGLESKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!get_requirements || !XR_OK(get_requirements(xr_instance, xr_system, &req))) return 0;

#ifdef __ANDROID__
    XrGraphicsBindingOpenGLESAndroidKHR gb = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
#else
    XrGraphicsBindingEGLMNDX gb = {XR_TYPE_GRAPHICS_BINDING_EGL_MNDX};
    gb.getProcAddress = eglGetProcAddress;
#endif
    gb.display = egl_dpy; gb.config = egl_cfg; gb.context = egl_ctx;
    XrSessionCreateInfo sc = {XR_TYPE_SESSION_CREATE_INFO};
    sc.next = &gb; sc.systemId = xr_system;
    if (!XR_OK(xrCreateSession(xr_instance, &sc, &xr_session))) return 0;
    LOGI("OpenXR: session made");
    plat_stage("xr-eyes");

    /* the floor is y = 0 where the headset knows where the floor is */
    uint32_t nspaces = 0;
    int has_stage = 0;
    if (XR_OK(xrEnumerateReferenceSpaces(xr_session, 0, &nspaces, NULL)) && nspaces > 0) {
        XrReferenceSpaceType *spaces = calloc(nspaces, sizeof *spaces);
        if (XR_OK(xrEnumerateReferenceSpaces(xr_session, nspaces, &nspaces, spaces)))
            for (uint32_t i = 0; i < nspaces; i++) if (spaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) has_stage = 1;
        free(spaces);
    } else has_stage = 1;                        /* couldn't ask: try the floor space, and fall back if it's refused */
    XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = has_stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!has_stage) rs.poseInReferenceSpace.position.y = -1.5f;      /* guess: your eyes started 1.5 m up */
    if (!XR_OK(xrCreateReferenceSpace(xr_session, &rs, &xr_stage))) {
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; rs.poseInReferenceSpace.position.y = -1.5f; has_stage = 0;
        if (!XR_OK(xrCreateReferenceSpace(xr_session, &rs, &xr_stage))) return 0;
    }
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW; rs.poseInReferenceSpace.position.y = 0;
    if (!XR_OK(xrCreateReferenceSpace(xr_session, &rs, &xr_view))) return 0;

    /* a picture for each eye */
    uint32_t nviews = 0;
    XrViewConfigurationView vc[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    if (!XR_OK(xrEnumerateViewConfigurationViews(xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nviews, vc)) || nviews != 2)
        return 0;
    /* Which colour formats the headset offers.  Ask how many there are first: a Quest 3 offers more
     * than fit in a small fixed list, and then the whole request fails and gives back nothing. */
    uint32_t nformats = 0;
    int64_t *formats = NULL, format = 0;
    if (XR_OK(xrEnumerateSwapchainFormats(xr_session, 0, &nformats, NULL)) && nformats > 0) {
        formats = calloc(nformats, sizeof *formats);
        if (!XR_OK(xrEnumerateSwapchainFormats(xr_session, nformats, &nformats, formats))) nformats = 0;
    } else nformats = 0;
    LOGI("the headset offers %u colour formats", nformats);
    for (uint32_t i = 0; i < nformats && !format; i++) if (formats[i] == GL_SRGB8_ALPHA8) format = formats[i];
    for (uint32_t i = 0; i < nformats && !format; i++) if (formats[i] == GL_RGBA8) format = formats[i];
    free(formats);
    if (!format) {                              /* couldn't find out: every headset has this one, so try it */
        LOGI("no colour format found in the list; trying sRGB anyway");
        format = GL_SRGB8_ALPHA8;
    }
    g_encode = format == GL_RGBA8;        /* an sRGB picture does the colour conversion itself */
    for (int e = 0; e < 2; e++) {
        XrSwapchainCreateInfo si = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        si.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        si.format = format; si.sampleCount = 1;
        si.width = vc[e].recommendedImageRectWidth; si.height = vc[e].recommendedImageRectHeight;
        si.faceCount = 1; si.arraySize = 1; si.mipCount = 1;
        if (!XR_OK(xrCreateSwapchain(xr_session, &si, &eyes[e].handle))) {
            int64_t other = format == GL_SRGB8_ALPHA8 ? GL_RGBA8 : GL_SRGB8_ALPHA8;
            if (e != 0) return 0;
            LOGI("that colour format was refused; trying 0x%x", (unsigned)other);
            format = other; g_encode = format == GL_RGBA8; si.format = format;
            if (!XR_OK(xrCreateSwapchain(xr_session, &si, &eyes[e].handle))) return 0;
        }
        eyes[e].w = (int)si.width; eyes[e].h = (int)si.height;
        for (int i = 0; i < MAX_IMAGES; i++) eyes[e].images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        uint32_t nimages = 0;
        if (!XR_OK(xrEnumerateSwapchainImages(eyes[e].handle, 0, &nimages, NULL)) || nimages == 0 || nimages > MAX_IMAGES) {
            LOGE("an eye wants %u pictures; Flaux VR can keep up to %d", nimages, MAX_IMAGES);
            return 0;
        }
        if (!XR_OK(xrEnumerateSwapchainImages(eyes[e].handle, nimages, &eyes[e].count, (XrSwapchainImageBaseHeader *)eyes[e].images)))
            return 0;
        glGenFramebuffers((GLsizei)eyes[e].count, eyes[e].fbo);
        for (uint32_t i = 0; i < eyes[e].count; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, eyes[e].fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, eyes[e].images[i].image, 0);
        }
    }
    LOGI("eyes: %dx%d, format 0x%x, %u pictures each, %s", eyes[0].w, eyes[0].h, (unsigned)format, eyes[0].count,
         has_stage ? "floor known" : "floor guessed");

    /* smooth edges: draw into a multisampled buffer, then copy to the eye's picture */
    GLint max_samples = 1; glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    plat_stage("smooth-edges");
    int samples = max_samples >= 4 ? 4 : max_samples;
    if (plat_skip("smooth-edges")) samples = 1;
    if (getenv("FLAUX_NO_MSAA")) samples = 1;
    glGenFramebuffers(1, &ms_fbo); glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
    glGenRenderbuffers(1, &ms_color); glBindRenderbuffer(GL_RENDERBUFFER, ms_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, (GLenum)format, eyes[0].w, eyes[0].h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ms_color);
    glGenRenderbuffers(1, &ms_depth); glBindRenderbuffer(GL_RENDERBUFFER, ms_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, eyes[0].w, eyes[0].h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ms_depth);
    if (samples < 2 || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        /* no smooth edges on this headset: draw straight into the eye pictures, with a depth buffer of their own */
        LOGI("no multisampled buffer (status 0x%x, %d samples); drawing without smooth edges", glCheckFramebufferStatus(GL_FRAMEBUFFER), samples);
        glDeleteFramebuffers(1, &ms_fbo); ms_fbo = 0;
        glBindRenderbuffer(GL_RENDERBUFFER, ms_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, eyes[0].w, eyes[0].h);
        for (int e = 0; e < 2; e++)
            for (uint32_t i = 0; i < eyes[e].count; i++) {
                glBindFramebuffer(GL_FRAMEBUFFER, eyes[e].fbo[i]);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ms_depth);
            }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("drawing buffers ready (GL error 0x%x)", glGetError());

    plat_stage("xr-controls");
    int actions = xr_make_actions();
    LOGI("OpenXR: controls %s", actions ? "ready" : "FAILED (carrying on without controllers)");
    return 1;
}

static Pose to_pose(XrPosef p) {
    Pose r = {{p.position.x, p.position.y, p.position.z}, {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w}};
    r.q = q4norm(r.q);
    return r;
}

static int locate(XrSpace space, XrTime time, Pose *out) {
    XrSpaceLocation loc = {XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(xrLocateSpace(space, xr_stage, time, &loc))) return 0;
    const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & need) != need) return 0;
    *out = to_pose(loc.pose);
    return 1;
}

static float get_float(XrAction a, int h) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO}; gi.action = a; gi.subactionPath = hand_path[h];
    XrActionStateFloat s = {XR_TYPE_ACTION_STATE_FLOAT};
    return XR_SUCCEEDED(xrGetActionStateFloat(xr_session, &gi, &s)) && s.isActive ? s.currentState : 0;
}
static int get_bool(XrAction a, int h) {
    XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO}; gi.action = a; gi.subactionPath = hand_path[h];
    XrActionStateBoolean s = {XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(xr_session, &gi, &s)) && s.isActive && s.currentState;
}

static void xr_events(void) {
    XrEventDataBuffer ev = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(xr_instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            xr_state = ((XrEventDataSessionStateChanged *)&ev)->state;
            LOGI("session state %d", (int)xr_state);
            if (xr_state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xr_running = XR_OK(xrBeginSession(xr_session, &bi));
            } else if (xr_state == XR_SESSION_STATE_STOPPING) {
                if (g_ready) app_pause();
                xrEndSession(xr_session); xr_running = 0;
            } else if (xr_state == XR_SESSION_STATE_EXITING || xr_state == XR_SESSION_STATE_LOSS_PENDING) {
                g_quit = 1;
            }
            xr_focused = xr_state == XR_SESSION_STATE_FOCUSED;
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g_quit = 1;
        }
        ev.type = XR_TYPE_EVENT_DATA_BUFFER; ev.next = NULL;
    }
}

static const char *g_dir;
#ifndef __ANDROID__
__attribute__((unused)) static void unused_on_pc(void) { (void)g_resumed; }
#endif

static void start_editor(void) {
    plat_stage("sound");
    if (!plat_skip("sound")) sound_init();
    app_init(g_dir);
    g_ready = 1;
    LOGI("ready; projects are kept in %s", g_dir);
}

static void xr_frame(void) {
    static XrTime last_time; static double clock_s; static FrameIn fi;
    static int first_update = 1, first_draw = 1, said_done;
    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    if (!XR_OK(xrWaitFrame(xr_session, NULL, &fs))) { usleep(10000); return; }
    if (!XR_OK(xrBeginFrame(xr_session, NULL))) return;

    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader *layers[1] = {(const XrCompositionLayerBaseHeader *)&layer};
    int drawn = 0;

    if (fs.shouldRender) {
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        XrViewState vs = {XR_TYPE_VIEW_STATE};
        XrViewLocateInfo vl = {XR_TYPE_VIEW_LOCATE_INFO};
        vl.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; vl.displayTime = fs.predictedDisplayTime; vl.space = xr_stage;
        uint32_t nv = 0;
        int located = XR_SUCCEEDED(xrLocateViews(xr_session, &vl, &vs, 2, &nv, views)) && nv == 2 &&
                      (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) && (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
        if (located) {
            float dt = last_time ? (float)((double)(fs.predictedDisplayTime - last_time) * 1e-9) : 1.0f / 72;
            last_time = fs.predictedDisplayTime;
            fi.dt = fclamp(dt, 0.001f, 0.1f);
            clock_s += fi.dt; fi.time = clock_s;
            locate(xr_view, fs.predictedDisplayTime, &fi.head);

            XrActiveActionSet active = {act_set, XR_NULL_PATH};
            XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
            sync.countActiveActionSets = 1; sync.activeActionSets = &active;
            int synced = xr_focused && xrSyncActions(xr_session, &sync) == XR_SUCCESS;
            for (int h = 0; h < 2; h++) {
                HandIn *hi = &fi.hand[h];
                Pose aim, grip;
                int ok = synced && locate(xr_aim[h], fs.predictedDisplayTime, &aim);
                if (ok && !locate(xr_grip[h], fs.predictedDisplayTime, &grip)) grip = aim;
                hi->active = ok;
                if (!ok) { hi->trigger = hi->squeeze = hi->sx = hi->sy = 0; hi->a = hi->b = hi->stick = hi->menu = 0; continue; }
                hi->aim = aim; hi->grip = grip;
                hi->trigger = get_float(act_trigger, h); hi->squeeze = get_float(act_squeeze, h);
                XrActionStateGetInfo gi = {XR_TYPE_ACTION_STATE_GET_INFO}; gi.action = act_stick; gi.subactionPath = hand_path[h];
                XrActionStateVector2f st = {XR_TYPE_ACTION_STATE_VECTOR2F};
                int has_stick = XR_SUCCEEDED(xrGetActionStateVector2f(xr_session, &gi, &st)) && st.isActive;
                hi->sx = has_stick ? st.currentState.x : 0; hi->sy = has_stick ? st.currentState.y : 0;
                hi->a = get_bool(act_a, h); hi->b = get_bool(act_b, h);
                hi->stick = get_bool(act_stick_click, h); hi->menu = get_bool(act_menu, h);
            }

            if (frames_drawn < 2) LOGI("frame %d: head at %.2f %.2f %.2f, hands %d %d", frames_drawn, (double)fi.head.p.x, (double)fi.head.p.y,
                                       (double)fi.head.p.z, fi.hand[0].active, fi.hand[1].active);
            if (!g_ready) {                       /* the notice is up; when its time is over, start the editor */
                if (boot_hold > 0) boot_hold--;
                else if (boot_hold == 0) start_editor();
            }
            if (g_ready) {
                if (first_update) { plat_stage("first-update"); first_update = 0; }
                app_update(&fi);
                app_prerender();
            }
            if (frames_drawn < 2) LOGI("frame %d: updated", frames_drawn);

            for (int e = 0; e < 2; e++) {
                uint32_t index = 0;
                XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = XR_INFINITE_DURATION;
                if (!XR_OK(xrAcquireSwapchainImage(eyes[e].handle, NULL, &index))) continue;
                if (!XR_OK(xrWaitSwapchainImage(eyes[e].handle, &wi))) { xrReleaseSwapchainImage(eyes[e].handle, NULL); continue; }
                EyeView ev;
                ev.pose = to_pose(views[e].pose);
                ev.l = views[e].fov.angleLeft; ev.r = views[e].fov.angleRight; ev.u = views[e].fov.angleUp; ev.d = views[e].fov.angleDown;
                ev.w = eyes[e].w; ev.h = eyes[e].h;
                if (g_ready && first_draw) { plat_stage("first-draw"); first_draw = 0; }
                if (!g_ready) {
                    boot_draw(eyes[e].fbo[index], ev.w, ev.h);
                } else if (ms_fbo) {
                    app_render(&ev, ms_fbo);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, eyes[e].fbo[index]);
                    glBlitFramebuffer(0, 0, ev.w, ev.h, 0, 0, ev.w, ev.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                } else {
                    app_render(&ev, eyes[e].fbo[index]);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                if (frames_drawn < 2) LOGI("frame %d: eye %d drawn (picture %u, GL error 0x%x)", frames_drawn, e, index, glGetError());
                xrReleaseSwapchainImage(eyes[e].handle, NULL);
                if (e == 0) last_image = index;
                pv[e].pose = views[e].pose; pv[e].fov = views[e].fov;
                pv[e].subImage.swapchain = eyes[e].handle;
                pv[e].subImage.imageRect.extent.width = ev.w; pv[e].subImage.imageRect.extent.height = ev.h;
                drawn++;
            }

            for (int h = 0; h < 2 && g_ready; h++) {
                float amount = app_haptic(h);
                if (amount <= 0.01f || !synced) continue;
                XrHapticVibration vib = {XR_TYPE_HAPTIC_VIBRATION};
                vib.amplitude = fclamp(amount, 0, 1); vib.duration = 30000000; vib.frequency = XR_FREQUENCY_UNSPECIFIED;
                XrHapticActionInfo hi = {XR_TYPE_HAPTIC_ACTION_INFO}; hi.action = act_buzz; hi.subactionPath = hand_path[h];
                xrApplyHapticFeedback(xr_session, &hi, (const XrHapticBaseHeader *)&vib);
            }
        }
    }

    XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
    end.displayTime = fs.predictedDisplayTime; end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (drawn == 2) {
        frames_drawn++;
        if (g_ready && !first_draw && said_done < 5 && ++said_done == 5) plat_stage("done");
        layer.space = xr_stage; layer.viewCount = 2; layer.views = pv;
        end.layerCount = 1; end.layers = layers;
    }
    int ended = XR_OK(xrEndFrame(xr_session, &end));
    if (frames_drawn <= 2 && drawn == 2) LOGI("frame %d: shown (%s)", frames_drawn - 1, ended ? "ok" : "FAILED");
}

/* ---------- starting and stopping ---------- */

static void shutdown_all(int ok) {
    if (ok) {
        if (g_ready) app_pause();
        if (xr_running && xr_state == XR_SESSION_STATE_STOPPING) xrEndSession(xr_session);
    }
    if (xr_session) xrDestroySession(xr_session);
    if (xr_instance) xrDestroyInstance(xr_instance);
    if (egl_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
        if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
        eglTerminate(egl_dpy);
    }
}

#ifdef __ANDROID__

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_RESUME: g_resumed = 1; break;
    case APP_CMD_PAUSE: g_resumed = 0; if (g_ready) app_pause(); break;
    case APP_CMD_DESTROY: g_quit = 1; break;
    default: break;
    }
}

void android_main(struct android_app *app) {
    g_app = app;
    app->onAppCmd = on_cmd;
    JNIEnv *env = NULL;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);
    const char *dir = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
    if (dir) {
        char path[512]; snprintf(path, sizeof path, "%s/log.txt", dir);
        g_log = fopen(path, "w");
    }
    watch_for_crashes();
    LOGI("Flaux VR " FX_VERSION " starting");
    g_dir = dir ? dir : "/data/local/tmp";
    boot_begin(dir);
    if (boot_tries % 2 == 1) {                    /* it stopped early last time: this time, wait until the app is in front */
        LOGI("waiting to be in front before starting OpenXR");
        for (int waited = 0; waited < 40 && !g_resumed && !app->destroyRequested; waited++) {
            int events; struct android_poll_source *src = NULL;
            while (ALooper_pollOnce(100, NULL, &events, (void **)&src) >= 0) { if (src) src->process(app, src); src = NULL; }
        }
    }

    int ok = egl_init() && xr_init();
    if (ok) {
        plat_stage("xr-wait");                   /* the editor itself starts a moment after the first pictures */
        boot_notice();
    } else {
        boot_write("boot.txt", "openxr-refused");
        LOGE("Flaux VR couldn't start. It needs a headset with OpenXR (a Meta Quest).");
        ANativeActivity_finish(app->activity);
    }

    while (!app->destroyRequested && !g_quit) {
        int events; struct android_poll_source *src = NULL;
        while (ALooper_pollOnce(ok && xr_running ? 0 : 100, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            src = NULL;
            if (app->destroyRequested) break;
        }
        if (!ok) continue;
        xr_events();
        if (xr_running) xr_frame();
    }

    LOGI("closing");
    shutdown_all(ok);
    if (g_log) fclose(g_log);
    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
    _exit(0);       /* a fresh start next time, rather than a half-alive process */
}

#else   /* the PC test: run some frames against a real OpenXR runtime, then say how it went */

int main(int argc, char **argv) {
    int want = argc > 2 ? atoi(argv[2]) : 200;
    const char *dir = argc > 1 ? argv[1] : "build/xr_data";
    mkdir(dir, 0775);
    g_dir = dir;
    boot_begin(dir);
    if (!(egl_init() && xr_init())) { LOGE("couldn't start"); shutdown_all(0); return 1; }
    plat_stage("xr-wait");
    boot_notice();
    if (getenv("FLAUX_NOTICE_PICTURE")) {          /* see the notice at a headset's size */
        RT big; rt_create(&big, 1024, 1024, 1);
        boot_draw(big.fbo, 1024, 1024);
        unsigned char *px = malloc(1024 * 1024 * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, big.fbo);
        glReadPixels(0, 0, 1024, 1024, GL_RGBA, GL_UNSIGNED_BYTE, px);
        FILE *f = fopen(getenv("FLAUX_NOTICE_PICTURE"), "wb");
        fprintf(f, "P6\n1024 1024\n255\n");
        for (int y = 1023; y >= 0; y--) for (int x = 0; x < 1024; x++) fwrite(px + ((size_t)y * 1024 + x) * 4, 1, 3, f);
        fclose(f); free(px);
    }
    int frames = 0;
    for (int spins = 0; spins < 3000 && frames < want && !g_quit; spins++) {
        xr_events();
        if (xr_running) { xr_frame(); frames += frames_drawn > frames ? 1 : 0; }
        else usleep(10000);
    }
    LOGI("%d frames drawn with both eyes, session state %d, focused %d, GL error 0x%x", frames_drawn, (int)xr_state, xr_focused,
         glGetError());
    if (frames_drawn > 0 && argc > 3) {            /* save what the left eye saw */
        int w = eyes[0].w, h = eyes[0].h;
        unsigned char *px = malloc((size_t)w * h * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, eyes[0].fbo[last_image]);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        FILE *f = fopen(argv[3], "wb");
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; y--) for (int x = 0; x < w; x++) fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
        fclose(f); free(px);
    }
    shutdown_all(1);
    return frames_drawn >= want ? 0 : 2;
}

#endif
