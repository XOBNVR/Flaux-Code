/* Flaux VR: the world renderer.  A starry sky, a baseplate floating in space, the shapes you
 * build (lit, with shadows on the plate), and glowing ribbons for lasers and outlines. */
#include "fx.h"

int g_encode = 1;
const char *const SHAPE_NAMES[SH_COUNT] = {"Cube", "Ball", "Cylinder", "Cone", "Ramp", "Ring"};

#define GLSL_HEAD "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#define GLSL_OUT \
    "uniform int u_enc;\n" \
    "vec4 fx_out(vec3 lin, float a) { return vec4(u_enc == 1 ? pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2)) : lin, a); }\n" \
    "vec4 fx_disp(vec3 c, float a) { return vec4(u_enc == 1 ? c : pow(max(c, vec3(0.0)), vec3(2.2)), a); }\n"

/* ---------- programs ---------- */

static GLuint compile(GLenum type, const char *src, const char *name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, NULL, log); plat_log("shader %s: %s", name, log); }
    return s;
}

GLuint gfx_program(const char *vs, const char *fs, const char *name) {
    GLuint p = glCreateProgram();
    GLuint a = compile(GL_VERTEX_SHADER, vs, name), b = compile(GL_FRAGMENT_SHADER, fs, name);
    glAttachShader(p, a); glAttachShader(p, b);
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, sizeof log, NULL, log); plat_log("link %s: %s", name, log); }
    glDeleteShader(a); glDeleteShader(b);
    return p;
}

static const char *VS_FULL = GLSL_HEAD
    "out vec2 v_ndc;\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;\n"
    "  v_ndc = p; gl_Position = vec4(p, 1.0, 1.0);\n"
    "}\n";

static const char *FS_NEBULA = GLSL_HEAD
    "in vec2 v_ndc; out vec4 o;\n"
    "uniform mat3 u_face;\n"
    "float h13(vec3 p) { p = fract(p * 0.1031); p += dot(p, p.zyx + 31.32); return fract((p.x + p.y) * p.z); }\n"
    "float vn(vec3 p) {\n"
    "  vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(mix(h13(i), h13(i + vec3(1,0,0)), f.x), mix(h13(i + vec3(0,1,0)), h13(i + vec3(1,1,0)), f.x), f.y),\n"
    "             mix(mix(h13(i + vec3(0,0,1)), h13(i + vec3(1,0,1)), f.x), mix(h13(i + vec3(0,1,1)), h13(i + vec3(1,1,1)), f.x), f.y), f.z);\n"
    "}\n"
    "float fbm(vec3 p) { float a = 0.5, s = 0.0; for (int i = 0; i < 5; i++) { s += a * vn(p); p = p * 2.03 + vec3(17.1, 3.7, 9.2); a *= 0.5; } return s; }\n"
    "void main() {\n"
    "  vec3 d = normalize(u_face * vec3(v_ndc, 1.0));\n"
    "  float b = dot(d, normalize(vec3(0.35, 0.8, 0.5)));\n"
    "  float band = exp(-b * b * 9.0);\n"                       /* the milky way */
    "  float n1 = fbm(d * 3.0 + vec3(4.0)), n2 = fbm(d * 6.0 + vec3(11.0)), n3 = fbm(d * 2.0 - vec3(7.0));\n"
    "  float cloud = smoothstep(0.42, 0.9, n1 * 0.7 + n2 * 0.5);\n"
    "  vec3 tint = mix(vec3(0.42, 0.20, 0.85), vec3(0.08, 0.50, 0.75), smoothstep(0.38, 0.62, n3));\n"
    "  tint = mix(tint, vec3(0.85, 0.25, 0.55), smoothstep(0.52, 0.75, n2) * 0.6);\n"
    "  vec3 c = vec3(0.010, 0.012, 0.034);\n"
    "  c += tint * cloud * (0.07 + 0.42 * band);\n"
    "  c += vec3(0.5, 0.55, 0.8) * band * 0.05;\n"
    "  c *= 1.0 - 0.55 * band * smoothstep(0.48, 0.7, fbm(d * 9.0 + vec3(2.0)));\n"   /* dust lanes */
    "  vec3 p = d * 230.0; vec3 ip = floor(p); float r = h13(ip);\n"                  /* faint star dust */
    "  vec3 off = vec3(h13(ip + 1.3), h13(ip + 2.7), h13(ip + 4.1));\n"
    "  float dist = length(fract(p) - 0.2 - off * 0.6);\n"
    "  float s = step(0.94 - band * 0.12, r) * smoothstep(0.25, 0.0, dist);\n"
    "  c += vec3(0.8, 0.85, 1.0) * s * (0.2 + 0.5 * h13(ip + 9.0));\n"
    "  o = vec4(c, 1.0);\n"
    "}\n";

static const char *FS_SKY = GLSL_HEAD GLSL_OUT
    "in vec2 v_ndc; out vec4 o;\n"
    "uniform samplerCube u_cube; uniform mat3 u_rot; uniform vec4 u_tan; uniform float u_bright;\n"
    "void main() {\n"
    "  vec2 t = v_ndc * 0.5 + 0.5;\n"
    "  vec3 d = normalize(u_rot * vec3(mix(u_tan.x, u_tan.y, t.x), mix(u_tan.w, u_tan.z, t.y), -1.0));\n"
    "  vec3 c = texture(u_cube, d).rgb * u_bright;\n"
    "  c += pow(1.0 - abs(d.y), 8.0) * vec3(0.07, 0.05, 0.17);\n"       /* a glow along the horizon */
    "  c *= mix(0.6, 1.0, smoothstep(-0.5, 0.0, d.y));\n"
    "  o = fx_disp(c, 1.0);\n"
    "}\n";

static const char *VS_STARS = GLSL_HEAD
    "layout(location = 0) in vec4 a_dir;\n"      /* direction, and size in radians */
    "layout(location = 1) in vec4 a_col;\n"      /* colour, and a random number for the twinkle */
    "uniform mat4 u_vp; uniform float u_px, u_time, u_bright;\n"
    "out vec4 v_col; out float v_big;\n"
    "void main() {\n"
    "  vec4 p = u_vp * vec4(a_dir.xyz, 0.0);\n"
    "  gl_Position = p.xyww;\n"
    "  float px = a_dir.w * u_px;\n"
    "  float tw = 0.78 + 0.22 * sin(u_time * (1.2 + a_col.a * 3.0) + a_col.a * 40.0);\n"
    "  gl_PointSize = max(px * 4.0, 3.0);\n"
    "  v_big = smoothstep(3.0, 5.5, px);\n"
    "  v_col = vec4(a_col.rgb * tw * u_bright * min(1.0, px * px / 3.0), 1.0);\n"
    "}\n";

static const char *FS_STARS = GLSL_HEAD GLSL_OUT
    "in vec4 v_col; in float v_big; out vec4 o;\n"
    "void main() {\n"
    "  vec2 q = gl_PointCoord - 0.5;\n"
    "  float d = length(q) * 2.0;\n"
    "  float i = exp(-d * d * 22.0) + 0.10 * exp(-d * 5.0);\n"
    "  i += v_big * 0.5 * (exp(-abs(q.x) * 70.0) * exp(-abs(q.y) * 9.0) + exp(-abs(q.y) * 70.0) * exp(-abs(q.x) * 9.0));\n"
    "  i *= 1.0 - smoothstep(0.8, 1.0, d);\n"
    "  o = fx_disp(v_col.rgb * i, 1.0);\n"
    "}\n";

static const char *VS_MESH = GLSL_HEAD
    "layout(location = 0) in vec3 a_pos; layout(location = 1) in vec3 a_nrm;\n"
    "uniform mat4 u_vp, u_model; uniform mat3 u_nrm;\n"
    "out vec3 v_w, v_n, v_l;\n"
    "void main() {\n"
    "  vec4 w = u_model * vec4(a_pos, 1.0);\n"
    "  v_w = w.xyz; v_n = u_nrm * a_nrm; v_l = a_pos;\n"
    "  gl_Position = u_vp * w;\n"
    "}\n";

static const char *FS_SHAPE = GLSL_HEAD GLSL_OUT
    "in vec3 v_w, v_n, v_l; out vec4 o;\n"
    "uniform vec3 u_color, u_cam; uniform float u_alpha, u_glow, u_time;\n"
    "void main() {\n"
    "  vec3 n = normalize(v_n), v = normalize(u_cam - v_w);\n"
    "  if (!gl_FrontFacing) n = -n;\n"
    "  vec3 base = pow(u_color, vec3(2.2));\n"
    "  vec3 l1 = normalize(vec3(0.45, 0.80, 0.35)), l2 = normalize(vec3(-0.6, 0.25, -0.5));\n"
    "  vec3 amb = mix(vec3(0.10, 0.09, 0.20), vec3(0.30, 0.38, 0.60), n.y * 0.5 + 0.5);\n"
    "  float d1 = max(dot(n, l1), 0.0), d2 = max(dot(n, l2), 0.0);\n"
    "  vec3 c = base * (amb + vec3(1.0, 0.96, 0.90) * d1 * 0.95 + vec3(0.45, 0.35, 0.90) * d2 * 0.40);\n"
    "  c += vec3(1.0) * pow(max(dot(n, normalize(l1 + v)), 0.0), 48.0) * 0.22 * step(0.001, d1);\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);\n"
    "  c += vec3(0.25, 0.45, 0.90) * rim * 0.22;\n"
    "  c += vec3(0.20, 0.75, 1.00) * u_glow * (0.18 + 0.7 * rim + 0.08 * sin(u_time * 5.0));\n"
    "  float dist = length(u_cam - v_w);\n"
    "  c = mix(c, vec3(0.010, 0.012, 0.035), smoothstep(40.0, 120.0, dist));\n"
    "  o = fx_out(c, u_alpha);\n"
    "}\n";

static const char *VS_MODEL = GLSL_HEAD
    "layout(location = 0) in vec3 a_pos; layout(location = 1) in vec3 a_nrm; layout(location = 2) in vec2 a_uv;\n"
    "uniform mat4 u_vp, u_model; uniform mat3 u_nrm;\n"
    "out vec3 v_w, v_n; out vec2 v_uv;\n"
    "void main() {\n"
    "  vec4 w = u_model * vec4(a_pos, 1.0);\n"
    "  v_w = w.xyz; v_n = u_nrm * a_nrm; v_uv = a_uv;\n"
    "  gl_Position = u_vp * w;\n"
    "}\n";

static const char *FS_MODEL = GLSL_HEAD GLSL_OUT
    "in vec3 v_w, v_n; in vec2 v_uv; out vec4 o;\n"
    "uniform sampler2D u_tex; uniform vec3 u_cam, u_tint; uniform float u_glow;\n"
    "void main() {\n"
    "  vec3 n = normalize(v_n), v = normalize(u_cam - v_w);\n"
    "  if (!gl_FrontFacing) n = -n;\n"
    "  vec3 base = texture(u_tex, v_uv).rgb;\n"                 /* an sRGB picture: already linear here */
    "  vec3 l1 = normalize(vec3(0.45, 0.80, 0.35)), l2 = normalize(vec3(-0.6, 0.25, -0.5));\n"
    "  vec3 amb = mix(vec3(0.16, 0.15, 0.26), vec3(0.42, 0.48, 0.66), n.y * 0.5 + 0.5);\n"
    "  vec3 c = base * (amb + vec3(1.0, 0.96, 0.90) * max(dot(n, l1), 0.0) * 0.9 + vec3(0.45, 0.35, 0.90) * max(dot(n, l2), 0.0) * 0.4);\n"
    "  c += vec3(1.0) * pow(max(dot(n, normalize(l1 + v)), 0.0), 40.0) * 0.18;\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);\n"
    "  c += u_tint * rim * (0.10 + 0.5 * u_glow);\n"
    "  o = fx_out(c, 1.0);\n"
    "}\n";

static const char *FS_PLATE = GLSL_HEAD GLSL_OUT
    "in vec3 v_w, v_n, v_l; out vec4 o;\n"
    "uniform vec3 u_cam; uniform float u_half, u_time; uniform int u_grid;\n"
    "float lines(vec2 c, float fat) { vec2 g = abs(fract(c - 0.5) - 0.5) / (fwidth(c) * fat); return 1.0 - min(min(g.x, g.y), 1.0); }\n"
    "void main() {\n"
    "  vec3 n = normalize(v_n);\n"
    "  float dist = length(u_cam - v_w);\n"
    "  float e = u_half - max(abs(v_w.x), abs(v_w.z));\n"        /* how far in from the edge */
    "  vec3 c;\n"
    "  if (n.y > 0.5) {\n"
    "    c = vec3(0.0065, 0.008, 0.021) + vec3(0.005, 0.006, 0.016) * exp(-dot(v_w.xz, v_w.xz) * 0.004);\n"
    "    if (u_grid == 1) {\n"
    "      c += vec3(0.10, 0.14, 0.42) * lines(v_w.xz, 1.0) * (1.0 - smoothstep(6.0, 24.0, dist)) * 0.55;\n"
    "      c += vec3(0.12, 0.45, 0.75) * lines(v_w.xz / 5.0, 1.6) * (1.0 - smoothstep(25.0, 70.0, dist)) * 0.60;\n"
    "    }\n"
    "    vec3 v = normalize(u_cam - v_w);\n"
    "    c += vec3(0.5, 0.6, 1.0) * pow(1.0 - v.y, 5.0) * 0.05;\n"   /* a faint sheen at low angles */
    "    c += vec3(0.15, 0.75, 1.0) * exp(-e * 3.5) * 0.9 + vec3(0.5, 0.3, 1.0) * exp(-e * 0.5) * 0.08;\n"
    "  } else if (n.y < -0.5) {\n"
    "    c = vec3(0.004, 0.005, 0.012);\n"
    "  } else {\n"
    "    float t = v_l.y + 0.5;\n"
    "    c = mix(vec3(0.008, 0.010, 0.028), vec3(0.15, 0.65, 0.95), pow(t, 4.0));\n"
    "  }\n"
    "  o = fx_out(c, 1.0);\n"
    "}\n";

static const char *VS_SHADOW = GLSL_HEAD
    "layout(location = 0) in vec3 a_pos;\n"
    "uniform mat4 u_vp, u_model; uniform vec3 u_light;\n"
    "out vec3 v_w;\n"
    "void main() {\n"
    "  vec4 w = u_model * vec4(a_pos, 1.0);\n"
    "  float h = max(w.y, 0.0);\n"
    "  w.xz -= u_light.xz / u_light.y * h; w.y = 0.004;\n"       /* slide it down the light onto the plate */
    "  v_w = vec3(w.x, h, w.z);\n"
    "  gl_Position = u_vp * w;\n"
    "}\n";

static const char *FS_SHADOW = GLSL_HEAD
    "in vec3 v_w; out vec4 o; uniform float u_half;\n"
    "void main() {\n"
    "  if (max(abs(v_w.x), abs(v_w.z)) > u_half) discard;\n"
    "  o = vec4(0.0, 0.0, 0.0, 0.45);\n"
    "}\n";

static const char *VS_RIBBON = GLSL_HEAD
    "layout(location = 0) in vec4 a_pos; layout(location = 1) in vec4 a_col;\n"
    "uniform mat4 u_vp; out vec4 v_col; out float v_side;\n"
    "void main() { v_col = a_col; v_side = a_pos.w; gl_Position = u_vp * vec4(a_pos.xyz, 1.0); }\n";

static const char *FS_RIBBON = GLSL_HEAD GLSL_OUT
    "in vec4 v_col; in float v_side; out vec4 o;\n"
    "void main() {\n"
    "  float a = pow(max(1.0 - v_side * v_side, 0.0), 1.5);\n"
    "  o = fx_disp(v_col.rgb * v_col.a * a, 1.0);\n"
    "}\n";

/* ---------- meshes ---------- */

typedef struct { GLuint vao; int count; } Mesh;
static Mesh mesh[SH_COUNT];
static float *mb; static int mb_n, mb_cap;

static void mv(V3 p, V3 n) {
    if (mb_n + 6 > mb_cap) { mb_cap = mb_cap ? mb_cap * 2 : 4096; mb = realloc(mb, sizeof(float) * (size_t)mb_cap); }
    float *f = mb + mb_n; mb_n += 6;
    f[0] = p.x; f[1] = p.y; f[2] = p.z; f[3] = n.x; f[4] = n.y; f[5] = n.z;
}
/* one triangle; turned so its front faces the way its normals point */
static void tri(V3 a, V3 b, V3 c, V3 na, V3 nb, V3 nc) {
    V3 face = v3cross(v3sub(b, a), v3sub(c, a));
    if (v3dot(face, v3add(na, v3add(nb, nc))) < 0) { V3 t = b; b = c; c = t; t = nb; nb = nc; nc = t; }
    mv(a, na); mv(b, nb); mv(c, nc);
}
static void quad_flat(V3 a, V3 b, V3 c, V3 d) {
    V3 n = v3norm(v3cross(v3sub(b, a), v3sub(c, a)));
    V3 mid = v3mul(v3add(v3add(a, b), v3add(c, d)), 0.25f);
    if (v3dot(n, mid) < 0) n = v3mul(n, -1);      /* outwards from the middle of the shape */
    tri(a, b, c, n, n, n); tri(a, c, d, n, n, n);
}
static void mesh_done(int shape) {
    GLuint vbo;
    glGenVertexArrays(1, &mesh[shape].vao); glBindVertexArray(mesh[shape].vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(float) * (size_t)mb_n), mb, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);
    glBindVertexArray(0);
    mesh[shape].count = mb_n / 6;
    mb_n = 0;
}

static void build_meshes(void) {
    const float h = 0.5f;
    V3 c[8];
    for (int i = 0; i < 8; i++) c[i] = v3((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
    quad_flat(c[0], c[1], c[3], c[2]); quad_flat(c[4], c[5], c[7], c[6]);
    quad_flat(c[0], c[1], c[5], c[4]); quad_flat(c[2], c[3], c[7], c[6]);
    quad_flat(c[0], c[2], c[6], c[4]); quad_flat(c[1], c[3], c[7], c[5]);
    mesh_done(SH_CUBE);

    const int NS = 40, NR = 20;
    for (int i = 0; i < NS; i++)
        for (int j = 0; j < NR; j++) {
            V3 p[4];
            for (int k = 0; k < 4; k++) {
                float u = (float)(i + (k == 1 || k == 2)) / NS * 2 * FX_PI, v = (float)(j + (k >= 2)) / NR * FX_PI;
                p[k] = v3(sinf(v) * cosf(u), cosf(v), sinf(v) * sinf(u));
            }
            if (j > 0) tri(v3mul(p[0], h), v3mul(p[1], h), v3mul(p[2], h), p[0], p[1], p[2]);
            if (j < NR - 1) tri(v3mul(p[0], h), v3mul(p[2], h), v3mul(p[3], h), p[0], p[2], p[3]);
        }
    mesh_done(SH_SPHERE);

    for (int i = 0; i < NS; i++) {
        float a0 = (float)i / NS * 2 * FX_PI, a1 = (float)(i + 1) / NS * 2 * FX_PI;
        V3 n0 = v3(cosf(a0), 0, sinf(a0)), n1 = v3(cosf(a1), 0, sinf(a1));
        V3 b0 = v3(n0.x * h, -h, n0.z * h), b1 = v3(n1.x * h, -h, n1.z * h);
        V3 t0 = v3(n0.x * h, h, n0.z * h), t1 = v3(n1.x * h, h, n1.z * h);
        tri(b0, b1, t1, n0, n1, n1); tri(b0, t1, t0, n0, n1, n0);
        tri(v3(0, h, 0), t0, t1, v3(0, 1, 0), v3(0, 1, 0), v3(0, 1, 0));
        tri(v3(0, -h, 0), b0, b1, v3(0, -1, 0), v3(0, -1, 0), v3(0, -1, 0));
    }
    mesh_done(SH_CYLINDER);

    for (int i = 0; i < NS; i++) {
        float a0 = (float)i / NS * 2 * FX_PI, a1 = (float)(i + 1) / NS * 2 * FX_PI, am = (a0 + a1) * 0.5f;
        V3 n0 = v3norm(v3(cosf(a0), 0.5f, sinf(a0))), n1 = v3norm(v3(cosf(a1), 0.5f, sinf(a1)));
        V3 nm = v3norm(v3(cosf(am), 0.5f, sinf(am)));
        V3 b0 = v3(cosf(a0) * h, -h, sinf(a0) * h), b1 = v3(cosf(a1) * h, -h, sinf(a1) * h);
        tri(b0, b1, v3(0, h, 0), n0, n1, nm);
        tri(v3(0, -h, 0), b0, b1, v3(0, -1, 0), v3(0, -1, 0), v3(0, -1, 0));
    }
    mesh_done(SH_CONE);

    {   /* a ramp: flat at the front, rising to the back */
        V3 fl = v3(-h, -h, h), fr = v3(h, -h, h), bl = v3(-h, -h, -h), br = v3(h, -h, -h);
        V3 tl = v3(-h, h, -h), tr = v3(h, h, -h);
        V3 down = v3(0, -1, 0), back = v3(0, 0, -1), slope = v3norm(v3(0, 1, 1));
        tri(fl, fr, br, down, down, down); tri(fl, br, bl, down, down, down);
        tri(bl, br, tr, back, back, back); tri(bl, tr, tl, back, back, back);
        tri(fl, fr, tr, slope, slope, slope); tri(fl, tr, tl, slope, slope, slope);
        tri(fl, bl, tl, v3(-1, 0, 0), v3(-1, 0, 0), v3(-1, 0, 0));
        tri(fr, br, tr, v3(1, 0, 0), v3(1, 0, 0), v3(1, 0, 0));
    }
    mesh_done(SH_WEDGE);

    const float R = 0.36f, r = 0.14f;      /* a ring lying flat: 1 wide, 0.28 tall */
    for (int i = 0; i < NS; i++)
        for (int j = 0; j < NR; j++) {
            V3 p[4], n[4];
            for (int k = 0; k < 4; k++) {
                float u = (float)(i + (k == 1 || k == 2)) / NS * 2 * FX_PI, v = (float)(j + (k >= 2)) / NR * 2 * FX_PI;
                n[k] = v3(cosf(v) * cosf(u), sinf(v), cosf(v) * sinf(u));
                p[k] = v3((R + r * cosf(v)) * cosf(u), r * sinf(v), (R + r * cosf(v)) * sinf(u));
            }
            tri(p[0], p[1], p[2], n[0], n[1], n[2]); tri(p[0], p[2], p[3], n[0], n[2], n[3]);
        }
    mesh_done(SH_TORUS);
}

/* ---------- state ---------- */

static GLuint p_nebula, p_sky, p_stars, p_shape, p_plate, p_shadow, p_ribbon, p_model;
static Cam cur_cam;
static GLuint vao_empty, vao_stars, vao_ribbon, vbo_ribbon, tex_nebula;
static int star_count;
static GLint u_shape_model, u_shape_nrm, u_shape_color, u_shape_alpha, u_shape_glow, u_shadow_model;
static const V3 LIGHT = {0.45f, 0.80f, 0.35f};
static float plate_half = 24.0f;

static unsigned rng_state = 0x1234567u;
static float frand(void) { rng_state = rng_state * 1664525u + 1013904223u; return (float)(rng_state >> 8) / 16777216.0f; }

static void build_stars(void) {
    enum { N = 3600 };
    static float d[N * 8];
    for (int i = 0; i < N; i++) {
        float z = frand() * 2 - 1, a = frand() * 2 * FX_PI, s = sqrtf(1 - z * z);
        float m = frand(); m = m * m * m; m = m * m;            /* most stars are small */
        float t = frand();
        V3 col = t < 0.55f ? v3(1.0f, 1.0f, 1.0f) : t < 0.75f ? v3(0.72f, 0.82f, 1.0f)
               : t < 0.9f ? v3(1.0f, 0.92f, 0.72f) : v3(1.0f, 0.72f, 0.62f);
        float bright = 0.45f + 0.55f * frand();
        float *f = d + i * 8;
        f[0] = s * cosf(a); f[1] = z; f[2] = s * sinf(a); f[3] = 0.0016f + 0.0052f * m;
        f[4] = col.x * bright; f[5] = col.y * bright; f[6] = col.z * bright; f[7] = frand();
    }
    GLuint vbo;
    glGenVertexArrays(1, &vao_stars); glBindVertexArray(vao_stars);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof d, d, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 32, (void *)16);
    glBindVertexArray(0);
    star_count = N;
}

/* paint the nebula once, onto the six faces of a cube */
static void bake_nebula(void) {
    const int S = 512;
    static const float faces[6][9] = {      /* columns: across, up, towards the face */
        {0, 0, -1,  0, -1, 0,   1, 0, 0}, {0, 0, 1,   0, -1, 0,  -1, 0, 0},
        {1, 0, 0,   0, 0, 1,    0, 1, 0}, {1, 0, 0,   0, 0, -1,   0, -1, 0},
        {1, 0, 0,   0, -1, 0,   0, 0, 1}, {-1, 0, 0,  0, -1, 0,   0, 0, -1}};
    GLuint fbo;
    glGenTextures(1, &tex_nebula);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_nebula);
    for (int i = 0; i < 6; i++)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, 0, GL_RGBA8, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, S, S);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glUseProgram(p_nebula);
    glBindVertexArray(vao_empty);
    for (int i = 0; i < 6; i++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)i, tex_nebula, 0);
        glUniformMatrix3fv(glGetUniformLocation(p_nebula, "u_face"), 1, GL_FALSE, faces[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
}

void gfx_init(void) {
    p_nebula = gfx_program(VS_FULL, FS_NEBULA, "nebula");
    p_sky = gfx_program(VS_FULL, FS_SKY, "sky");
    p_stars = gfx_program(VS_STARS, FS_STARS, "stars");
    p_shape = gfx_program(VS_MESH, FS_SHAPE, "shape");
    p_plate = gfx_program(VS_MESH, FS_PLATE, "plate");
    p_shadow = gfx_program(VS_SHADOW, FS_SHADOW, "shadow");
    p_ribbon = gfx_program(VS_RIBBON, FS_RIBBON, "ribbon");
    p_model = gfx_program(VS_MODEL, FS_MODEL, "model");
    u_shape_model = glGetUniformLocation(p_shape, "u_model"); u_shape_nrm = glGetUniformLocation(p_shape, "u_nrm");
    u_shape_color = glGetUniformLocation(p_shape, "u_color"); u_shape_alpha = glGetUniformLocation(p_shape, "u_alpha");
    u_shape_glow = glGetUniformLocation(p_shape, "u_glow"); u_shadow_model = glGetUniformLocation(p_shadow, "u_model");
    glGenVertexArrays(1, &vao_empty);
    build_meshes();
    build_stars();
    bake_nebula();
    glGenVertexArrays(1, &vao_ribbon); glBindVertexArray(vao_ribbon);
    glGenBuffers(1, &vbo_ribbon); glBindBuffer(GL_ARRAY_BUFFER, vbo_ribbon);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 32, (void *)16);
    glBindVertexArray(0);
}

Cam cam_make(Pose pose, float l, float r, float u, float d, int w, int h) {
    Cam c;
    c.pos = pose.p; c.rot = pose.q;
    c.tl = tanf(l); c.tr = tanf(r); c.tu = tanf(u); c.td = tanf(d);
    c.view = m4view(pose);
    c.proj = m4proj(c.tl, c.tr, c.tu, c.td, 0.05f, 400.0f);
    c.vp = m4mul(c.proj, c.view);
    c.w = w; c.h = h;
    return c;
}

/* ---------- render targets ---------- */

void rt_create(RT *rt, int w, int h, int samples) {
    memset(rt, 0, sizeof *rt);
    rt->w = w; rt->h = h;
    glGenTextures(1, &rt->tex);
    glBindTexture(GL_TEXTURE_2D, rt->tex);
    GLenum format = g_encode ? GL_RGBA8 : GL_SRGB8_ALPHA8;      /* match the eye pictures, so shaders work the same way */
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &rt->fbo); glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt->tex, 0);
    GLint max_samples = 1; glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    if (samples > max_samples) samples = max_samples;
    glGenFramebuffers(1, &rt->fbo_ms); glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo_ms);
    glGenRenderbuffers(1, &rt->rb_color); glBindRenderbuffer(GL_RENDERBUFFER, rt->rb_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rt->rb_color);
    glGenRenderbuffers(1, &rt->rb_depth); glBindRenderbuffer(GL_RENDERBUFFER, rt->rb_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rt->rb_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        plat_log("render target %dx%d isn't complete", w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void rt_begin(RT *rt) { glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo_ms); glViewport(0, 0, rt->w, rt->h); }

void rt_end(RT *rt) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, rt->fbo_ms);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rt->fbo);
    glBlitFramebuffer(0, 0, rt->w, rt->h, 0, 0, rt->w, rt->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ---------- drawing ---------- */

static void mat3_of(Q4 q, V3 s, float *out) {      /* rotation times 1/scale: what normals need */
    V3 x = q4rot(q, v3(1.0f / s.x, 0, 0)), y = q4rot(q, v3(0, 1.0f / s.y, 0)), z = q4rot(q, v3(0, 0, 1.0f / s.z));
    out[0] = x.x; out[1] = x.y; out[2] = x.z; out[3] = y.x; out[4] = y.y; out[5] = y.z; out[6] = z.x; out[7] = z.y; out[8] = z.z;
}

void gfx_sky(const Cam *c, float time, float brightness) {
    float rot[9];
    mat3_of(c->rot, v3(1, 1, 1), rot);
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glUseProgram(p_sky);
    glUniform1i(glGetUniformLocation(p_sky, "u_enc"), g_encode);
    glUniformMatrix3fv(glGetUniformLocation(p_sky, "u_rot"), 1, GL_FALSE, rot);
    glUniform4f(glGetUniformLocation(p_sky, "u_tan"), c->tl, c->tr, c->tu, c->td);
    glUniform1f(glGetUniformLocation(p_sky, "u_bright"), brightness);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP, tex_nebula);
    glUniform1i(glGetUniformLocation(p_sky, "u_cube"), 0);
    glBindVertexArray(vao_empty);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    Pose turn = {{0, 0, 0}, c->rot};
    M4 vp = m4mul(c->proj, m4view(turn));
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(p_stars);
    glUniform1i(glGetUniformLocation(p_stars, "u_enc"), g_encode);
    glUniformMatrix4fv(glGetUniformLocation(p_stars, "u_vp"), 1, GL_FALSE, vp.m);
    glUniform1f(glGetUniformLocation(p_stars, "u_px"), (float)c->h / (c->tu - c->td));
    glUniform1f(glGetUniformLocation(p_stars, "u_time"), time);
    glUniform1f(glGetUniformLocation(p_stars, "u_bright"), brightness);
    glBindVertexArray(vao_stars);
    glDrawArrays(GL_POINTS, 0, star_count);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
}

static void set_model(GLuint p, V3 pos, Q4 rot, V3 scale) {
    M4 m = m4trs(pos, rot, scale);
    float n[9]; mat3_of(rot, scale, n);
    glUniformMatrix4fv(glGetUniformLocation(p, "u_model"), 1, GL_FALSE, m.m);
    glUniformMatrix3fv(glGetUniformLocation(p, "u_nrm"), 1, GL_FALSE, n);
}

void gfx_plate(const Cam *c, float size, int grid, float time) {
    plate_half = size * 0.5f;
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    glUseProgram(p_plate);
    glUniform1i(glGetUniformLocation(p_plate, "u_enc"), g_encode);
    glUniformMatrix4fv(glGetUniformLocation(p_plate, "u_vp"), 1, GL_FALSE, c->vp.m);
    glUniform3f(glGetUniformLocation(p_plate, "u_cam"), c->pos.x, c->pos.y, c->pos.z);
    glUniform1f(glGetUniformLocation(p_plate, "u_half"), plate_half);
    glUniform1f(glGetUniformLocation(p_plate, "u_time"), time);
    glUniform1i(glGetUniformLocation(p_plate, "u_grid"), grid);
    set_model(p_plate, v3(0, -0.4f, 0), q4ident(), v3(size, 0.8f, size));
    glBindVertexArray(mesh[SH_CUBE].vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh[SH_CUBE].count);
}

void gfx_shadows_begin(const Cam *c) {
    glUseProgram(p_shadow);
    glUniformMatrix4fv(glGetUniformLocation(p_shadow, "u_vp"), 1, GL_FALSE, c->vp.m);
    glUniform3f(glGetUniformLocation(p_shadow, "u_light"), LIGHT.x, LIGHT.y, LIGHT.z);
    glUniform1f(glGetUniformLocation(p_shadow, "u_half"), plate_half);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glEnable(GL_STENCIL_TEST);              /* each bit of plate is darkened once, however many shadows overlap */
    glStencilFunc(GL_EQUAL, 0, 0xFF); glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
}

void gfx_shadow(int shape, V3 pos, Q4 rot, V3 scale) {
    M4 m = m4trs(pos, rot, scale);
    glUniformMatrix4fv(u_shadow_model, 1, GL_FALSE, m.m);
    glBindVertexArray(mesh[shape].vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh[shape].count);
}

void gfx_shadows_end(void) {
    glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}

void gfx_shapes_begin(const Cam *c, float time) {
    cur_cam = *c;
    glUseProgram(p_shape);
    glUniform1i(glGetUniformLocation(p_shape, "u_enc"), g_encode);
    glUniformMatrix4fv(glGetUniformLocation(p_shape, "u_vp"), 1, GL_FALSE, c->vp.m);
    glUniform3f(glGetUniformLocation(p_shape, "u_cam"), c->pos.x, c->pos.y, c->pos.z);
    glUniform1f(glGetUniformLocation(p_shape, "u_time"), time);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

void gfx_shape(int shape, V3 pos, Q4 rot, V3 scale, V3 color, float alpha, float glow) {
    if (shape < 0 || shape >= SH_COUNT) return;
    M4 m = m4trs(pos, rot, scale);
    float n[9]; mat3_of(rot, scale, n);
    glUniformMatrix4fv(u_shape_model, 1, GL_FALSE, m.m);
    glUniformMatrix3fv(u_shape_nrm, 1, GL_FALSE, n);
    glUniform3f(u_shape_color, color.x, color.y, color.z);
    glUniform1f(u_shape_alpha, alpha);
    glUniform1f(u_shape_glow, glow);
    int ghost = alpha < 0.999f;
    if (ghost) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
    glBindVertexArray(mesh[shape].vao);
    glDrawArrays(GL_TRIANGLES, 0, mesh[shape].count);
    if (ghost) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
}

/* ---------- ribbons: lasers, outlines ---------- */

#define MAX_RIBBONS 1024
typedef struct { V3 a, b; float width; float ca[4], cb[4]; int top; } Ribbon;
static int ribbon_layer;

/* what's queued next goes under the panels (0) or over them (1): your own lasers go over */
void gfx_ribbon_layer(int top) { ribbon_layer = top; }
static Ribbon ribbons[MAX_RIBBONS];
static int ribbon_count;

void gfx_ribbon(V3 a, V3 b, float width, const float ca[4], const float cb[4]) {
    if (ribbon_count >= MAX_RIBBONS) return;
    Ribbon *r = &ribbons[ribbon_count++];
    r->a = a; r->b = b; r->width = width; r->top = ribbon_layer;
    memcpy(r->ca, ca, sizeof r->ca); memcpy(r->cb, cb, sizeof r->cb);
}

void gfx_wire_box(V3 pos, Q4 rot, V3 scale, float width, const float col[4]) {
    V3 c[8];
    for (int i = 0; i < 8; i++)
        c[i] = v3add(pos, q4rot(rot, v3(((i & 1) ? 0.5f : -0.5f) * scale.x, ((i & 2) ? 0.5f : -0.5f) * scale.y,
                                        ((i & 4) ? 0.5f : -0.5f) * scale.z)));
    static const int e[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int i = 0; i < 12; i++) gfx_ribbon(c[e[i][0]], c[e[i][1]], width, col, col);
}

/* draws the ribbons queued so far (the queue is kept: it's the same for both eyes) */
void gfx_ribbons_flush(const Cam *c, int layer) {
    if (!ribbon_count) return;
    static float v[MAX_RIBBONS * 6 * 8];
    int n = 0;
    for (int i = 0; i < ribbon_count; i++) {
        Ribbon *r = &ribbons[i];
        if (layer >= 0 && r->top != layer) continue;
        V3 side = v3norm(v3cross(v3sub(r->b, r->a), v3sub(c->pos, r->a)));
        side = v3mul(side, r->width * 0.5f);
        V3 p[4] = {v3sub(r->a, side), v3add(r->a, side), v3add(r->b, side), v3sub(r->b, side)};
        float s[4] = {-1, 1, 1, -1};
        const float *col[4] = {r->ca, r->ca, r->cb, r->cb};
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++) {
            int j = order[k];
            float *f = v + n * 8; n++;
            f[0] = p[j].x; f[1] = p[j].y; f[2] = p[j].z; f[3] = s[j];
            memcpy(f + 4, col[j], 16);
        }
    }
    if (!n) return;
    glUseProgram(p_ribbon);
    glUniform1i(glGetUniformLocation(p_ribbon, "u_enc"), g_encode);
    glUniformMatrix4fv(glGetUniformLocation(p_ribbon, "u_vp"), 1, GL_FALSE, c->vp.m);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glBindVertexArray(vao_ribbon);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_ribbon);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(float) * 8 * (size_t)n), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, n);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}

void gfx_ribbons_clear(void);
void gfx_ribbons_clear(void) { ribbon_count = 0; ribbon_layer = 0; }

/* ---------- pictures of the shapes, for the Build tab ---------- */

void gfx_thumbnails(GLuint *tex_out) {
    const int S = 224;
    RT rt; rt_create(&rt, S * SH_COUNT, S, 4);
    rt_begin(&rt);
    glClearColor(0, 0, 0, 0); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    V3 eye = v3(1.25f, 1.05f, 1.75f);
    Q4 q = q4mul(q4axis(v3(0, 1, 0), atan2f(eye.x, eye.z)), q4axis(v3(1, 0, 0), -atan2f(eye.y, sqrtf(eye.x * eye.x + eye.z * eye.z))));
    Pose p = {eye, q};
    Cam c = cam_make(p, -0.27f, 0.27f, 0.27f, -0.27f, S, S);
    gfx_shapes_begin(&c, 0);
    for (int i = 0; i < SH_COUNT; i++) {
        glViewport(i * S, 0, S, S);
        gfx_shape(i, v3(0, 0, 0), q4ident(), v3(1, i == SH_TORUS ? 1.0f : 1, 1), v3(0.93f, 0.93f, 0.93f), 1.0f, 0.0f);
    }
    rt_end(&rt);
    *tex_out = rt.tex;
}

/* ---------- models: the controllers ---------- */

typedef struct { GLuint vao, tex; int count; } Model;
static Model models[4];
static int model_count;

int gfx_model_load(const char *asset) {
    size_t size = 0;
    unsigned char *d = plat_asset(asset, &size);
    uint32_t head[5];
    if (!d || size < 20 || memcmp(d, "FXM1", 4) != 0 || model_count >= 4) { free(d); return -1; }
    memcpy(head, d, 20);
    size_t nv = head[1], ni = head[2], tw = head[3], th = head[4];
    if (size < 20 + nv * 32 + ni * 4 + tw * th * 4) { free(d); return -1; }
    Model *m = &models[model_count];
    GLuint vbo, ibo;
    glGenVertexArrays(1, &m->vao); glBindVertexArray(m->vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * 32), d + 20, GL_STATIC_DRAW);
    glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(ni * 4), d + 20 + nv * 32, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);
    glBindVertexArray(0);
    glGenTextures(1, &m->tex); glBindTexture(GL_TEXTURE_2D, m->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)tw, (GLsizei)th, 0, GL_RGBA, GL_UNSIGNED_BYTE, d + 20 + nv * 32 + ni * 4);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m->count = (int)ni;
    free(d);
    return model_count++;
}

/* between gfx_shapes_begin() and the next thing: draws a model, then goes back to drawing shapes */
void gfx_model(int id, V3 pos, Q4 rot, V3 tint, float glow) {
    if (id < 0 || id >= model_count) return;
    glUseProgram(p_model);
    glUniform1i(glGetUniformLocation(p_model, "u_enc"), g_encode);
    glUniformMatrix4fv(glGetUniformLocation(p_model, "u_vp"), 1, GL_FALSE, cur_cam.vp.m);
    glUniform3f(glGetUniformLocation(p_model, "u_cam"), cur_cam.pos.x, cur_cam.pos.y, cur_cam.pos.z);
    glUniform3f(glGetUniformLocation(p_model, "u_tint"), tint.x, tint.y, tint.z);
    glUniform1f(glGetUniformLocation(p_model, "u_glow"), glow);
    glUniform1i(glGetUniformLocation(p_model, "u_tex"), 0);
    set_model(p_model, pos, rot, v3(1, 1, 1));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, models[id].tex);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(models[id].vao);
    glDrawElements(GL_TRIANGLES, models[id].count, GL_UNSIGNED_INT, (void *)0);
    glEnable(GL_CULL_FACE);
    glUseProgram(p_shape);
}
