/* Flaux VR: the co-pilot.  It reads a sentence like "a tower of 6 red cubes" and builds it on the
 * plate in front of you.  It runs on the headset: no internet, no account.  It knows a set of
 * patterns (tower, row, wall, floor, ring, stairs, pyramid, spiral, scatter) and a few whole
 * things (house, tree, forest, castle, snowman, rocket). */
#include "editor.h"
#include <ctype.h>

static char words[24][20];
static int nwords;
static int nums[4], nnums;
static V3 anchor, right, fwd;
static float yaw;
static int made, rainbow, color;
static Obj *sink, *last; static int sink_n, sink_max;      /* a preview is built into a list of its own, not the scene */

static int is_word(const char *w) {
    size_t n = strlen(w);
    for (int i = 0; i < nwords; i++) {
        const char *t = words[i];
        size_t tn = strlen(t);
        if (!strcmp(t, w)) return 1;
        if (tn == n + 1 && !strncmp(t, w, n) && t[n] == 's') return 1;                     /* cubes */
        if (tn == n + 2 && !strncmp(t, w, n) && t[n] == 'e' && t[n + 1] == 's') return 1;   /* boxes */
    }
    return 0;
}

static int any(const char *list) {      /* "tower stack pile" */
    char buf[160]; snprintf(buf, sizeof buf, "%s", list);
    for (char *w = strtok(buf, " "); w; w = strtok(NULL, " ")) if (is_word(w)) return 1;
    return 0;
}

static void split(const char *prompt) {
    static const char *number_words[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                         "eleven", "twelve"};
    nwords = nnums = 0;
    const char *p = prompt;
    while (*p && nwords < 24) {
        while (*p && !isalnum((unsigned char)*p)) p++;
        int k = 0;
        while (*p && isalnum((unsigned char)*p)) { if (k < 19) words[nwords][k++] = (char)tolower((unsigned char)*p); p++; }
        if (!k) break;
        words[nwords][k] = 0;
        const char *w = words[nwords++];
        int value = -1;
        if (isdigit((unsigned char)w[0])) value = atoi(w);
        for (int i = 0; i <= 12; i++) if (!strcmp(w, number_words[i])) value = i;
        if (!strcmp(w, "dozen")) value = 12;
        if (value >= 0 && nnums < 4) nums[nnums++] = value;
    }
}

static int find_color(void) {
    static const char *alias[][2] = {{"gray", "grey"}, {"amber", "gold"}, {"golden", "gold"}, {"dark", "black"}, {"navy", "indigo"},
                                     {"magenta", "pink"}, {"turquoise", "teal"}, {"aqua", "cyan"}, {"snow", "white"}};
    for (int i = 0; i < PALETTE_N; i++) if (is_word(PALETTE_NAMES[i])) return i;
    for (size_t a = 0; a < sizeof alias / sizeof alias[0]; a++)
        if (is_word(alias[a][0]))
            for (int i = 0; i < PALETTE_N; i++) if (!strcmp(PALETTE_NAMES[i], alias[a][1])) return i;
    return -1;
}

static int find_shape(int skip_ring) {
    if (any("cube box block brick")) return SH_CUBE;
    if (any("ball sphere orb bubble")) return SH_SPHERE;
    if (any("cylinder pillar tube column pole")) return SH_CYLINDER;
    if (any("cone spike")) return SH_CONE;
    if (any("ramp wedge slope")) return SH_WEDGE;
    if (!skip_ring && any("ring donut doughnut torus hoop")) return SH_TORUS;
    return -1;
}

static void put(int shape, float x, float y, float z, V3 scale, int col, float turn) {
    if ((!sink && g_obj_count >= MAX_OBJ) || made >= 400) return;
    if (rainbow) col = (made * 2) % 15;
    unsigned c = PALETTE[col];
    V3 p = v3add(anchor, v3add(v3mul(right, x), v3add(v3mul(fwd, z), v3(0, y, 0))));
    Q4 q = q4axis(v3(0, 1, 0), yaw + turn);
    if (sink) {
        if (sink_n >= sink_max) return;
        last = &sink[sink_n++];
        memset(last, 0, sizeof *last);
        last->shape = shape; last->pos = p; last->rot = q; last->scale = scale;
        last->r = (uint8_t)(c >> 16); last->g = (uint8_t)(c >> 8); last->b = (uint8_t)c;
        made++;
    } else if ((last = scene_add(shape, p, q, scale, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c)) != NULL) {
        made++;
    }
}

static int color_named(const char *name) {
    for (int i = 0; i < PALETTE_N; i++) if (!strcmp(PALETTE_NAMES[i], name)) return i;
    return 0;
}

static void tree(float x, float z, float s) {
    put(SH_CYLINDER, x, 0.6f * s, z, v3(0.3f * s, 1.2f * s, 0.3f * s), color_named("brown"), 0);
    put(SH_CONE, x, 1.7f * s, z, v3(1.5f * s, 1.4f * s, 1.5f * s), color_named("green"), 0);
    put(SH_CONE, x, 2.5f * s, z, v3(1.1f * s, 1.2f * s, 1.1f * s), color_named("lime"), 0);
}

static void tower_part(float x, float z, float r, float h, int wall, int roof) {
    put(SH_CYLINDER, x, h * 0.5f, z, v3(r * 2, h, r * 2), wall, 0);
    put(SH_CONE, x, h + r * 0.9f, z, v3(r * 2.5f, r * 1.8f, r * 2.5f), roof, 0);
}

void copilot_run(const char *prompt) {
    split(prompt);
    yaw = sink ? 0 : ed_front_yaw();
    anchor = sink ? v3(0, 0, 0) : ed_front_point(is_word("castle") ? 5.0f : any("house home hut forest wood rocket spaceship") ? 4.0f : 3.2f);
    right = v3(cosf(yaw), 0, -sinf(yaw)); fwd = v3(-sinf(yaw), 0, -cosf(yaw));
    made = 0;
    rainbow = any("rainbow colourful colorful");
    int named = find_color();
    color = named >= 0 ? named : ed.color;
    float s = any("tiny") ? 0.15f : any("small little") ? 0.3f : any("huge giant enormous") ? 2.0f : any("big large") ? 1.0f : 0.5f;
    int n = nnums ? nums[0] : -1;
    V3 one = v3(s, s, s);
    const char *what = NULL;
    char desc[64];

    if (!sink && (any("clear reset") || (any("delete remove erase") && any("everything all")))) {
        int had = g_obj_count;
        scene_clear(); scene_commit("Clear"); ed.selected = 0;
        copilot_say(0, had ? "Cleared %d shape%s. Press A if you want them back." : "The plate is already empty.", had, had == 1 ? "" : "s");
        return;
    }
    if (!sink && is_word("undo")) { copilot_say(0, scene_undo() ? "Undone." : "There's nothing to undo."); return; }
    if (!sink && is_word("redo")) { copilot_say(0, scene_redo() ? "Redone." : "There's nothing to redo."); return; }

    Obj *sel = sink ? NULL : scene_find(ed.selected);
    if (!sink && any("paint color colour recolor recolour make turn") && named >= 0 && (any("everything all") || (sel && any("it this selected that")))) {
        unsigned c = PALETTE[named];
        int count = 0;
        for (int i = 0; i < g_obj_count; i++)
            if (any("everything all") || g_obj[i].id == ed.selected) {
                g_obj[i].r = (uint8_t)(c >> 16); g_obj[i].g = (uint8_t)(c >> 8); g_obj[i].b = (uint8_t)c; count++;
            }
        scene_commit("Paint");
        copilot_say(0, "Painted %d shape%s %s.", count, count == 1 ? "" : "s", PALETTE_NAMES[named]);
        return;
    }
    if (sel && any("bigger larger grow") ) { sel->scale = v3mul(sel->scale, 1.5f); scene_commit("Resize"); copilot_say(0, "Made %s bigger.", sel->name); return; }
    if (sel && any("smaller shrink")) { sel->scale = v3mul(sel->scale, 0.66f); scene_commit("Resize"); copilot_say(0, "Made %s smaller.", sel->name); return; }
    if (sel && any("delete remove erase")) { ed_delete_selected(); copilot_say(0, "Deleted it."); return; }
    if (sel && any("copy duplicate clone")) { ed_duplicate_selected(); copilot_say(0, "Made a copy."); return; }

    int shape = find_shape(any("ring circle") && nwords > 2);
    int sh = shape >= 0 ? shape : SH_CUBE;
    const char *plural[SH_COUNT] = {"cubes", "balls", "cylinders", "cones", "ramps", "rings"};
    float h = s * (sh == SH_TORUS ? 0.28f : 1.0f);

    if (is_word("castle")) {
        int wall = named >= 0 ? named : color_named("silver"), roof = color_named("red");
        float d = 2.5f;
        for (int i = 0; i < 4; i++) tower_part((i & 1) ? d : -d, d + ((i & 2) ? d : -d), 0.6f, 3.0f, wall, roof);
        put(SH_CUBE, 0, 1.0f, 0, v3(d * 2 - 1.0f, 2.0f, 0.4f), wall, 0);
        put(SH_CUBE, 0, 1.0f, d * 2, v3(d * 2 - 1.0f, 2.0f, 0.4f), wall, 0);
        put(SH_CUBE, -d, 1.0f, d, v3(0.4f, 2.0f, d * 2 - 1.0f), wall, 0);
        put(SH_CUBE, d, 1.0f, d, v3(0.4f, 2.0f, d * 2 - 1.0f), wall, 0);
        put(SH_CUBE, 0, 0.7f, -0.05f, v3(0.9f, 1.4f, 0.45f), color_named("brown"), 0);
        tower_part(0, d, 0.9f, 4.2f, wall, color_named("indigo"));
        what = "a castle";
    } else if (is_word("house") || is_word("home") || is_word("hut")) {
        int wall = named >= 0 ? named : color_named("gold");
        put(SH_CUBE, 0, 0.9f, 1.2f, v3(2.4f, 1.8f, 2.4f), wall, 0);
        put(SH_CONE, 0, 2.45f, 1.2f, v3(3.6f, 1.3f, 3.6f), color_named("red"), 0);
        put(SH_CUBE, 0, 0.55f, -0.02f, v3(0.6f, 1.1f, 0.1f), color_named("brown"), 0);
        put(SH_CUBE, -0.75f, 1.15f, -0.02f, v3(0.45f, 0.45f, 0.1f), color_named("sky"), 0);
        put(SH_CUBE, 0.75f, 1.15f, -0.02f, v3(0.45f, 0.45f, 0.1f), color_named("sky"), 0);
        put(SH_CUBE, 0.7f, 2.6f, 1.6f, v3(0.35f, 0.9f, 0.35f), color_named("grey"), 0);
        what = "a house";
    } else if (is_word("forest") || is_word("wood") || (is_word("tree") && n > 1)) {
        int count = n > 1 ? (n > 40 ? 40 : n) : 7;
        unsigned seed = 12345u + (unsigned)(ed.time * 1000);
        for (int i = 0; i < count; i++) {
            seed = seed * 1664525u + 1013904223u; float a = (float)(seed >> 8) / 16777216.0f;
            seed = seed * 1664525u + 1013904223u; float b = (float)(seed >> 8) / 16777216.0f;
            seed = seed * 1664525u + 1013904223u; float c = (float)(seed >> 8) / 16777216.0f;
            tree((a - 0.5f) * 9.0f, b * 8.0f, 0.7f + c * 0.7f);
        }
        snprintf(desc, sizeof desc, "a forest of %d trees", count); what = desc;
    } else if (is_word("tree")) {
        tree(0, 0.5f, 1.0f); what = "a tree";
    } else if (is_word("snowman")) {
        int w = color_named("white");
        put(SH_SPHERE, 0, 0.45f, 0.5f, v3(0.9f, 0.9f, 0.9f), w, 0);
        put(SH_SPHERE, 0, 1.12f, 0.5f, v3(0.65f, 0.65f, 0.65f), w, 0);
        put(SH_SPHERE, 0, 1.62f, 0.5f, v3(0.45f, 0.45f, 0.45f), w, 0);
        put(SH_CONE, 0, 1.62f, 0.22f, v3(0.09f, 0.3f, 0.09f), color_named("orange"), 0);
        if (last) last->rot = q4mul(last->rot, q4axis(v3(1, 0, 0), -FX_PI / 2));
        put(SH_SPHERE, -0.09f, 1.70f, 0.30f, v3(0.05f, 0.05f, 0.05f), color_named("black"), 0);
        put(SH_SPHERE, 0.09f, 1.70f, 0.30f, v3(0.05f, 0.05f, 0.05f), color_named("black"), 0);
        put(SH_CYLINDER, 0, 1.90f, 0.5f, v3(0.34f, 0.22f, 0.34f), color_named("black"), 0);
        put(SH_CYLINDER, 0, 1.80f, 0.5f, v3(0.5f, 0.03f, 0.5f), color_named("black"), 0);
        what = "a snowman";
    } else if (is_word("rocket") || is_word("spaceship")) {
        int body = named >= 0 ? named : color_named("white");
        put(SH_CYLINDER, 0, 1.5f, 0.6f, v3(0.9f, 2.4f, 0.9f), body, 0);
        put(SH_CONE, 0, 3.2f, 0.6f, v3(0.9f, 1.0f, 0.9f), color_named("red"), 0);
        put(SH_SPHERE, 0, 2.0f, 0.17f, v3(0.36f, 0.36f, 0.12f), color_named("sky"), 0);
        for (int i = 0; i < 4; i++) {
            float a = i * FX_PI / 2;
            put(SH_WEDGE, sinf(a) * 0.65f, 0.55f, 0.6f + cosf(a) * 0.65f, v3(0.12f, 0.9f, 0.5f), color_named("red"), a + FX_PI);
        }
        put(SH_CONE, 0, 0.15f, 0.6f, v3(0.6f, 0.3f, 0.6f), color_named("orange"), 0);
        what = "a rocket";
    } else if (any("tower stack pile")) {
        int count = fclamp((float)(n > 0 ? n : 5), 1, 60);
        for (int i = 0; i < count; i++) put(sh, 0, h * 0.5f + i * h, 0, one, color, 0);
        snprintf(desc, sizeof desc, "a tower of %d %s", count, plural[sh]); what = desc;
    } else if (any("stair step staircase")) {
        int count = fclamp((float)(n > 0 ? n : 8), 1, 40);
        for (int i = 0; i < count; i++) put(SH_CUBE, 0, s * 0.25f + i * s * 0.5f, i * s, v3(s * 3, s * 0.5f, s), color, 0);
        snprintf(desc, sizeof desc, "%d stairs", count); what = desc;
    } else if (is_word("pyramid")) {
        int levels = fclamp((float)(n > 0 ? n : 4), 1, 9);
        for (int l = 0; l < levels; l++) {
            int side = levels - l;
            for (int i = 0; i < side; i++)
                for (int j = 0; j < side; j++)
                    put(sh, (i - (side - 1) * 0.5f) * s, h * 0.5f + l * h, (j - (side - 1) * 0.5f) * s + levels * s * 0.5f, one, color, 0);
        }
        snprintf(desc, sizeof desc, "a pyramid %d high", levels); what = desc;
    } else if (is_word("wall")) {
        int wn = fclamp((float)(n > 0 ? n : 6), 1, 30), hn = fclamp((float)(nnums > 1 ? nums[1] : 3), 1, 20);
        for (int j = 0; j < hn; j++)
            for (int i = 0; i < wn; i++) put(sh, (i - (wn - 1) * 0.5f) * s, h * 0.5f + j * h, 0, one, color, 0);
        snprintf(desc, sizeof desc, "a wall %d wide and %d high", wn, hn); what = desc;
    } else if (any("floor grid platform field carpet")) {
        int wn = fclamp((float)(n > 0 ? n : 6), 1, 20), dn = fclamp((float)(nnums > 1 ? nums[1] : wn), 1, 20);
        for (int j = 0; j < dn; j++)
            for (int i = 0; i < wn; i++) put(sh, (i - (wn - 1) * 0.5f) * s, h * 0.5f, j * s, one, color, 0);
        snprintf(desc, sizeof desc, "a floor %d by %d", wn, dn); what = desc;
    } else if (is_word("spiral") || is_word("helix")) {
        int count = fclamp((float)(n > 0 ? n : 24), 2, 120);
        for (int i = 0; i < count; i++) {
            float a = i * 0.5f, r = 1.6f;
            put(sh, sinf(a) * r, h * 0.5f + i * h * 0.35f, r + cosf(a) * r, one, color, -a);
        }
        snprintf(desc, sizeof desc, "a spiral of %d %s", count, plural[sh]); what = desc;
    } else if (any("ring circle") && nwords > 2) {
        int count = fclamp((float)(n > 0 ? n : 12), 3, 80);
        float r = fmaxf(count * s * 1.15f / (2 * FX_PI), s);
        for (int i = 0; i < count; i++) {
            float a = (float)i / count * 2 * FX_PI;
            put(sh, sinf(a) * r, h * 0.5f, r + cosf(a) * r, one, color, -a);
        }
        snprintf(desc, sizeof desc, "a ring of %d %s", count, plural[sh]); what = desc;
    } else if (any("random scatter scattered confetti")) {
        int count = fclamp((float)(n > 0 ? n : 20), 1, 150);
        unsigned seed = 777u + (unsigned)(ed.time * 1000);
        for (int i = 0; i < count; i++) {
            seed = seed * 1664525u + 1013904223u; float a = (float)(seed >> 8) / 16777216.0f;
            seed = seed * 1664525u + 1013904223u; float b = (float)(seed >> 8) / 16777216.0f;
            seed = seed * 1664525u + 1013904223u; float c = (float)(seed >> 8) / 16777216.0f;
            int rs = shape >= 0 ? shape : (int)(c * 5.99f);
            int save = rainbow; rainbow = named < 0;
            put(rs, (a - 0.5f) * 8.0f, s * (rs == SH_TORUS ? 0.14f : 0.5f), b * 7.0f, one, color, c * 6.28f);
            rainbow = save;
        }
        snprintf(desc, sizeof desc, "%d scattered shapes", count); what = desc;
    } else if (shape >= 0 || any("row line")) {
        int count = fclamp((float)(n > 0 ? n : (any("row line") ? 5 : 1)), 1, 60);
        for (int i = 0; i < count; i++) put(sh, (i - (count - 1) * 0.5f) * s * 1.25f, h * 0.5f, 0, one, color, 0);
        if (count == 1) snprintf(desc, sizeof desc, "a %s %s", named >= 0 ? PALETTE_NAMES[named] : PALETTE_NAMES[ed.color], SHAPE_NAMES[sh]);
        else snprintf(desc, sizeof desc, "a row of %d %s", count, plural[sh]);
        for (char *p = desc; *p; p++) *p = (char)tolower((unsigned char)*p);
        what = desc;
    }

    if (sink) return;
    if (!what || !made) {
        if (what) copilot_say(0, "The scene is full, so I couldn't add that.");
        else copilot_say(0, "I didn't get that. Try \"a wall 6 by 3\", \"a ring of 12 balls\", \"a castle\" or \"clear\".");
        plat_sound(SND_ERROR);
        return;
    }
    scene_commit("Co-pilot");
    ed.selected = 0;
    copilot_say(0, "Built %s in front of you (%d shape%s).", what, made, made == 1 ? "" : "s");
    ed_toast("Built %s.", what);
    plat_sound(SND_PLACE);
}

/* Builds what the words ask for into a list instead of the scene, standing at 0, 0, 0 and facing
 * you.  The Assets page uses it for its pictures.  Gives back how many shapes it made. */
int copilot_preview(const char *prompt, Obj *out, int max) {
    sink = out; sink_n = 0; sink_max = max;
    copilot_run(prompt);
    sink = NULL;
    return sink_n;
}
