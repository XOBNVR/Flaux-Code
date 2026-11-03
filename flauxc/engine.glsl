// Flaux 3D: the programs that run on the graphics card.
//
// Each section starts with a line "//@@ name". The same code is used by the
// desktop runtime (gl.h, OpenGL 3.3) and the web runtime (fx3d.js, WebGL 2);
// only the first line (#version) differs. "//@@USER@@" is where a shader
// written in Flaux goes (see shadergen.py).

//@@ common
float fx_pow(float a, float b) {
    if (b == floor(b)) {
        float r = pow(abs(a), b);
        return (a < 0.0 && mod(b, 2.0) == 1.0) ? -r : r;
    }
    return pow(a, b);
}
float fx_round(float a) { return floor(a + 0.5); }
float fx_mod(float a, float b) { return a - b * floor(a / b); }
vec3 fx_lin(vec3 c) { return pow(max(c, vec3(0.0)), vec3(2.2)); }

//@@ scene_vs
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_row0;
layout(location = 4) in vec4 a_row1;
layout(location = 5) in vec4 a_row2;
layout(location = 6) in vec4 a_color;   // the shape's colour (linear), a = 1: pictures tile by size
layout(location = 7) in vec4 a_extra;   // numbers from the program, for its shader
layout(location = 8) in vec4 a_vcolor;  // a model's own colours
uniform mat4 u_viewproj;
out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out vec2 v_tile;
out vec3 v_color;
out vec4 v_extra;
void main() {
    vec4 p = vec4(a_pos, 1.0);
    v_world = vec3(dot(a_row0, p), dot(a_row1, p), dot(a_row2, p));
    vec3 c0 = vec3(a_row0.x, a_row1.x, a_row2.x);
    vec3 c1 = vec3(a_row0.y, a_row1.y, a_row2.y);
    vec3 c2 = vec3(a_row0.z, a_row1.z, a_row2.z);
    vec3 s2 = max(vec3(dot(c0, c0), dot(c1, c1), dot(c2, c2)), vec3(1e-12));
    vec3 n = a_normal / s2;   // stretched shapes still get the right normals
    v_normal = vec3(dot(a_row0.xyz, n), dot(a_row1.xyz, n), dot(a_row2.xyz, n));
    v_uv = a_uv;
    v_tile = a_color.a > 0.5 ? sqrt(vec2(s2.x, s2.z)) : vec2(1.0);
    v_color = a_color.rgb * pow(a_vcolor.rgb, vec3(2.2));
    v_extra = a_extra;
    gl_Position = u_viewproj * vec4(v_world, 1.0);
}

//@@ scene_fs
in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in vec2 v_tile;
in vec3 v_color;
in vec4 v_extra;
out vec4 o_color;
uniform sampler2D u_tex;
uniform int u_textured;
uniform highp sampler2DShadow u_shadow_map;
uniform int u_shadows;
uniform mat4 u_shadow_mat;
uniform vec3 u_sun_dir;      // towards the sun
uniform vec3 u_sun_color;    // linear; black when there's no sun
uniform vec3 u_amb_top;      // light from the sky above
uniform vec3 u_amb_bottom;   // light bouncing up from below
uniform vec3 u_camera;
uniform vec4 u_lights[16];   // 8 lights: position + range, then colour
uniform int u_light_count;
uniform vec3 u_fog_color;
uniform vec2 u_fog;          // where fog starts and where it's complete (0 = no fog)
uniform float u_time;

float fx_shadow(vec3 n) {
    vec4 s = u_shadow_mat * vec4(v_world + n * 0.03, 1.0);
    vec3 p = s.xyz / s.w * 0.5 + 0.5;
    vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0));
    float sum = 0.0;
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++)
            sum += texture(u_shadow_map, vec3(p.xy + vec2(float(i), float(j)) * texel, p.z - 0.0004));
    bool outside = u_shadows == 0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0;
    return outside ? 1.0 : sum / 9.0;
}

//@@USER@@

void main() {
    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) n = -n;
    vec3 base = v_color;
#ifdef FX_USER
    base = fx_lin(clamp(fx_user_surface(v_world.x, v_world.y, v_world.z, v_uv.x, v_uv.y, n.x, n.y, n.z,
                                        u_time, v_extra) / 255.0, 0.0, 1.0)) * v_color;
#endif
    if (u_textured == 1) {
        vec4 t = texture(u_tex, v_uv * v_tile);
        if (t.a < 0.5) discard;   // see-through parts of pictures
        base *= fx_lin(t.rgb);
    }
    vec3 view = normalize(u_camera - v_world);
    float sh = fx_shadow(n);
    vec3 color = base * mix(u_amb_bottom, u_amb_top, n.y * 0.5 + 0.5);
    float ndl = max(dot(n, u_sun_dir), 0.0);
    float spec = ndl > 0.0 ? pow(max(dot(n, normalize(u_sun_dir + view)), 0.0), 64.0) * 0.05 : 0.0;
    color += u_sun_color * sh * (base * ndl + spec);
    for (int i = 0; i < 8; i++) {
        if (i >= u_light_count) break;
        vec3 d = u_lights[2 * i].xyz - v_world;
        float dist = length(d);
        float att = clamp(1.0 - dist / u_lights[2 * i].w, 0.0, 1.0);
        att *= att;
        vec3 l = d / max(dist, 0.0001);
        float nl = max(dot(n, l), 0.0);
        float sp = nl > 0.0 ? pow(max(dot(n, normalize(l + view)), 0.0), 64.0) * 0.05 : 0.0;
        color += u_lights[2 * i + 1].rgb * att * (base * nl + sp);
    }
    if (u_fog.y > 0.0)
        color = mix(color, u_fog_color, smoothstep(u_fog.x, u_fog.y, length(u_camera - v_world)));
    o_color = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}

//@@ shadow_vs
layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec4 a_row0;
layout(location = 4) in vec4 a_row1;
layout(location = 5) in vec4 a_row2;
uniform mat4 u_viewproj;
void main() {
    vec4 p = vec4(a_pos, 1.0);
    gl_Position = u_viewproj * vec4(dot(a_row0, p), dot(a_row1, p), dot(a_row2, p), 1.0);
}

//@@ shadow_fs
out vec4 o_color;
void main() { o_color = vec4(1.0); }

//@@ fullscreen_vs
out vec2 v_ndc;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0 - 1.0;
    v_ndc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}

//@@ sky_fs
in vec2 v_ndc;
out vec4 o_color;
uniform mat4 u_inv_viewproj;
uniform vec3 u_top;
uniform vec3 u_horizon;
void main() {
    vec4 a = u_inv_viewproj * vec4(v_ndc, 1.0, 1.0);
    vec4 b = u_inv_viewproj * vec4(v_ndc, -1.0, 1.0);
    vec3 dir = normalize(a.xyz / a.w - b.xyz / b.w);
    o_color = vec4(mix(u_horizon, u_top, pow(clamp(dir.y, 0.0, 1.0), 0.6)), 1.0);
}

//@@ flat_vs
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
uniform vec2 u_screen;
out vec2 v_uv;
out vec4 v_color;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0, 0.0, 1.0);
}

//@@ flat_fs
in vec2 v_uv;
in vec4 v_color;
out vec4 o_color;
uniform sampler2D u_tex;
void main() { o_color = texture(u_tex, v_uv) * v_color; }

//@@ effect_fs
out vec4 o_color;
uniform sampler2D u_scene;
uniform vec2 u_size;
uniform float u_time;
uniform float u_vars[8];
vec3 fx_at(float x, float y) { return texture(u_scene, vec2(x / u_size.x, 1.0 - y / u_size.y)).rgb * 255.0; }
float red_at(float x, float y) { return fx_at(x, y).r; }
float green_at(float x, float y) { return fx_at(x, y).g; }
float blue_at(float x, float y) { return fx_at(x, y).b; }

//@@USER@@

void main() {
    float x = gl_FragCoord.x;
    float y = u_size.y - gl_FragCoord.y;
    vec3 c = fx_at(x, y);
    o_color = vec4(clamp(fx_user_screen(x, y, u_size.x, u_size.y, u_time, c.r, c.g, c.b) / 255.0, 0.0, 1.0), 1.0);
}
