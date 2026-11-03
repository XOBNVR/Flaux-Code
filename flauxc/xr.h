/* ===================================================================
 * Flaux VR: 3D programs in a VR headset, with OpenXR.
 *
 * Added to 3D programs that use in_vr() or hand_x/y/z(...). When the
 * program opens its window, Flaux looks for OpenXR (SteamVR, Meta Quest Link,
 * Windows Mixed Reality, Monado, ...). If a headset is there, the program
 * runs in it: both eyes are drawn every frame, camera(...) is where you
 * stand, and the controllers work like a game controller (trigger = "rt"
 * or "lt", grip = "rb" or "lb", A/B/X/Y, sticks). The window on the screen
 * keeps showing the normal camera. Without a headset nothing changes.
 * FLAUX_VR=0 turns it off.
 *
 * OpenXR is loaded when the program starts (not linked), so programs run
 * fine on computers that don't have it.
 * =================================================================== */

#define XR_NO_PROTOTYPES 1
#include "openxr/openxr.h"
#ifdef _WIN32
#define FX_XR_LOADER_NAME "openxr_loader.dll"
#else
#define FX_XR_LOADER_NAME "libopenxr_loader.so.1"
#endif
#ifndef FX_XR_LOADER
#define FX_XR_LOADER ""
#endif

/* the parts of openxr_platform.h Flaux needs (without the X11 and Windows
   headers, which would clash with Flaux's own OpenGL names) */
typedef struct { XrStructureType type; const void *next; void *hDC; void *hGLRC; } fx_xr_binding_win32_t;
typedef struct { XrStructureType type; const void *next; void *xDisplay; uint32_t visualid; void *glxFBConfig;
                 unsigned long glxDrawable; void *glxContext; } fx_xr_binding_xlib_t;
typedef struct { XrStructureType type; void *next; uint32_t image; } fx_xr_image_gl_t;
typedef struct { XrStructureType type; void *next; XrVersion min_version, max_version; } fx_xr_gl_needs_t;
typedef XrResult (XRAPI_PTR *fx_xr_gl_needs_fn)(XrInstance, XrSystemId, fx_xr_gl_needs_t *);

#define FX_XR_FUNCTIONS(X) \
    X(xrCreateInstance) X(xrDestroyInstance) X(xrGetSystem) X(xrCreateSession) X(xrDestroySession) \
    X(xrBeginSession) X(xrEndSession) X(xrRequestExitSession) X(xrPollEvent) X(xrWaitFrame) X(xrBeginFrame) \
    X(xrEndFrame) X(xrLocateViews) X(xrEnumerateViewConfigurationViews) X(xrEnumerateSwapchainFormats) \
    X(xrCreateSwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) X(xrWaitSwapchainImage) \
    X(xrReleaseSwapchainImage) X(xrCreateReferenceSpace) X(xrStringToPath) X(xrCreateActionSet) X(xrCreateAction) \
    X(xrSuggestInteractionProfileBindings) X(xrAttachSessionActionSets) X(xrSyncActions) \
    X(xrGetActionStateFloat) X(xrGetActionStateBoolean) X(xrGetActionStateVector2f) X(xrCreateActionSpace) \
    X(xrLocateSpace) X(xrEnumerateInstanceExtensionProperties)
#define FX_XR_DECLARE(name) static PFN_##name fx_##name;
FX_XR_FUNCTIONS(FX_XR_DECLARE)
static PFN_xrGetInstanceProcAddr fx_xrGetInstanceProcAddr;

enum { FX_XR_TRIGGER, FX_XR_GRIP, FX_XR_PRIMARY, FX_XR_SECONDARY, FX_XR_STICK_CLICK, FX_XR_MENU, FX_XR_BUTTONS };
static struct {
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSessionState state;
    bool running;
    XrSpace space;
    bool floor_space;
    uint32_t views;
    XrViewConfigurationView view_info[2];
    XrSwapchain swapchain[2];
    GLuint images[2][8];
    uint32_t image_count[2];
    GLuint fbo[2], msaa_fbo[2], msaa_color[2], msaa_depth[2];
    XrActionSet set;
    XrAction pose, buttons[FX_XR_BUTTONS], stick;
    XrPath hand_path[2];
    XrSpace hand_space[2];
} fx_xr;

static void fx_xr_quit(const char *why) {
    if (why && getenv("FLAUX_VR_DEBUG")) fprintf(stderr, "Flaux VR: %s\n", why);
    if (fx_xr.session) fx_xrDestroySession(fx_xr.session);
    if (fx_xr.instance) fx_xrDestroyInstance(fx_xr.instance);
    memset(&fx_xr, 0, sizeof fx_xr);
    fx_xr_running = false;
}

static XrPath fx_xr_path(const char *s) {
    XrPath p = XR_NULL_PATH;
    fx_xrStringToPath(fx_xr.instance, s, &p);
    return p;
}

static XrAction fx_xr_action(const char *name, const char *label, XrActionType type) {
    XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
    snprintf(info.actionName, sizeof info.actionName, "%s", name);
    snprintf(info.localizedActionName, sizeof info.localizedActionName, "%s", label);
    info.actionType = type;
    info.countSubactionPaths = 2;
    info.subactionPaths = fx_xr.hand_path;
    XrAction a = XR_NULL_HANDLE;
    fx_xrCreateAction(fx_xr.set, &info, &a);
    return a;
}

/* Which controller buttons the actions are, for each kind of controller.
   A kind the runtime doesn't know is skipped. */
static void fx_xr_bindings(void) {
    static const struct { const char *profile; const char *const paths[16][2]; } kinds[] = {
        { "/interaction_profiles/khr/simple_controller", {
            { "pose", "/user/hand/left/input/grip/pose" }, { "pose", "/user/hand/right/input/grip/pose" },
            { "trigger", "/user/hand/left/input/select/click" }, { "trigger", "/user/hand/right/input/select/click" },
            { "menu", "/user/hand/left/input/menu/click" }, { "menu", "/user/hand/right/input/menu/click" } } },
        { "/interaction_profiles/oculus/touch_controller", {
            { "pose", "/user/hand/left/input/grip/pose" }, { "pose", "/user/hand/right/input/grip/pose" },
            { "trigger", "/user/hand/left/input/trigger/value" }, { "trigger", "/user/hand/right/input/trigger/value" },
            { "grip", "/user/hand/left/input/squeeze/value" }, { "grip", "/user/hand/right/input/squeeze/value" },
            { "primary", "/user/hand/left/input/x/click" }, { "primary", "/user/hand/right/input/a/click" },
            { "secondary", "/user/hand/left/input/y/click" }, { "secondary", "/user/hand/right/input/b/click" },
            { "stick", "/user/hand/left/input/thumbstick" }, { "stick", "/user/hand/right/input/thumbstick" },
            { "stick_click", "/user/hand/left/input/thumbstick/click" },
            { "stick_click", "/user/hand/right/input/thumbstick/click" },
            { "menu", "/user/hand/left/input/menu/click" } } },
        { "/interaction_profiles/valve/index_controller", {
            { "pose", "/user/hand/left/input/grip/pose" }, { "pose", "/user/hand/right/input/grip/pose" },
            { "trigger", "/user/hand/left/input/trigger/value" }, { "trigger", "/user/hand/right/input/trigger/value" },
            { "grip", "/user/hand/left/input/squeeze/value" }, { "grip", "/user/hand/right/input/squeeze/value" },
            { "primary", "/user/hand/left/input/a/click" }, { "primary", "/user/hand/right/input/a/click" },
            { "secondary", "/user/hand/left/input/b/click" }, { "secondary", "/user/hand/right/input/b/click" },
            { "stick", "/user/hand/left/input/thumbstick" }, { "stick", "/user/hand/right/input/thumbstick" },
            { "stick_click", "/user/hand/left/input/thumbstick/click" },
            { "stick_click", "/user/hand/right/input/thumbstick/click" } } },
        { "/interaction_profiles/htc/vive_controller", {
            { "pose", "/user/hand/left/input/grip/pose" }, { "pose", "/user/hand/right/input/grip/pose" },
            { "trigger", "/user/hand/left/input/trigger/value" }, { "trigger", "/user/hand/right/input/trigger/value" },
            { "grip", "/user/hand/left/input/squeeze/click" }, { "grip", "/user/hand/right/input/squeeze/click" },
            { "stick", "/user/hand/left/input/trackpad" }, { "stick", "/user/hand/right/input/trackpad" },
            { "stick_click", "/user/hand/left/input/trackpad/click" },
            { "stick_click", "/user/hand/right/input/trackpad/click" },
            { "menu", "/user/hand/left/input/menu/click" }, { "menu", "/user/hand/right/input/menu/click" } } },
        { "/interaction_profiles/microsoft/motion_controller", {
            { "pose", "/user/hand/left/input/grip/pose" }, { "pose", "/user/hand/right/input/grip/pose" },
            { "trigger", "/user/hand/left/input/trigger/value" }, { "trigger", "/user/hand/right/input/trigger/value" },
            { "grip", "/user/hand/left/input/squeeze/click" }, { "grip", "/user/hand/right/input/squeeze/click" },
            { "stick", "/user/hand/left/input/thumbstick" }, { "stick", "/user/hand/right/input/thumbstick" },
            { "stick_click", "/user/hand/left/input/thumbstick/click" },
            { "stick_click", "/user/hand/right/input/thumbstick/click" },
            { "menu", "/user/hand/left/input/menu/click" }, { "menu", "/user/hand/right/input/menu/click" } } },
    };
    for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++) {
        XrActionSuggestedBinding b[16];
        uint32_t n = 0;
        for (int i = 0; i < 16 && kinds[k].paths[i][0]; i++) {
            const char *what = kinds[k].paths[i][0];
            XrAction a = !strcmp(what, "pose") ? fx_xr.pose : !strcmp(what, "stick") ? fx_xr.stick
                       : !strcmp(what, "trigger") ? fx_xr.buttons[FX_XR_TRIGGER] : !strcmp(what, "grip") ? fx_xr.buttons[FX_XR_GRIP]
                       : !strcmp(what, "primary") ? fx_xr.buttons[FX_XR_PRIMARY] : !strcmp(what, "secondary") ? fx_xr.buttons[FX_XR_SECONDARY]
                       : !strcmp(what, "stick_click") ? fx_xr.buttons[FX_XR_STICK_CLICK] : fx_xr.buttons[FX_XR_MENU];
            b[n].action = a;
            b[n].binding = fx_xr_path(kinds[k].paths[i][1]);
            n++;
        }
        XrInteractionProfileSuggestedBinding s = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
        s.interactionProfile = fx_xr_path(kinds[k].profile);
        s.countSuggestedBindings = n;
        s.suggestedBindings = b;
        fx_xrSuggestInteractionProfileBindings(fx_xr.instance, &s);
    }
}

static void *fx_xr_load_loader(void) {
    void *lib = NULL;
    const char *own = getenv("FLAUX_OPENXR_LOADER");
    if (own && *own) lib = SDL_LoadObject(own);
    if (!lib && FX_XR_LOADER[0]) lib = SDL_LoadObject(FX_XR_LOADER);
    if (!lib) lib = SDL_LoadObject(FX_XR_LOADER_NAME);
#ifndef _WIN32
    if (!lib) lib = SDL_LoadObject("libopenxr_loader.so");
#endif
    return lib;
}

/* Look for a headset, and start VR if there is one. */
static void fx_xr_start(void) {
    const char *off = getenv("FLAUX_VR");
    if (off && !strcmp(off, "0")) return;
    void *lib = fx_xr_load_loader();
    if (!lib) { fx_xr_quit("no OpenXR loader"); return; }
    fx_xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)SDL_LoadFunction(lib, "xrGetInstanceProcAddr");
    if (!fx_xrGetInstanceProcAddr) { fx_xr_quit("no xrGetInstanceProcAddr"); return; }
#define FX_XR_LOAD(name) fx_xrGetInstanceProcAddr(XR_NULL_HANDLE, #name, (PFN_xrVoidFunction *)&fx_##name);
    FX_XR_LOAD(xrCreateInstance)
    FX_XR_LOAD(xrEnumerateInstanceExtensionProperties)
    if (!fx_xrCreateInstance) { fx_xr_quit("no xrCreateInstance"); return; }

    const char *ext = "XR_KHR_opengl_enable";
    XrInstanceCreateInfo ci = { XR_TYPE_INSTANCE_CREATE_INFO };
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Flaux");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "Flaux");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = &ext;
    if (fx_xrCreateInstance(&ci, &fx_xr.instance) != XR_SUCCESS) { fx_xr_quit("no OpenXR runtime with OpenGL"); return; }
#undef FX_XR_LOAD
#define FX_XR_LOAD(name) fx_xrGetInstanceProcAddr(fx_xr.instance, #name, (PFN_xrVoidFunction *)&fx_##name);
    FX_XR_FUNCTIONS(FX_XR_LOAD)
#undef FX_XR_LOAD
    XrSystemGetInfo si = { XR_TYPE_SYSTEM_GET_INFO };
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (fx_xrGetSystem(fx_xr.instance, &si, &fx_xr.system) != XR_SUCCESS) { fx_xr_quit("no headset"); return; }
    fx_xr_gl_needs_fn needs = NULL;
    fx_xrGetInstanceProcAddr(fx_xr.instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&needs);
    fx_xr_gl_needs_t req = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    if (!needs || needs(fx_xr.instance, fx_xr.system, &req) != XR_SUCCESS) { fx_xr_quit("no OpenGL requirements"); return; }

    /* the OpenGL that Flaux draws with, for the headset to use */
#ifdef _WIN32
    fx_xr_binding_win32_t bind = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    void *(FXGL_API *dc)(void) = (void *(FXGL_API *)(void))SDL_GL_GetProcAddress("wglGetCurrentDC");
    void *(FXGL_API *ctx)(void) = (void *(FXGL_API *)(void))SDL_GL_GetProcAddress("wglGetCurrentContext");
    if (!dc || !ctx) { fx_xr_quit("no WGL"); return; }
    bind.hDC = dc();
    bind.hGLRC = ctx();
#else
    fx_xr_binding_xlib_t bind = { XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR };
    void *(*dpy)(void) = (void *(*)(void))SDL_GL_GetProcAddress("glXGetCurrentDisplay");
    unsigned long (*draw)(void) = (unsigned long (*)(void))SDL_GL_GetProcAddress("glXGetCurrentDrawable");
    void *(*ctx)(void) = (void *(*)(void))SDL_GL_GetProcAddress("glXGetCurrentContext");
    if (!dpy || !draw || !ctx || !ctx()) { fx_xr_quit("no GLX (VR needs the X11 video driver)"); return; }
    bind.xDisplay = dpy();
    bind.glxDrawable = draw();
    bind.glxContext = ctx();
#endif
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &bind;
    sci.systemId = fx_xr.system;
    XrResult r = fx_xrCreateSession(fx_xr.instance, &sci, &fx_xr.session);
    if (r != XR_SUCCESS) {
        char why[100];
        snprintf(why, sizeof why, "couldn't start a session (%d)", (int)r);
        fx_xr_quit(why);
        return;
    }

    /* where you stand: the floor of your room if the headset knows it */
    XrReferenceSpaceCreateInfo rs = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rs.poseInReferenceSpace.orientation.w = 1;
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    fx_xr.floor_space = fx_xrCreateReferenceSpace(fx_xr.session, &rs, &fx_xr.space) == XR_SUCCESS;
    if (!fx_xr.floor_space) {
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        fx_xrCreateReferenceSpace(fx_xr.session, &rs, &fx_xr.space);
    }

    /* a picture for each eye */
    fx_xrEnumerateViewConfigurationViews(fx_xr.instance, fx_xr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                         0, &fx_xr.views, NULL);
    if (fx_xr.views > 2) fx_xr.views = 2;
    for (uint32_t i = 0; i < 2; i++) fx_xr.view_info[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    fx_xrEnumerateViewConfigurationViews(fx_xr.instance, fx_xr.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                         fx_xr.views, &fx_xr.views, fx_xr.view_info);
    int64_t formats[64];
    uint32_t nf = 0;
    fx_xrEnumerateSwapchainFormats(fx_xr.session, 64, &nf, formats);
    int64_t format = 0;
    /* Flaux's shaders already give sRGB colours, so an sRGB picture holds them
       as they are (OpenXR treats plain RGBA as linear, and would brighten them) */
    for (uint32_t i = 0; i < nf && !format; i++) if (formats[i] == 0x8C43 /* GL_SRGB8_ALPHA8 */) format = formats[i];
    for (uint32_t i = 0; i < nf && !format; i++) if (formats[i] == GL_RGBA8) format = GL_RGBA8;
    if (!format && nf) format = formats[0];
    if (getenv("FLAUX_VR_DEBUG")) {
        fprintf(stderr, "Flaux VR: picture formats");
        for (uint32_t i = 0; i < nf; i++) fprintf(stderr, " 0x%llx", (unsigned long long)formats[i]);
        fprintf(stderr, ", using 0x%llx\n", (unsigned long long)format);
    }
    for (uint32_t v = 0; v < fx_xr.views; v++) {
        XrSwapchainCreateInfo sc = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        sc.format = format;
        sc.sampleCount = 1;
        sc.width = fx_xr.view_info[v].recommendedImageRectWidth;
        sc.height = fx_xr.view_info[v].recommendedImageRectHeight;
        sc.faceCount = sc.arraySize = sc.mipCount = 1;
        if (fx_xrCreateSwapchain(fx_xr.session, &sc, &fx_xr.swapchain[v]) != XR_SUCCESS) { fx_xr_quit("no swapchain"); return; }
        fx_xr_image_gl_t imgs[8];
        for (int i = 0; i < 8; i++) { imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR; imgs[i].next = NULL; }
        fx_xrEnumerateSwapchainImages(fx_xr.swapchain[v], 8, &fx_xr.image_count[v], (XrSwapchainImageBaseHeader *)imgs);
        for (uint32_t i = 0; i < fx_xr.image_count[v]; i++) fx_xr.images[v][i] = imgs[i].image;
        glGenFramebuffers(1, &fx_xr.fbo[v]);
        int samples = fx_samples;
        glGenRenderbuffers(1, &fx_xr.msaa_color[v]);
        glBindRenderbuffer(GL_RENDERBUFFER, fx_xr.msaa_color[v]);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, (GLsizei)sc.width, (GLsizei)sc.height);
        glGenRenderbuffers(1, &fx_xr.msaa_depth[v]);
        glBindRenderbuffer(GL_RENDERBUFFER, fx_xr.msaa_depth[v]);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, (GLsizei)sc.width, (GLsizei)sc.height);
        fx_xr.msaa_fbo[v] = fx_framebuffer(0, fx_xr.msaa_color[v], fx_xr.msaa_depth[v], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* the controllers */
    fx_xr.hand_path[0] = fx_xr_path("/user/hand/left");
    fx_xr.hand_path[1] = fx_xr_path("/user/hand/right");
    XrActionSetCreateInfo as = { XR_TYPE_ACTION_SET_CREATE_INFO };
    snprintf(as.actionSetName, sizeof as.actionSetName, "flaux");
    snprintf(as.localizedActionSetName, sizeof as.localizedActionSetName, "Flaux");
    fx_xrCreateActionSet(fx_xr.instance, &as, &fx_xr.set);
    fx_xr.pose = fx_xr_action("hand_pose", "Hand", XR_ACTION_TYPE_POSE_INPUT);
    fx_xr.buttons[FX_XR_TRIGGER] = fx_xr_action("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT);
    fx_xr.buttons[FX_XR_GRIP] = fx_xr_action("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT);
    fx_xr.buttons[FX_XR_PRIMARY] = fx_xr_action("primary", "A or X", XR_ACTION_TYPE_BOOLEAN_INPUT);
    fx_xr.buttons[FX_XR_SECONDARY] = fx_xr_action("secondary", "B or Y", XR_ACTION_TYPE_BOOLEAN_INPUT);
    fx_xr.buttons[FX_XR_STICK_CLICK] = fx_xr_action("stick_click", "Stick click", XR_ACTION_TYPE_BOOLEAN_INPUT);
    fx_xr.buttons[FX_XR_MENU] = fx_xr_action("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT);
    fx_xr.stick = fx_xr_action("stick", "Stick", XR_ACTION_TYPE_VECTOR2F_INPUT);
    fx_xr_bindings();
    XrSessionActionSetsAttachInfo at = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    at.countActionSets = 1;
    at.actionSets = &fx_xr.set;
    fx_xrAttachSessionActionSets(fx_xr.session, &at);
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo sp = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        sp.action = fx_xr.pose;
        sp.subactionPath = fx_xr.hand_path[h];
        sp.poseInActionSpace.orientation.w = 1;
        fx_xrCreateActionSpace(fx_xr.session, &sp, &fx_xr.hand_space[h]);
    }
    if (getenv("FLAUX_VR_DEBUG")) fprintf(stderr, "Flaux VR: headset found (%u eyes, %ux%u each)\n", fx_xr.views,
                                          fx_xr.view_info[0].recommendedImageRectWidth, fx_xr.view_info[0].recommendedImageRectHeight);
}

static void fx_xr_events(void) {
    XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
    while (fx_xr.instance && fx_xrPollEvent(fx_xr.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged *s = (XrEventDataSessionStateChanged *)&ev;
            fx_xr.state = s->state;
            if (s->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo b = { XR_TYPE_SESSION_BEGIN_INFO };
                b.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                fx_xr.running = fx_xrBeginSession(fx_xr.session, &b) == XR_SUCCESS;
            } else if (s->state == XR_SESSION_STATE_STOPPING) {
                fx_xrEndSession(fx_xr.session);
                fx_xr.running = false;
            } else if (s->state == XR_SESSION_STATE_EXITING || s->state == XR_SESSION_STATE_LOSS_PENDING) {
                fx_xr_quit("the headset was put away");
                return;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            fx_xr_quit("OpenXR stopped");
            return;
        }
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

/* where you stand (camera(...)), facing where the camera looks */
static fx_mat4 fx_xr_play_to_world(void) {
    fx_v3 d = fx_sub3(fx_cam_target, fx_cam_eye);
    float yaw = atan2f(-d.x, -d.z), c = cosf(yaw), s = sinf(yaw);
    float floor_y = fx_cam_eye.y - (fx_xr.floor_space ? 1.6f : 0.0f);
    fx_mat4 m = { { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, fx_cam_eye.x, floor_y, fx_cam_eye.z, 1 } };
    return m;
}

static fx_mat4 fx_xr_pose_matrix(XrPosef p) {
    float x = p.orientation.x, y = p.orientation.y, z = p.orientation.z, w = p.orientation.w;
    fx_mat4 m = { { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
                    2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
                    2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
                    p.position.x, p.position.y, p.position.z, 1 } };
    return m;
}

static fx_mat4 fx_xr_projection(XrFovf f, float zn, float zf) {
    float l = tanf(f.angleLeft), r = tanf(f.angleRight), u = tanf(f.angleUp), d = tanf(f.angleDown);
    fx_mat4 m = { { 0 } };
    m.m[0] = 2 / (r - l);
    m.m[5] = 2 / (u - d);
    m.m[8] = (r + l) / (r - l);
    m.m[9] = (u + d) / (u - d);
    m.m[10] = -(zf + zn) / (zf - zn);
    m.m[11] = -1;
    m.m[14] = -2 * zf * zn / (zf - zn);
    return m;
}

static void fx_xr_input(XrTime when, fx_mat4 base) {
    XrActiveActionSet active = { fx_xr.set, XR_NULL_PATH };
    XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    fx_xrSyncActions(fx_xr.session, &sync);
    memset(fx_xr_buttons, 0, sizeof fx_xr_buttons);
    memset(fx_xr_stick, 0, sizeof fx_xr_stick);
    static const int names[FX_XR_BUTTONS][2] = {
        { FX_PAD_LT, FX_PAD_RT }, { FX_PAD_LB, FX_PAD_RB }, { FX_PAD_X, FX_PAD_A }, { FX_PAD_Y, FX_PAD_B },
        { FX_PAD_L3, FX_PAD_R3 }, { FX_PAD_START, FX_PAD_START } };
    for (int h = 0; h < 2; h++) {
        XrActionStateGetInfo g = { XR_TYPE_ACTION_STATE_GET_INFO };
        g.subactionPath = fx_xr.hand_path[h];
        for (int b = 0; b < FX_XR_BUTTONS; b++) {
            g.action = fx_xr.buttons[b];
            double value = 0;
            if (b == FX_XR_TRIGGER || b == FX_XR_GRIP) {
                XrActionStateFloat st = { XR_TYPE_ACTION_STATE_FLOAT };
                if (fx_xrGetActionStateFloat(fx_xr.session, &g, &st) == XR_SUCCESS && st.isActive) value = st.currentState;
            } else {
                XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
                if (fx_xrGetActionStateBoolean(fx_xr.session, &g, &st) == XR_SUCCESS && st.isActive) value = st.currentState;
            }
            if (value > fx_xr_buttons[names[b][h]]) fx_xr_buttons[names[b][h]] = value;
        }
        g.action = fx_xr.stick;
        XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
        if (fx_xrGetActionStateVector2f(fx_xr.session, &g, &st) == XR_SUCCESS && st.isActive) {
            fx_xr_stick[h][0] = st.currentState.x;
            fx_xr_stick[h][1] = st.currentState.y;
        }
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        if (fx_xrLocateSpace(fx_xr.hand_space[h], fx_xr.space, when, &loc) == XR_SUCCESS &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            fx_xr_hands[h] = fx_mat_point(base, fx_v(loc.pose.position.x, loc.pose.position.y, loc.pose.position.z));
            fx_xr_hand_seen[h] = true;
        }
    }
}

/* One frame in the headset: called while drawing each frame, after the
   shapes are uploaded and the sun's shadows are drawn. */
static void fx_xr_frame(void) {
    fx_xr_events();
    if (!fx_xr.session || !fx_xr.running) { fx_xr_running = false; return; }
    XrFrameWaitInfo wi = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState fs = { XR_TYPE_FRAME_STATE };
    if (fx_xrWaitFrame(fx_xr.session, &wi, &fs) != XR_SUCCESS) return;
    XrFrameBeginInfo bi = { XR_TYPE_FRAME_BEGIN_INFO };
    if (fx_xrBeginFrame(fx_xr.session, &bi) != XR_SUCCESS) return;
    fx_xr_running = fx_xr.state == XR_SESSION_STATE_FOCUSED || fx_xr.state == XR_SESSION_STATE_VISIBLE;
    fx_mat4 base = fx_xr_play_to_world();
    fx_xr_input(fs.predictedDisplayTime, base);

    XrCompositionLayerProjectionView pv[2];
    XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    const XrCompositionLayerBaseHeader *layers[1] = { (const XrCompositionLayerBaseHeader *)&layer };
    uint32_t layer_count = 0;
    XrView views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    XrViewState vs = { XR_TYPE_VIEW_STATE };
    XrViewLocateInfo li = { XR_TYPE_VIEW_LOCATE_INFO };
    li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    li.displayTime = fs.predictedDisplayTime;
    li.space = fx_xr.space;
    uint32_t nv = 0;
    if (fs.shouldRender && fx_xrLocateViews(fx_xr.session, &li, &vs, 2, &nv, views) == XR_SUCCESS &&
        (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)) {
        for (uint32_t v = 0; v < nv && v < fx_xr.views; v++) {
            uint32_t idx = 0;
            XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if (fx_xrAcquireSwapchainImage(fx_xr.swapchain[v], &ai, &idx) != XR_SUCCESS) continue;
            XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            wait.timeout = XR_INFINITE_DURATION;
            fx_xrWaitSwapchainImage(fx_xr.swapchain[v], &wait);
            int w = (int)fx_xr.view_info[v].recommendedImageRectWidth, h = (int)fx_xr.view_info[v].recommendedImageRectHeight;
            fx_mat4 eye = fx_mat_mul(base, fx_xr_pose_matrix(views[v].pose));
            fx_v3 eye_pos = fx_v(eye.m[12], eye.m[13], eye.m[14]);
            glBindFramebuffer(GL_FRAMEBUFFER, fx_xr.msaa_fbo[v]);
            glViewport(0, 0, w, h);
            fx_draw_world(fx_mat_invert(eye), fx_xr_projection(views[v].fov, 0.05f, 1000.0f), eye_pos);
            glBindFramebuffer(GL_FRAMEBUFFER, fx_xr.fbo[v]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fx_xr.images[v][idx], 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fx_xr.msaa_fbo[v]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fx_xr.fbo[v]);
            glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            /* tests: on the last frame, keep what the left eye saw too (as NAME-vr.bmp) */
            if (v == 0 && fx_shot_path && fx_frame_limit >= 0 && fx_frames + 1 >= fx_frame_limit) {
                char path[4096];
                snprintf(path, sizeof path, "%.*s-vr.bmp", (int)(strlen(fx_shot_path) - (strlen(fx_shot_path) > 4 ? 4 : 0)),
                         fx_shot_path);
                fx_save_picture(fx_xr.fbo[0], w, h, path, 0);
            }
            XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            fx_xrReleaseSwapchainImage(fx_xr.swapchain[v], &ri);
            memset(&pv[v], 0, sizeof pv[v]);
            pv[v].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            pv[v].pose = views[v].pose;
            pv[v].fov = views[v].fov;
            pv[v].subImage.swapchain = fx_xr.swapchain[v];
            pv[v].subImage.imageRect.extent.width = w;
            pv[v].subImage.imageRect.extent.height = h;
        }
        layer.space = fx_xr.space;
        layer.viewCount = nv < fx_xr.views ? nv : fx_xr.views;
        layer.views = pv;
        layer_count = 1;
    }
    XrFrameEndInfo ei = { XR_TYPE_FRAME_END_INFO };
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = layer_count;
    ei.layers = layers;
    fx_xrEndFrame(fx_xr.session, &ei);
}

/* ========================== end of Flaux VR ================================ */
