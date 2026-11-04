/* ===================================================================
 * Flaux 3D: the graphics runtime for programs that use 3D or shaders.
 *
 * Everything is drawn by the graphics card with OpenGL 3.3, which every
 * Windows PC from the last 15 years has. The 2D functions (rect, write,
 * image, ...) work here too and are drawn on top of the 3D world, with
 * smooth edges.
 *
 * How a frame is drawn (at show()):
 *   1. every shape drawn since the last frame is sorted into batches (same
 *      shape, picture and shader), so thousands of cubes are a few draws
 *   2. the sun's shadow map: the scene seen from the sun
 *   3. the sky, then every batch, lit by the sun, the lights and the sky,
 *      with shadows and fog, into a multisampled (smooth-edged) picture
 *   4. the 2D drawing on top
 *   5. the screen effect (a shader written in Flaux), or a plain copy, to
 *      the window
 *
 * Fullscreen and sharp screens work as in 2D (see "Fullscreen" in
 * graphics.h): the program keeps its window(...) size in pixels, and the
 * picture is shown as big as fits, with black bars. Steps 1-4 are drawn at
 * the size it's shown at (fx_cw by fx_ch: every pixel of the screen), so 3D
 * is sharp in fullscreen too; 2D on top is still in the program's pixels.
 * With Mesa (no good graphics card) it's drawn at the window(...) size and
 * scaled up, to keep games fast.
 * =================================================================== */

/* ------------------------------------------------- OpenGL, without headers */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#ifdef _WIN32
#define FXGL_API __stdcall
#else
#define FXGL_API
#endif

#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_BACK 0x0405
#define GL_NONE 0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT 0x1902
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_MAX_SAMPLES 0x8D57
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#define GL_UNPACK_ALIGNMENT 0x0CF5

#define FX_GL_FUNCTIONS(X) \
    X(const GLubyte *, GetString, (GLenum name)) \
    X(void, GetIntegerv, (GLenum pname, GLint *data)) \
    X(void, GetFloatv, (GLenum pname, GLfloat *data)) \
    X(GLenum, GetError, (void)) \
    X(void, Enable, (GLenum cap)) \
    X(void, Disable, (GLenum cap)) \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, Clear, (GLbitfield mask)) \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, DepthFunc, (GLenum f)) \
    X(void, DepthMask, (GLboolean f)) \
    X(void, ColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
    X(void, BlendFunc, (GLenum s, GLenum d)) \
    X(void, PolygonOffset, (GLfloat factor, GLfloat units)) \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)) \
    X(void, PixelStorei, (GLenum p, GLint v)) \
    X(void, DrawBuffer, (GLenum b)) \
    X(void, ReadBuffer, (GLenum b)) \
    X(void, GenTextures, (GLsizei n, GLuint *t)) \
    X(void, DeleteTextures, (GLsizei n, const GLuint *t)) \
    X(void, BindTexture, (GLenum target, GLuint t)) \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, \
                         GLenum format, GLenum type, const void *pixels)) \
    X(void, TexParameteri, (GLenum target, GLenum p, GLint v)) \
    X(void, ActiveTexture, (GLenum t)) \
    X(void, GenerateMipmap, (GLenum target)) \
    X(void, GenSamplers, (GLsizei n, GLuint *s)) \
    X(void, BindSampler, (GLuint unit, GLuint s)) \
    X(void, SamplerParameteri, (GLuint s, GLenum p, GLint v)) \
    X(void, SamplerParameterf, (GLuint s, GLenum p, GLfloat v)) \
    X(void, GenBuffers, (GLsizei n, GLuint *b)) \
    X(void, BindBuffer, (GLenum target, GLuint b)) \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, BufferSubData, (GLenum target, GLintptr off, GLsizeiptr size, const void *data)) \
    X(void, GenVertexArrays, (GLsizei n, GLuint *a)) \
    X(void, BindVertexArray, (GLuint a)) \
    X(void, EnableVertexAttribArray, (GLuint i)) \
    X(void, VertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, \
                                  const void *ptr)) \
    X(void, VertexAttrib4f, (GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w)) \
    X(void, VertexAttribDivisor, (GLuint i, GLuint d)) \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(void, DrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void *idx, GLsizei n)) \
    X(GLuint, CreateShader, (GLenum type)) \
    X(void, ShaderSource, (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len)) \
    X(void, CompileShader, (GLuint s)) \
    X(void, GetShaderiv, (GLuint s, GLenum p, GLint *v)) \
    X(void, GetShaderInfoLog, (GLuint s, GLsizei max, GLsizei *len, GLchar *log)) \
    X(void, DeleteShader, (GLuint s)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint p, GLuint s)) \
    X(void, LinkProgram, (GLuint p)) \
    X(void, GetProgramiv, (GLuint p, GLenum q, GLint *v)) \
    X(void, GetProgramInfoLog, (GLuint p, GLsizei max, GLsizei *len, GLchar *log)) \
    X(void, UseProgram, (GLuint p)) \
    X(GLint, GetUniformLocation, (GLuint p, const GLchar *name)) \
    X(void, Uniform1i, (GLint l, GLint v)) \
    X(void, Uniform1f, (GLint l, GLfloat v)) \
    X(void, Uniform2f, (GLint l, GLfloat a, GLfloat b)) \
    X(void, Uniform3f, (GLint l, GLfloat a, GLfloat b, GLfloat c)) \
    X(void, Uniform1fv, (GLint l, GLsizei n, const GLfloat *v)) \
    X(void, Uniform4fv, (GLint l, GLsizei n, const GLfloat *v)) \
    X(void, UniformMatrix4fv, (GLint l, GLsizei n, GLboolean t, const GLfloat *v)) \
    X(void, GenFramebuffers, (GLsizei n, GLuint *f)) \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *f)) \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint *r)) \
    X(void, Uniform4f, (GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d)) \
    X(void, BindFramebuffer, (GLenum target, GLuint f)) \
    X(void, FramebufferTexture2D, (GLenum target, GLenum att, GLenum textarget, GLuint t, GLint level)) \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum att, GLenum rbt, GLuint rb)) \
    X(GLenum, CheckFramebufferStatus, (GLenum target)) \
    X(void, GenRenderbuffers, (GLsizei n, GLuint *r)) \
    X(void, BindRenderbuffer, (GLenum target, GLuint r)) \
    X(void, RenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum fmt, GLsizei w, GLsizei h)) \
    X(void, BlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, \
                              GLint dy1, GLbitfield mask, GLenum filter))

#define FX_GL_DECLARE(ret, name, args) typedef ret (FXGL_API *fxgl_##name##_t) args; static fxgl_##name##_t gl##name;
FX_GL_FUNCTIONS(FX_GL_DECLARE)

static const char *fx_gl_header = "#version 330 core\n";

/* ------------------------------------------------------------- maths */

typedef struct { float m[16]; } fx_mat4;   /* column by column, like OpenGL */
typedef struct { float x, y, z; } fx_v3;

static fx_v3 fx_v(float x, float y, float z) { return (fx_v3){ x, y, z }; }
static fx_v3 fx_sub3(fx_v3 a, fx_v3 b) { return fx_v(a.x - b.x, a.y - b.y, a.z - b.z); }
static fx_v3 fx_add3(fx_v3 a, fx_v3 b) { return fx_v(a.x + b.x, a.y + b.y, a.z + b.z); }
static fx_v3 fx_mul3(fx_v3 a, float s) { return fx_v(a.x * s, a.y * s, a.z * s); }
static float fx_dot3(fx_v3 a, fx_v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static fx_v3 fx_cross3(fx_v3 a, fx_v3 b) {
    return fx_v(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static fx_v3 fx_norm3(fx_v3 a) {
    float l = sqrtf(fx_dot3(a, a));
    return l > 1e-12f ? fx_mul3(a, 1.0f / l) : fx_v(0, 1, 0);
}

static fx_mat4 fx_mat_mul(fx_mat4 a, fx_mat4 b) {
    fx_mat4 r;
    for (int c = 0; c < 4; c++)
        for (int w = 0; w < 4; w++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + w] * b.m[c * 4 + k];
            r.m[c * 4 + w] = s;
        }
    return r;
}

static fx_mat4 fx_perspective(float fovy, float aspect, float zn, float zf) {
    fx_mat4 r = { { 0 } };
    float f = 1.0f / tanf(fovy / 2);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}

static fx_mat4 fx_ortho(float l, float r_, float b, float t, float zn, float zf) {
    fx_mat4 r = { { 0 } };
    r.m[0] = 2 / (r_ - l);
    r.m[5] = 2 / (t - b);
    r.m[10] = -2 / (zf - zn);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(zf + zn) / (zf - zn);
    r.m[15] = 1;
    return r;
}

static fx_mat4 fx_look_at(fx_v3 eye, fx_v3 target) {
    fx_v3 f = fx_norm3(fx_sub3(target, eye));
    fx_v3 up = fabsf(f.y) > 0.999f ? fx_v(0, 0, f.y > 0 ? 1.0f : -1.0f) : fx_v(0, 1, 0);
    fx_v3 s = fx_norm3(fx_cross3(f, up));
    fx_v3 u = fx_cross3(s, f);
    fx_mat4 r = { { s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                    -fx_dot3(s, eye), -fx_dot3(u, eye), fx_dot3(f, eye), 1 } };
    return r;
}

static fx_mat4 fx_mat_invert(fx_mat4 a) {
    const float *m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    fx_mat4 r;
    float d = det != 0 ? 1.0f / det : 0;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * d;
    return r;
}

static fx_v3 fx_mat_point(fx_mat4 a, fx_v3 p) {
    return fx_v(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
                a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
                a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]);
}

/* sRGB colour (0-255) to the linear light the shaders work in */
static float fx_lin_table[256];
static float fx_lin(uint8_t c) { return fx_lin_table[c]; }

/* -------------------------------------------------------------- state */

static SDL_GLContext fx_gl;
static int fx_samples;                 /* smooth edges: how many samples per pixel */
static GLuint fx_msaa_fbo, fx_msaa_color, fx_msaa_depth;
static GLuint fx_scene_fbo, fx_scene_tex;
static bool fx_soft_gl;                 /* drawing with Mesa (the processor), not a graphics card */
static GLint fx_max_size = 4096;        /* the biggest picture this graphics card can draw into */
static GLuint fx_shadow_fbo, fx_shadow_tex;
#define FX_SHADOW_SIZE 2048
static GLuint fx_inst_vbo, fx_flat_vao, fx_flat_vbo, fx_empty_vao;
static GLuint fx_white_tex, fx_font_gl;
static GLuint fx_samp_pixel, fx_samp_smooth, fx_samp_blocky, fx_samp_shadow, fx_samp_scene;

/* the camera */
static fx_v3 fx_cam_eye = { 0, 3, 10 }, fx_cam_target = { 0, 0, 0 };
static float fx_cam_fov = 60;
/* the sky: top and horizon colours (sRGB 0-255) */
static fx_rgb_t fx_sky_top = { 0, 0, 0 }, fx_sky_horizon = { 0, 0, 0 };
static bool fx_sky_set;
static bool fx_drew_3d;                /* any 3D shape so far (for the default sky) */
/* the sun: direction towards it, and its colour */
static fx_v3 fx_sun_dir = { 0.4f, 1.0f, 0.3f };
static fx_rgb_t fx_sun_rgb = { 255, 255, 255 };
static bool fx_sun_on = true;
/* fog */
static float fx_fog_far;
static bool fx_fog_own_color;
static fx_rgb_t fx_fog_rgb;
/* lights for this frame */
static float fx_lights[16 * 4];
static int fx_light_count;
/* turning: applies to the shapes drawn after turn(...), until the next frame */
static float fx_turn_m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
/* the screen effect for this frame */
static int fx_effect_id = -1;
static float fx_effect_vals[8];

/* ------------------------------------------------------------ shaders */

#ifndef FX_SHADERS
#define FX_SHADERS 0
static const char *const fx_shader_surface[1] = { NULL };
static const char *const fx_shader_screen[1] = { NULL };
static const char *const fx_shader_names[1] = { NULL };
#endif

/* the engine's GPU programs (engine.glsl), filled in by graphics.py */
/*@GLSL@*/

static GLuint fx_compile(GLenum type, const char *const *parts, int n, const char *what) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, n, parts, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fx_fail(0, "This is a bug in Flaux, not in your program. Please report it, with your program.",
                "The graphics card didn't accept the %s shader:\n%s", what, log);
    }
    return s;
}

static GLuint fx_program(const char *vs, const char *fs, const char *user, const char *what) {
    const char *vparts[] = { fx_gl_header, fx_glsl_common, vs };
    const char *fparts[] = { fx_gl_header, fx_glsl_common, fs, user ? user : "", "" };
    /* the user's shader goes where engine.glsl says //@@USER@@ */
    const char *mark = strstr(fs, "//@@USER@@");
    char *before = NULL;
    int nf = 3;
    if (mark) {
        size_t n = (size_t)(mark - fs);
        before = fx_alloc(n + 1);
        memcpy(before, fs, n);
        before[n] = 0;
        fparts[2] = before;
        fparts[3] = user ? user : "";
        fparts[4] = mark;
        nf = 5;
    }
    GLuint v = fx_compile(GL_VERTEX_SHADER, vparts, 3, what);
    GLuint f = fx_compile(GL_FRAGMENT_SHADER, fparts, nf, what);
    free(before);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fx_fail(0, "This is a bug in Flaux, not in your program. Please report it, with your program.",
                "The graphics card couldn't link the %s shader:\n%s", what, log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

typedef struct {
    GLuint id;
    GLint viewproj, tex, textured, shadow_map, shadows, shadow_mat, sun_dir, sun_color, amb_top, amb_bottom,
          camera, lights, light_count, fog_color, fog, time;
} fx_scene_prog_t;

static fx_scene_prog_t fx_scene_prog;                     /* plain colours and pictures */
static fx_scene_prog_t fx_surface_progs[FX_SHADERS + 1];  /* one per Flaux shader, made when first used */
static GLuint fx_effect_progs[FX_SHADERS + 1];
static GLuint fx_shadow_prog, fx_sky_prog, fx_flat_prog;

static fx_scene_prog_t fx_scene_program(const char *user, const char *what) {
    fx_scene_prog_t p;
    p.id = fx_program(fx_glsl_scene_vs, fx_glsl_scene_fs, user, what);
#define FX_U(field, name) p.field = glGetUniformLocation(p.id, name)
    FX_U(viewproj, "u_viewproj"); FX_U(tex, "u_tex"); FX_U(textured, "u_textured");
    FX_U(shadow_map, "u_shadow_map"); FX_U(shadows, "u_shadows"); FX_U(shadow_mat, "u_shadow_mat");
    FX_U(sun_dir, "u_sun_dir"); FX_U(sun_color, "u_sun_color"); FX_U(amb_top, "u_amb_top");
    FX_U(amb_bottom, "u_amb_bottom"); FX_U(camera, "u_camera"); FX_U(lights, "u_lights");
    FX_U(light_count, "u_light_count"); FX_U(fog_color, "u_fog_color"); FX_U(fog, "u_fog"); FX_U(time, "u_time");
#undef FX_U
    glUseProgram(p.id);
    glUniform1i(p.tex, 0);
    glUniform1i(p.shadow_map, 1);
    return p;
}

static fx_scene_prog_t *fx_surface_program(int shader) {
    if (shader < 0) return &fx_scene_prog;
    if (!fx_surface_progs[shader].id) {
        char what[200];
        snprintf(what, sizeof what, "\"%s\"", fx_shader_names[shader] ? fx_shader_names[shader] : "?");
        fx_surface_progs[shader] = fx_scene_program(fx_shader_surface[shader], what);
    }
    return &fx_surface_progs[shader];
}

static GLuint fx_effect_program(int shader) {
    if (!fx_effect_progs[shader]) {
        char what[200];
        snprintf(what, sizeof what, "\"%s\" screen effect", fx_shader_names[shader] ? fx_shader_names[shader] : "?");
        GLuint p = fx_program(fx_glsl_fullscreen_vs, fx_glsl_effect_fs, fx_shader_screen[shader], what);
        glUseProgram(p);
        glUniform1i(glGetUniformLocation(p, "u_scene"), 0);
        fx_effect_progs[shader] = p;
    }
    return fx_effect_progs[shader];
}

/* ------------------------------------------------------------- meshes */

typedef struct { float pos[3], normal[3], uv[2]; uint8_t rgba[4]; } fx_vertex_t;
typedef struct { GLuint vao, vbo, ebo; int count; bool two_sided; char *name; } fx_mesh_t;
enum { FX_CUBE, FX_BALL, FX_CYLINDER, FX_CONE, FX_PLANE, FX_FIRST_MODEL };
static fx_mesh_t *fx_meshes;
static int fx_mesh_count, fx_mesh_cap;

typedef struct { fx_vertex_t *v; uint32_t *i; int nv, ni, cv, ci; } fx_builder_t;

static int fx_add_vertex(fx_builder_t *b, fx_v3 p, fx_v3 n, float u, float v) {
    if (b->nv == b->cv) { b->cv = b->cv ? b->cv * 2 : 256; b->v = fx_realloc(b->v, sizeof(fx_vertex_t) * (size_t)b->cv); }
    b->v[b->nv] = (fx_vertex_t){ { p.x, p.y, p.z }, { n.x, n.y, n.z }, { u, v }, { 255, 255, 255, 255 } };
    return b->nv++;
}

static void fx_add_tri(fx_builder_t *b, int x, int y, int z) {
    if (b->ni + 3 > b->ci) { b->ci = b->ci ? b->ci * 2 : 512; b->i = fx_realloc(b->i, sizeof(uint32_t) * (size_t)b->ci); }
    b->i[b->ni++] = (uint32_t)x;
    b->i[b->ni++] = (uint32_t)y;
    b->i[b->ni++] = (uint32_t)z;
}

static int fx_finish_mesh(fx_builder_t *b, bool two_sided, const char *name) {
    if (fx_mesh_count == fx_mesh_cap) {
        fx_mesh_cap = fx_mesh_cap ? fx_mesh_cap * 2 : 16;
        fx_meshes = fx_realloc(fx_meshes, sizeof(fx_mesh_t) * (size_t)fx_mesh_cap);
    }
    fx_mesh_t *m = &fx_meshes[fx_mesh_count];
    memset(m, 0, sizeof *m);
    m->count = b->ni;
    m->two_sided = two_sided;
    m->name = name ? (char *)fx_text_make(name, (int64_t)strlen(name)).p : NULL;
    glGenVertexArrays(1, &m->vao);
    glBindVertexArray(m->vao);
    glGenBuffers(1, &m->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(fx_vertex_t) * (size_t)b->nv), b->v, GL_STATIC_DRAW);
    glGenBuffers(1, &m->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(sizeof(uint32_t) * (size_t)b->ni), b->i, GL_STATIC_DRAW);
    GLsizei st = sizeof(fx_vertex_t);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, 0, st, (void *)offsetof(fx_vertex_t, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, 0, st, (void *)offsetof(fx_vertex_t, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, 0, st, (void *)offsetof(fx_vertex_t, uv));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(8, 4, GL_UNSIGNED_BYTE, 1, st, (void *)offsetof(fx_vertex_t, rgba));
    glBindBuffer(GL_ARRAY_BUFFER, fx_inst_vbo);
    for (int a = 3; a <= 7; a++) {
        glEnableVertexAttribArray((GLuint)a);
        glVertexAttribDivisor((GLuint)a, 1);
    }
    glBindVertexArray(0);
    free(b->v);
    free(b->i);
    return fx_mesh_count++;
}

/* one side of a cube: n is the way it faces, u and v run across it */
static void fx_cube_face(fx_builder_t *b, fx_v3 n, fx_v3 u, fx_v3 v, float half) {
    fx_v3 c = fx_mul3(n, half);
    int a = fx_add_vertex(b, fx_sub3(fx_sub3(c, fx_mul3(u, half)), fx_mul3(v, half)), n, 0, 1);
    int b1 = fx_add_vertex(b, fx_sub3(fx_add3(c, fx_mul3(u, half)), fx_mul3(v, half)), n, 1, 1);
    int c1 = fx_add_vertex(b, fx_add3(fx_add3(c, fx_mul3(u, half)), fx_mul3(v, half)), n, 1, 0);
    int d = fx_add_vertex(b, fx_add3(fx_sub3(c, fx_mul3(u, half)), fx_mul3(v, half)), n, 0, 0);
    fx_add_tri(b, a, b1, c1);
    fx_add_tri(b, a, c1, d);
}

static void fx_make_shapes(void) {
    fx_builder_t b = { 0 };
    /* cube: 1 wide, centred on 0 */
    fx_cube_face(&b, fx_v(1, 0, 0), fx_v(0, 0, -1), fx_v(0, 1, 0), 0.5f);
    fx_cube_face(&b, fx_v(-1, 0, 0), fx_v(0, 0, 1), fx_v(0, 1, 0), 0.5f);
    fx_cube_face(&b, fx_v(0, 1, 0), fx_v(1, 0, 0), fx_v(0, 0, -1), 0.5f);
    fx_cube_face(&b, fx_v(0, -1, 0), fx_v(1, 0, 0), fx_v(0, 0, 1), 0.5f);
    fx_cube_face(&b, fx_v(0, 0, 1), fx_v(1, 0, 0), fx_v(0, 1, 0), 0.5f);
    fx_cube_face(&b, fx_v(0, 0, -1), fx_v(-1, 0, 0), fx_v(0, 1, 0), 0.5f);
    fx_finish_mesh(&b, false, NULL);

    /* ball: radius 1 */
    memset(&b, 0, sizeof b);
    const int rings = 24, segs = 48;
    for (int i = 0; i <= rings; i++) {
        float th = (float)M_PI * i / rings;
        for (int j = 0; j <= segs; j++) {
            float ph = 2 * (float)M_PI * j / segs;
            fx_v3 p = fx_v(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
            fx_add_vertex(&b, p, p, (float)j / segs, (float)i / rings);
        }
    }
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            int a = i * (segs + 1) + j, lo = a + segs + 1;
            fx_add_tri(&b, a, a + 1, lo);
            fx_add_tri(&b, a + 1, lo + 1, lo);
        }
    fx_finish_mesh(&b, false, NULL);

    /* cylinder and cone: radius 1, 1 tall, centred on 0 */
    for (int cone = 0; cone < 2; cone++) {
        memset(&b, 0, sizeof b);
        for (int j = 0; j <= segs; j++) {
            float ph = 2 * (float)M_PI * j / segs, phm = 2 * (float)M_PI * (j + 0.5f) / segs;
            fx_v3 n = cone ? fx_norm3(fx_v(cosf(ph), 1, sinf(ph))) : fx_v(cosf(ph), 0, sinf(ph));
            fx_v3 top = cone ? fx_v(0, 0.5f, 0) : fx_v(cosf(ph), 0.5f, sinf(ph));
            fx_add_vertex(&b, top, cone ? fx_norm3(fx_v(cosf(phm), 1, sinf(phm))) : n, (float)j / segs, 0);
            fx_add_vertex(&b, fx_v(cosf(ph), -0.5f, sinf(ph)), n, (float)j / segs, 1);
        }
        for (int j = 0; j < segs; j++) {
            int t = 2 * j, lo = t + 1, t2 = t + 2, lo2 = t + 3;
            if (!cone) fx_add_tri(&b, t, t2, lo);
            fx_add_tri(&b, t2, lo2, lo);
            if (cone) b.i[b.ni - 3] = (uint32_t)t;   /* the tip, with the normal of this slice */
        }
        for (int cap = cone; cap < 2; cap++) {    /* cap 0: the top, cap 1: the bottom */
            float y = cap ? -0.5f : 0.5f;
            fx_v3 n = fx_v(0, cap ? -1.0f : 1.0f, 0);
            int c = fx_add_vertex(&b, fx_v(0, y, 0), n, 0.5f, 0.5f);
            for (int j = 0; j <= segs; j++) {
                float ph = 2 * (float)M_PI * j / segs;
                fx_add_vertex(&b, fx_v(cosf(ph), y, sinf(ph)), n, 0.5f + cosf(ph) / 2, 0.5f + sinf(ph) / 2);
            }
            for (int j = 0; j < segs; j++) {
                if (cap) fx_add_tri(&b, c, c + 1 + j, c + 2 + j);
                else fx_add_tri(&b, c, c + 2 + j, c + 1 + j);
            }
        }
        fx_finish_mesh(&b, false, NULL);
    }

    /* plane: flat, 1 by 1, facing up */
    memset(&b, 0, sizeof b);
    fx_cube_face(&b, fx_v(0, 1, 0), fx_v(1, 0, 0), fx_v(0, 0, -1), 0.5f);
    for (int i = 0; i < b.nv; i++) b.v[i].pos[1] = 0;
    fx_finish_mesh(&b, true, NULL);
}

/* ------------------------------------------------------- models (.obj) */

typedef struct { char name[128]; uint8_t rgb[3]; } fx_material_t;

static void fx_read_materials(const char *path, fx_material_t **mats, int *count) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1024];
    int cap = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (!strncmp(s, "newmtl ", 7)) {
            if (*count == cap) { cap = cap ? cap * 2 : 8; *mats = fx_realloc(*mats, sizeof(fx_material_t) * (size_t)cap); }
            fx_material_t *m = &(*mats)[(*count)++];
            snprintf(m->name, sizeof m->name, "%s", s + 7);
            m->name[strcspn(m->name, "\r\n")] = 0;
            m->rgb[0] = m->rgb[1] = m->rgb[2] = 255;
        } else if (!strncmp(s, "Kd ", 3) && *count) {
            float r = 1, g = 1, bl = 1;
            sscanf(s + 3, "%f %f %f", &r, &g, &bl);
            float c[3] = { r, g, bl };
            for (int k = 0; k < 3; k++) {
                float v = c[k] < 0 ? 0 : c[k] > 1 ? 1 : c[k];
                (*mats)[*count - 1].rgb[k] = (uint8_t)lroundf(powf(v, 1 / 2.2f) * 255); /* .mtl colours are linear */
            }
        }
    }
    fclose(f);
}

static FILE *fx_open_beside(const char *file, char *path, size_t size) {
    FILE *f = NULL;
    if (!fx_is_absolute(file) && FX_PROGRAM_DIR[0]) {
        snprintf(path, size, "%s/%s", FX_PROGRAM_DIR, file);
        f = fopen(path, "r");
    }
    if (!f) {
        snprintf(path, size, "%s", file);
        f = fopen(path, "r");
    }
    if (!f && !fx_is_absolute(file)) {   /* next to the program itself (a game someone shared) */
        char *base = SDL_GetBasePath();
        if (base) {
            snprintf(path, size, "%s%s", base, file);
            SDL_free(base);
            f = fopen(path, "r");
        }
    }
    return f;
}

/* Load a model made in Blender (or downloaded): a .obj file, with its .mtl
   colours if it has them. It's resized to fit in a 1 by 1 by 1 box. */
static int fx_load_model(const char *file, int line) {
    char path[4096];
    FILE *f = fx_open_beside(file, path, sizeof path);
    if (!f) {
        char fix[4400];
        snprintf(fix, sizeof fix, "Put the model in the same folder as your program%s%s,\n"
                 "  and check the name is exactly right (including .obj at the end).",
                 FX_PROGRAM_DIR[0] ? ":\n\n      " : "", FX_PROGRAM_DIR[0] ? FX_PROGRAM_DIR : "");
        fx_fail(line, fix, "I can't find the model \"%s\".", file);
    }
    float *pos = NULL, *nor = NULL, *tex = NULL;
    int np = 0, nn = 0, nt = 0, cp = 0, cn = 0, ct = 0;
    fx_material_t *mats = NULL;
    int nmats = 0;
    uint8_t rgb[3] = { 255, 255, 255 };
    fx_builder_t b = { 0 };
    char line_buf[4096];
    while (fgets(line_buf, sizeof line_buf, f)) {
        char *s = line_buf;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            if (np + 3 > cp) { cp = cp ? cp * 2 : 3072; pos = fx_realloc(pos, sizeof(float) * (size_t)cp); }
            float x = 0, y = 0, z = 0;
            sscanf(s + 2, "%f %f %f", &x, &y, &z);
            pos[np++] = x; pos[np++] = y; pos[np++] = z;
        } else if (s[0] == 'v' && s[1] == 'n') {
            if (nn + 3 > cn) { cn = cn ? cn * 2 : 3072; nor = fx_realloc(nor, sizeof(float) * (size_t)cn); }
            float x = 0, y = 0, z = 0;
            sscanf(s + 3, "%f %f %f", &x, &y, &z);
            nor[nn++] = x; nor[nn++] = y; nor[nn++] = z;
        } else if (s[0] == 'v' && s[1] == 't') {
            if (nt + 2 > ct) { ct = ct ? ct * 2 : 2048; tex = fx_realloc(tex, sizeof(float) * (size_t)ct); }
            float u = 0, v = 0;
            sscanf(s + 3, "%f %f", &u, &v);
            tex[nt++] = u; tex[nt++] = 1 - v;
        } else if (!strncmp(s, "mtllib ", 7)) {
            char name[1024], mpath[4096];
            snprintf(name, sizeof name, "%s", s + 7);
            name[strcspn(name, "\r\n")] = 0;
            /* the .mtl file is next to the .obj */
            const char *slash = strrchr(path, '/');
            const char *bslash = strrchr(path, '\\');
            if (bslash && (!slash || bslash > slash)) slash = bslash;
            if (slash) snprintf(mpath, sizeof mpath, "%.*s/%s", (int)(slash - path), path, name);
            else snprintf(mpath, sizeof mpath, "%s", name);
            fx_read_materials(mpath, &mats, &nmats);
        } else if (!strncmp(s, "usemtl ", 7)) {
            char name[128];
            snprintf(name, sizeof name, "%s", s + 7);
            name[strcspn(name, "\r\n")] = 0;
            rgb[0] = rgb[1] = rgb[2] = 255;
            for (int i = 0; i < nmats; i++)
                if (!strcmp(mats[i].name, name)) memcpy(rgb, mats[i].rgb, 3);
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            int idx[64][3], count = 0;
            char *p = s + 2;
            while (*p && count < 64) {
                while (*p == ' ' || *p == '\t') p++;
                if (!*p || *p == '\r' || *p == '\n') break;
                int vi = 0, ti = 0, ni = 0;
                vi = (int)strtol(p, &p, 10);
                if (*p == '/') {
                    p++;
                    if (*p != '/') ti = (int)strtol(p, &p, 10);
                    if (*p == '/') { p++; ni = (int)strtol(p, &p, 10); }
                }
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
                /* negative numbers count back from the end */
                if (vi < 0) vi = np / 3 + vi + 1;
                if (ti < 0) ti = nt / 2 + ti + 1;
                if (ni < 0) ni = nn / 3 + ni + 1;
                idx[count][0] = vi; idx[count][1] = ti; idx[count][2] = ni;
                count++;
            }
            if (count < 3) continue;
            int first = b.nv;
            fx_v3 c[3];
            for (int k = 0; k < 3; k++) {
                int vi = idx[k][0] - 1;
                c[k] = (vi >= 0 && vi < np / 3) ? fx_v(pos[vi * 3], pos[vi * 3 + 1], pos[vi * 3 + 2]) : fx_v(0, 0, 0);
            }
            fx_v3 flat = fx_norm3(fx_cross3(fx_sub3(c[1], c[0]), fx_sub3(c[2], c[0])));
            for (int k = 0; k < count; k++) {
                int vi = idx[k][0] - 1, ti = idx[k][1] - 1, ni = idx[k][2] - 1;
                fx_v3 p3 = (vi >= 0 && vi < np / 3) ? fx_v(pos[vi * 3], pos[vi * 3 + 1], pos[vi * 3 + 2]) : fx_v(0, 0, 0);
                fx_v3 n3 = (ni >= 0 && ni < nn / 3) ? fx_v(nor[ni * 3], nor[ni * 3 + 1], nor[ni * 3 + 2]) : flat;
                float u = (ti >= 0 && ti < nt / 2) ? tex[ti * 2] : 0, v = (ti >= 0 && ti < nt / 2) ? tex[ti * 2 + 1] : 0;
                int at = fx_add_vertex(&b, p3, n3, u, v);
                memcpy(b.v[at].rgba, rgb, 3);
            }
            for (int k = 1; k + 1 < count; k++) fx_add_tri(&b, first, first + k, first + k + 1);
        }
    }
    fclose(f);
    free(pos); free(nor); free(tex); free(mats);
    if (b.nv == 0)
        fx_fail(line, "Check it's a 3D model saved as .obj (in Blender: File, Export, Wavefront (.obj)).",
                "The model \"%s\" has nothing in it that Flaux can draw.", file);
    /* fit it in a 1 by 1 by 1 box, centred on 0 */
    fx_v3 lo = fx_v(1e30f, 1e30f, 1e30f), hi = fx_v(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < b.nv; i++) {
        float *q = b.v[i].pos;
        lo = fx_v(fminf(lo.x, q[0]), fminf(lo.y, q[1]), fminf(lo.z, q[2]));
        hi = fx_v(fmaxf(hi.x, q[0]), fmaxf(hi.y, q[1]), fmaxf(hi.z, q[2]));
    }
    fx_v3 mid = fx_mul3(fx_add3(lo, hi), 0.5f), size = fx_sub3(hi, lo);
    float big = fmaxf(size.x, fmaxf(size.y, size.z));
    float s = big > 0 ? 1 / big : 1;
    for (int i = 0; i < b.nv; i++) {
        float *q = b.v[i].pos;
        q[0] = (q[0] - mid.x) * s; q[1] = (q[1] - mid.y) * s; q[2] = (q[2] - mid.z) * s;
    }
    return fx_finish_mesh(&b, true, file);
}

static int fx_model_mesh(fx_text name, int line) {
    static const char *last_p;
    static int64_t last_n = -1;
    static int last_m = -1;
    if (name.p == last_p && name.n == last_n && last_m >= 0) return last_m;
    int found = -1;
    for (int i = FX_FIRST_MODEL; i < fx_mesh_count && found < 0; i++)
        if ((int64_t)strlen(fx_meshes[i].name) == name.n && !memcmp(fx_meshes[i].name, name.p, (size_t)name.n))
            found = i;
    if (found < 0) found = fx_load_model(fx_text_make(name.p, name.n).p, line);
    last_p = name.p;
    last_n = name.n;
    last_m = found;
    return found;
}

/* ----------------------------------------------------------- pictures */

typedef struct { char *name; GLuint tex; int w, h; } fx_image_t;
static fx_image_t *fx_images;
static int fx_image_count, fx_image_cap;

static GLuint fx_upload(const unsigned char *px, int w, int h, bool mipmaps) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    return t;
}

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
        GLuint tex = fx_upload(px, w, h, true);
        stbi_image_free(px);
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

static int64_t fx_image_width(fx_text name, int line) { return fx_image(name, line)->w; }
static int64_t fx_image_height(fx_text name, int line) { return fx_image(name, line)->h; }

/* ------------------------------------------------- what to paint shapes with */

typedef struct { fx_text t; int shader; float extra[4]; } fx_paint;
#define FX_PAINT(t) ((fx_paint){ (t), -1, { 0, 0, 0, 0 } })
#define FX_SHADE(id, a, b, c, d) ((fx_paint){ { "", 0 }, (id), { (float)(a), (float)(b), (float)(c), (float)(d) } })

static bool fx_is_picture_name(fx_text t) {
    static const char *const kinds[] = { ".png", ".jpg", ".jpeg", ".bmp", ".gif" };
    for (int k = 0; k < 5; k++) {
        int64_t n = (int64_t)strlen(kinds[k]);
        if (t.n > n) {
            bool same = true;
            for (int64_t i = 0; i < n && same; i++) same = tolower((unsigned char)t.p[t.n - n + i]) == kinds[k][i];
            if (same) return true;
        }
    }
    return false;
}

/* ------------------------------------------------------- shapes to draw */

typedef struct { float row[12], color[4], extra[4]; } fx_inst_t;
typedef struct { int mesh, shader; GLuint tex; bool blocky; int64_t n, cap; fx_inst_t *items; size_t offset; } fx_batch_t;
static fx_batch_t *fx_batches;
static int fx_batch_count, fx_batch_cap, fx_last_batch = -1;

static fx_batch_t *fx_batch(int mesh, GLuint tex, int shader) {
    if (fx_last_batch >= 0) {
        fx_batch_t *b = &fx_batches[fx_last_batch];
        if (b->mesh == mesh && b->tex == tex && b->shader == shader) return b;
    }
    for (int i = 0; i < fx_batch_count; i++) {
        fx_batch_t *b = &fx_batches[i];
        if (b->mesh == mesh && b->tex == tex && b->shader == shader) { fx_last_batch = i; return b; }
    }
    if (fx_batch_count == fx_batch_cap) {
        fx_batch_cap = fx_batch_cap ? fx_batch_cap * 2 : 16;
        fx_batches = fx_realloc(fx_batches, sizeof(fx_batch_t) * (size_t)fx_batch_cap);
    }
    fx_batch_t *b = &fx_batches[fx_batch_count];
    memset(b, 0, sizeof *b);
    b->mesh = mesh;
    b->tex = tex;
    b->shader = shader;
    fx_last_batch = fx_batch_count++;
    return b;
}

static void fx_shape(int mesh, double x, double y, double z, double sx, double sy, double sz, fx_paint paint,
                     bool tile, int line) {
    fx_need_window(line);
    float r = 1, g = 1, bl = 1;
    GLuint tex = 0;
    bool blocky = false;
    if (paint.shader < 0) {
        if (fx_is_picture_name(paint.t)) {
            fx_image_t *im = fx_image(paint.t, line);
            tex = im->tex;
            blocky = im->w <= 64 && im->h <= 64;   /* small pictures are pixel art: keep them crisp */
        } else {
            fx_rgb_t c = fx_color(paint.t, line);
            r = fx_lin(c.r); g = fx_lin(c.g); bl = fx_lin(c.b);
        }
    }
    fx_batch_t *b = fx_batch(mesh, tex, paint.shader);
    b->blocky = blocky;
    if (b->n == b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->items = fx_realloc(b->items, sizeof(fx_inst_t) * (size_t)b->cap);
    }
    fx_inst_t *it = &b->items[b->n++];
    const float *m = fx_turn_m;
    float s[3] = { (float)sx, (float)sy, (float)sz }, p[3] = { (float)x, (float)y, (float)z };
    for (int i = 0; i < 3; i++) {
        it->row[i * 4 + 0] = m[i * 3 + 0] * s[0];
        it->row[i * 4 + 1] = m[i * 3 + 1] * s[1];
        it->row[i * 4 + 2] = m[i * 3 + 2] * s[2];
        it->row[i * 4 + 3] = p[i];
    }
    it->color[0] = r; it->color[1] = g; it->color[2] = bl; it->color[3] = tile ? 1.0f : 0.0f;
    memcpy(it->extra, paint.extra, sizeof it->extra);
    fx_drew_3d = true;
    fx_dirty = true;
}

static void fx_g_cube(double x, double y, double z, double size, fx_paint p, int line) {
    fx_shape(FX_CUBE, x, y, z, size, size, size, p, false, line);
}
static void fx_g_box(double x, double y, double z, double w, double h, double d, fx_paint p, int line) {
    fx_shape(FX_CUBE, x, y, z, w, h, d, p, false, line);
}
static void fx_g_ball(double x, double y, double z, double radius, fx_paint p, int line) {
    fx_shape(FX_BALL, x, y, z, radius, radius, radius, p, false, line);
}
static void fx_g_cylinder(double x, double y, double z, double radius, double height, fx_paint p, int line) {
    fx_shape(FX_CYLINDER, x, y, z, radius, height, radius, p, false, line);
}
static void fx_g_cone(double x, double y, double z, double radius, double height, fx_paint p, int line) {
    fx_shape(FX_CONE, x, y, z, radius, height, radius, p, false, line);
}
static void fx_g_plane(double x, double y, double z, double w, double d, fx_paint p, int line) {
    fx_shape(FX_PLANE, x, y, z, w, 1, d, p, true, line);
}
static void fx_g_model(fx_text file, double x, double y, double z, double size, fx_paint p, int line) {
    fx_need_window(line);
    fx_shape(fx_model_mesh(file, line), x, y, z, size, size, size, p, false, line);
}

/* ------------------------------------------------- the scene's settings */

static void fx_g_camera(double x, double y, double z, double lx, double ly, double lz, double fov, int line) {
    (void)line;
    fx_cam_eye = fx_v((float)x, (float)y, (float)z);
    fx_cam_target = fx_v((float)lx, (float)ly, (float)lz);
    if (fx_cam_target.x == fx_cam_eye.x && fx_cam_target.y == fx_cam_eye.y && fx_cam_target.z == fx_cam_eye.z)
        fx_cam_target.z -= 1;   /* looking at itself: look ahead instead */
    fx_cam_fov = fov < 5 ? 5 : fov > 170 ? 170 : (float)fov;
}

static void fx_g_sky(fx_text top, fx_text horizon, bool two, int line) {
    fx_sky_top = fx_color(top, line);
    fx_sky_horizon = two ? fx_color(horizon, line) : fx_sky_top;
    fx_sky_set = true;
}

static void fx_g_sun(double x, double y, double z, fx_text color, int line) {
    fx_sun_on = !(x == 0 && y == 0 && z == 0);
    if (fx_sun_on) fx_sun_dir = fx_norm3(fx_v((float)x, (float)y, (float)z));
    fx_sun_rgb = fx_color(color, line);
}

static void fx_g_light(double x, double y, double z, fx_text color, double range, int line) {
    fx_rgb_t c = fx_color(color, line);
    if (fx_light_count >= 8 || range <= 0) return;   /* the 8 drawn first light the scene */
    float *l = &fx_lights[fx_light_count * 8];
    l[0] = (float)x; l[1] = (float)y; l[2] = (float)z; l[3] = (float)range;
    l[4] = fx_lin(c.r) * 1.5f; l[5] = fx_lin(c.g) * 1.5f; l[6] = fx_lin(c.b) * 1.5f; l[7] = 0;
    fx_light_count++;
}

static void fx_g_fog(double distance, fx_text color, bool colored, int line) {
    fx_fog_far = distance > 0 ? (float)distance : 0;
    fx_fog_own_color = colored;
    if (colored) fx_fog_rgb = fx_color(color, line);
}

static void fx_g_turn(double ax, double ay, double az) {
    double rx = ax * M_PI / 180, ry = ay * M_PI / 180, rz = az * M_PI / 180;
    double cx = cos(rx), sx = sin(rx), cy = cos(ry), sy = sin(ry), cz = cos(rz), sz = sin(rz);
    /* turn around z (roll), then x (tip forward), then y (turn left) */
    double Rx[9] = { 1, 0, 0, 0, cx, -sx, 0, sx, cx };
    double Ry[9] = { cy, 0, sy, 0, 1, 0, -sy, 0, cy };
    double Rz[9] = { cz, -sz, 0, sz, cz, 0, 0, 0, 1 };
    double t[9], r[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            t[i * 3 + j] = 0;
            for (int k = 0; k < 3; k++) t[i * 3 + j] += Rx[i * 3 + k] * Rz[k * 3 + j];
        }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            r[i * 3 + j] = 0;
            for (int k = 0; k < 3; k++) r[i * 3 + j] += Ry[i * 3 + k] * t[k * 3 + j];
        }
    for (int i = 0; i < 9; i++) fx_turn_m[i] = (float)r[i];
}

static void fx_g_effect(int shader, const float *vals, int n) {
    fx_effect_id = shader;
    memset(fx_effect_vals, 0, sizeof fx_effect_vals);
    for (int i = 0; i < n && i < 8; i++) fx_effect_vals[i] = vals[i];
    fx_dirty = true;
}

/* ------------------------------------------------------------ VR and AR */

/* Where your hands are (in a VR headset), and if you're in one. Without a
   headset, your hands are where the camera is. */
static fx_v3 fx_xr_hands[2];
static bool fx_xr_hand_seen[2];

static bool fx_in_vr(void) { return fx_xr_running; }

static double fx_hand(fx_text side, int axis, int line) {
    char key[40];
    fx_squash(side, key, sizeof key);
    int h = !strcmp(key, "left") ? 0 : !strcmp(key, "right") ? 1 : -1;
    if (h < 0) fx_fail(line, "Use hand_x(\"left\") or hand_x(\"right\").", "You have a \"left\" and a \"right\" hand, not \"%.*s\".",
                       (int)side.n, side.p);
    fx_v3 p = fx_xr_hand_seen[h] ? fx_xr_hands[h] : fx_cam_eye;
    return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}

/* ------------------------------------------------------------ 2D on top */

typedef struct { float x, y, u, v; uint8_t rgba[4]; } fx_flat_vertex_t;
typedef struct { GLuint tex; int first, count; } fx_flat_run_t;
static fx_flat_vertex_t *fx_flat;
static int fx_flat_n, fx_flat_cap;
static fx_flat_run_t *fx_runs;
static int fx_run_n, fx_run_cap;

static void fx_flat_use(GLuint tex) {
    if (fx_run_n && fx_runs[fx_run_n - 1].tex == tex) return;
    if (fx_run_n == fx_run_cap) {
        fx_run_cap = fx_run_cap ? fx_run_cap * 2 : 64;
        fx_runs = fx_realloc(fx_runs, sizeof(fx_flat_run_t) * (size_t)fx_run_cap);
    }
    fx_runs[fx_run_n++] = (fx_flat_run_t){ tex, fx_flat_n, 0 };
}

static void fx_flat_vertex(float x, float y, float u, float v, fx_rgb_t c) {
    if (fx_flat_n == fx_flat_cap) {
        fx_flat_cap = fx_flat_cap ? fx_flat_cap * 2 : 1024;
        fx_flat = fx_realloc(fx_flat, sizeof(fx_flat_vertex_t) * (size_t)fx_flat_cap);
    }
    fx_flat[fx_flat_n++] = (fx_flat_vertex_t){ x, y, u, v, { c.r, c.g, c.b, 255 } };
    fx_runs[fx_run_n - 1].count++;
    fx_dirty = true;
}

static void fx_flat_tri(float x1, float y1, float x2, float y2, float x3, float y3, fx_rgb_t c) {
    fx_flat_use(fx_white_tex);
    fx_flat_vertex(x1, y1, 0.5f, 0.5f, c);
    fx_flat_vertex(x2, y2, 0.5f, 0.5f, c);
    fx_flat_vertex(x3, y3, 0.5f, 0.5f, c);
}

static void fx_flat_quad(GLuint tex, const float xy[8], const float uv[8], fx_rgb_t c) {
    fx_flat_use(tex);
    const int order[6] = { 0, 1, 2, 0, 2, 3 };
    for (int k = 0; k < 6; k++) fx_flat_vertex(xy[order[k] * 2], xy[order[k] * 2 + 1], uv[order[k] * 2], uv[order[k] * 2 + 1], c);
}

static void fx_flat_rect(float x, float y, float w, float h, fx_rgb_t c) {
    float xy[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    float uv[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    fx_flat_quad(fx_white_tex, xy, uv, c);
}

/* clear(...) starts the picture again, with this colour as the sky */
static void fx_g_clear(fx_text color, int line) {
    fx_need_window(line);
    fx_sky_top = fx_sky_horizon = fx_color(color, line);
    fx_sky_set = true;
    for (int i = 0; i < fx_batch_count; i++) fx_batches[i].n = 0;
    fx_flat_n = fx_run_n = 0;
    fx_dirty = true;
}

static void fx_g_rect(double x, double y, double w, double h, fx_text color, int line) {
    fx_need_window(line);
    fx_rgb_t c = fx_color(color, line);
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    int x0 = fx_px(x), y0 = fx_px(y);
    fx_flat_rect((float)x0, (float)y0, (float)(fx_px(x + w) - x0), (float)(fx_px(y + h) - y0), c);
}

static void fx_g_circle(double cx, double cy, double radius, fx_text color, int line) {
    fx_need_window(line);
    fx_rgb_t c = fx_color(color, line);
    if (radius <= 0) return;
    if (radius > 20000) radius = 20000;
    int segs = (int)(radius * 1.5);
    if (segs < 16) segs = 16;
    if (segs > 256) segs = 256;
    for (int i = 0; i < segs; i++) {
        double a0 = 2 * M_PI * i / segs, a1 = 2 * M_PI * (i + 1) / segs;
        fx_flat_tri((float)cx, (float)cy, (float)(cx + cos(a0) * radius), (float)(cy + sin(a0) * radius),
                    (float)(cx + cos(a1) * radius), (float)(cy + sin(a1) * radius), c);
    }
}

static void fx_g_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          fx_text color, int line) {
    fx_need_window(line);
    fx_flat_tri((float)x1, (float)y1, (float)x2, (float)y2, (float)x3, (float)y3, fx_color(color, line));
}

static void fx_g_line(double x1, double y1, double x2, double y2, fx_text color, double thickness, int line) {
    fx_need_window(line);
    fx_rgb_t c = fx_color(color, line);
    if (thickness < 1) thickness = 1;
    bool thin = thickness <= 1.5;
    if (thin) { x1 += 0.5; y1 += 0.5; x2 += 0.5; y2 += 0.5; }   /* through the middle of the pixels */
    double dx = x2 - x1, dy = y2 - y1, len = sqrt(dx * dx + dy * dy), r = thickness / 2;
    if (len > 0) {
        double nx = -dy / len * r, ny = dx / len * r;
        double ex = thin ? dx / len * 0.5 : 0, ey = thin ? dy / len * 0.5 : 0;   /* include both end pixels */
        float xy[8] = { (float)(x1 - ex + nx), (float)(y1 - ey + ny), (float)(x2 + ex + nx), (float)(y2 + ey + ny),
                        (float)(x2 + ex - nx), (float)(y2 + ey - ny), (float)(x1 - ex - nx), (float)(y1 - ey - ny) };
        float uv[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        fx_flat_quad(fx_white_tex, xy, uv, c);
    }
    if (!thin) {
        fx_g_circle(x1, y1, r, color, line);
        fx_g_circle(x2, y2, r, color, line);
    }
}

static void fx_g_dot(double x, double y, fx_text color, int line) {
    fx_need_window(line);
    fx_flat_rect((float)fx_px(x), (float)fx_px(y), 1, 1, fx_color(color, line));
}

static void fx_g_write(fx_text t, double x, double y, fx_text color, int64_t size, int line) {
    fx_need_window(line);
    fx_rgb_t c = fx_color(color, line);
    if (size < 1) size = 1;
    if (size > 50) size = 50;
    float px = (float)fx_px(x), py = (float)fx_px(y), s = (float)size;
    int col = 0, row = 0;
    for (int64_t i = 0; i < t.n; i++) {
        unsigned char ch = (unsigned char)t.p[i];
        if (ch == '\n') { row++; col = 0; continue; }
        if (ch >= 0x80 && ch < 0xC0) continue;
        if (ch < 32 || ch > 126) ch = '?';
        float x0 = px + col * 6 * s, y0 = py + row * 9 * s;
        float u0 = (ch - 32) * 6 / 570.0f, u1 = ((ch - 32) * 6 + 5) / 570.0f, v1 = 7 / 8.0f;
        float xy[8] = { x0, y0, x0 + 5 * s, y0, x0 + 5 * s, y0 + 7 * s, x0, y0 + 7 * s };
        float uv[8] = { u0, 0, u1, 0, u1, v1, u0, v1 };
        fx_flat_quad(fx_font_gl, xy, uv, c);
        col++;
    }
}

static void fx_g_image(fx_text name, double x, double y, double w, double h, double angle,
                       bool sized, int line) {
    fx_image_t *im = fx_image(name, line);
    double dw = sized ? w : im->w, dh = sized ? h : im->h;
    float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    if (dw < 0) { dw = -dw; u0 = 1; u1 = 0; }   /* a negative width flips it */
    if (dh < 0) { dh = -dh; v0 = 1; v1 = 0; }
    double x0 = fx_px(x), y0 = fx_px(y), x1 = fx_px(x + dw), y1 = fx_px(y + dh);
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, a = angle * M_PI / 180, ca = cos(a), sa = sin(a);
    double corners[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    float xy[8];
    for (int k = 0; k < 4; k++) {
        double dx = corners[k][0] - cx, dy = corners[k][1] - cy;
        xy[k * 2] = (float)(cx + dx * ca - dy * sa);
        xy[k * 2 + 1] = (float)(cy + dx * sa + dy * ca);
    }
    float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
    fx_rgb_t white = { 255, 255, 255 };
    fx_flat_quad(im->tex, xy, uv, white);
}

/* ------------------------------------------------------------ drawing */

static fx_mat4 fx_view, fx_proj;

static void fx_sky_colors(fx_rgb_t *top, fx_rgb_t *horizon) {
    if (fx_sky_set) { *top = fx_sky_top; *horizon = fx_sky_horizon; return; }
    if (fx_drew_3d) {   /* a 3D world with no sky(...): a blue one */
        *top = (fx_rgb_t){ 0x5a, 0xa8, 0xe8 };
        *horizon = (fx_rgb_t){ 0xcf, 0xe8, 0xf7 };
        return;
    }
    *top = *horizon = (fx_rgb_t){ 0, 0, 0 };
}

static int64_t fx_instance_total(void) {
    int64_t n = 0;
    for (int i = 0; i < fx_batch_count; i++) n += fx_batches[i].n;
    return n;
}

static void fx_upload_instances(void) {
    int64_t total = fx_instance_total();
    if (!total) return;
    glBindBuffer(GL_ARRAY_BUFFER, fx_inst_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(fx_inst_t) * (size_t)total), NULL, GL_STREAM_DRAW);
    size_t at = 0;
    for (int i = 0; i < fx_batch_count; i++) {
        fx_batch_t *b = &fx_batches[i];
        b->offset = at;
        if (!b->n) continue;
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)at, (GLsizeiptr)(sizeof(fx_inst_t) * (size_t)b->n), b->items);
        at += sizeof(fx_inst_t) * (size_t)b->n;
    }
}

static void fx_draw_batch(fx_batch_t *b) {
    fx_mesh_t *m = &fx_meshes[b->mesh];
    glBindVertexArray(m->vao);
    glBindBuffer(GL_ARRAY_BUFFER, fx_inst_vbo);
    GLsizei st = sizeof(fx_inst_t);
    for (int r = 0; r < 3; r++)
        glVertexAttribPointer((GLuint)(3 + r), 4, GL_FLOAT, 0, st, (void *)(b->offset + offsetof(fx_inst_t, row) + 16 * (size_t)r));
    glVertexAttribPointer(6, 4, GL_FLOAT, 0, st, (void *)(b->offset + offsetof(fx_inst_t, color)));
    glVertexAttribPointer(7, 4, GL_FLOAT, 0, st, (void *)(b->offset + offsetof(fx_inst_t, extra)));
    if (m->two_sided) glDisable(GL_CULL_FACE);
    else glEnable(GL_CULL_FACE);
    glDrawElementsInstanced(GL_TRIANGLES, m->count, GL_UNSIGNED_INT, 0, (GLsizei)b->n);
}

static fx_mat4 fx_shadow_mat;
static bool fx_shadows_now;

/* The scene seen from the sun, to know what's in shadow. It covers the area
   around where the camera looks, and moves in whole pixels so shadows don't
   shimmer as the camera moves. */
static void fx_shadow_pass(fx_v3 eye, fx_v3 target) {
    fx_shadows_now = fx_sun_on && fx_instance_total() > 0 && (fx_sun_rgb.r || fx_sun_rgb.g || fx_sun_rgb.b);
    if (!fx_shadows_now) return;
    fx_v3 to = fx_sub3(target, eye);
    float dist = sqrtf(fx_dot3(to, to));
    float radius = dist * 1.2f + 8;
    if (radius < 12) radius = 12;
    if (radius > 60) radius = 60;
    fx_v3 center = dist > radius ? fx_add3(eye, fx_mul3(to, radius / dist)) : target;
    fx_mat4 rot = fx_look_at(fx_v(0, 0, 0), fx_mul3(fx_sun_dir, -1));
    fx_v3 c = fx_mat_point(rot, center);
    float texel = 2 * radius / FX_SHADOW_SIZE;
    c.x = floorf(c.x / texel) * texel;
    c.y = floorf(c.y / texel) * texel;
    fx_mat4 proj = fx_ortho(c.x - radius, c.x + radius, c.y - radius, c.y + radius, -c.z - 3 * radius, -c.z + 3 * radius);
    fx_shadow_mat = fx_mat_mul(proj, rot);
    glBindFramebuffer(GL_FRAMEBUFFER, fx_shadow_fbo);
    glViewport(0, 0, FX_SHADOW_SIZE, FX_SHADOW_SIZE);
    glDepthMask(1);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glUseProgram(fx_shadow_prog);
    glUniformMatrix4fv(glGetUniformLocation(fx_shadow_prog, "u_viewproj"), 1, 0, fx_shadow_mat.m);
    for (int i = 0; i < fx_batch_count; i++)
        if (fx_batches[i].n) fx_draw_batch(&fx_batches[i]);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

/* Draw the sky and every shape, seen through view and proj, into whatever
   is bound now (the window's picture, or one eye of a VR headset). */
static void fx_draw_world(fx_mat4 view, fx_mat4 proj, fx_v3 eye) {
    fx_rgb_t top, horizon;
    fx_sky_colors(&top, &horizon);
    fx_mat4 viewproj = fx_mat_mul(proj, view);
    glDisable(GL_BLEND);
    glDepthMask(1);
    glClearColor(top.r / 255.0f, top.g / 255.0f, top.b / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (fx_instance_total() == 0) return;
    if (top.r != horizon.r || top.g != horizon.g || top.b != horizon.b) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(fx_sky_prog);
        fx_mat4 inv = fx_mat_invert(viewproj);
        glUniformMatrix4fv(glGetUniformLocation(fx_sky_prog, "u_inv_viewproj"), 1, 0, inv.m);
        glUniform3f(glGetUniformLocation(fx_sky_prog, "u_top"), top.r / 255.0f, top.g / 255.0f, top.b / 255.0f);
        glUniform3f(glGetUniformLocation(fx_sky_prog, "u_horizon"), horizon.r / 255.0f, horizon.g / 255.0f, horizon.b / 255.0f);
        glBindVertexArray(fx_empty_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, fx_shadow_tex);
    glBindSampler(1, fx_samp_shadow);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, fx_samp_smooth);
    float amb_top[3] = { fx_lin(top.r) * 0.45f + 0.10f, fx_lin(top.g) * 0.45f + 0.10f, fx_lin(top.b) * 0.45f + 0.10f };
    float amb_bottom[3] = { fx_lin(horizon.r) * 0.25f + 0.05f, fx_lin(horizon.g) * 0.25f + 0.05f,
                            fx_lin(horizon.b) * 0.25f + 0.05f };
    fx_rgb_t fog = fx_fog_own_color ? fx_fog_rgb : horizon;
    int last_prog = -2;
    for (int i = 0; i < fx_batch_count; i++) {
        fx_batch_t *b = &fx_batches[i];
        if (!b->n) continue;
        fx_scene_prog_t *p = fx_surface_program(b->shader);
        if (b->shader != last_prog) {
            last_prog = b->shader;
            glUseProgram(p->id);
            glUniformMatrix4fv(p->viewproj, 1, 0, viewproj.m);
            glUniformMatrix4fv(p->shadow_mat, 1, 0, fx_shadow_mat.m);
            glUniform1i(p->shadows, fx_shadows_now);
            glUniform3f(p->sun_dir, fx_sun_dir.x, fx_sun_dir.y, fx_sun_dir.z);
            if (fx_sun_on) glUniform3f(p->sun_color, fx_lin(fx_sun_rgb.r), fx_lin(fx_sun_rgb.g), fx_lin(fx_sun_rgb.b));
            else glUniform3f(p->sun_color, 0, 0, 0);
            glUniform3f(p->amb_top, amb_top[0], amb_top[1], amb_top[2]);
            glUniform3f(p->amb_bottom, amb_bottom[0], amb_bottom[1], amb_bottom[2]);
            glUniform3f(p->camera, eye.x, eye.y, eye.z);
            glUniform4fv(p->lights, 16, fx_lights);
            glUniform1i(p->light_count, fx_light_count);
            glUniform3f(p->fog_color, fx_lin(fog.r), fx_lin(fog.g), fx_lin(fog.b));
            glUniform2f(p->fog, fx_fog_far * 0.35f, fx_fog_far);
            glUniform1f(p->time, (float)fx_seconds());
        }
        glUniform1i(p->textured, b->tex != 0);
        glBindTexture(GL_TEXTURE_2D, b->tex ? b->tex : fx_white_tex);
        glBindSampler(0, b->blocky ? fx_samp_blocky : fx_samp_smooth);
        fx_draw_batch(b);
    }
    glDisable(GL_CULL_FACE);
}

static void fx_draw_flat(void) {
    if (!fx_flat_n) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(fx_flat_prog);
    glUniform2f(glGetUniformLocation(fx_flat_prog, "u_screen"), (float)fx_win_w, (float)fx_win_h);
    glBindVertexArray(fx_flat_vao);
    glBindBuffer(GL_ARRAY_BUFFER, fx_flat_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(fx_flat_vertex_t) * (size_t)fx_flat_n), fx_flat, GL_STREAM_DRAW);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, fx_samp_pixel);
    for (int i = 0; i < fx_run_n; i++) {
        if (!fx_runs[i].count) continue;
        glBindTexture(GL_TEXTURE_2D, fx_runs[i].tex);
        glDrawArrays(GL_TRIANGLES, fx_runs[i].first, fx_runs[i].count);
    }
    glDisable(GL_BLEND);
}

#ifdef FX_USES_XR
static void fx_xr_frame(void);   /* in xr.h */
static void fx_xr_start(void);
static GLuint fx_framebuffer(GLuint color_tex, GLuint color_rb, GLuint depth_rb, GLuint depth_tex);
static void fx_save_picture(GLuint fbo, int w, int h, const char *path, int line);
#endif

/* ---------------------------------------- fullscreen and sharp screens */

static void fx_gl_failed(int line, const char *why);
static GLuint fx_framebuffer(GLuint color_tex, GLuint color_rb, GLuint depth_rb, GLuint depth_tex);

/* How many pixels the window really has (more than its size on a sharp screen). */
static void fx_measure(void) {
    if (!fx_win) return;
    SDL_GL_GetDrawableSize(fx_win, &fx_out_w, &fx_out_h);
    if (fx_out_w < 1 || fx_out_h < 1) { fx_out_w = fx_win_w; fx_out_h = fx_win_h; }
}

/* Where the picture goes in the window, in OpenGL's pixels (counted from the bottom). */
static SDL_Rect fx_shown_rect(void) {
    SDL_Rect v = fx_fit(fx_out_w, fx_out_h, fx_win_w, fx_win_h);
    v.y = fx_out_h - v.y - v.h;
    return v;
}

/* The pictures each frame is drawn into, w by h pixels: smooth-edged (multisampled), then plain. */
static void fx_make_targets(int w, int h, int line) {
    glGenRenderbuffers(1, &fx_msaa_color);
    glBindRenderbuffer(GL_RENDERBUFFER, fx_msaa_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, fx_samples, GL_RGBA8, w, h);
    glGenRenderbuffers(1, &fx_msaa_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, fx_msaa_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, fx_samples, GL_DEPTH_COMPONENT24, w, h);
    fx_msaa_fbo = fx_framebuffer(0, fx_msaa_color, fx_msaa_depth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE && fx_samples > 0) {
        fx_samples = 0;   /* no smooth edges on this computer */
        glDeleteFramebuffers(1, &fx_msaa_fbo);
        glDeleteRenderbuffers(1, &fx_msaa_color);
        glDeleteRenderbuffers(1, &fx_msaa_depth);
        fx_make_targets(w, h, line);
        return;
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fx_gl_failed(line, "drawing off-screen didn't work");
    glGenTextures(1, &fx_scene_tex);
    glBindTexture(GL_TEXTURE_2D, fx_scene_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    fx_scene_fbo = fx_framebuffer(fx_scene_tex, 0, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fx_cw = w;
    fx_ch = h;
    fx_sx = (double)w / fx_win_w;
    fx_sy = (double)h / fx_win_h;
}

/* Draw at as many pixels as the picture is shown at (fullscreen, a sharp screen), but never
   fewer than the window(...) size, and not more with Mesa. Keeps the last frame, scaled. */
static void fx_fit_targets(void) {
    fx_measure();
    SDL_Rect v = fx_shown_rect();
    int w = v.w, h = v.h;
    if (fx_soft_gl || w < fx_win_w || h < fx_win_h || w > fx_max_size || h > fx_max_size) {
        w = fx_win_w;
        h = fx_win_h;
    }
    if (w == fx_cw && h == fx_ch) return;
    GLuint old_fbo = fx_scene_fbo, old_tex = fx_scene_tex, old_ms[3] = { fx_msaa_fbo, fx_msaa_color, fx_msaa_depth };
    int old_w = fx_cw, old_h = fx_ch;
    fx_make_targets(w, h, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, old_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fx_scene_fbo);
    glBlitFramebuffer(0, 0, old_w, old_h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &old_fbo);
    glDeleteTextures(1, &old_tex);
    glDeleteFramebuffers(1, &old_ms[0]);
    glDeleteRenderbuffers(2, &old_ms[1]);
}

/* The whole frame, into fx_scene_tex. `headset`: draw it in the VR headset too, if there is one. */
static void fx_render_frame(bool headset) {
    fx_fit_targets();
    float aspect = (float)fx_win_w / (float)fx_win_h;
    fx_view = fx_look_at(fx_cam_eye, fx_cam_target);
    fx_proj = fx_perspective(fx_cam_fov * (float)M_PI / 180, aspect, 0.1f, 1000.0f);
    fx_upload_instances();
    fx_shadow_pass(fx_cam_eye, fx_cam_target);
#ifdef FX_USES_XR
    if (headset) fx_xr_frame();   /* draws both eyes in the headset */
#else
    (void)headset;
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, fx_msaa_fbo);
    glViewport(0, 0, fx_cw, fx_ch);
    fx_draw_world(fx_view, fx_proj, fx_cam_eye);
    fx_draw_flat();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fx_msaa_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fx_scene_fbo);
    glBlitFramebuffer(0, 0, fx_cw, fx_ch, 0, 0, fx_cw, fx_ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

static int fx_shown_effect = -1;
static float fx_shown_vals[8];
static double fx_shown_time;

/* The finished picture to the window, through the screen effect if there is one. */
static void fx_present(void) {
    fx_fit_targets();     /* (the window may have changed: fullscreen, or a sharper screen) */
    SDL_Rect v = fx_shown_rect();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fx_out_w, fx_out_h);
    if (v.w != fx_out_w || v.h != fx_out_h) {   /* black bars */
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glViewport(v.x, v.y, v.w, v.h);
    if (fx_shown_effect >= 0) {
        GLuint p = fx_effect_program(fx_shown_effect);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(p);
        /* the effect sees the program's own pixels, whatever size it's shown at */
        glUniform2f(glGetUniformLocation(p, "u_size"), (float)fx_win_w, (float)fx_win_h);
        glUniform4f(glGetUniformLocation(p, "u_view"), (float)v.x, (float)v.y, (float)v.w, (float)v.h);
        glUniform1f(glGetUniformLocation(p, "u_time"), (float)fx_shown_time);
        glUniform1fv(glGetUniformLocation(p, "u_vars"), 8, fx_shown_vals);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fx_scene_tex);
        glBindSampler(0, fx_samp_scene);
        glBindVertexArray(fx_empty_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fx_scene_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, fx_cw, fx_ch, v.x, v.y, v.x + v.w, v.y + v.h, GL_COLOR_BUFFER_BIT,
                          v.w == fx_cw && v.h == fx_ch ? GL_NEAREST : GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

/* Start the next frame: the shapes, lights, turning and effect were for this one. */
static void fx_new_frame(void) {
    for (int i = 0; i < fx_batch_count; i++) fx_batches[i].n = 0;
    fx_flat_n = fx_run_n = 0;
    fx_light_count = 0;
    fx_g_turn(0, 0, 0);
    fx_effect_id = -1;
    fx_dirty = false;
}

static void fx_draw_and_present(bool headset) {
    fx_render_frame(headset);
    fx_shown_effect = fx_effect_id;
    memcpy(fx_shown_vals, fx_effect_vals, sizeof fx_shown_vals);
    fx_shown_time = fx_seconds();
    fx_present();
}

/* ----------------------------------------------------------- window */

static void fx_gl_failed(int line, const char *why) {
    fx_fail(line, "3D needs a graphics card with OpenGL 3.3, which nearly every computer from the last 15 years has.\n"
                  "  Update your graphics driver (from Intel, AMD or NVIDIA's website, or Windows Update), then try again.",
            "Flaux couldn't start 3D graphics on this computer: %s", why);
}

static GLuint fx_framebuffer(GLuint color_tex, GLuint color_rb, GLuint depth_rb, GLuint depth_tex) {
    GLuint f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    if (color_tex) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
    if (color_rb) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb);
    if (depth_rb) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
    if (depth_tex) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex, 0);
    if (!color_tex && !color_rb) { glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE); }
    return f;
}

/* OpenGL 3.3 or newer? */
static bool fx_gl_good_enough(void) {
    fxgl_GetString_t get = (fxgl_GetString_t)SDL_GL_GetProcAddress("glGetString");
    const char *v = get ? (const char *)get(GL_VERSION) : NULL;
    int major = 0, minor = 0;
    if (!v || sscanf(v, "%d.%d", &major, &minor) != 2) return false;
    return major > 3 || (major == 3 && minor >= 3);
}

/* No good OpenGL on this computer (an old or virtual graphics card, or a
   broken driver)? On Windows, Flaux comes with Mesa: OpenGL done by the
   processor instead. Slower, but every 3D program still works. Setting
   FLAUX_SOFTWARE_GL=1 uses it on purpose. */
#ifndef FX_SOFTWARE_GL
#define FX_SOFTWARE_GL ""
#endif
static bool fx_software_gl(int64_t w, int64_t h, fx_text title, int line) {
#ifdef _WIN32
    /* where Mesa is: FLAUX_SOFTWARE_GL_DIR, Flaux's own copy, or (a game someone shared,
       exported from Flaux Studio) a "mesa" folder next to the program */
    char here[4096] = "", test[4200];
    char *base = SDL_GetBasePath();
    if (base) { snprintf(here, sizeof here, "%smesa", base); SDL_free(base); }
    const char *dirs[3] = { getenv("FLAUX_SOFTWARE_GL_DIR"), FX_SOFTWARE_GL, here }, *dir = NULL;
    for (int i = 0; i < 3 && !dir; i++) {
        if (!dirs[i] || !dirs[i][0]) continue;
        snprintf(test, sizeof test, "%s/opengl32.dll", dirs[i]);
        SDL_RWops *f = SDL_RWFromFile(test, "rb");
        if (f) { SDL_RWclose(f); dir = dirs[i]; }
    }
    if (!dir) return false;
    if (fx_gl) SDL_GL_DeleteContext(fx_gl);
    fx_gl = NULL;
    if (fx_win) SDL_DestroyWindow(fx_win);
    fx_win = NULL;
    SDL_GL_UnloadLibrary();
    if (!getenv("GALLIUM_DRIVER")) {
        putenv("GALLIUM_DRIVER=llvmpipe");
        SDL_setenv("GALLIUM_DRIVER", "llvmpipe", 1);
    }
    char path[4096];
    snprintf(path, sizeof path, "%s/libglapi.dll", dir);   /* (older Mesa) */
    SDL_LoadObject(path);
    snprintf(path, sizeof path, "%s/libgallium_wgl.dll", dir);
    SDL_LoadObject(path);
    snprintf(path, sizeof path, "%s/opengl32.dll", dir);
    if (SDL_GL_LoadLibrary(path) != 0) return false;
    fx_make_window(w, h, title, SDL_WINDOW_OPENGL, line);
    fx_gl = SDL_GL_CreateContext(fx_win);
    fx_soft_gl = true;
    return fx_gl != NULL && fx_gl_good_enough();
#else
    (void)w; (void)h; (void)title; (void)line;
    return false;
#endif
}

static void fx_g_window(int64_t w, int64_t h, fx_text title, int line) {
    fx_start_window(w, h, line);
    for (int i = 0; i < 256; i++) fx_lin_table[i] = powf(i / 255.0f, 2.2f);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    const char *force = getenv("FLAUX_SOFTWARE_GL");
    bool forced = force && *force && strcmp(force, "0") != 0;
    if (!forced) {
        fx_make_window(w, h, title, SDL_WINDOW_OPENGL, line);
        fx_gl = SDL_GL_CreateContext(fx_win);
    }
    if (forced || !fx_gl || !fx_gl_good_enough()) {
        char why[512];
        snprintf(why, sizeof why, "%s", fx_gl ? "its OpenGL is older than 3.3" : SDL_GetError());
        if (!fx_software_gl(w, h, title, line)) fx_gl_failed(line, why);
    }
#define FX_GL_LOAD(ret, name, args) \
    gl##name = (fxgl_##name##_t)SDL_GL_GetProcAddress("gl" #name); \
    if (!gl##name) fx_gl_failed(line, "its OpenGL is missing gl" #name);
    FX_GL_FUNCTIONS(FX_GL_LOAD)
#undef FX_GL_LOAD
    /* wait for the screen between frames (smooth, no tearing), if it runs at 60 */
    SDL_DisplayMode mode;
    bool sixty = SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate >= 59 && mode.refresh_rate <= 61;
    SDL_GL_SetSwapInterval(sixty && !fx_fast ? 1 : 0);

    glGenBuffers(1, &fx_inst_vbo);
    glGenVertexArrays(1, &fx_empty_vao);
    fx_make_shapes();

    /* 2D drawing */
    glGenVertexArrays(1, &fx_flat_vao);
    glBindVertexArray(fx_flat_vao);
    glGenBuffers(1, &fx_flat_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, fx_flat_vbo);
    GLsizei st = sizeof(fx_flat_vertex_t);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, st, (void *)offsetof(fx_flat_vertex_t, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, st, (void *)offsetof(fx_flat_vertex_t, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, 1, st, (void *)offsetof(fx_flat_vertex_t, rgba));
    glBindVertexArray(0);
    static const unsigned char white[4] = { 255, 255, 255, 255 };
    fx_white_tex = fx_upload(white, 1, 1, false);
    unsigned char *font = fx_alloc(570 * 8 * 4);
    memset(font, 0, 570 * 8 * 4);
    for (int c = 0; c < 95; c++)
        for (int x = 0; x < 5; x++)
            for (int y = 0; y < 7; y++)
                if (fx_font[c][x] >> y & 1) memset(&font[(y * 570 + c * 6 + x) * 4], 255, 4);
    fx_font_gl = fx_upload(font, 570, 8, false);
    free(font);

    /* how pictures are read: crisp for 2D, smooth (and sharp at an angle) for 3D */
    glGenSamplers(1, &fx_samp_pixel);
    glSamplerParameteri(fx_samp_pixel, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(fx_samp_pixel, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(fx_samp_pixel, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(fx_samp_pixel, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenSamplers(1, &fx_samp_smooth);
    glSamplerParameteri(fx_samp_smooth, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glSamplerParameteri(fx_samp_smooth, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(fx_samp_smooth, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(fx_samp_smooth, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenSamplers(1, &fx_samp_blocky);
    glSamplerParameteri(fx_samp_blocky, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glSamplerParameteri(fx_samp_blocky, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(fx_samp_blocky, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glSamplerParameteri(fx_samp_blocky, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLfloat aniso = 0;
    while (glGetError() != 0) {}
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &aniso);
    /* sharp pictures on floors seen at an angle (not for pixel art: some
       graphics cards would make it blurry) */
    if (glGetError() == 0 && aniso > 1) glSamplerParameterf(fx_samp_smooth, GL_TEXTURE_MAX_ANISOTROPY, aniso < 8 ? aniso : 8);
    glGenSamplers(1, &fx_samp_shadow);
    glSamplerParameteri(fx_samp_shadow, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(fx_samp_shadow, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(fx_samp_shadow, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(fx_samp_shadow, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(fx_samp_shadow, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri(fx_samp_shadow, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenSamplers(1, &fx_samp_scene);
    glSamplerParameteri(fx_samp_scene, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(fx_samp_scene, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(fx_samp_scene, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(fx_samp_scene, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* the pictures frames are drawn into */
    GLint max_samples = 0, max_rb = 0, max_tex = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    fx_samples = max_samples >= 4 ? 4 : max_samples;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_rb);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    if (max_rb > 0 && max_tex > 0) fx_max_size = max_rb < max_tex ? max_rb : max_tex;
    fx_make_targets(fx_win_w, fx_win_h, line);
    glGenTextures(1, &fx_shadow_tex);
    glBindTexture(GL_TEXTURE_2D, fx_shadow_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, FX_SHADOW_SIZE, FX_SHADOW_SIZE, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    fx_shadow_fbo = fx_framebuffer(0, 0, 0, fx_shadow_tex);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    fx_scene_prog = fx_scene_program(NULL, "3D");
    fx_shadow_prog = fx_program(fx_glsl_shadow_vs, fx_glsl_shadow_fs, NULL, "shadow");
    fx_sky_prog = fx_program(fx_glsl_fullscreen_vs, fx_glsl_sky_fs, NULL, "sky");
    fx_flat_prog = fx_program(fx_glsl_flat_vs, fx_glsl_flat_fs, NULL, "2D");
    glUseProgram(fx_flat_prog);
    glUniform1i(glGetUniformLocation(fx_flat_prog, "u_tex"), 0);

#ifdef FX_USES_XR
    fx_xr_start();
#endif
    fx_draw_and_present(false);   /* a black window to start with (sharp screens: more pixels) */
    SDL_GL_SwapWindow(fx_win);
    if (getenv("FLAUX_FULLSCREEN") && *getenv("FLAUX_FULLSCREEN")) fx_set_fullscreen(true); /* for tests */
    SDL_PumpEvents();
    fx_read_input();
    fx_next_frame = SDL_GetPerformanceCounter();
}

/* Read w by h pixels at (x, y) of a framebuffer as an out_w by out_h picture (each pixel the
   colour at the middle of its part, when they're not the same size). NULL if there's no memory. */
static SDL_Surface *fx_read_picture_gl(GLuint fbo, int x, int y, int w, int h, int out_w, int out_h) {
    unsigned char *px = fx_alloc((size_t)w * (size_t)h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, out_w, out_h, 32, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        for (int oy = 0; oy < out_h; oy++) {   /* (OpenGL counts rows from the bottom) */
            int from_y = out_h == h ? oy : (int)((oy + 0.5) * h / out_h);
            const uint8_t *src = px + (size_t)(h - 1 - (from_y < h ? from_y : h - 1)) * (size_t)w * 4;
            uint8_t *dst = (uint8_t *)s->pixels + (size_t)oy * (size_t)s->pitch;
            if (out_w == w) { memcpy(dst, src, (size_t)w * 4); continue; }
            for (int ox = 0; ox < out_w; ox++) {
                int from_x = (int)((ox + 0.5) * w / out_w);
                memcpy(dst + ox * 4, src + (from_x < w ? from_x : w - 1) * 4, 4);
            }
        }
    }
    free(px);
    return s;
}

static void fx_save_surface(SDL_Surface *s, const char *path, int line) {
    if (!s || SDL_SaveBMP(s, path) != 0)
        fx_fail(line, "Check the file name, and that you're allowed to save files in that folder.",
                "Flaux couldn't save the screenshot: %s", SDL_GetError());
    SDL_FreeSurface(s);
}

/* Save what's in a framebuffer as a .bmp picture. */
static void fx_save_picture(GLuint fbo, int w, int h, const char *path, int line) {
    fx_save_surface(fx_read_picture_gl(fbo, 0, 0, w, h, w, h), path, line);
}

/* Save what's in the window (without the black bars) as a .bmp picture: in the program's own
   pixels, or at the size it's shown at if `full`. */
static void fx_save_shown(const char *path, bool full, int line) {
    SDL_Rect v = fx_shown_rect();
    fx_save_surface(fx_read_picture_gl(0, v.x, v.y, v.w, v.h, full ? v.w : fx_win_w, full ? v.h : fx_win_h),
                    path, line);
}

/* For tests: the screenshots asked for with FLAUX_SCREENSHOT and FLAUX_SCREENSHOT_FULL. */
static void fx_test_shots(int line) {
    if (fx_shot_path) fx_save_shown(fx_shot_path, false, line);
    const char *full = getenv("FLAUX_SCREENSHOT_FULL");
    if (full && *full) fx_save_shown(full, true, line);
}

/* screenshot("file.bmp"): what's been drawn so far this frame. */
static void fx_g_screenshot(fx_text path, int line) {
    fx_need_window(line);
    fx_draw_and_present(false);
    fx_save_shown(fx_text_make(path.p, path.n).p, false, line);
}

static void fx_redisplay(void) {
    if (!fx_gl) return;
    fx_present();
    SDL_GL_SwapWindow(fx_win);
}
static void fx_targets_reset(void) {}

static void fx_set_fullscreen(bool on) {
    if (!fx_win || on == fx_full) return;
    int sw = 0, sh = 0;
    const char *screen = getenv("FLAUX_SCREEN"); /* for tests: a pretend screen of that size */
    if (screen && sscanf(screen, "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0)
        SDL_SetWindowSize(fx_win, on ? sw : fx_win_w, on ? sh : fx_win_h);
    else if (SDL_SetWindowFullscreen(fx_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0)
        return;
    fx_full = on;
    if (!on) SDL_SetWindowSize(fx_win, fx_win_w, fx_win_h);
    fx_redisplay();       /* (this also draws at the screen's resolution from now on) */
    SDL_PumpEvents();
    fx_read_mouse();      /* the same spot on the screen is somewhere else in the picture now */
}

/* Show what was drawn, then wait so the game runs at 60 frames a second. */
static void fx_g_show(int line) {
    fx_need_window(line);
    fx_draw_and_present(true);
    fx_frames++;
    bool last = fx_frame_limit >= 0 && fx_frames >= fx_frame_limit;
    if (last) fx_test_shots(line);
    SDL_GL_SwapWindow(fx_win);
    fx_new_frame();
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
    if (fx_dirty) fx_draw_and_present(true);
    else fx_present();
    if (fx_frame_limit >= 0) { /* test mode */
        fx_test_shots(0);
        fx_g_close();
        return;
    }
    SDL_GL_SwapWindow(fx_win);
    SDL_Event ev;
    while (SDL_WaitEvent(&ev))
        if (fx_handle_event(&ev)) break;
    fx_g_close();
}

/* ======================== end of the 3D runtime ============================ */
