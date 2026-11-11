/* Flaux VR on the headset: Android's NativeActivity, an OpenGL ES context, and OpenXR for the
 * head, the controllers and the two eye pictures.  Everything you see is drawn by editor.c; this
 * file only feeds it input and gives it somewhere to draw. */
#include "editor.h"

#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <time.h>
#include <unistd.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define TAG "FlauxVR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static struct android_app *g_app;
static int g_resumed, g_quit;

/* ---------- what the app asks of the platform ---------- */

void plat_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, TAG, fmt, ap);
    va_end(ap);
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

/* ---------- OpenGL ES ---------- */

static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLConfig egl_cfg;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static EGLSurface egl_surf = EGL_NO_SURFACE;

static int egl_init(void) {
    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL)) { LOGE("eglInitialize failed 0x%x", eglGetError()); return 0; }
    const EGLint want[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
                           EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, 0x0040 /* ES3 */, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(egl_dpy, want, &egl_cfg, 1, &n) || n < 1) { LOGE("no EGL config 0x%x", eglGetError()); return 0; }
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    egl_ctx = eglCreateContext(egl_dpy, egl_cfg, EGL_NO_CONTEXT, ctx_attr);
    if (egl_ctx == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed 0x%x", eglGetError()); return 0; }
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    egl_surf = eglCreatePbufferSurface(egl_dpy, egl_cfg, pb);      /* the eyes get their own pictures from OpenXR */
    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) { LOGE("eglMakeCurrent failed 0x%x", eglGetError()); return 0; }
    LOGI("OpenGL: %s on %s", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
    return 1;
}

/* ---------- OpenXR ---------- */

#define MAX_IMAGES 16
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
static int xr_running, xr_focused;
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
    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &ai;
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Flaux VR");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Flaux");
    ci.applicationInfo.applicationVersion = 2; ci.applicationInfo.engineVersion = 2;
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = 2; ci.enabledExtensionNames = exts;
    if (!XR_OK(xrCreateInstance(&ci, &xr_instance))) return 0;

    XrSystemGetInfo gi = {XR_TYPE_SYSTEM_GET_INFO};
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XR_OK(xrGetSystem(xr_instance, &gi, &xr_system))) return 0;

    /* OpenXR insists on being asked this before a session is made (extension calls are looked up by name) */
    XrResult (*get_requirements)(XrInstance, XrSystemId, XrGraphicsRequirementsOpenGLESKHR *) = NULL;
    xrGetInstanceProcAddr(xr_instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&get_requirements);
    XrGraphicsRequirementsOpenGLESKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!get_requirements || !XR_OK(get_requirements(xr_instance, xr_system, &req))) return 0;

    XrGraphicsBindingOpenGLESAndroidKHR gb = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gb.display = egl_dpy; gb.config = egl_cfg; gb.context = egl_ctx;
    XrSessionCreateInfo sc = {XR_TYPE_SESSION_CREATE_INFO};
    sc.next = &gb; sc.systemId = xr_system;
    if (!XR_OK(xrCreateSession(xr_instance, &sc, &xr_session))) return 0;

    /* the floor is y = 0 where the headset knows where the floor is */
    uint32_t nspaces = 0; XrReferenceSpaceType spaces[16];
    xrEnumerateReferenceSpaces(xr_session, 16, &nspaces, spaces);
    int has_stage = 0;
    for (uint32_t i = 0; i < nspaces; i++) if (spaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) has_stage = 1;
    XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = has_stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!has_stage) rs.poseInReferenceSpace.position.y = -1.5f;      /* guess: your eyes started 1.5 m up */
    if (!XR_OK(xrCreateReferenceSpace(xr_session, &rs, &xr_stage))) return 0;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW; rs.poseInReferenceSpace.position.y = 0;
    if (!XR_OK(xrCreateReferenceSpace(xr_session, &rs, &xr_view))) return 0;

    /* a picture for each eye */
    uint32_t nviews = 0;
    XrViewConfigurationView vc[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    if (!XR_OK(xrEnumerateViewConfigurationViews(xr_instance, xr_system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nviews, vc)) || nviews != 2)
        return 0;
    uint32_t nformats = 0; int64_t formats[64];
    xrEnumerateSwapchainFormats(xr_session, 64, &nformats, formats);
    int64_t format = 0;
    for (uint32_t i = 0; i < nformats && !format; i++) if (formats[i] == GL_SRGB8_ALPHA8) format = formats[i];
    for (uint32_t i = 0; i < nformats && !format; i++) if (formats[i] == GL_RGBA8) format = formats[i];
    if (!format) { LOGE("the headset offers no colour format Flaux VR can draw to"); return 0; }
    g_encode = format == GL_RGBA8;        /* an sRGB picture does the colour conversion itself */
    for (int e = 0; e < 2; e++) {
        XrSwapchainCreateInfo si = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        si.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        si.format = format; si.sampleCount = 1;
        si.width = vc[e].recommendedImageRectWidth; si.height = vc[e].recommendedImageRectHeight;
        si.faceCount = 1; si.arraySize = 1; si.mipCount = 1;
        if (!XR_OK(xrCreateSwapchain(xr_session, &si, &eyes[e].handle))) return 0;
        eyes[e].w = (int)si.width; eyes[e].h = (int)si.height;
        for (int i = 0; i < MAX_IMAGES; i++) eyes[e].images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        if (!XR_OK(xrEnumerateSwapchainImages(eyes[e].handle, MAX_IMAGES, &eyes[e].count, (XrSwapchainImageBaseHeader *)eyes[e].images)))
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
    int samples = max_samples >= 4 ? 4 : max_samples;
    glGenFramebuffers(1, &ms_fbo); glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
    glGenRenderbuffers(1, &ms_color); glBindRenderbuffer(GL_RENDERBUFFER, ms_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, (GLenum)format, eyes[0].w, eyes[0].h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ms_color);
    glGenRenderbuffers(1, &ms_depth); glBindRenderbuffer(GL_RENDERBUFFER, ms_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, eyes[0].w, eyes[0].h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ms_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { LOGE("the drawing buffer isn't complete"); return 0; }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return xr_make_actions();
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
                app_pause();
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

static void xr_frame(void) {
    static XrTime last_time; static double clock_s; static FrameIn fi;
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

            app_update(&fi);
            app_prerender();

            for (int e = 0; e < 2; e++) {
                uint32_t index = 0;
                XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wi.timeout = XR_INFINITE_DURATION;
                if (!XR_OK(xrAcquireSwapchainImage(eyes[e].handle, NULL, &index))) continue;
                if (!XR_OK(xrWaitSwapchainImage(eyes[e].handle, &wi))) { xrReleaseSwapchainImage(eyes[e].handle, NULL); continue; }
                EyeView ev;
                ev.pose = to_pose(views[e].pose);
                ev.l = views[e].fov.angleLeft; ev.r = views[e].fov.angleRight; ev.u = views[e].fov.angleUp; ev.d = views[e].fov.angleDown;
                ev.w = eyes[e].w; ev.h = eyes[e].h;
                app_render(&ev, ms_fbo);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, eyes[e].fbo[index]);
                glBlitFramebuffer(0, 0, ev.w, ev.h, 0, 0, ev.w, ev.h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                const GLenum drop[2] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
                glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 2, drop);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                xrReleaseSwapchainImage(eyes[e].handle, NULL);
                pv[e].pose = views[e].pose; pv[e].fov = views[e].fov;
                pv[e].subImage.swapchain = eyes[e].handle;
                pv[e].subImage.imageRect.extent.width = ev.w; pv[e].subImage.imageRect.extent.height = ev.h;
                drawn++;
            }

            for (int h = 0; h < 2; h++) {
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
        layer.space = xr_stage; layer.viewCount = 2; layer.views = pv;
        end.layerCount = 1; end.layers = layers;
    }
    XR_OK(xrEndFrame(xr_session, &end));
}

/* ---------- Android ---------- */

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_RESUME: g_resumed = 1; break;
    case APP_CMD_PAUSE: g_resumed = 0; if (xr_session) app_pause(); break;
    case APP_CMD_DESTROY: g_quit = 1; break;
    default: break;
    }
}

void android_main(struct android_app *app) {
    g_app = app;
    app->onAppCmd = on_cmd;
    JNIEnv *env = NULL;
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &env, NULL);
    LOGI("Flaux VR " FX_VERSION " starting");

    int ok = egl_init() && xr_init();
    if (ok) {
        const char *dir = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
        sound_init();
        app_init(dir ? dir : "/data/local/tmp");
        LOGI("ready; projects are kept in %s", dir);
    } else {
        LOGE("Flaux VR couldn't start. It needs a headset with OpenXR (a Meta Quest).");
    }

    while (!app->destroyRequested && !g_quit) {
        int events; struct android_poll_source *src;
        while (ALooper_pollOnce(ok && xr_running ? 0 : 100, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (!ok) { if (!app->destroyRequested) ANativeActivity_finish(app->activity); continue; }
        xr_events();
        if (xr_running) xr_frame();
    }

    if (ok) {
        app_pause();
        if (xr_running) xrEndSession(xr_session);
        xrDestroySession(xr_session);
        xrDestroyInstance(xr_instance);
    }
    if (egl_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
        if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
        eglTerminate(egl_dpy);
    }
    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
    _exit(0);       /* a fresh start next time, rather than a half-alive process */
}
