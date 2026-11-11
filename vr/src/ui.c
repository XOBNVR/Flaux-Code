/* Flaux VR: the panels.  Everything on a panel is a rounded rectangle, a piece of text or a
 * picture, drawn in "panel pixels" and placed in the world by the panel's pose.  The widgets are
 * immediate-mode: calling ui_button() both draws the button and says whether it was clicked. */
#include "fx.h"
#include "font_data.h"

Pointer ui_ptr[2];
float ui_dt;
double ui_time;
int ui_clicked_any;

#define VFLOATS 28
#define MAX_VERTS 120000
#define MAX_SEGS 256
typedef struct { int panel; GLuint tex; int first, count; } Seg;

static float *verts;
static int nverts;
static Seg segs[MAX_SEGS];
static int nsegs;
static Panel *panels[UI_MAX_PANELS];
static int npanels, cur = -1;
static float clip[4] = {-1e6f, -1e6f, 1e6f, 1e6f};
static unsigned active_id[2];
static int active_panel[2] = {-1, -1};
static float buzz[2];
static GLuint prog, vao, vbo, tex_font;

#define ANIM_SLOTS 512
static struct { unsigned id; float v; double seen; } anims[ANIM_SLOTS];

static const char *VS = "#version 300 es\nprecision highp float;\n"
    "layout(location = 0) in vec4 a_pl;\n"       /* position, and position inside the rectangle */
    "layout(location = 1) in vec4 a_hr;\n"       /* half size, corner radius, border */
    "layout(location = 2) in vec4 a_mu;\n"       /* mode, gradient direction, texture coordinates */
    "layout(location = 3) in vec4 a_c0;\n"
    "layout(location = 4) in vec4 a_c1;\n"
    "layout(location = 5) in vec4 a_bc;\n"
    "layout(location = 6) in vec4 a_clip;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_local, v_half, v_uv, v_px; out float v_rad, v_border, v_mode, v_gdir;\n"
    "out vec4 v_c0, v_c1, v_bc, v_clip;\n"
    "void main() {\n"
    "  v_px = a_pl.xy; v_local = a_pl.zw; v_half = a_hr.xy; v_rad = a_hr.z; v_border = a_hr.w;\n"
    "  v_mode = a_mu.x; v_gdir = a_mu.y; v_uv = a_mu.zw; v_c0 = a_c0; v_c1 = a_c1; v_bc = a_bc; v_clip = a_clip;\n"
    "  gl_Position = u_mvp * vec4(a_pl.xy, 0.0, 1.0);\n"
    "}\n";

static const char *FS = "#version 300 es\nprecision highp float;\n"
    "in vec2 v_local, v_half, v_uv, v_px; in float v_rad, v_border, v_mode, v_gdir;\n"
    "in vec4 v_c0, v_c1, v_bc, v_clip;\n"
    "uniform sampler2D u_tex, u_font; uniform int u_enc;\n"
    "out vec4 o;\n"
    "float sdbox(vec2 p, vec2 b, float r) { vec2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "void main() {\n"
    "  if (v_px.x < v_clip.x || v_px.y < v_clip.y || v_px.x > v_clip.z || v_px.y > v_clip.w) discard;\n"
    "  int mode = int(v_mode + 0.5);\n"
    "  vec4 col;\n"
    "  if (mode == 1) {\n"                                        /* text: a distance-field glyph */
    "    float s = texture(u_font, v_uv).r;\n"
    "    float w = fwidth(s) * 0.75 + 0.0001;\n"
    "    col = vec4(v_c0.rgb, v_c0.a * smoothstep(v_border - w, v_border + w, s));\n"
    "  } else {\n"
    "    float d = sdbox(v_local, v_half, v_rad);\n"
    "    float aa = max(fwidth(d), 0.0001);\n"
    "    if (mode == 3) {\n"                                      /* a soft glow or shadow */
    "      float a = 1.0 - smoothstep(-v_border, v_border, d);\n"
    "      col = vec4(v_c0.rgb, v_c0.a * a * a);\n"
    "    } else {\n"
    "      vec2 t = v_local / v_half * 0.5 + 0.5;\n"
    "      vec4 fill = mix(v_c0, v_c1, clamp(v_gdir > 0.5 ? t.x : t.y, 0.0, 1.0));\n"
    "      if (mode == 2) {\n"
    "        vec4 tx = texture(u_tex, v_uv); tx.rgb /= max(tx.a, 0.001);\n"
    "        if (u_enc == 0) tx.rgb = pow(tx.rgb, vec3(1.0 / 2.2));\n"        /* an sRGB picture arrives already decoded */
    "        fill = vec4(tx.rgb * v_c0.rgb, tx.a * v_c0.a);\n"
    "      }\n"
    "      float edge = smoothstep(-v_border - aa, -v_border + aa, d) * step(0.001, v_border) * v_bc.a;\n"
    "      fill = vec4(mix(fill.rgb, v_bc.rgb, edge), max(fill.a, edge));\n"
    "      col = vec4(fill.rgb, fill.a * (1.0 - smoothstep(-aa, aa, d)));\n"
    "    }\n"
    "  }\n"
    "  if (col.a < 0.003) discard;\n"
    "  o = vec4(u_enc == 1 ? col.rgb : pow(col.rgb, vec3(2.2)), col.a);\n"
    "}\n";

/* the arrow from the first Flaux VR: X is its black outline, a dot is white */
static const char *const ARROW[27] = {
    "X               ", "XX              ", "X.X             ", "X..X            ", "X...X           ", "X....X          ",
    "X.....X         ", "X......X        ", "X.......X       ", "X........X      ", "X.........X     ", "X..........X    ",
    "X...........X   ", "X............X  ", "X.............X ", "X........XXXXXXX", "X.....XX..X     ", "X....X X..X     ",
    "X...X  X...X    ", "X..X    X..X    ", "X.X     X...X   ", "XX       X..X   ", "X        X...X  ", "          X..X  ",
    "          X...X ", "           X..X ", "            XX  "};
#define ARROW_W 16
#define ARROW_H 27
#define ARROW_UP 8            /* each square of the arrow becomes 8x8 pixels, so its edges stay sharp */
static GLuint tex_arrow, tex_arrow_fill, tex_arrow_edge;

/* part: 0 the whole arrow (black outline, white inside), 1 just the inside, 2 just the outline (white, to tint) */
static GLuint make_arrow(int part) {
    const int w = (ARROW_W + 2) * ARROW_UP, h = (ARROW_H + 2) * ARROW_UP;
    unsigned char *px = calloc((size_t)w * h, 4);
    for (int y = 0; y < ARROW_H * ARROW_UP; y++)
        for (int x = 0; x < ARROW_W * ARROW_UP; x++) {
            char c = ARROW[y / ARROW_UP][x / ARROW_UP];
            if (c == ' ' || (part == 1 && c != '.') || (part == 2 && c != 'X')) continue;
            unsigned char *p = px + ((size_t)(y + ARROW_UP) * w + x + ARROW_UP) * 4;
            p[0] = p[1] = p[2] = (c == '.' || part == 2) ? 255 : 0; p[3] = 255;
        }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, g_encode ? GL_RGBA8 : GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(px);
    return tex;
}

void ui_init(void) {
    tex_arrow = make_arrow(0); tex_arrow_fill = make_arrow(1); tex_arrow_edge = make_arrow(2);
    prog = gfx_program(VS, FS, "ui");
    verts = malloc(sizeof(float) * VFLOATS * MAX_VERTS);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int i = 0; i < 7; i++) {
        glEnableVertexAttribArray((GLuint)i);
        glVertexAttribPointer((GLuint)i, 4, GL_FLOAT, GL_FALSE, VFLOATS * 4, (void *)(size_t)(i * 16));
    }
    glBindVertexArray(0);
    size_t n = 0;
    unsigned char *atlas = plat_asset("ui/font.bin", &n);
    glGenTextures(1, &tex_font);
    glBindTexture(GL_TEXTURE_2D, tex_font);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (atlas && n >= (size_t)FONT_W * FONT_H)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_W, FONT_H, 0, GL_RED, GL_UNSIGNED_BYTE, atlas);
    else
        plat_log("the font (ui/font.bin) is missing");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(atlas);
}

/* ---------- pointing ---------- */

V3 ui_panel_point(const Panel *p, float px, float py) {
    return pose_pt(p->pose, v3((px / p->pw - 0.5f) * p->w_m, (0.5f - py / p->ph) * p->h_m, 0));
}

static int ray_panel(const Panel *p, V3 o, V3 d, float *px, float *py, float *t) {
    V3 n = q4rot(p->pose.q, v3(0, 0, 1));
    float dn = v3dot(d, n);
    if (dn > -1e-4f) return 0;                      /* from behind, or along it */
    *t = v3dot(v3sub(p->pose.p, o), n) / dn;
    if (*t <= 0) return 0;
    V3 l = q4rot(q4conj(p->pose.q), v3sub(v3add(o, v3mul(d, *t)), p->pose.p));
    *px = (l.x / p->w_m + 0.5f) * p->pw;
    *py = (0.5f - l.y / p->h_m) * p->ph;
    return 1;
}

void ui_frame(Panel **ps, int count, float dt, double time) {
    npanels = count > UI_MAX_PANELS ? UI_MAX_PANELS : count;
    for (int i = 0; i < npanels; i++) panels[i] = ps[i];
    ui_dt = dt; ui_time = time;
    ui_clicked_any = 0;
    nverts = 0; nsegs = 0; cur = -1;
    for (int h = 0; h < 2; h++) {
        Pointer *p = &ui_ptr[h];
        p->panel = -1; p->dist = 1e9f;
        if (!p->active) { active_id[h] = 0; continue; }
        if (active_id[h] && !p->down) active_id[h] = 0;
        for (int i = 0; i < npanels; i++) {
            float px, py, t;
            if (!panels[i]->visible || panels[i]->passive || !ray_panel(panels[i], p->origin, p->dir, &px, &py, &t)) continue;
            int held = active_id[h] && active_panel[h] == i;       /* dragging: stay with this panel */
            int inside = px >= 0 && py >= 0 && px <= panels[i]->pw && py <= panels[i]->ph;
            if (held || (inside && t < p->dist && !(active_id[h] && active_panel[h] != i))) {
                p->panel = i; p->px = px; p->py = py; p->dist = t;
                if (held) break;
            }
        }
    }
}

static int in_clip(float x, float y) { return x >= clip[0] && y >= clip[1] && x <= clip[2] && y <= clip[3]; }

int ui_hover(float x, float y, float w, float h) {
    for (int i = 1; i >= 0; i--) {          /* the right hand wins a tie */
        Pointer *p = &ui_ptr[i];
        if (p->panel == cur && !active_id[i] && p->px >= x && p->py >= y && p->px <= x + w && p->py <= y + h && in_clip(p->px, p->py))
            return i;
    }
    return -1;
}

int ui_press(float x, float y, float w, float h) {
    for (int i = 1; i >= 0; i--) {
        Pointer *p = &ui_ptr[i];
        if (p->panel == cur && p->pressed && !active_id[i] && p->px >= x && p->py >= y && p->px <= x + w && p->py <= y + h &&
            in_clip(p->px, p->py)) {
            p->pressed = 0;                  /* one click, one widget */
            ui_clicked_any = 1;
            ui_buzz(i, 0.45f);
            return i + 1;
        }
    }
    return 0;
}

int ui_drag(unsigned id, float x, float y, float w, float h, float *px, float *py) {
    for (int i = 0; i < 2; i++) {
        Pointer *p = &ui_ptr[i];
        if (active_id[i] == id && active_panel[i] == cur) {
            if (!p->down) { active_id[i] = 0; continue; }
            if (p->panel == cur) { *px = p->px; *py = p->py; }
            else { *px = x + w * 0.5f; *py = y + h * 0.5f; }
            return i + 1;
        }
    }
    int hand = ui_press(x, y, w, h);
    if (hand) {
        active_id[hand - 1] = id; active_panel[hand - 1] = cur;
        *px = ui_ptr[hand - 1].px; *py = ui_ptr[hand - 1].py;
        return hand;
    }
    return 0;
}

float ui_scroll_y(float x, float y, float w, float h) {
    int hand = ui_hover(x, y, w, h);
    if (hand < 0 || fabsf(ui_ptr[hand].sy) < 0.2f) return 0;
    return ui_ptr[hand].sy * ui_dt;
}

void ui_buzz(int hand, float amount) { if (hand >= 0 && hand < 2 && amount > buzz[hand]) buzz[hand] = amount; }
float ui_take_buzz(int hand);
float ui_take_buzz(int hand) { float b = buzz[hand]; buzz[hand] = 0; return b; }
int ui_dragging(int hand);
int ui_dragging(int hand) { return active_id[hand] != 0; }

unsigned ui_id(const char *s, int n) {
    unsigned h = 2166136261u;
    for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
    h ^= (unsigned)n * 2654435761u;
    h ^= (unsigned)(cur + 1) * 0x9E3779B9u;
    return h ? h : 1;
}

float ui_anim(unsigned id, float target, float speed) {
    unsigned slot = id % ANIM_SLOTS;
    int free_slot = -1;
    for (int k = 0; k < 8; k++) {
        unsigned s = (slot + (unsigned)k) % ANIM_SLOTS;
        if (anims[s].id == id) {
            float t = fclamp(ui_dt * speed, 0.0f, 1.0f);
            anims[s].v += (target - anims[s].v) * t;
            anims[s].seen = ui_time;
            return anims[s].v;
        }
        if (free_slot < 0 && (anims[s].id == 0 || ui_time - anims[s].seen > 1.0)) free_slot = (int)s;
    }
    if (free_slot < 0) return target;
    anims[free_slot].id = id; anims[free_slot].v = target * 0.0f; anims[free_slot].seen = ui_time;
    return anims[free_slot].v;
}

/* ---------- drawing ---------- */

void ui_begin(int panel) { cur = panel; ui_unclip(); }

void ui_clip(float x, float y, float w, float h) { clip[0] = x; clip[1] = y; clip[2] = x + w; clip[3] = y + h; }
void ui_unclip(void) { clip[0] = clip[1] = -1e6f; clip[2] = clip[3] = 1e6f; }

static void quad(float x, float y, float w, float h, float grow, float mode, float rad, float border, float gdir,
                 float u0, float v0, float u1, float v1, GLuint tex, Col c0, Col c1, Col bc) {
    if (nverts + 6 > MAX_VERTS || cur < 0) return;
    if (x > clip[2] || y > clip[3] || x + w < clip[0] || y + h < clip[1]) return;
    Seg *s = nsegs ? &segs[nsegs - 1] : NULL;
    if (!s || s->panel != cur || (tex && s->tex && s->tex != tex)) {
        if (nsegs >= MAX_SEGS) return;
        s = &segs[nsegs++];
        s->panel = cur; s->tex = tex; s->first = nverts; s->count = 0;
    }
    if (tex) s->tex = tex;
    float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    if (rad > hw) rad = hw;
    if (rad > hh) rad = hh;
    static const float corner[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 6; i++) {
        float lx = corner[i][0] * (hw + grow), ly = corner[i][1] * (hh + grow);
        float *f = verts + (size_t)nverts * VFLOATS; nverts++;
        f[0] = cx + lx; f[1] = cy + ly; f[2] = lx; f[3] = ly;
        f[4] = hw; f[5] = hh; f[6] = rad; f[7] = border;
        f[8] = mode; f[9] = gdir;
        f[10] = corner[i][0] < 0 ? u0 : u1; f[11] = corner[i][1] < 0 ? v0 : v1;
        memcpy(f + 12, &c0, 16); memcpy(f + 16, &c1, 16); memcpy(f + 20, &bc, 16); memcpy(f + 24, clip, 16);
    }
    s->count += 6;
}

static const Col NOCOL = {0, 0, 0, 0};

void ui_rect(float x, float y, float w, float h, float radius, Col c) {
    quad(x, y, w, h, 1, 0, radius, 0, 0, 0, 0, 0, 0, 0, c, c, NOCOL);
}
void ui_grad(float x, float y, float w, float h, float radius, Col c0, Col c1, int horizontal) {
    quad(x, y, w, h, 1, 0, radius, 0, horizontal ? 1.0f : 0.0f, 0, 0, 0, 0, 0, c0, c1, NOCOL);
}
void ui_frame_rect(float x, float y, float w, float h, float radius, Col fill, float border, Col bcol) {
    quad(x, y, w, h, 1, 0, radius, border, 0, 0, 0, 0, 0, 0, fill, fill, bcol);
}
void ui_glow(float x, float y, float w, float h, float radius, float blur, Col c) {
    quad(x, y, w, h, blur, 3, radius, blur, 0, 0, 0, 0, 0, 0, c, c, NOCOL);
}
void ui_image(float x, float y, float w, float h, float radius, GLuint tex, float u0, float v0, float u1, float v1, Col tint) {
    quad(x, y, w, h, 0, 2, radius, 0, 0, u0, v0, u1, v1, tex, tint, tint, NOCOL);
}

static const FontGlyph *glyph(unsigned cp) {
    int lo = 0, hi = FONT_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (FONT_GLYPHS[mid].cp == cp) return &FONT_GLYPHS[mid];
        if (FONT_GLYPHS[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

static unsigned utf8(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7u) << 18) | ((p[0] & 63u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15u) << 12) | ((p[0] & 63u) << 6) | (p[1] & 63u); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31u) << 6) | (p[0] & 63u); p += 1; }
    *s = (const char *)p;
    return c;
}

float ui_text_width(float size, const char *s) {
    float w = 0, k = size / FONT_EM;
    while (s && *s) { const FontGlyph *g = glyph(utf8(&s)); if (!g) g = glyph('?'); w += g->adv * k; }
    return w;
}

static float text_impl(float x, float y, float size, Col c, const char *s, float edge) {
    float k = size / FONT_EM, x0 = x;
    while (s && *s) {
        const FontGlyph *g = glyph(utf8(&s));
        if (!g) g = glyph('?');
        if (g->w)
            quad(x + g->xo * k, y - 0.159f * size + g->yo * k, g->w * k, g->h * k, 0, 1, 0, edge, 0,
                 (float)g->x / FONT_W, (float)g->y / FONT_H, (float)(g->x + g->w) / FONT_W, (float)(g->y + g->h) / FONT_H,
                 0, c, c, NOCOL);
        x += g->adv * k;
    }
    return x - x0;
}

float ui_text(float x, float y, float size, Col c, const char *s) { return text_impl(x, y, size, c, s, 0.5f); }
float ui_text_bold(float x, float y, float size, Col c, const char *s) { return text_impl(x, y, size, c, s, 0.43f); }
void ui_text_center(float cx, float y, float size, Col c, const char *s) { ui_text(cx - ui_text_width(size, s) * 0.5f, y, size, c, s); }
void ui_text_right(float rx, float y, float size, Col c, const char *s) { ui_text(rx - ui_text_width(size, s), y, size, c, s); }

void ui_text_fit(float x, float y, float size, float maxw, Col c, const char *s) {
    if (ui_text_width(size, s) <= maxw) { ui_text(x, y, size, c, s); return; }
    char buf[160];
    size_t n = strlen(s);
    if (n > sizeof buf - 4) n = sizeof buf - 4;
    while (n > 0) {
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;      /* don't cut a character in half */
        memcpy(buf, s, n); memcpy(buf + n, "\xE2\x80\xA6", 4);
        if (ui_text_width(size, buf) <= maxw) break;
        n--;
    }
    ui_text(x, y, size, c, n ? buf : "");
}

/* another player's arrow: white inside, with an outline in their colour (its tip at x, y) */
void ui_arrow(float x, float y, float k, Col outline) {
    ui_image(x, y, (ARROW_W + 2) * k, (ARROW_H + 2) * k, 0, tex_arrow_fill, 0, 0, 1, 1, rgba(0xFFFFFF, 1));
    ui_image(x, y, (ARROW_W + 2) * k, (ARROW_H + 2) * k, 0, tex_arrow_edge, 0, 0, 1, 1, outline);
}

void ui_end(void) {
    if (cur < 0) return;
    ui_unclip();
    for (int i = 0; i < 2; i++) {            /* where each laser lands */
        Pointer *p = &ui_ptr[i];
        if (p->panel != cur) continue;
        float k = p->down ? 1.75f : 2.0f;         /* panel pixels per square of the arrow; its tip is where you point */
        ui_glow(p->px - 10, p->py - 10, 20, 20, 10, 18, i ? rgba(0x4FE3F5, 0.6f) : rgba(0xA78BFA, 0.6f));
        ui_image(p->px - k, p->py - k, (ARROW_W + 2) * k, (ARROW_H + 2) * k, 0, tex_arrow, 0, 0, 1, 1, rgba(0xFFFFFF, 1));
    }
    cur = -1;
}

void ui_render(const Cam *c) {
    if (!nsegs) return;
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "u_enc"), g_encode);
    glUniform1i(glGetUniformLocation(prog, "u_tex"), 0);
    glUniform1i(glGetUniformLocation(prog, "u_font"), 1);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, tex_font);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(float) * VFLOATS * (size_t)nverts), verts, GL_STREAM_DRAW);
    for (int i = 0; i < nsegs; i++) {
        const Panel *p = panels[segs[i].panel];
        M4 l = m4ident();
        l.m[0] = p->w_m / p->pw; l.m[5] = -p->h_m / p->ph; l.m[12] = -p->w_m * 0.5f; l.m[13] = p->h_m * 0.5f;
        M4 mvp = m4mul(c->vp, m4mul(m4trs(p->pose.p, p->pose.q, v3(1, 1, 1)), l));
        glUniformMatrix4fv(glGetUniformLocation(prog, "u_mvp"), 1, GL_FALSE, mvp.m);
        glBindTexture(GL_TEXTURE_2D, segs[i].tex);
        glDrawArrays(GL_TRIANGLES, segs[i].first, segs[i].count);
    }
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

/* ---------- widgets ---------- */

void ui_card(float x, float y, float w, float h) {
    ui_frame_rect(x, y, w, h, 26, rgba(0x18204A, 0.78f), 1.5f, rgba(0xFFFFFF, 0.09f));
}

void ui_title(float x, float y, const char *title, const char *sub) {
    ui_text_bold(x, y, 50, C_TEXT, title);
    if (sub) ui_text(x + 2, y + 60, 24, C_DIM, sub);
}

int ui_icon_button(float x, float y, float w, float h, const char *icon, const char *label, int style) {
    unsigned id = ui_id(label ? label : icon, (int)(x * 7 + y * 131));
    int hand = ui_hover(x, y, w, h);
    int clicked = ui_press(x, y, w, h) != 0;
    float hv = ui_anim(id, hand >= 0 ? 1.0f : 0.0f, 14.0f);
    float fl = ui_anim(id ^ 0x5bd1e995u, 0.0f, 6.0f);
    if (clicked) { for (int s = 0; s < ANIM_SLOTS; s++) if (anims[s].id == (id ^ 0x5bd1e995u)) anims[s].v = 1.0f; fl = 1.0f; plat_sound(SND_CLICK); }
    if (hand >= 0 && hv < 0.2f) ui_buzz(hand, 0.12f);
    float rad = h * 0.30f;
    Col text = C_TEXT;
    switch (style) {
    case BTN_PRIMARY:
    case BTN_ON:
        ui_glow(x, y, w, h, rad, 22, rgba(0x4FE3F5, 0.10f + 0.28f * hv + 0.3f * fl));
        ui_grad(x, y, w, h, rad, col_mix(rgba(0x7C3AED, 1), rgba(0x9F67FF, 1), hv), col_mix(rgba(0x22B8EE, 1), rgba(0x5EEBFF, 1), hv), 1);
        ui_frame_rect(x, y, w, h, rad, NOCOL, 1.5f, rgba(0xFFFFFF, 0.25f + 0.35f * hv));
        break;
    case BTN_DANGER:
        ui_glow(x, y, w, h, rad, 20, rgba(0xF43F5E, 0.30f * hv + 0.3f * fl));
        ui_frame_rect(x, y, w, h, rad, col_mix(rgba(0x3A1530, 0.92f), rgba(0xC0264A, 1), hv), 1.5f, rgba(0xF43F5E, 0.55f + 0.45f * hv));
        text = col_mix(rgba(0xFF9DB0, 1), C_TEXT, hv);
        break;
    case BTN_GHOST:
        ui_rect(x, y, w, h, rad, rgba(0xFFFFFF, 0.09f * hv + 0.1f * fl));
        text = col_mix(C_DIM, C_TEXT, hv);
        break;
    default:
        ui_glow(x, y, w, h, rad, 18, rgba(0x4FE3F5, 0.22f * hv + 0.3f * fl));
        ui_frame_rect(x, y, w, h, rad, col_mix(rgba(0x222B5C, 0.92f), rgba(0x34418A, 1), hv), 1.5f,
                      col_mix(rgba(0xFFFFFF, 0.12f), rgba(0x4FE3F5, 0.9f), hv));
        break;
    }
    float size = fclamp(h * 0.42f, 18.0f, 30.0f);
    float lw = label ? ui_text_width(size, label) : 0, iw = icon ? ui_text_width(size * 1.1f, icon) : 0;
    float gap = (icon && label && *label) ? size * 0.4f : 0;
    float tx = x + (w - lw - iw - gap) * 0.5f, ty = y + (h - size) * 0.5f;
    if (icon) { ui_text(tx, ty - size * 0.03f, size * 1.1f, text, icon); tx += iw + gap; }
    if (label) ui_text(tx, ty, size, text, label);
    return clicked;
}

int ui_button(float x, float y, float w, float h, const char *label, int style) {
    return ui_icon_button(x, y, w, h, NULL, label, style);
}

int ui_toggle(float x, float y, float w, const char *label, int *value) {
    const float h = 56, sw = 76, sh = 40;
    unsigned id = ui_id(label, (int)(x + y * 31));
    int hand = ui_hover(x, y, w, h);
    int clicked = ui_press(x, y, w, h) != 0;
    if (clicked) { *value = !*value; plat_sound(SND_CLICK); }
    float hv = ui_anim(id, hand >= 0 ? 1.0f : 0.0f, 14.0f);
    float on = ui_anim(id ^ 0x1234567u, *value ? 1.0f : 0.0f, 16.0f);
    ui_rect(x - 12, y, w + 24, h, 16, rgba(0xFFFFFF, 0.06f * hv));
    ui_text(x, y + (h - 27) * 0.5f, 27, C_TEXT, label);
    float sx = x + w - sw, sy = y + (h - sh) * 0.5f;
    ui_grad(sx, sy, sw, sh, sh * 0.5f, col_mix(rgba(0x2A3366, 1), rgba(0x7C3AED, 1), on), col_mix(rgba(0x2A3366, 1), rgba(0x22B8EE, 1), on), 1);
    ui_frame_rect(sx, sy, sw, sh, sh * 0.5f, NOCOL, 1.5f, rgba(0xFFFFFF, 0.18f + 0.3f * hv));
    float kx = sx + 5 + (sw - sh) * on;
    ui_rect(kx, sy + 5, sh - 10, sh - 10, (sh - 10) * 0.5f, rgba(0xFFFFFF, 1));
    return clicked;
}

int ui_slider(float x, float y, float w, const char *label, float *value, float lo, float hi, const char *fmt) {
    const float h = 84, ty = y + 56;
    unsigned id = ui_id(label, (int)(x + y * 31));
    float px = 0, py = 0;
    int hover = ui_hover(x - 10, y + 30, w + 20, h - 30);
    int hand = ui_drag(id, x - 10, y + 30, w + 20, h - 30, &px, &py);
    int changed = 0;
    if (hand) {
        float v = lo + (hi - lo) * fclamp((px - x) / w, 0.0f, 1.0f);
        if (fabsf(v - *value) > 1e-5f) { *value = v; changed = 1; }
    }
    float hv = ui_anim(id, (hand || hover >= 0) ? 1.0f : 0.0f, 14.0f);
    float t = fclamp((*value - lo) / (hi - lo), 0.0f, 1.0f);
    char buf[48];
    snprintf(buf, sizeof buf, fmt, (double)*value);
    ui_text(x, y, 26, C_TEXT, label);
    ui_text_right(x + w, y, 26, C_CYAN, buf);
    ui_rect(x, ty, w, 12, 6, rgba(0x2A3366, 1));
    if (t > 0.01f) ui_grad(x, ty, w * t, 12, 6, rgba(0x7C3AED, 1), rgba(0x22B8EE, 1), 1);
    float kr = 17 + 4 * hv, kx = x + w * t, ky = ty + 6;
    ui_glow(kx - kr, ky - kr, kr * 2, kr * 2, kr, 16, rgba(0x4FE3F5, 0.25f + 0.4f * hv));
    ui_frame_rect(kx - kr, ky - kr, kr * 2, kr * 2, kr, rgba(0xFFFFFF, 1), 4, rgba(0x4FE3F5, 1));
    return changed;
}
