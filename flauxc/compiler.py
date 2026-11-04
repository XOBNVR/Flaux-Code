"""The Flaux compiler: works out every type, then writes C code.

How it works
------------
1. Type inference. Flaux programs never mention types, so the compiler walks
   the program over and over, learning a bit more each time ("score is a
   whole number", "fib gives back a whole number"), until nothing changes.

2. Specialisation. Each function is compiled separately for every
   combination of input types it is called with. `square(3)` and
   `square(2.5)` become two C functions, one for whole numbers and one for
   decimals, so there is never any checking of types while the program runs.

3. Code generation. The same walk that infers types also writes C. Once the
   types have settled, one last walk writes the real program.

The C code is then handed to a normal C compiler (gcc or clang) which turns
it into a fast native program.

Errors teach: each one says what's wrong, how to fix it, and when possible
shows the user's own line already fixed.
"""

import difflib
import os

from . import graphics
from . import nodes as N
from .shadergen import shader_glsl
from .errors import FlauxError
from .types import (INT, FLOAT, BOOL, TEXT, VOID, UNK, SHADER, ListT, TypeConflict,
                    is_list, is_num, has_unknown, resolve, merge, describe, mangle)

RUNTIME_PATH = os.path.join(os.path.dirname(__file__), "runtime.h")

# Built-in functions, with a short example of each.
BUILTINS = {
    "sqrt": "say sqrt(16)        # 4",
    "sin": "say sin(0.5)        # angles are in radians",
    "cos": "say cos(0.5)        # angles are in radians",
    "tan": "say tan(0.5)        # angles are in radians",
    "log": "say log(10)         # natural logarithm",
    "abs": "say abs(-5)         # 5",
    "floor": "say floor(3.7)      # 3",
    "ceil": "say ceil(3.2)       # 4",
    "round": "say round(3.5)      # 4",
    "min": "say min(4, 9)       # 4",
    "max": "say max(4, 9)       # 9",
    "random": "set dice to random(1, 6)\nset pick to random([\"rock\", \"paper\", \"scissors\"])",
    "text": "say text(42) + \"!\"  # 42!",
    "number": "say number(\"3.5\")   # 3.5",
    "whole": "say whole(\"42\")     # 42\nsay whole(3.9)       # 3",
    "ask": "set name to ask(\"What's your name?\")",
    "is_number": "if is_number(\"42\")\n    say \"that's a number\"\nend",
    "clock": "set start to clock()\n# ... do some work ...\nsay clock() - start",
    "length": "say length(\"hello\")  # 5",
    "contains": "say contains([1, 2, 3], 2)        # true\nsay contains(\"hello\", \"ell\")     # true",
    "uppercase": "say uppercase(\"hi\")  # HI",
    "lowercase": "say lowercase(\"HI\")  # hi",
    "distance": "say distance(0, 0, 3, 4)   # 5",
    "rgb": "set orange to rgb(255, 136, 0)\ncircle(100, 100, 20, orange)",
    # graphics
    "window": "window(800, 600)\nwindow(800, 600, \"My Game\")",
    "show": "repeat forever\n    clear(\"black\")\n    circle(x, 300, 20, \"yellow\")\n    show()\nend",
    "clear": "clear(\"sky\")",
    "rect": "rect(100, 50, 200, 80, \"blue\")     # x, y, width, height, colour",
    "circle": "circle(400, 300, 50, \"red\")       # x, y, radius, colour",
    "line": "line(0, 0, 800, 600, \"white\")      # from x, y to x, y, colour\nline(0, 0, 800, 600, \"white\", 5)   # 5 pixels thick",
    "triangle": "triangle(100, 200, 150, 100, 200, 200, \"orange\")   # three corners, colour",
    "dot": "dot(10, 20, \"yellow\")",
    "write": "write(\"Score: {score}\", 10, 10)\nwrite(\"GAME OVER\", 250, 280, \"red\", 5)   # text, x, y, colour, size",
    "key_down": "if key_down(\"left\")\n    subtract 5 from x\nend",
    "key_pressed": "if key_pressed(\"space\")\n    say \"jump!\"\nend",
    "mouse_x": "circle(mouse_x(), mouse_y(), 10, \"white\")",
    "mouse_y": "circle(mouse_x(), mouse_y(), 10, \"white\")",
    "mouse_down": "if mouse_down()\n    circle(mouse_x(), mouse_y(), 5, \"red\")\nend",
    "mouse_clicked": "if mouse_clicked()\n    add 1 to clicks\nend",
    "mouse_moved_x": "set turn to turn + mouse_moved_x() * 0.004    # look left and right",
    "mouse_moved_y": "set tilt to tilt + mouse_moved_y() * 0.004    # look up and down",
    "lock_mouse": "window(800, 600)\nlock_mouse()    # hide the pointer: the mouse turns the view (Esc lets go)",
    "unlock_mouse": "unlock_mouse()    # give the pointer back",
    "mouse_locked": "if not mouse_locked()\n    write(\"Click to look around\", 10, 10)\nend",
    "beep": "beep(440, 0.2)    # pitch, seconds",
    "screenshot": "screenshot(\"picture.bmp\")",
    "fullscreen": "window(800, 600)\nfullscreen()    # fill the screen (F11 switches back)",
    "image": "image(\"ship.png\", 100, 200)              # a picture at x, y\n"
             "image(\"ship.png\", 100, 200, 64, 64)      # ...64 by 64 pixels\n"
             "image(\"ship.png\", 100, 200, 64, 64, 90)  # ...turned 90 degrees",
    "image_width": "say image_width(\"ship.png\")",
    "image_height": "say image_height(\"ship.png\")",
    "text_width": "set w to text_width(\"GAME OVER\", 5)\nwrite(\"GAME OVER\", (800 - w) / 2, 280, \"red\", 5)",
    "fps": "write(\"{fps()} frames a second\", 10, 10)",
    "seconds": "set t to seconds()     # seconds since the window opened",
    # game controllers
    "button_down": "if button_down(\"a\")\n    say \"jump!\"\nend",
    "button_pressed": "if button_pressed(\"start\")\n    set paused to not paused\nend",
    "stick_x": "add stick_x() * 5 to x      # the left stick: -1 (left) to 1 (right)\n"
               "add stick_x(\"right\") * 2 to angle",
    "stick_y": "add stick_y() * 5 to y      # the left stick: -1 (down) to 1 (up)",
    # 3D
    "camera": "camera(0, 5, 10, 0, 0, 0)        # where the camera is (x, y, z), then the point it looks at\n"
              "camera(0, 5, 10, 0, 0, 0, 90)    # ...seeing more (90 degrees wide)",
    "sky": "sky(\"sky\")\nsky(\"blue\", \"white\")     # the colour at the top, then at the horizon",
    "sun": "sun(1, 2, 1)              # the direction the sunlight comes from\n"
           "sun(1, 2, 1, \"orange\")    # ...in a colour\nsun(0, 0, 0)              # no sun (night)",
    "light": "light(0, 3, 0, \"yellow\")        # a lamp at x, y, z\n"
             "light(0, 3, 0, \"yellow\", 20)    # ...that reaches 20 steps",
    "fog": "fog(50)             # things fade into the sky, until 50 steps away they're gone\n"
           "fog(30, \"gray\")     # grey fog\nfog(0)              # no fog",
    "turn": "turn(0, 45, 0)              # turn the next shapes 45 degrees (around x, y, z)\n"
            "cube(0, 0, 0, 1, \"red\")",
    "cube": "cube(0, 0, 0, 1, \"red\")             # x, y, z, size, colour",
    "box": "box(0, 0, 0, 4, 1, 2, \"brown\")      # x, y, z, width, height, depth, colour",
    "ball": "ball(0, 1, 0, 0.5, \"orange\")        # x, y, z, radius, colour",
    "cylinder": "cylinder(0, 1, 0, 0.5, 2, \"gray\")   # x, y, z, radius, height, colour",
    "cone": "cone(0, 1, 0, 0.5, 2, \"green\")      # x, y, z, radius, height, colour",
    "plane": "plane(0, 0, 0, 20, 20, \"green\")       # a flat floor: x, y, z, width, depth, colour\n"
             "plane(0, 0, 0, 20, 20, \"grass.png\")   # ...covered in a picture",
    "model": "model(\"tree.obj\", 0, 1, 0, 2)          # a 3D model file, x, y, z, size\n"
             "model(\"tree.obj\", 0, 1, 0, 2, \"red\")   # ...painted red",
    "effect": "effect(wobble)      # put the whole screen through the shader called wobble",
    # VR and AR
    "in_vr": "if in_vr()\n    say \"You're in a VR headset!\"\nend",
    "hand_x": "ball(hand_x(\"right\"), hand_y(\"right\"), hand_z(\"right\"), 0.1, \"gold\")   # where your hand is",
    "hand_y": "ball(hand_x(\"left\"), hand_y(\"left\"), hand_z(\"left\"), 0.1, \"gold\")   # where your hand is",
    "hand_z": "ball(hand_x(\"left\"), hand_y(\"left\"), hand_z(\"left\"), 0.1, \"gold\")   # where your hand is",
}

# Built-ins that draw in 3D (they need the OpenGL runtime).
THREE_D_BUILTINS = {"camera", "sky", "sun", "light", "fog", "turn", "cube", "box", "ball", "cylinder", "cone",
                    "plane", "model", "effect", "in_vr", "hand_x", "hand_y", "hand_z"}

# Which input of a built-in can be a shader (to paint with).
SHADER_INPUT = {"cube": 4, "box": 6, "ball": 4, "cylinder": 5, "cone": 5, "plane": 5, "model": 5, "effect": 0}

MODEL_TYPES = (".obj",)

GRAPHICS_BUILTINS = {"window", "show", "clear", "rect", "circle", "line", "triangle", "dot", "write", "key_down",
                     "key_pressed", "mouse_x", "mouse_y", "mouse_down", "mouse_clicked", "beep",
                     "screenshot", "text_width", "image", "image_width", "image_height", "fps", "seconds",
                     "button_down", "button_pressed", "stick_x", "stick_y", "fullscreen",
                     "mouse_moved_x", "mouse_moved_y", "lock_mouse", "unlock_mouse", "mouse_locked"} | THREE_D_BUILTINS

PICTURE_TYPES = (".png", ".jpg", ".jpeg", ".bmp", ".gif")

# Function names from other languages, and the Flaux version.
OTHER_LANGUAGE_FUNCTIONS = {
    "print": "say", "input": "ask", "readline": "ask", "prompt": "ask", "len": "length",
    "size": "length", "count": "length", "str": "text", "string": "text", "tostring": "text",
    "int": "whole", "parseint": "whole", "float": "number", "parsefloat": "number",
    "num": "number", "upper": "uppercase", "lower": "lowercase", "toupper": "uppercase",
    "tolower": "lowercase", "touppercase": "uppercase", "tolowercase": "lowercase",
    "rand": "random", "randint": "random", "randrange": "random", "sqr": "sqrt",
    "squareroot": "sqrt", "absolute": "abs", "minimum": "min", "maximum": "max", "time": "clock",
    "includes": "contains", "has": "contains", "indexof": "contains",
}

LIST_TEMPLATE = """\
typedef struct { int64_t len, cap; @CT *items; } @N;
static @N *@N_new(int64_t cap) {
    @N *l = fx_alloc(sizeof *l);
    l->len = 0;
    l->cap = cap > 0 ? cap : 0;
    l->items = l->cap ? fx_alloc(sizeof(@CT) * (size_t)l->cap) : NULL;
    return l;
}
static @N *@N_of(int64_t n, const @CT *src) {
    @N *l = @N_new(n);
    memcpy(l->items, src, sizeof(@CT) * (size_t)n);
    l->len = n;
    return l;
}
static @N *@N_fill(int64_t n, @CT v, int line) {
    if (n < 0) fx_fail(line, "The number before `copies of` must be 0 or more.",
                       "You can't make %" PRId64 " copies of something.", n);
    @N *l = @N_new(n);
    for (int64_t i = 0; i < n; i++) l->items[i] = v;
    l->len = n;
    return l;
}
__attribute__((noinline)) static void @N_grow(@N *l) {
    l->cap = l->cap ? l->cap * 2 : 4;
    l->items = fx_realloc(l->items, sizeof(@CT) * (size_t)l->cap);
}
FX_INLINE void @N_push(@N *l, @CT v) {
    if (l->len == l->cap) @N_grow(l);
    l->items[l->len++] = v;
}
static void @N_extend(@N *l, @N *other) {
    int64_t n = other->len;
    for (int64_t i = 0; i < n; i++) @N_push(l, other->items[i]);
}
FX_INLINE @CT @N_get(@N *l, int64_t i, int line) {
    if ((uint64_t)(i - 1) >= (uint64_t)l->len) fx_index_fail(i, l->len, line, "item");
    return l->items[i - 1];
}
FX_INLINE @CT *@N_at(@N *l, int64_t i, int line) {
    if ((uint64_t)(i - 1) >= (uint64_t)l->len) fx_index_fail(i, l->len, line, "item");
    return &l->items[i - 1];
}
static @N *@N_cat(@N *a, @N *b) {
    @N *l = @N_new(a->len + b->len);
    for (int64_t i = 0; i < a->len; i++) l->items[i] = a->items[i];
    for (int64_t i = 0; i < b->len; i++) l->items[a->len + i] = b->items[i];
    l->len = a->len + b->len;
    return l;
}
static @CT @N_pick(@N *l, int line) {
    if (l->len == 0)
        fx_fail(line, "Make sure the list has items before picking one:\\n\\n"
                      "      if length of my_list > 0\\n"
                      "          say random(my_list)\\n"
                      "      end",
                "You can't pick a random item from an empty list.");
    return l->items[fx_rng() % (uint64_t)l->len];
}
static fx_text @N_text(@N *l) {
    fx_text out = FX_LIT("[");
    for (int64_t i = 0; i < l->len; i++) {
        if (i) out = fx_concat(out, FX_LIT(", "));
        out = fx_concat(out, @TXT(l->items[i]));
    }
    return fx_concat(out, FX_LIT("]"));
}
"""

HAS_TEMPLATE = """\
static bool @N_has(@N *l, @CT v) {
    for (int64_t i = 0; i < l->len; i++) { @CT a = l->items[i]; if (@EQ) return true; }
    return false;
}
"""

SAY_FUNCS = {INT: "fx_say_int", FLOAT: "fx_say_float", BOOL: "fx_say_bool", TEXT: "fx_say_text"}
SAY_SAME_FUNCS = {t: f.replace("fx_say_", "fx_say_same_") for t, f in SAY_FUNCS.items()}
TEXT_FUNCS = {INT: "fx_int_text", FLOAT: "fx_float_text", BOOL: "fx_bool_text"}
C_COMPARE = {"=": "==", "!=": "!=", "<": "<", ">": ">", "<=": "<=", ">=": ">="}
RENAME_SUFFIX = {TEXT: "_text", INT: "_number", FLOAT: "_decimal", BOOL: "_check"}

BASE_CASE_EXAMPLE = """\
function count_down(n)
    if n = 0
        return 0              # stops here, without calling itself
    end
    return count_down(n - 1)
end"""


def c_string(s):
    """Write a Python string as a C string literal (UTF-8, safely escaped)."""
    out = ['"']
    for b in s.encode("utf-8"):
        ch = chr(b)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "?":
            out.append("\\?")  # avoid C trigraphs
        elif 32 <= b < 127:
            out.append(ch)
        else:
            out.append(f"\\{b:03o}")
    out.append('"')
    return "".join(out)


def noun(t):
    """describe() without "a"/"an": "a whole number" -> "whole number"."""
    d = describe(t)
    for article in ("a ", "an "):
        if d.startswith(article):
            return d[len(article):]
    return d


def power_of_two(n):
    return n > 0 and n & (n - 1) == 0


def walk_statements(stmts):
    """Yield every statement, including ones nested inside blocks."""
    for s in stmts:
        yield s
        if isinstance(s, N.If):
            for _, body in s.branches:
                yield from walk_statements(body)
            if s.else_body:
                yield from walk_statements(s.else_body)
        elif isinstance(s, (N.RepeatTimes, N.RepeatWhile, N.RepeatForever,
                            N.RepeatRange, N.RepeatEach)):
            yield from walk_statements(s.body)


class Scope:
    """The main program, or one specialised copy of a function."""

    def __init__(self, name, body, func=None, arg_types=(), c_name="main"):
        self.name = name
        self.body = body
        self.func = func
        self.arg_types = arg_types
        self.c_name = c_name
        self.params = [p.name for p in func.params] if func else []
        self.vars = dict(zip(self.params, arg_types))
        self.first_set = {}
        if func:
            for p in func.params:
                self.first_set[p.name] = p
        self.assigned = set(self.params)
        for s in walk_statements(body):
            if isinstance(s, N.Set) and isinstance(s.target, N.Name):
                self.assigned.add(s.target.name)
                self.first_set.setdefault(s.target.name, s)
            elif isinstance(s, (N.RepeatRange, N.RepeatEach)):
                self.assigned.add(s.var.name)
                self.first_set.setdefault(s.var.name, s.var)
        # None means "gives back something we haven't worked out yet".
        self.ret = None if (func and func.returns_value) else VOID


class Compiler:
    def __init__(self, program, filename, source, program_dir=None):
        self.filename = filename
        self.source = source
        self.program_dir = program_dir  # the folder the program is in, for finding pictures
        self.lines = source.splitlines()
        self.funcs = {}
        for f in program.funcs:
            if f.name in BUILTINS:
                raise self.err(f, f"`{f.name}` is already a built-in Flaux function, so your function needs a different name.",
                               hint="Change the line to:",
                               fix_line=self.line_text(f.line).replace(f"function {f.name}", f"function my_{f.name}", 1))
            if f.name in self.funcs:
                first = self.funcs[f.name]
                raise self.err(f, f"There's already a function called `{f.name}`.",
                               hint="Each function needs its own name. Rename one of them (and the places "
                                    "that call it).",
                               see=(first.line, f"The first `{f.name}` is here:"))
            returns = [s for s in walk_statements(f.body) if isinstance(s, N.Return)]
            f.returns_value = any(r.value is not None for r in returns)
            if f.returns_value:
                giving = next(r for r in returns if r.value is not None)
                for r in returns:
                    if r.value is None:
                        raise self.err(r, "This `return` doesn't give back a value, but another `return` in "
                                          "the same function does.",
                                       hint="A function either always gives back a value or never does. "
                                            "Add a value here too:",
                                       fix=f"return {self.zero_example(giving.value)}",
                                       see=(giving.line, "The other `return` is here:"))
            self.funcs[f.name] = f
        self.shaders = {}       # name -> ShaderDef
        for st in program.main:
            if isinstance(st, N.ShaderDef):
                if st.name in BUILTINS or st.name in self.funcs:
                    raise self.err(st, f"`{st.name}` is already the name of a function, so your shader needs "
                                       f"a different name.",
                                   hint="Change the line to:", fix_line=f"shader my_{st.name}")
                if st.name in self.shaders:
                    raise self.err(st, f"There's already a shader called `{st.name}`.",
                                   hint="Each shader needs its own name.",
                                   see=(self.shaders[st.name].line, "The first one is here:"))
                self.shaders[st.name] = st
        self.shader_ids = {name: i for i, name in enumerate(self.shaders)}
        self.shader_uses = {}   # (name, kind) -> (glsl, outside names)
        self.main = Scope("main", program.main)
        self.specs = {}
        self.spec_count = 0
        self.emitting = False
        self.changed = False
        self.list_order = []
        self.list_set = set()
        self.uses_graphics = False
        self.uses_3d = False
        self.uses_xr = False     # in_vr() or the hands: look for a VR headset

    # ================================================================ driver

    def compile(self):
        """The whole program as C code."""
        main_code, func_codes = self.check()
        return self.assemble_c(main_code, func_codes)

    def check(self):
        """Work out every type and find every problem (raises FlauxError).
        Afterwards the compiler knows the type of everything, which the
        JavaScript backend (jsgen.py) uses too."""
        for _ in range(1000):
            self.changed = False
            self.walk(self.main)
            for spec in list(self.specs.values()):
                self.walk(spec)
            if not self.changed:
                break
        else:
            raise FlauxError("Flaux got stuck working out the types in this program. "
                             "This is a bug in Flaux, please report it.")

        # check shaders that are never used too (as either kind)
        for name, shader in self.shaders.items():
            if (name, "surface") in self.shader_uses or (name, "screen") in self.shader_uses:
                continue
            try:
                shader_glsl(shader, "surface", self)
            except FlauxError as first:
                try:
                    shader_glsl(shader, "screen", self)
                except FlauxError:
                    raise first

        self.emitting = True
        self.used = []
        self.used_set = set()
        main_code = self.walk(self.main)
        func_codes = []
        i = 0
        while i < len(self.used):  # walking a function can discover more functions
            func_codes.append(self.walk(self.used[i]))
            i += 1
        return main_code, func_codes

    def assemble_c(self, main_code, func_codes):
        prototypes = [self.signature(s) + ";" for s in self.used]
        list_defs = [self.list_def(t) for t in self.list_order]

        with open(RUNTIME_PATH, encoding="utf-8") as fh:
            runtime = fh.read()
        if self.uses_graphics:
            runtime += f"\n#define FX_PROGRAM_DIR {c_string(self.program_dir or '')}\n"
            if self.uses_3d:
                runtime += self.shader_tables()
            runtime += graphics.runtime_c(three_d=self.uses_3d, xr=self.uses_xr)
            main_code = main_code.replace("/*@FINISH@*/", "fx_g_finish();")
        else:
            main_code = main_code.replace("    /*@FINISH@*/\n", "")
        src_lines = [c_string(line.rstrip()) for line in self.lines] + ['""']
        # (whether the program says or asks anything: an exported game that doesn't needs no console window)
        says = any("fx_say" in code or "fx_ask(" in code for code in [main_code, *func_codes])
        out = [
            f"/* Generated by the Flaux compiler from {self.filename}. */",
            *(["#define FX_SAYS 1"] if says else []),
            runtime,
            "/* ---------------------------------------------------------- list types */",
            *list_defs,
            "/* ------------------------------------------ source (for error messages) */",
            "static const char *const fx_src[] = {\n    " + ",\n    ".join(src_lines) + "\n};",
            "/* ----------------------------------------------------------- functions */",
            *prototypes,
            "",
            *func_codes,
            "/* ---------------------------------------------------------------- main */",
            main_code,
            "",
        ]
        return "\n".join(out)

    def shader_tables(self):
        """The shaders, as GLSL for the graphics card, for the 3D runtime."""
        if not self.shaders:
            return ""
        names = list(self.shaders)
        surface = [self.shader_uses.get((n, "surface"), (None,))[0] for n in names]
        screen = [self.shader_uses.get((n, "screen"), (None,))[0] for n in names]
        def table(name, items):
            body = ",\n    ".join("NULL" if t is None else graphics.c_text(t) for t in items)
            return f"static const char *const {name}[FX_SHADERS + 1] = {{\n    {body},\n    NULL\n}};"
        return "\n".join([f"#define FX_SHADERS {len(names)}",
                          table("fx_shader_names", names),
                          table("fx_shader_surface", surface), table("fx_shader_screen", screen), ""])

    def walk(self, scope):
        self.scope = scope
        self.out = []
        self.depth = 1
        self.tmp_count = 0
        self.loops = 0
        for s in scope.body:
            self.stmt(s)
        if not self.emitting:
            return None

        decls = []
        for name, t in scope.vars.items():
            if t == UNK:
                node = scope.first_set.get(name)
                raise self.err(node, f"Flaux can't work out what kind of value `{name}` holds.",
                               hint="This happens when a variable only ever gets its value from a function "
                                    "that calls itself forever. Make sure the function has a way to stop:",
                               example=BASE_CASE_EXAMPLE)
            if name not in scope.params:
                decls.append(f"    {self.ctype(t)} v_{name} = {self.zero(t)};")
        if scope is self.main:
            head = "int main(void) {"
            init = [f"    fx_init({c_string(self.filename)}, fx_src, {len(self.lines)});"]
            tail = ["    return 0;", "}"]
            tail = ["    /*@FINISH@*/"] + tail  # filled in once we know if graphics are used
            return "\n".join([head] + decls + init + self.out + tail)
        kinds = ", ".join(describe(t) for t in scope.arg_types) or "no inputs"
        comment = f"/* {scope.name}: {kinds} */"
        tail = ["}"] if scope.ret == VOID else [f"    return {self.zero(scope.ret)};", "}"]
        return "\n".join([comment, self.signature(scope) + " {"] + decls + self.out + tail) + "\n"

    def signature(self, s):
        ret = "void" if s.ret == VOID else self.ctype(s.ret)
        params = ", ".join(f"{self.ctype(s.vars[p])} v_{p}" for p in s.params) or "void"
        return f"static {ret} {s.c_name}({params})"

    # ============================================================== teaching

    def err(self, node, msg, hint=None, **teach):
        if node is None:
            return FlauxError(msg, hint=hint, **teach)
        return FlauxError(msg, node.line, node.col, node.length, hint, **teach)

    def line_text(self, line):
        return self.lines[line - 1] if 1 <= line <= len(self.lines) else ""

    def indent_of(self, line):
        text = self.line_text(line)
        return text[: len(text) - len(text.lstrip())]

    def src(self, node):
        """The source text a node came from."""
        return self.line_text(node.line)[node.col - 1: node.col - 1 + node.length]

    def replaced(self, node, new_text, length=None):
        """The line `node` is on, with the node's text swapped for new_text."""
        line = self.line_text(node.line)
        n = node.length if length is None else length
        return line[: node.col - 1] + new_text + line[node.col - 1 + n:]

    def zero_example(self, node):
        if isinstance(node, N.Str):
            return '""'
        if isinstance(node, N.ListLit):
            return "[]"
        if isinstance(node, N.Bool):
            return "false"
        return "0"

    def made_here(self, name, scope=None):
        """A `see=` pointer to where a variable was first made."""
        node = (scope or self.scope).first_set.get(name)
        if node is None:
            return None
        return (node.line, f"`{name}` was made here:")

    # ================================================================ helpers

    def emit(self, line):
        self.out.append("    " * self.depth + line)

    def block(self, stmts):
        self.depth += 1
        for s in stmts:
            self.stmt(s)
        self.depth -= 1

    def loop_body(self, stmts):
        self.loops += 1
        self.block(stmts)
        self.loops -= 1

    def tmp(self):
        self.tmp_count += 1
        return f"_t{self.tmp_count}"

    def ctype(self, t):
        t = resolve(t)
        if t == INT:
            return "int64_t"
        if t == FLOAT:
            return "double"
        if t == BOOL:
            return "bool"
        if t == TEXT:
            return "fx_text"
        if t == VOID:
            return "void"
        if is_list(t):
            self.register(t)
            return self.lname(t) + "*"
        if not self.emitting:
            return "?"
        raise FlauxError(f"Internal error: no C type for {t!r}. This is a bug in Flaux.")

    def lname(self, t):
        if not is_list(t):
            return "?"
        self.register(t)
        return "fx_" + mangle(t)

    def register(self, t):
        if not self.emitting:
            return
        t = resolve(t)
        if t in self.list_set:
            return
        if is_list(t[1]):
            self.register(t[1])
        self.list_set.add(t)
        self.list_order.append(t)

    def list_def(self, t):
        name = self.lname(t)
        elem = resolve(t[1])
        ct = self.ctype(elem)
        if is_list(elem):
            txt = self.lname(elem) + "_text"
        else:
            txt = {**TEXT_FUNCS, TEXT: "fx_text_quote"}[elem]
        code = LIST_TEMPLATE.replace("@N", name).replace("@CT", ct).replace("@TXT", txt)
        if not is_list(elem):
            eq = "fx_text_eq(a, v)" if elem == TEXT else "a == v"
            code += HAS_TEMPLATE.replace("@N", name).replace("@CT", ct).replace("@EQ", eq)
        return code

    def zero(self, t):
        t = resolve(t)
        if t == INT:
            return "0"
        if t == FLOAT:
            return "0.0"
        if t == BOOL:
            return "false"
        if t == TEXT:
            return 'FX_LIT("")'
        if is_list(t):
            return f"{self.lname(t)}_new(0)"
        return "0"

    def conv(self, code, from_t, to_t):
        if to_t == FLOAT and from_t == INT:
            return f"((double)({code}))"
        return code

    def to_text(self, code, t):
        if t == TEXT:
            return code
        if t in TEXT_FUNCS:
            return f"{TEXT_FUNCS[t]}({code})"
        if is_list(t):
            return f"{self.lname(t)}_text({code})"
        return code

    def set_var(self, name, t, node, target=None):
        """Record that variable `name` now (also) holds type `t`."""
        old = self.scope.vars.get(name)
        try:
            new = merge(old, t)
        except TypeConflict:
            raise self.var_conflict(name, old, t, node, target)
        if new == UNK:
            return old
        if new != old:
            if self.emitting:
                raise FlauxError(f"Internal error: type of `{name}` changed late. This is a bug in Flaux.")
            self.scope.vars[name] = new
            self.changed = True
        return new

    def var_conflict(self, name, old, t, node, target):
        first = self.scope.first_set.get(name)
        see = None
        if first is not None and first.line != node.line:
            see = (first.line, f"`{name}` got {describe(old)} here:")
        if is_list(old) and is_list(t):
            return self.err(node, f"`{name}` is {describe(old)}, so it can't become {describe(t)}.",
                            hint="A list keeps the same kind of items for its whole life. Use a new name "
                                 "for the other list.", see=see)
        hint = (f"A variable keeps the same kind of value for the whole program. `{name}` holds "
                f"{describe(old)}, so give the {noun(t)} a name of its own.")
        if TEXT in (old, t) and (is_num(old) or is_num(t)):
            hint += " (To turn a number into text use text(...), and to turn text into a number use number(...).)"
        fix_line = None
        if isinstance(target, N.Name):
            fix_line = self.replaced(target, name + RENAME_SUFFIX.get(t, "_list" if is_list(t) else "_2"))
            hint += " For example:"
        return self.err(node, f"`{name}` holds {describe(old)}, so it can't hold {describe(t)} too.",
                        hint=hint, fix_line=fix_line, see=see)

    def refine(self, node, t):
        """Fill in what we learned about the list at `node` (e.g. its item type)."""
        if isinstance(node, N.Name):
            if self.scope.vars.get(node.name) is not None:
                self.set_var(node.name, t, node)
        elif isinstance(node, N.Index):
            self.refine(node.base, ListT(t))

    def no_void(self, node, t, what):
        if t != VOID:
            return
        if isinstance(node, N.Call) and what == "say":
            f = self.funcs.get(node.name)
            raise self.err(node, f"`{node.name}` doesn't give anything back, so there's nothing to say.",
                           hint=f"`{node.name}` does its job without giving back a value. If it already shows "
                                f"what you want, just call it on its own:",
                           fix_line=self.indent_of(node.line) + self.src(node),
                           see=(f.line, f"`{node.name}` is made here:") if f else None)
        if isinstance(node, N.Call):
            f = self.funcs.get(node.name)
            raise self.err(node, f"`{node.name}` doesn't give anything back, so there's nothing to {what}.",
                           hint=f"`{node.name}` does its job without giving back a value (it has no `return` "
                                f"with a value). Either call it on a line by itself, or make it give back a "
                                f"value with `return`, like this:",
                           example="function double(x)\n    return x * 2\nend\n\nsay double(5)",
                           see=(f.line, f"`{node.name}` is made here:") if f else None)
        raise self.err(node, f"This doesn't give anything back, so there's nothing to {what}.")

    def unknown_name(self, e):
        name = e.name
        line = e.line
        if name in ("True", "TRUE", "False", "FALSE"):
            return self.err(e, f"`{name}` should be written in lowercase in Flaux.", fix=name.lower())
        if name in ("None", "null", "nil", "NULL", "undefined", "nothing"):
            return self.err(e, f"Flaux doesn't have `{name}`.",
                            hint="Use a starting value of the right kind instead, like 0, \"\" or [].")
        if name in self.funcs or name in BUILTINS:
            f = self.funcs.get(name)
            inside = ", ".join(p.name for p in f.params) if f else "..."
            return self.err(e, f"`{name}` is a function. To use it, call it with round brackets.",
                            hint="Change the line to:", fix=f"{name}({inside})",
                            see=(f.line, f"`{name}` is made here:") if f else None)
        if self.scope is not self.main and name in self.main.assigned:
            fname = self.scope.name
            return self.err(e, f"Functions can't see `{name}`, because it was made outside the function.",
                            hint=f"Hand `{name}` to the function as an input. If the function changes it, "
                                 f"give back the new value with `return`, and set it when you call the "
                                 f"function, like this:",
                            example=f"function {fname}({name})\n    ...\n    return {name}\nend\n\n"
                                    f"set {name} to {fname}({name})",
                            see=self.made_here(name, self.main) and
                            (self.main.first_set[name].line, f"`{name}` was made here, outside the function:"))
        candidates = sorted(self.scope.assigned)
        close = difflib.get_close_matches(name, candidates, n=1, cutoff=0.6)
        if not close:
            lowered = {c.lower(): c for c in candidates}
            if name.lower() in lowered:
                close = [lowered[name.lower()]]
        if close:
            where = self.scope.first_set.get(close[0])
            said = f"You made `{close[0]}` on line {where.line}. " if where else ""
            return self.err(e, f"I don't know what `{name}` is.",
                            hint=f"{said}Did you mean that? (Names must match exactly, including capital "
                                 f"letters.) Change the line to:",
                            fix=close[0])
        return self.err(e, f"I don't know what `{name}` is.",
                        hint=f"A variable has to be made with `set` before it's used. Add a line above "
                             f"this one that gives `{name}` a starting value:",
                        fix_lines=[("+", self.indent_of(line) + f"set {name} to 0"),
                                   (line, self.line_text(line))])

    def what(self, target):
        if isinstance(target, N.Name):
            return f"`{target.name}`"
        return "this list item"

    # ============================================================ statements

    def stmt(self, s):
        getattr(self, "s_" + type(s).__name__)(s)

    def s_ShaderDef(self, s):
        pass   # shaders become GLSL where they're used (see paint_arg)

    def s_Say(self, s, same=False):
        funcs = SAY_SAME_FUNCS if same else SAY_FUNCS
        results = []
        for value in s.values:
            c, t = self.expr(value)
            self.no_void(value, t, "say")
            results.append((c, t))
        if any(t == UNK for _, t in results):
            return
        if not results:  # say() shows an empty line
            self.emit('fx_say_text(FX_LIT(""));')
            return
        if len(results) == 1:
            c, t = results[0]
            if is_list(t):
                self.emit(f"{funcs[TEXT]}({self.to_text(c, t)});")
            else:
                self.emit(f"{funcs[t]}({c});")
            return
        parts = []
        for i, (c, t) in enumerate(results):  # several things: put a space between them
            if i:
                parts.append('FX_LIT(" ")')
            parts.append(self.to_text(c, t))
        self.emit(f"{funcs[TEXT]}(fx_join((fx_text[]){{{', '.join(parts)}}}, {len(parts)}));")

    def s_SaySame(self, s):  # say.same: the same, without finishing the line
        self.s_Say(s, same=True)

    def s_Set(self, s):
        target = s.target
        if isinstance(target, N.Name):
            name = target.name
            c, t = self.expr(s.value, self.scope.vars.get(name))
            self.no_void(s.value, t, f"put in `{name}`")
            if t == UNK:
                return
            vt = self.set_var(name, t, s.value, target)
            if isinstance(s.value, N.Name) and s.value.name != name:
                other = self.scope.vars.get(s.value.name)
                if other is not None and has_unknown(other) and other != vt:
                    self.set_var(s.value.name, vt, s.value)
            self.emit(f"v_{name} = {self.conv(c, t, vt)};")
            return
        bc, bt, ic, elem = self.item_target(target)
        vc, vt = self.expr(s.value, None if elem == UNK else elem)
        self.no_void(s.value, vt, "put in the list")
        if bt == UNK or vt == UNK:
            return
        final = self.store_into(target, bt, vt, s.value)
        self.emit(f"*{self.lname(bt)}_at({bc}, {ic}, {target.line}) = {self.conv(vc, vt, final)};")

    def item_target(self, target):
        bc, bt = self.expr(target.base)
        ic, it = self.expr(target.index)
        if bt == TEXT:
            raise self.err(target, "Text can't be changed one letter at a time.",
                           hint="Build new text instead. You can join pieces together with +, and "
                                "go through text letter by letter with `repeat with`:",
                           example="set shout to \"\"\nrepeat with letter in word\n"
                                   "    set shout to shout + uppercase(letter)\nend")
        if bt != UNK and not is_list(bt):
            raise self.err(target.base, f"Only lists can use [ ], but this is {describe(bt)}.",
                           hint="Square brackets pick an item out of a list, like nums[1].",
                           see=self.made_here(target.base.name) if isinstance(target.base, N.Name) else None)
        self.check_index(target.index, it)
        return bc, bt, ic, (bt[1] if is_list(bt) else UNK)

    def check_index(self, node, t):
        if t in (INT, UNK):
            return
        if t == FLOAT:
            raise self.err(node, "The position inside [ ] must be a whole number, but this is a decimal number.",
                           hint="Item positions are 1, 2, 3 and so on. Round the decimal to a whole number:",
                           fix=f"round({self.src(node)})")
        raise self.err(node, f"The position inside [ ] must be a whole number, but this is {describe(t)}.",
                       hint="Item positions are 1, 2, 3 and so on, like nums[1] for the first item.")

    def store_into(self, target, bt, vt, value_node):
        """Check a value can go into a list; returns the list's item type."""
        elem = bt[1]
        if elem == FLOAT and vt == INT:
            return FLOAT
        try:
            merged = merge(elem, vt, widen=False)
        except TypeConflict:
            hint = "A list keeps the same kind of item for its whole life."
            if elem == INT and vt == FLOAT:
                hint += " To store decimals, start the list with decimals, like [0.0, 1.0] instead of [0, 1]."
            raise self.err(value_node, f"This list holds {describe(elem, plural=True)}, so it can't hold {describe(vt)}.",
                           hint=hint,
                           see=self.made_here(target.base.name) if isinstance(target.base, N.Name) else None)
        if merged != elem:
            self.refine(target.base, ListT(merged))
        return merged

    def target_value(self, target, starter="0"):
        """Code and type for the current value of an add/subtract target."""
        if isinstance(target, N.Name):
            name = target.name
            if name not in self.scope.assigned:
                if self.scope is not self.main and name in self.main.assigned:
                    raise self.unknown_name(target)
                line = target.line
                raise self.err(target, f"`{name}` needs a starting value before you can change it.",
                               hint=f"Make `{name}` with `set` first. Add a line above this one:",
                               fix_lines=[("+", self.indent_of(line) + f"set {name} to {starter}"),
                                          (line, self.line_text(line))])
            return f"v_{name}", self.scope.vars.get(name) or UNK
        bc, bt, ic, elem = self.item_target(target)
        if bt == UNK:
            return "", UNK
        return f"(*{self.lname(bt)}_at({bc}, {ic}, {target.line}))", elem

    def update_with(self, target, tc, elem_t, make):
        """Emit `target = make(current)` without evaluating the target twice."""
        if isinstance(target, N.Name):
            self.emit(f"{tc} = {make(tc)};")
        else:
            p = self.tmp()
            self.emit(f"{{ {self.ctype(elem_t)} *{p} = &{tc}; *{p} = {make('*' + p)}; }}")

    def s_Add(self, s):
        target = s.target
        starter = "[]" if isinstance(s.value, (N.ListLit,)) else '""' if isinstance(s.value, N.Str) else "0"
        tc, tt = self.target_value(target, starter)
        hint = (tt[1] if is_list(tt) else tt) if tt != UNK else None
        vc, vt = self.expr(s.value, hint)
        self.no_void(s.value, vt, "add")
        if tt == UNK or vt == UNK:
            return
        if is_num(tt):
            if not is_num(vt):
                fix = None
                h = f"{self.what(target)} holds {describe(tt)}, so only numbers can be added to it."
                if isinstance(s.value, N.Str) and len(s.value.parts) == 1 and \
                        s.value.parts[0].strip().replace(".", "", 1).lstrip("-").isdigit():
                    h += " This number is inside quotes, which makes it text. Take the quotes away:"
                    fix = s.value.parts[0].strip()
                elif vt == TEXT:
                    h += " If the text holds a number, turn it into a number with number(...):"
                    fix = f"number({self.src(s.value)})"
                raise self.err(s.value, f"You can't add {describe(vt)} to {describe(tt)}.", hint=h, fix=fix,
                               see=self.made_here(target.name) if isinstance(target, N.Name) else None)
            if isinstance(target, N.Name):
                tt = self.set_var(target.name, vt, s.value)
            elif tt == INT and vt == FLOAT:
                raise self.err(s.value, "This list holds whole numbers, so you can't add a decimal to its items.",
                               hint="To store decimals, start the list with decimals, like [0.0, 1.0].")
            self.emit(f"{tc} += {self.conv(vc, vt, tt)};")
        elif tt == TEXT:
            self.update_with(target, tc, TEXT, lambda cur: f"fx_concat({cur}, {self.to_text(vc, vt)})")
        elif is_list(tt):
            elem = tt[1]
            if vt == tt and elem != UNK:
                self.emit(f"{self.lname(tt)}_extend({tc}, {vc});")
                return
            if elem == FLOAT and vt == INT:
                final = FLOAT
            else:
                try:
                    final = merge(elem, vt, widen=False)
                except TypeConflict:
                    hint = "A list keeps the same kind of item for its whole life."
                    if elem == INT and vt == FLOAT:
                        hint += " To store decimals, start the list with decimals, like [0.0] instead of [0]."
                    elif elem == INT and vt == TEXT:
                        hint += " To keep text, make a second list for it, like `set names to []`."
                    what = self.what(target)
                    raise self.err(s.value, f"{what[0].upper() + what[1:]} is a list of {describe(elem, plural=True)}, "
                                            f"so you can't add {describe(vt)} to it.",
                                   hint=hint,
                                   see=self.made_here(target.name) if isinstance(target, N.Name) else None)
                if final != elem:
                    self.refine(target, ListT(final))
            self.emit(f"{self.lname(tt)}_push({tc}, {self.conv(vc, vt, final)});")
        else:
            raise self.err(target, f"You can't add to {describe(tt)}.",
                           hint="`add` works on numbers, text and lists.")

    def s_Subtract(self, s):
        target = s.target
        tc, tt = self.target_value(target)
        vc, vt = self.expr(s.value, None if tt == UNK else tt)
        self.no_void(s.value, vt, "subtract")
        if tt == UNK or vt == UNK:
            return
        if not is_num(tt) or not is_num(vt):
            bad = target if not is_num(tt) else s.value
            hint = "`subtract` takes a number away from a number."
            if is_list(tt):
                hint = "Taking items out of a list isn't in Flaux yet. You could build a new list with only the items you want to keep."
            raise self.err(bad, f"`subtract` only works with numbers, but this is {describe(tt if bad is target else vt)}.",
                           hint=hint)
        if isinstance(target, N.Name):
            tt = self.set_var(target.name, vt, s.value)
        elif tt == INT and vt == FLOAT:
            raise self.err(s.value, "This list holds whole numbers, so you can't subtract a decimal from its items.",
                           hint="To store decimals, start the list with decimals, like [0.0, 1.0].")
        self.emit(f"{tc} -= {self.conv(vc, vt, tt)};")

    def check_bool(self, node, t, what):
        if t in (BOOL, UNK):
            return
        text = self.src(node)
        if is_num(t):
            fix, meaning = f"{text} != 0", "is not zero"
        elif t == TEXT:
            fix, meaning = f'{text} != ""', "is not empty"
        elif is_list(t):
            fix, meaning = f"length of {text} > 0", "has items in it"
        else:
            fix, meaning = None, None
        hint = f"`{what}` needs a check that's true or false, like `score > 10` or `name = \"Sam\"`."
        if fix:
            hint += f" If you meant \"{what} {text} {meaning}\", write:"
        raise self.err(node, f"`{what}` needs a true-or-false check, but this is {describe(t)}.",
                       hint=hint, fix=fix)

    def cheap_pure(self, e, depth=0):
        """True for small whole-number sums that can't go wrong (no dividing
        by a variable, no lists, no functions), so working them out when
        they're not needed is harmless."""
        if depth > 4:
            return False
        if isinstance(e, N.Num):
            return not e.is_float
        if isinstance(e, N.Name):
            return self.scope.vars.get(e.name) in (INT, BOOL)
        if isinstance(e, N.Unary):
            return self.cheap_pure(e.operand, depth + 1)
        if isinstance(e, N.Binary):
            if e.op in ("+", "-", "*", "<", ">", "<=", ">=", "=", "!=", "and", "or"):
                return self.cheap_pure(e.left, depth + 1) and self.cheap_pure(e.right, depth + 1)
            if e.op in ("//", "mod"):
                right = e.right
                return (isinstance(right, N.Num) and not right.is_float and power_of_two(right.value)
                        and self.cheap_pure(e.left, depth + 1))
        return False

    def branch_free_if(self, s):
        """`if c / set v to a / else / set v to b / end`, for whole numbers,
        without a jump: both are worked out and the right one is kept. The
        processor never has to guess which way the `if` goes, which can make
        loops like the Collatz one much faster."""
        if len(s.branches) != 1 or not s.else_body or len(s.else_body) != 1 or len(s.branches[0][1]) != 1:
            return False
        cond, (then,) = s.branches[0]
        (other,) = s.else_body
        if not (isinstance(then, N.Set) and isinstance(other, N.Set) and isinstance(then.target, N.Name)
                and isinstance(other.target, N.Name) and then.target.name == other.target.name):
            return False
        name = then.target.name
        if self.scope.vars.get(name) != INT or not all(self.cheap_pure(x) for x in (cond, then.value, other.value)):
            return False
        cc, ct = self.expr(cond)
        ac, at = self.expr(then.value)
        bc, bt = self.expr(other.value)
        if ct != BOOL or at != INT or bt != INT:
            return False
        a, b, m = self.tmp(), self.tmp(), self.tmp()
        self.emit(f"{{ int64_t {a} = {ac}, {b} = {bc}, {m} = -(int64_t)({cc}); "
                  f"v_{name} = ({a} & {m}) | ({b} & ~{m}); }}")
        return True

    def s_If(self, s):
        if self.emitting and self.branch_free_if(s):
            return
        for i, (cond, body) in enumerate(s.branches):
            cc, ct = self.expr(cond)
            self.check_bool(cond, ct, "if" if i == 0 else "else if")
            self.emit(f"if ({cc}) {{" if i == 0 else f"}} else if ({cc}) {{")
            self.block(body)
        if s.else_body is not None:
            self.emit("} else {")
            self.block(s.else_body)
        self.emit("}")

    def s_RepeatTimes(self, s):
        cc, ct = self.expr(s.count)
        if ct == FLOAT:
            raise self.err(s.count, "`repeat ... times` needs a whole number, but this is a decimal number.",
                           hint="You can't repeat something part of a time. Round it to a whole number:",
                           fix=f"round({self.src(s.count)})")
        if ct not in (INT, UNK):
            hint = "Turn the text into a whole number first:" if ct == TEXT else None
            raise self.err(s.count, f"`repeat ... times` needs a whole number, but this is {describe(ct)}.",
                           hint=hint, fix=f"whole({self.src(s.count)})" if ct == TEXT else None,
                           example=None if ct == TEXT else "repeat 3 times\n    say \"hi\"\nend")
        i, n = self.tmp(), self.tmp()
        self.emit(f"for (int64_t {i} = 0, {n} = {cc}; {i} < {n}; {i}++) {{")
        self.loop_body(s.body)
        self.emit("}")

    def s_RepeatWhile(self, s):
        cc, ct = self.expr(s.cond)
        self.check_bool(s.cond, ct, "repeat until" if s.until else "repeat while")
        self.emit(f"while (!({cc})) {{" if s.until else f"while ({cc}) {{")
        self.loop_body(s.body)
        self.emit("}")

    def s_RepeatForever(self, s):
        self.emit("for (;;) {")
        self.loop_body(s.body)
        self.emit("}")

    def s_RepeatRange(self, s):
        sc, st = self.expr(s.start)
        ec, et = self.expr(s.end)
        pc, pt = self.expr(s.step) if s.step is not None else ("1", INT)
        vt = UNK
        if UNK not in (st, et, pt):
            for node, t in ((s.start, st), (s.end, et), (s.step, pt)):
                if node is not None and not is_num(t):
                    raise self.err(node, f"Counting needs numbers, but this is {describe(t)}.",
                                   hint="To go through each item of a list or each letter of text, use "
                                        "`in` instead of `from ... to`:" if is_list(t) or t == TEXT else None,
                                   example="repeat with item in my_list\n    say item\nend"
                                   if is_list(t) or t == TEXT else "repeat with i from 1 to 10\n    say i\nend")
            vt = self.set_var(s.var.name, merge(merge(st, et), pt), s.var)
        sign = 1
        if s.step is not None:
            if isinstance(s.step, N.Num):
                if s.step.value == 0:
                    raise self.err(s.step, "Counting by 0 would never finish.",
                                   hint="Count up with a positive step or down with a negative one:",
                                   fix="1")
                sign = 1 if s.step.value > 0 else -1
            else:
                sign = None
        v = f"v_{s.var.name}"
        ct = self.ctype(vt)
        end, step = self.tmp(), self.tmp()
        self.emit("{")
        self.depth += 1
        self.emit(f"{ct} {end} = {self.conv(ec, et, vt)};")
        if s.step is None:
            step = "1"
        else:
            self.emit(f"{ct} {step} = {self.conv(pc, pt, vt)};")
        if sign is None:
            self.emit(f'if ({step} == 0) fx_fail({s.step.line}, "Make sure the number after `by` is never 0.", '
                      f'"Counting by 0 would never finish.");')
            cond = f"({step} > 0 ? {v} <= {end} : {v} >= {end})"
        else:
            cond = f"{v} {'<=' if sign > 0 else '>='} {end}"
        self.emit(f"for ({v} = {self.conv(sc, st, vt)}; {cond}; {v} += {step}) {{")
        self.loop_body(s.body)
        self.emit("}")
        self.depth -= 1
        self.emit("}")

    def s_RepeatEach(self, s):
        ic, it = self.expr(s.iterable)
        name = s.var.name
        if it != UNK:
            if is_list(it):
                if it[1] != UNK:
                    self.set_var(name, it[1], s.var)
            elif it == TEXT:
                self.set_var(name, TEXT, s.var)
            else:
                count = is_num(it)
                raise self.err(s.iterable, f"`repeat with {name} in ...` goes through a list or text, "
                                           f"but this is {describe(it)}.",
                               hint="To count up to a number, use `from ... to` instead:" if count else None,
                               fix=f"from 1 to {self.src(s.iterable)}" if count else None,
                               length=None)
        lst, k = self.tmp(), self.tmp()
        var_t = self.scope.vars.get(name)
        self.emit("{")
        self.depth += 1
        assign = None
        if is_list(it):
            self.emit(f"{self.ctype(it)} {lst} = {ic};")
            self.emit(f"for (int64_t {k} = 0; {k} < {lst}->len; {k}++) {{")
            if var_t is not None:
                assign = f"v_{name} = {self.conv(f'{lst}->items[{k}]', it[1], var_t)};"
        elif it == TEXT:
            self.emit(f"fx_text {lst} = {ic};")
            self.emit(f"for (int64_t {k} = 0; {k} < {lst}.n; {k}++) {{")
            assign = f"v_{name} = fx_byte_text((unsigned char){lst}.p[{k}]);"
        else:
            self.emit("for (;;) {")
        if assign:
            self.depth += 1
            self.emit(assign)
            self.depth -= 1
        self.loop_body(s.body)
        self.emit("}")
        self.depth -= 1
        self.emit("}")

    WAIT_SECONDS = {"s": 1, "m": 60, "h": 3600, "d": 86400}
    WAIT_WORDS = {"s": "seconds", "m": "minutes", "h": "hours", "d": "days"}

    def s_Wait(self, s):
        c, t = self.expr(s.amount)
        if t == UNK:
            return
        if not is_num(t):
            text = self.src(s.amount)
            raise self.err(s.amount, f"`wait` needs a number of {self.WAIT_WORDS[s.unit]}, but this is {describe(t)}.",
                           hint="If the text holds a number, turn it into a number first:" if t == TEXT else
                                "Put a number here, like this:",
                           fix=f"number({text})" if t == TEXT else None,
                           example=None if t == TEXT else "wait 2s\nwait delay seconds")
        if isinstance(s.amount, N.Num) and s.amount.value < 0:
            raise self.err(s.amount, "You can't wait for a negative amount of time.",
                           hint="Waiting goes forwards only. Use a number that's 0 or more:",
                           fix=self.src(s.amount).lstrip("-").lstrip() or "1")
        self.emit(f"fx_wait({self.conv(c, t, FLOAT)}, {self.WAIT_SECONDS[s.unit]}, {s.line});")

    def s_Stop(self, s):
        if self.loops == 0:
            hint = "`stop` ends the `repeat` it's inside."
            if self.scope is not self.main:
                hint += " To leave a function early, use `return` instead:"
                raise self.err(s, "`stop` only works inside a `repeat`.", hint=hint, fix="return")
            raise self.err(s, "`stop` only works inside a `repeat`.", hint=hint,
                           example="repeat forever\n    set answer to ask(\"Again? \")\n"
                                   "    if answer = \"no\"\n        stop\n    end\nend")
        self.emit("break;")

    def s_Skip(self, s):
        if self.loops == 0:
            raise self.err(s, "`skip` only works inside a `repeat`.",
                           hint="`skip` jumps straight to the next time around the loop:",
                           example="repeat with i from 1 to 10\n    if i mod 2 = 0\n        skip      "
                                   "# leave out even numbers\n    end\n    say i\nend")
        self.emit("continue;")

    def s_Return(self, s):
        if self.scope is self.main:
            raise self.err(s, "`return` only works inside a function.",
                           hint="`return` gives back a value from a function to whoever called it:",
                           example="function double(x)\n    return x * 2\nend\n\nsay double(4)    # 8")
        if s.value is None:
            self.emit("return;")
            return
        ret = self.scope.ret
        c, t = self.expr(s.value, ret)
        self.no_void(s.value, t, "give back")
        if t == UNK:
            return
        try:
            new = merge(ret, t)
        except TypeConflict:
            hint = "A function always gives back the same kind of value, wherever it returns."
            if TEXT in (ret, t):
                hint += " To give back a number as text, use text(...):"
            raise self.err(s.value, f"This function gives back {describe(ret)} somewhere else, so it can't "
                                    f"give back {describe(t)} here.",
                           hint=hint, fix=f"text({self.src(s.value)})" if t != TEXT and ret == TEXT else None)
        if new != ret:
            if self.emitting:
                raise FlauxError("Internal error: return type changed late. This is a bug in Flaux.")
            self.scope.ret = new
            self.changed = True
        self.emit(f"return {self.conv(c, t, new)};")

    def s_ExprStmt(self, s):
        c, t = self.expr(s.expr)
        if c:
            self.emit(f"{c};")

    # =========================================================== expressions

    def expr(self, e, hint=None):
        c, t = getattr(self, "x_" + type(e).__name__)(e, hint)
        if self.emitting and t == UNK:
            raise self.err(e, "Flaux can't work out what kind of value this is.",
                           hint="This happens when a function only ever gives back the result of calling "
                                "itself, so it would never stop. Give it a `return` that doesn't call itself:",
                           example=BASE_CASE_EXAMPLE)
        return c, t

    def x_Num(self, e, hint):
        if e.is_float:
            r = repr(e.value)
            if "inf" in r or "nan" in r:
                raise self.err(e, "This number is too big, even for a decimal.")
            return r, FLOAT
        return f"{e.value}LL", INT

    def x_Bool(self, e, hint):
        return ("true" if e.value else "false"), BOOL

    def x_Str(self, e, hint):
        codes = []
        unknown = False
        for part in e.parts:
            if isinstance(part, str):
                codes.append(f"FX_LIT({c_string(part)})")
            else:
                c, t = self.expr(part)
                self.no_void(part, t, "put in the text")
                if t == UNK:
                    unknown = True
                else:
                    codes.append(self.to_text(c, t))
        if unknown:
            return "", TEXT
        if len(codes) == 1:
            return codes[0], TEXT
        return f"fx_join((fx_text[]){{{', '.join(codes)}}}, {len(codes)})", TEXT

    def x_Name(self, e, hint):
        t = self.scope.vars.get(e.name)
        if t is not None:
            return f"v_{e.name}", t
        if e.name in self.scope.assigned:
            return "", UNK
        if e.name in self.shaders:
            if hint == SHADER:
                return str(self.shader_ids[e.name]), SHADER
            raise self.err(e, f"`{e.name}` is a shader. Shaders paint shapes, or the whole screen.",
                           hint="Give it to a shape in place of its colour, or to effect(...):",
                           example=f"cube(0, 0, 0, 1, {e.name})\neffect({e.name})",
                           see=(self.shaders[e.name].line, f"`{e.name}` is made here:"))
        raise self.unknown_name(e)

    def x_ListLit(self, e, hint):
        hint_elem = hint[1] if is_list(hint) and hint[1] != UNK else None
        results = []
        for item in e.items:
            c, t = self.expr(item, hint_elem)
            self.no_void(item, t, "put in a list")
            results.append((c, t))
        if not e.items:
            lt = hint if is_list(hint) else ListT(UNK)
            return f"{self.lname(lt)}_new(0)", lt
        if any(t == UNK for _, t in results):
            return "", UNK
        elem = UNK
        for item, (_, t) in zip(e.items, results):
            try:
                elem = merge(elem, t)
            except TypeConflict:
                hint_text = "A list holds one kind of item."
                if TEXT in (elem, t) and (is_num(elem) or is_num(t)):
                    hint_text += " To keep numbers and text together, put the numbers in quotes too, " \
                                 "or use two separate lists."
                raise self.err(item, f"All the items in a list must be the same kind. This is {describe(t)}, "
                                     f"but the items before it are {describe(elem, plural=True)}.",
                               hint=hint_text)
        if hint_elem == FLOAT and elem == INT:
            elem = FLOAT
        lt = ListT(elem)
        if self.emitting:  # regenerate items now that we know the item type
            results = [self.expr(item, elem) for item in e.items]
        codes = [self.conv(c, t, elem) for c, t in results]
        return f"{self.lname(lt)}_of({len(codes)}, ({self.ctype(elem)}[]){{{', '.join(codes)}}})", lt

    def x_Copies(self, e, hint):
        cc, ct = self.expr(e.count)
        vhint = hint[1] if is_list(hint) and hint[1] != UNK else None
        vc, vt = self.expr(e.value, vhint)
        self.no_void(e.value, vt, "copy")
        if ct not in (INT, UNK):
            raise self.err(e.count, f"The number of copies must be a whole number, but this is {describe(ct)}.",
                           example="set board to 9 copies of \" \"\nset scores to 4 copies of 0")
        if vt == UNK:
            return "", UNK
        elem = FLOAT if (vhint == FLOAT and vt == INT) else vt
        lt = ListT(elem)
        name = self.lname(lt)
        if is_list(elem):  # each copy gets its own fresh list
            n, lst, k = self.tmp(), self.tmp(), self.tmp()
            return (f"({{ int64_t {n} = {cc}; {name} *{lst} = {name}_new({n}); "
                    f"for (int64_t {k} = 0; {k} < {n}; {k}++) {name}_push({lst}, {vc}); {lst}; }})"), lt
        return f"{name}_fill({cc}, {self.conv(vc, vt, elem)}, {e.line})", lt

    def x_Unary(self, e, hint):
        c, t = self.expr(e.operand, hint if e.op == "-" else None)
        self.no_void(e.operand, t, "use")
        if e.op == "not":
            if t not in (BOOL, UNK):
                raise self.err(e.operand, f"`not` flips a true-or-false check, but this is {describe(t)}.",
                               hint="Use `not` in front of a check, like `not (score > 10)` or `not finished`.")
            return f"(!{c})", BOOL
        if t == UNK:
            return "", UNK
        if not is_num(t):
            raise self.err(e.operand, f"Only numbers can be made negative, but this is {describe(t)}.")
        return f"(-{c})", t

    def x_Length(self, e, hint):
        c, t = self.expr(e.operand)
        return self.length_of(e.operand, c, t)

    def length_of(self, node, c, t):
        self.no_void(node, t, "measure")
        if t == UNK:
            return "", INT
        if is_list(t):
            return f"({c})->len", INT
        if t == TEXT:
            return f"({c}).n", INT
        hint = None
        if is_num(t):
            hint = "To count the digits of a number, turn it into text first:"
        raise self.err(node, f"Only lists and text have a length, but this is {describe(t)}.",
                       hint=hint, fix=f"text({self.src(node)})" if is_num(t) else None)

    def x_Index(self, e, hint):
        bc, bt = self.expr(e.base)
        ic, it = self.expr(e.index)
        self.no_void(e.base, bt, "look inside")
        self.check_index(e.index, it)
        if bt == UNK:
            return "", UNK
        if is_list(bt):
            return f"{self.lname(bt)}_get({bc}, {ic}, {e.line})", bt[1]
        if bt == TEXT:
            return f"fx_text_get({bc}, {ic}, {e.line})", TEXT
        raise self.err(e.base, f"Only lists and text can use [ ], but this is {describe(bt)}.",
                       hint="Square brackets pick out one item of a list (like nums[1]) or one letter of "
                            "text (like name[1]).",
                       see=self.made_here(e.base.name) if isinstance(e.base, N.Name) else None)

    def x_Binary(self, e, hint):
        op = e.op
        lc, lt = self.expr(e.left)
        rc, rt = self.expr(e.right)
        self.no_void(e.left, lt, "use")
        self.no_void(e.right, rt, "use")
        if op in ("and", "or"):
            return self.logic(e, op, lc, lt, rc, rt)
        if op in C_COMPARE:
            return self.compare(e, op, lc, lt, rc, rt)
        if lt == UNK or rt == UNK:
            if op == "+" and TEXT in (lt, rt):
                return "", TEXT
            if op == "/":
                return "", FLOAT
            return "", UNK
        if op == "+":
            if lt == TEXT or rt == TEXT:
                return f"fx_concat({self.to_text(lc, lt)}, {self.to_text(rc, rt)})", TEXT
            if is_list(lt) and is_list(rt):
                try:
                    t = merge(lt, rt, widen=False)
                except TypeConflict:
                    raise self.err(e, f"You can't join {describe(lt)} and {describe(rt)}.",
                                   hint="A list holds one kind of item, so both lists need the same kind.")
                return f"{self.lname(t)}_cat({lc}, {rc})", t
            if is_list(lt) and not is_list(rt):
                raise self.err(e, f"You can't use + to put {describe(rt)} on the end of a list.",
                               hint="To put an item in a list, use `add`. Or put the item in square "
                                    "brackets to join two lists:",
                               fix=f"{self.src(e.left)} + [{self.src(e.right)}]")
        if is_num(lt) and is_num(rt):
            return self.arithmetic(e, op, lc, lt, rc, rt)
        raise self.math_error(e, op, lt, rt)

    def arithmetic(self, e, op, lc, lt, rc, rt):
        res = INT if lt == rt == INT else FLOAT
        if op in ("+", "-", "*"):
            return f"({self.conv(lc, lt, res)} {op} {self.conv(rc, rt, res)})", res
        if op == "/":
            return f"fx_fdiv({self.conv(lc, lt, FLOAT)}, {self.conv(rc, rt, FLOAT)}, {e.line})", FLOAT
        # Whole-number division and mod by a positive number we can see,
        # like `n // 2` or `n mod 10`, get faster code (no zero check).
        const = e.right.value if isinstance(e.right, N.Num) and not e.right.is_float else None
        if op == "//":
            if res == INT:
                if const is not None and power_of_two(const):
                    return f"({lc} >> {const.bit_length() - 1})", INT
                if const is not None and const > 0:
                    return f"fx_idiv_pos({lc}, {rc})", INT
                return f"fx_idiv({lc}, {rc}, {e.line})", INT
            return f"fx_ffloordiv({self.conv(lc, lt, FLOAT)}, {self.conv(rc, rt, FLOAT)}, {e.line})", FLOAT
        if op == "mod":
            if res == INT:
                if const is not None and power_of_two(const):
                    return f"({lc} & {const - 1}LL)", INT
                if const is not None and const > 0:
                    return f"fx_mod_pos({lc}, {rc})", INT
                return f"fx_mod({lc}, {rc}, {e.line})", INT
            return f"fx_fmod({self.conv(lc, lt, FLOAT)}, {self.conv(rc, rt, FLOAT)}, {e.line})", FLOAT
        if op == "^":
            if res == INT:
                return f"fx_ipow({lc}, {rc}, {e.line})", INT
            return f"pow({self.conv(lc, lt, FLOAT)}, {self.conv(rc, rt, FLOAT)})", FLOAT
        raise FlauxError(f"Internal error: unknown operator {op}. This is a bug in Flaux.")

    def math_error(self, e, op, lt, rt):
        msg = f"You can't use `{op}` with {describe(lt)} and {describe(rt)}."
        if TEXT in (lt, rt) and (is_num(lt) or is_num(rt)) and op != "+":
            side = e.left if lt == TEXT else e.right
            return self.err(e, msg, hint="Maths only works on numbers. If the text holds a number "
                                         "(like \"5\"), turn it into a number first:",
                            fix_line=self.replaced(side, f"number({self.src(side)})"))
        if BOOL in (lt, rt):
            return self.err(e, msg, hint="true and false aren't numbers, so you can't do maths with them.")
        if is_list(lt) or is_list(rt):
            return self.err(e, msg, hint="To do maths on every item of a list, go through it with "
                                         "`repeat with item in my_list`.")
        return self.err(e, msg)

    def logic(self, e, op, lc, lt, rc, rt):
        for side, t in ((e.left, lt), (e.right, rt)):
            if t in (BOOL, UNK):
                continue
            # Common slip: `if name = "a" or "b"` should be `name = "a" or name = "b"`.
            if side is e.right and isinstance(e.left, N.Binary) and e.left.op in C_COMPARE:
                left_cmp = e.left
                fixed = f"{self.src(left_cmp)} {op} {self.src(left_cmp.left)} {self.cmp_word(left_cmp)} {self.src(side)}"
                raise self.err(side, f"Each side of `{op}` must be a complete check, but this is just {describe(t)}.",
                               hint="Computers need the whole comparison repeated on each side. Change it to:",
                               fix_line=self.replaced(e, fixed))
            raise self.err(side, f"Each side of `{op}` must be a true-or-false check, but this is {describe(t)}.",
                           hint=f"Join two complete checks with `{op}`, like this:",
                           example="if age > 12 and age < 20\n    say \"teenager\"\nend")
        return f"({lc} {'&&' if op == 'and' else '||'} {rc})", BOOL

    def cmp_word(self, node):
        """The comparison symbol as the user wrote it (e.g. `=` or `is`)."""
        between = self.line_text(node.line)[node.left.col - 1 + node.left.length: node.right.col - 1].strip()
        return between or C_COMPARE[node.op]

    def compare(self, e, op, lc, lt, rc, rt):
        if lt == UNK or rt == UNK:
            return "", BOOL
        cop = C_COMPARE[op]
        if is_num(lt) and is_num(rt):
            return f"({lc} {cop} {rc})", BOOL
        if lt == rt == TEXT:
            if op == "=":
                return f"fx_text_eq({lc}, {rc})", BOOL
            if op == "!=":
                return f"(!fx_text_eq({lc}, {rc}))", BOOL
            return f"(fx_text_cmp({lc}, {rc}) {cop} 0)", BOOL
        if lt == rt == BOOL and op in ("=", "!="):
            return f"({lc} {cop} {rc})", BOOL
        if is_list(lt) and is_list(rt):
            raise self.err(e, "Flaux can't compare whole lists yet.",
                           hint="Compare their lengths with `length of`, or go through the items with "
                                "`repeat with` and compare them one at a time.")
        msg = f"You can't compare {describe(lt)} with {describe(rt)}."
        if TEXT in (lt, rt) and (is_num(lt) or is_num(rt)):
            side = e.left if lt == TEXT else e.right
            if isinstance(side, N.Str) and len(side.parts) == 1 and \
                    side.parts[0].strip().replace(".", "", 1).lstrip("-").isdigit():
                raise self.err(e, msg, hint="The number in quotes is text, not a number. Take the quotes away:",
                               fix_line=self.replaced(side, side.parts[0].strip()))
            raise self.err(e, msg, hint="Text and numbers are different things, even if the text looks like a "
                                        "number. Turn the text into a number first:",
                           fix_line=self.replaced(side, f"number({self.src(side)})"))
        raise self.err(e, msg)

    # ----------------------------------------------------------------- calls

    def x_Call(self, e, hint):
        f = self.funcs.get(e.name)
        if f is not None:
            return self.call_user(e, f)
        if e.name in BUILTINS:
            paint_at = SHADER_INPUT.get(e.name)
            args = [self.expr(a, SHADER if i == paint_at else None) for i, a in enumerate(e.args)]
            for a, (_, t) in zip(e.args, args):
                self.no_void(a, t, "use")
            return getattr(self, "b_" + e.name)(e, args)
        raise self.unknown_function(e)

    def rename_call(self, e, new):
        return self.replaced(e, new, length=len(e.name))

    def unknown_function(self, e):
        name = e.name
        if e.name in self.scope.vars or e.name in self.scope.assigned:
            inside = ", ".join(self.src(a) for a in e.args)
            return self.err(e, f"`{name}` is a variable, not a function, so it can't be used with round brackets.",
                            hint="To get an item out of a list, use square brackets:",
                            fix=f"{name}[{inside}]", see=self.made_here(name))
        alias = OTHER_LANGUAGE_FUNCTIONS.get(name.lower())
        args_src = [self.src(a) for a in e.args]
        if name.lower() in ("pow", "power") and len(e.args) == 2:
            return self.err(e, f"Flaux doesn't have `{name}`. Use ^ for powers.",
                            fix=f"{args_src[0]} ^ {args_src[1]}")
        if name.lower() in ("append", "push") and len(e.args) == 2:
            return self.err(e, f"Flaux doesn't have `{name}`. Use `add ... to ...` to put an item in a list.",
                            fix_line=self.indent_of(e.line) + f"add {args_src[1]} to {args_src[0]}")
        if name.lower() == "range":
            return self.err(e, "Flaux doesn't have `range`. To count, use `repeat with`:",
                            example="repeat with i from 1 to 10\n    say i\nend")
        if alias == "say":
            return self.err(e, f"Flaux uses `say` instead of `{name}`.",
                            fix_line=self.indent_of(e.line) + "say " + ", ".join(args_src))
        if alias:
            return self.err(e, f"Flaux doesn't have `{name}`. The Flaux version is `{alias}`.",
                            hint="Change the line to:", fix_line=self.rename_call(e, alias),
                            example=BUILTINS.get(alias))
        names = list(self.funcs) + list(BUILTINS)
        close = difflib.get_close_matches(name, names, n=1, cutoff=0.6)
        if close:
            f = self.funcs.get(close[0])
            return self.err(e, f"There's no function called `{name}`.",
                            hint=f"Did you mean `{close[0]}`? Change the line to:",
                            fix_line=self.rename_call(e, close[0]),
                            see=(f.line, f"`{close[0]}` is made here:") if f else None)
        params = ", ".join(f"input{i + 1}" for i in range(len(e.args)))
        return self.err(e, f"There's no function called `{name}`.",
                        hint=f"Make it with `function`, somewhere in your program (usually at the top):",
                        example=f"function {name}({params})\n    # what {name} does goes here\nend")

    def call_user(self, e, f):
        need = len(f.params)
        if len(e.args) != need:
            params = ", ".join(p.name for p in f.params)
            got = len(e.args)
            raise self.err(e, f"`{f.name}` needs {need} input{'' if need == 1 else 's'}, but it was given {got}.",
                           hint=f"Give `{f.name}` one value for each of its inputs, in the same order: "
                                f"{f.name}({params})",
                           see=(f.line, f"`{f.name}` is made here:"))
        args = [self.expr(a) for a in e.args]
        for a, (_, t) in zip(e.args, args):
            self.no_void(a, t, "give to the function")
        if any(t == UNK for _, t in args):
            return "", UNK
        key = (f.name, tuple(t for _, t in args))
        spec = self.specs.get(key)
        if spec is None:
            if self.emitting:
                raise FlauxError("Internal error: a function was found late. This is a bug in Flaux.")
            self.spec_count += 1
            spec = Scope(f.name, f.body, f, key[1], f"f_{f.name}_{self.spec_count}")
            self.specs[key] = spec
            self.changed = True
        # If we passed in a list whose item type we didn't know yet, learn it
        # from how the function used it.
        for a, (_, t), p in zip(e.args, args, spec.params):
            pt = spec.vars.get(p)
            if isinstance(a, N.Name) and has_unknown(t) and pt is not None and pt != t:
                try:
                    self.set_var(a.name, merge(t, pt), a)
                except (TypeConflict, FlauxError):
                    pass
        code = ""
        if self.emitting:
            if spec.ret is None:
                raise self.err(e, f"Flaux can't work out what `{f.name}` gives back.",
                               hint=f"Every `return` in `{f.name}` calls `{f.name}` again, so it would never "
                                    f"stop. Add an `if` with a `return` that gives back a plain value:",
                               example=BASE_CASE_EXAMPLE, see=(f.line, f"`{f.name}` is made here:"))
            if id(spec) not in self.used_set:
                self.used_set.add(id(spec))
                self.used.append(spec)
            codes = []
            for a, p in zip(e.args, spec.params):
                pt = spec.vars[p]
                c, t = self.expr(a, pt)
                codes.append(self.conv(c, t, pt))
            code = f"{spec.c_name}({', '.join(codes)})"
        return code, (UNK if spec.ret is None else spec.ret)

    # -------------------------------------------------------------- builtins

    def arity(self, e, args, lo, hi=None):
        hi = lo if hi is None else hi
        n = len(args)
        if lo <= n <= hi:
            return
        if lo == hi:
            need = f"{lo} input{'' if lo == 1 else 's'}" if lo else "no inputs"
        elif hi > 99:
            need = f"at least {lo} inputs"
        else:
            need = f"{lo} or {hi} inputs"
        raise self.err(e, f"`{e.name}` needs {need}, but it was given {n}.",
                       hint=f"Here's how `{e.name}` is used:", example=BUILTINS[e.name])

    def need_num(self, e, node, t):
        if t == UNK or is_num(t):
            return
        if t == TEXT:
            raise self.err(node, f"`{e.name}` needs a number, but this is text.",
                           hint="If the text holds a number, turn it into a number first:",
                           fix=f"number({self.src(node)})")
        raise self.err(node, f"`{e.name}` needs a number, but this is {describe(t)}.",
                       hint=f"Here's how `{e.name}` is used:", example=BUILTINS[e.name])

    def need_text(self, e, node, t):
        if t in (TEXT, UNK):
            return
        raise self.err(node, f"`{e.name}` needs text, but this is {describe(t)}.",
                       hint="Turn it into text first:" if is_num(t) else f"Here's how `{e.name}` is used:",
                       fix=f"text({self.src(node)})" if is_num(t) else None,
                       example=None if is_num(t) else BUILTINS[e.name])

    def float_fn(self, e, args, cfn):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_num(e, e.args[0], t)
        return f"{cfn}({self.conv(c, t, FLOAT)})", FLOAT

    def b_sqrt(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_num(e, e.args[0], t)
        return f"fx_sqrt({self.conv(c, t, FLOAT)}, {e.line})", FLOAT

    def b_sin(self, e, args):
        return self.float_fn(e, args, "sin")

    def b_cos(self, e, args):
        return self.float_fn(e, args, "cos")

    def b_tan(self, e, args):
        return self.float_fn(e, args, "tan")

    def b_log(self, e, args):
        return self.float_fn(e, args, "log")

    def b_abs(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_num(e, e.args[0], t)
        if t == UNK:
            return "", UNK
        return (f"fx_iabs({c})" if t == INT else f"fabs({c})"), t

    def rounding(self, e, args, cfn):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_num(e, e.args[0], t)
        if t == INT:
            return c, INT
        return f"((int64_t){cfn}({c}))", INT

    def b_floor(self, e, args):
        return self.rounding(e, args, "floor")

    def b_ceil(self, e, args):
        return self.rounding(e, args, "ceil")

    def b_round(self, e, args):
        return self.rounding(e, args, "llround")

    def minmax(self, e, args, which):
        self.arity(e, args, 2, 999)
        for a, (_, t) in zip(e.args, args):
            self.need_num(e, a, t)
        if any(t == UNK for _, t in args):
            return "", UNK
        res = INT if all(t == INT for _, t in args) else FLOAT
        fn = f"fx_{'i' if res == INT else 'f'}{which}"
        code = self.conv(*args[0], res)
        for c, t in args[1:]:
            code = f"{fn}({code}, {self.conv(c, t, res)})"
        return code, res

    def b_min(self, e, args):
        return self.minmax(e, args, "min")

    def b_max(self, e, args):
        return self.minmax(e, args, "max")

    def b_random(self, e, args):
        self.arity(e, args, 1, 2)
        if len(args) == 1:
            (c, t), = args
            if t == UNK:
                return "", UNK
            if not is_list(t):
                raise self.err(e.args[0], f"random(...) with one input picks an item from a list, but this is {describe(t)}.",
                               hint="For a random number, give the smallest and biggest numbers:",
                               fix=f"1, {self.src(e.args[0])}" if is_num(t) else None,
                               example=None if is_num(t) else BUILTINS["random"])
            return f"{self.lname(t)}_pick({c}, {e.line})", t[1]
        for a, (_, t) in zip(e.args, args):
            self.need_num(e, a, t)
        if any(t == UNK for _, t in args):
            return "", UNK
        (ac, at), (bc, bt) = args
        if at == bt == INT:
            return f"fx_random_int({ac}, {bc}, {e.line})", INT
        return f"fx_random_float({self.conv(ac, at, FLOAT)}, {self.conv(bc, bt, FLOAT)}, {e.line})", FLOAT

    def b_text(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        if t == UNK:
            return "", TEXT
        return self.to_text(c, t), TEXT

    def b_number(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        if t == TEXT:
            return f"fx_number({c}, {e.line})", FLOAT
        if is_num(t):
            return self.conv(c, t, FLOAT), FLOAT
        if t == UNK:
            return "", FLOAT
        raise self.err(e.args[0], f"`number` turns text into a number, but this is {describe(t)}.",
                       example=BUILTINS["number"])

    def b_whole(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        if t == TEXT:
            return f"fx_whole_text({c}, {e.line})", INT
        if t == FLOAT:
            return f"((int64_t)trunc({c}))", INT
        if t in (INT, UNK):
            return c, INT
        raise self.err(e.args[0], f"`whole` turns text or decimals into a whole number, but this is {describe(t)}.",
                       example=BUILTINS["whole"])

    def b_ask(self, e, args):
        self.arity(e, args, 0, 1)
        if not args:
            return 'fx_ask(FX_LIT(""))', TEXT
        (c, t), = args
        return f"fx_ask({self.to_text(c, t)})", TEXT

    def b_is_number(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_text(e, e.args[0], t)
        return f"fx_is_number({c})", BOOL

    def b_clock(self, e, args):
        self.arity(e, args, 0)
        return "fx_clock()", FLOAT

    def b_length(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        return self.length_of(e.args[0], c, t)

    def b_contains(self, e, args):
        self.arity(e, args, 2)
        (hc, ht), (nc, nt) = args
        if UNK in (ht, nt):
            return "", BOOL
        if ht == TEXT:
            self.need_text(e, e.args[1], nt)
            return f"fx_text_contains({hc}, {nc})", BOOL
        if is_list(ht):
            elem = ht[1]
            if is_list(elem) or elem == UNK:
                raise self.err(e, "`contains` can't search lists of lists yet.",
                               hint="Go through the outer list with `repeat with` and search each inner list.")
            ok = nt == elem or (is_num(nt) and is_num(elem))
            if not ok:
                raise self.err(e.args[1], f"This list holds {describe(elem, plural=True)}, so it can't contain {describe(nt)}.")
            if elem == INT and nt == FLOAT:  # 2.0 can be found among whole numbers, 2.5 can't
                x = self.tmp()
                return (f"({{ double {x} = {nc}; {x} == trunc({x}) && "
                        f"{self.lname(ht)}_has({hc}, (int64_t){x}); }})"), BOOL
            return f"{self.lname(ht)}_has({hc}, {self.conv(nc, nt, elem)})", BOOL
        raise self.err(e.args[0], f"`contains` looks inside a list or text, but this is {describe(ht)}.",
                       example=BUILTINS["contains"])

    def b_uppercase(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_text(e, e.args[0], t)
        return f"fx_text_upper({c})", TEXT

    def b_lowercase(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_text(e, e.args[0], t)
        return f"fx_text_lower({c})", TEXT

    def b_distance(self, e, args):
        self.arity(e, args, 4)
        nums = self.numbers(e, args)
        if nums is None:
            return "", FLOAT
        x1, y1, x2, y2 = nums
        return f"hypot({x1} - {x2}, {y1} - {y2})", FLOAT

    def b_rgb(self, e, args):
        self.arity(e, args, 3)
        nums = self.numbers(e, args)
        if nums is None:
            return "", TEXT
        return f"fx_rgb_text({', '.join(nums)})", TEXT

    # ------------------------------------------------------------ graphics

    def numbers(self, e, args, start=0):
        """Check args[start:] are numbers; returns their C code as decimals
        (or None while their types are still being worked out)."""
        out = []
        for a, (c, t) in zip(e.args[start:], args[start:]):
            self.need_num(e, a, t)
            if t == UNK:
                return None
            out.append(self.conv(c, t, FLOAT))
        return out

    def whole_number(self, e, node, c, t):
        self.need_num(e, node, t)
        return f"((int64_t)llround({c}))" if t == FLOAT else c

    def color_arg(self, e, args, index, default):
        """C code for a colour input (or the default if it was left out)."""
        if len(args) <= index:
            return f'FX_LIT("{default}")'
        node, (c, t) = e.args[index], args[index]
        if t not in (TEXT, UNK):
            raise self.err(node, f"A colour is written as text in quotes, but this is {describe(t)}.",
                           hint=graphics.COLOR_HELP, example=BUILTINS[e.name])
        if isinstance(node, N.Str) and len(node.parts) == 1 and isinstance(node.parts[0], str):
            name = node.parts[0]
            if not graphics.is_color(name):
                close = difflib.get_close_matches(graphics.normalize(name), list(graphics.COLORS), n=1, cutoff=0.6)
                raise self.err(node, f"Flaux doesn't know the colour \"{name}\".",
                               hint=(f"Did you mean \"{close[0]}\"? Change the line to:" if close else graphics.COLOR_HELP),
                               fix=f'"{close[0]}"' if close else None,
                               example=None if close else 'circle(100, 100, 20, "orange")\n'
                                                          'circle(100, 100, 20, "#ff8800")\n'
                                                          'circle(100, 100, 20, rgb(255, 136, 0))')
        return c

    def key_arg(self, e, args):
        self.arity(e, args, 1)
        node, (c, t) = e.args[0], args[0]
        self.need_text(e, node, t)
        if isinstance(node, N.Str) and len(node.parts) == 1 and isinstance(node.parts[0], str):
            name = node.parts[0]
            if not graphics.is_key(name):
                close = difflib.get_close_matches(graphics.normalize(name), sorted(graphics.KEYS), n=1, cutoff=0.6)
                raise self.err(node, f"Flaux doesn't know the key \"{name}\".",
                               hint=(f"Did you mean \"{close[0]}\"? Change the line to:" if close else graphics.KEY_HELP),
                               fix=f'"{close[0]}"' if close else None)
        return c

    def graphics_call(self, code, t=VOID, three_d=False):
        if self.emitting:
            self.uses_graphics = True
            if three_d:
                self.uses_3d = True
        return code, t

    def b_window(self, e, args):
        self.arity(e, args, 2, 3)
        (wc, wt), (hc, ht) = args[0], args[1]
        w = self.whole_number(e, e.args[0], wc, wt)
        h = self.whole_number(e, e.args[1], hc, ht)
        title = 'FX_LIT("Flaux")'
        if len(args) == 3:
            c, t = args[2]
            title = self.to_text(c, t)
        return self.graphics_call(f"fx_g_window({w}, {h}, {title}, {e.line})")

    def b_show(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call(f"fx_g_show({e.line})")

    def b_clear(self, e, args):
        self.arity(e, args, 0, 1)
        color = self.color_arg(e, args, 0, "black")
        return self.graphics_call(f"fx_g_clear({color}, {e.line})")

    def shape(self, e, args, n, cfn, default="white"):
        self.arity(e, args, n, n + 1)
        nums = self.numbers(e, args[:n])
        color = self.color_arg(e, args, n, default)
        if nums is None:
            return "", VOID
        return self.graphics_call(f"{cfn}({', '.join(nums)}, {color}, {e.line})")

    def b_rect(self, e, args):
        return self.shape(e, args, 4, "fx_g_rect")

    def b_circle(self, e, args):
        return self.shape(e, args, 3, "fx_g_circle")

    def b_line(self, e, args):
        self.arity(e, args, 4, 6)
        nums = self.numbers(e, args[:4])
        color = self.color_arg(e, args, 4, "white")
        thickness = "1.0"
        if len(args) == 6:
            more = self.numbers(e, args, 5)
            thickness = more[0] if more else ""
        if nums is None or not thickness:
            return "", VOID
        return self.graphics_call(f"fx_g_line({', '.join(nums)}, {color}, {thickness}, {e.line})")

    def b_triangle(self, e, args):
        return self.shape(e, args, 6, "fx_g_triangle")

    def b_dot(self, e, args):
        return self.shape(e, args, 2, "fx_g_dot")

    def b_write(self, e, args):
        self.arity(e, args, 3, 5)
        tc, tt = args[0]
        xs = []
        for node, (c, t) in zip(e.args[1:3], args[1:3]):
            self.need_num(e, node, t)
            xs.append(self.conv(c, t, FLOAT))
        color = self.color_arg(e, args, 3, "white")
        size = "2"
        if len(args) == 5:
            size = self.whole_number(e, e.args[4], *args[4])
        if tt == UNK or any(t == UNK for _, t in args[1:]):
            return "", VOID
        return self.graphics_call(f"fx_g_write({self.to_text(tc, tt)}, {xs[0]}, {xs[1]}, {color}, {size}, {e.line})")

    def b_key_down(self, e, args):
        return self.graphics_call(f"fx_key_down({self.key_arg(e, args)}, {e.line})", BOOL)

    def b_key_pressed(self, e, args):
        return self.graphics_call(f"fx_key_pressed({self.key_arg(e, args)}, {e.line})", BOOL)

    def b_mouse_x(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_x()", INT)

    def b_mouse_y(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_y()", INT)

    def b_mouse_down(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_down()", BOOL)

    def b_mouse_clicked(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_clicked()", BOOL)

    def b_mouse_moved_x(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_moved_x()", INT)

    def b_mouse_moved_y(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_moved_y()", INT)

    def b_lock_mouse(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call(f"fx_g_lock_mouse(true, {e.line})")

    def b_unlock_mouse(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call(f"fx_g_lock_mouse(false, {e.line})")

    def b_mouse_locked(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_mouse_locked()", BOOL)

    def b_beep(self, e, args):
        self.arity(e, args, 1, 2)
        nums = self.numbers(e, args)
        if nums is None:
            return "", VOID
        seconds = nums[1] if len(nums) > 1 else "0.1"
        return self.graphics_call(f"fx_g_beep({nums[0]}, {seconds}, {e.line})")

    def picture_arg(self, e, args, index=0):
        """C code for a picture's file name, checking the file is really there."""
        node, (c, t) = e.args[index], args[index]
        self.need_text(e, node, t)
        self.check_file(node, PICTURE_TYPES, "picture", ".png")
        return c

    def check_file(self, node, kinds, what, example_ending):
        """If node is a file name written in the program, check the file is there."""
        if not (self.program_dir and isinstance(node, N.Str) and len(node.parts) == 1
                and isinstance(node.parts[0], str)):
            return
        name = node.parts[0]
        here = os.path.join(self.program_dir, name)
        if os.path.isfile(here) or os.path.isfile(name):
            return
        try:
            files = [f for f in os.listdir(self.program_dir) if f.lower().endswith(kinds)]
        except OSError:
            files = []
        close = difflib.get_close_matches(name, files, n=1, cutoff=0.5)
        if close:
            raise self.err(node, f"I can't find the {what} \"{name}\".",
                           hint=f"There's a {what} called \"{close[0]}\" next to your program. "
                                f"Did you mean that? Change the line to:",
                           fix=f'"{close[0]}"')
        raise self.err(node, f"I can't find the {what} \"{name}\".",
                       hint=f"Put the {what} file in the same folder as your program "
                            f"({self.program_dir}), and check its name is exactly right, "
                            f"including the {example_ending} at the end.")

    def b_image(self, e, args):
        if len(args) == 4:
            raise self.err(e, "`image` needs both a width and a height, or neither.",
                           hint="Give the size as width, height:", example=BUILTINS["image"])
        self.arity(e, args, 3, 6)
        name = self.picture_arg(e, args)
        nums = self.numbers(e, args, 1)
        if nums is None:
            return "", VOID
        x, y = nums[0], nums[1]
        sized = len(nums) >= 4
        w, h = (nums[2], nums[3]) if sized else ("0", "0")
        angle = nums[4] if len(nums) == 5 else "0"
        return self.graphics_call(f"fx_g_image({name}, {x}, {y}, {w}, {h}, {angle}, "
                                  f"{'true' if sized else 'false'}, {e.line})")

    def b_image_width(self, e, args):
        self.arity(e, args, 1)
        return self.graphics_call(f"fx_image_width({self.picture_arg(e, args)}, {e.line})", INT)

    def b_image_height(self, e, args):
        self.arity(e, args, 1)
        return self.graphics_call(f"fx_image_height({self.picture_arg(e, args)}, {e.line})", INT)

    def b_text_width(self, e, args):
        self.arity(e, args, 1, 2)
        tc, tt = args[0]
        size = self.whole_number(e, e.args[1], *args[1]) if len(args) == 2 else "2"
        if tt == UNK:
            return "", INT
        return self.graphics_call(f"fx_text_width({self.to_text(tc, tt)}, {size})", INT)

    def b_fullscreen(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call(f"fx_g_fullscreen({e.line})")

    def b_screenshot(self, e, args):
        self.arity(e, args, 1)
        (c, t), = args
        self.need_text(e, e.args[0], t)
        return self.graphics_call(f"fx_g_screenshot({c}, {e.line})")

    def b_fps(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_fps()", INT)

    def b_seconds(self, e, args):
        self.arity(e, args, 0)
        return self.graphics_call("fx_seconds()", FLOAT)

    # ------------------------------------------------------ game controllers

    def button_arg(self, e, args):
        self.arity(e, args, 1)
        node, (c, t) = e.args[0], args[0]
        self.need_text(e, node, t)
        if isinstance(node, N.Str) and len(node.parts) == 1 and isinstance(node.parts[0], str):
            name = node.parts[0]
            if not graphics.is_button(name):
                close = difflib.get_close_matches(graphics.normalize(name), sorted(graphics.BUTTONS), n=1, cutoff=0.6)
                raise self.err(node, f"Flaux doesn't know the controller button \"{name}\".",
                               hint=(f"Did you mean \"{close[0]}\"? Change the line to:" if close
                                     else graphics.BUTTON_HELP),
                               fix=f'"{close[0]}"' if close else None)
        return c

    def b_button_down(self, e, args):
        return self.graphics_call(f"fx_button_down({self.button_arg(e, args)}, {e.line})", BOOL)

    def b_button_pressed(self, e, args):
        return self.graphics_call(f"fx_button_pressed({self.button_arg(e, args)}, {e.line})", BOOL)

    def stick(self, e, args, axis):
        self.arity(e, args, 0, 1)
        side = 'FX_LIT("left")'
        if args:
            node, (c, t) = e.args[0], args[0]
            self.need_text(e, node, t)
            if isinstance(node, N.Str) and len(node.parts) == 1 and isinstance(node.parts[0], str):
                if graphics.normalize(node.parts[0]) not in ("left", "right"):
                    raise self.err(node, "A controller has a \"left\" and a \"right\" stick.",
                                   fix='"left"', hint="Change it to:")
            side = c
        return self.graphics_call(f"fx_stick_{axis}({side}, {e.line})", FLOAT)

    def b_stick_x(self, e, args):
        return self.stick(e, args, "x")

    def b_stick_y(self, e, args):
        return self.stick(e, args, "y")

    # ------------------------------------------------------------------- 3D

    def call_3d(self, code, t=VOID):
        return self.graphics_call(code, t, three_d=True)

    def shader_values(self, e, node, kind):
        """Use the shader `node` as `kind` ("surface" or "screen"): its GLSL is
        made once, and the C code for the numbers it uses from here is returned
        (or None while their types are still being worked out)."""
        name = node.name
        key = (name, kind)
        if key not in self.shader_uses:
            self.shader_uses[key] = shader_glsl(self.shaders[name], kind, self)
        _, outside = self.shader_uses[key]
        values = []
        for var in outside:
            t = self.scope.vars.get(var)
            if t is None and var in self.scope.assigned:
                return None
            if t is None:
                where = "screen effects" if kind == "screen" else "shapes"
                from .shadergen import INPUT_HELP
                raise self.err(node, f"The shader `{name}` uses `{var}`, but there's no `{var}` here.",
                               hint=f"Names a shader doesn't set itself come from where it's used, so make "
                                    f"`{var}` (a number) before this line. In {where}, a shader already "
                                    f"knows {INPUT_HELP[kind]}.",
                               see=(self.shaders[name].line, f"`{name}` is made here:"))
            if t == BOOL:
                values.append(f"(v_{var} ? 1.0 : 0.0)")
            elif is_num(t):
                values.append(f"(double)v_{var}")
            else:
                raise self.err(node, f"The shader `{name}` uses `{var}`, which is {describe(t)} here. "
                                     f"Shaders can only use numbers from the rest of the program.",
                               see=(self.shaders[name].line, f"`{name}` is made here:"))
        return values

    def paint_arg(self, e, args, index):
        """C code for what a shape is painted with: a colour, a picture or a shader."""
        if len(args) <= index:
            return 'FX_PAINT(FX_LIT("white"))'
        node, (c, t) = e.args[index], args[index]
        if t == SHADER:
            values = self.shader_values(e, node, "surface")
            if values is None:
                return None
            values += ["0"] * (4 - len(values))
            return f"FX_SHADE({c}, {', '.join(values)})"
        if t not in (TEXT, UNK):
            raise self.err(node, f"A shape is painted with a colour (like \"red\"), a picture (like \"crate.png\") "
                                 f"or a shader, but this is {describe(t)}.",
                           hint=f"Here's how `{e.name}` is used:", example=BUILTINS[e.name])
        literal = node.parts[0] if (isinstance(node, N.Str) and len(node.parts) == 1
                                    and isinstance(node.parts[0], str)) else None
        if literal is not None and literal.lower().endswith(PICTURE_TYPES):
            self.check_file(node, PICTURE_TYPES, "picture", ".png")
        else:
            self.color_arg(e, args, index, "white")
        return f"FX_PAINT({c})"

    def shape3d(self, e, args, n, cfn):
        self.arity(e, args, n, n + 1)
        nums = self.numbers(e, args[:n])
        paint = self.paint_arg(e, args, n)
        if nums is None or paint is None:
            return "", VOID
        return self.call_3d(f"{cfn}({', '.join(nums)}, {paint}, {e.line})")

    def b_cube(self, e, args):
        return self.shape3d(e, args, 4, "fx_g_cube")

    def b_box(self, e, args):
        return self.shape3d(e, args, 6, "fx_g_box")

    def b_ball(self, e, args):
        return self.shape3d(e, args, 4, "fx_g_ball")

    def b_cylinder(self, e, args):
        return self.shape3d(e, args, 5, "fx_g_cylinder")

    def b_cone(self, e, args):
        return self.shape3d(e, args, 5, "fx_g_cone")

    def b_plane(self, e, args):
        return self.shape3d(e, args, 5, "fx_g_plane")

    def b_model(self, e, args):
        self.arity(e, args, 5, 6)
        node, (fc, ft) = e.args[0], args[0]
        self.need_text(e, node, ft)
        if isinstance(node, N.Str) and len(node.parts) == 1 and isinstance(node.parts[0], str):
            if not node.parts[0].lower().endswith(MODEL_TYPES):
                raise self.err(node, "Models are .obj files.",
                               hint="Save the model as .obj (in Blender: File, Export, Wavefront (.obj)), "
                                    "and put it next to your program.", example=BUILTINS["model"])
            self.check_file(node, MODEL_TYPES, "model", ".obj")
        nums = []
        for a, (c, t) in zip(e.args[1:5], args[1:5]):
            self.need_num(e, a, t)
            nums.append(None if t == UNK else self.conv(c, t, FLOAT))
        paint = self.paint_arg(e, args, 5)
        if ft == UNK or None in nums or paint is None:
            return "", VOID
        return self.call_3d(f"fx_g_model({fc}, {', '.join(nums)}, {paint}, {e.line})")

    def b_camera(self, e, args):
        self.arity(e, args, 6, 7)
        nums = self.numbers(e, args)
        if nums is None:
            return "", VOID
        fov = nums[6] if len(nums) == 7 else "60.0"
        return self.call_3d(f"fx_g_camera({', '.join(nums[:6])}, {fov}, {e.line})")

    def b_sky(self, e, args):
        self.arity(e, args, 1, 2)
        top = self.color_arg(e, args, 0, "black")
        two = len(args) == 2
        horizon = self.color_arg(e, args, 1, "black") if two else 'FX_LIT("")'
        return self.call_3d(f"fx_g_sky({top}, {horizon}, {'true' if two else 'false'}, {e.line})")

    def b_sun(self, e, args):
        self.arity(e, args, 3, 4)
        nums = self.numbers(e, args[:3])
        color = self.color_arg(e, args, 3, "white")
        if nums is None:
            return "", VOID
        return self.call_3d(f"fx_g_sun({', '.join(nums)}, {color}, {e.line})")

    def b_light(self, e, args):
        self.arity(e, args, 3, 5)
        nums = self.numbers(e, args[:3])
        color = self.color_arg(e, args, 3, "white")
        reach = "10.0"
        if len(args) == 5:
            more = self.numbers(e, args, 4)
            reach = more[0] if more else None
        if nums is None or reach is None:
            return "", VOID
        return self.call_3d(f"fx_g_light({', '.join(nums)}, {color}, {reach}, {e.line})")

    def b_fog(self, e, args):
        self.arity(e, args, 1, 2)
        nums = self.numbers(e, args[:1])
        colored = len(args) == 2
        color = self.color_arg(e, args, 1, "white") if colored else 'FX_LIT("")'
        if nums is None:
            return "", VOID
        return self.call_3d(f"fx_g_fog({nums[0]}, {color}, {'true' if colored else 'false'}, {e.line})")

    def b_turn(self, e, args):
        self.arity(e, args, 3)
        nums = self.numbers(e, args)
        if nums is None:
            return "", VOID
        return self.call_3d(f"fx_g_turn({', '.join(nums)})")

    def b_in_vr(self, e, args):
        self.arity(e, args, 0)
        if self.emitting:
            self.uses_xr = True
        return self.call_3d("fx_in_vr()", BOOL)

    def hand(self, e, args, axis):
        self.arity(e, args, 0, 1)
        side = 'FX_LIT("right")'
        if args:
            node, (c, t) = e.args[0], args[0]
            self.need_text(e, node, t)
            if isinstance(node, N.Str) and len(node.parts) == 1 and isinstance(node.parts[0], str):
                if graphics.normalize(node.parts[0]) not in ("left", "right"):
                    raise self.err(node, "You have a \"left\" and a \"right\" hand.", fix='"right"',
                                   hint="Change it to:")
            side = c
        if self.emitting:
            self.uses_xr = True
        return self.call_3d(f"fx_hand({side}, {axis}, {e.line})", FLOAT)

    def b_hand_x(self, e, args):
        return self.hand(e, args, 0)

    def b_hand_y(self, e, args):
        return self.hand(e, args, 1)

    def b_hand_z(self, e, args):
        return self.hand(e, args, 2)

    def b_effect(self, e, args):
        self.arity(e, args, 1)
        node, (c, t) = e.args[0], args[0]
        if t == UNK:
            return "", VOID
        if t != SHADER:
            raise self.err(node, f"`effect` needs a shader, but this is {describe(t)}.",
                           hint="Make a shader, then give its name to effect:",
                           example="shader darker\n    return rgb(red / 2, green / 2, blue / 2)\nend\n\n"
                                   "effect(darker)")
        values = self.shader_values(e, node, "screen")
        if values is None:
            return "", VOID
        vals = f"(float[]){{{', '.join(values)}}}" if values else "NULL"
        return self.call_3d(f"fx_g_effect({c}, {vals}, {len(values)})")


def compile_source(source, filename="program.flx", program_dir=None):
    """Compile Flaux source code to C source code."""
    from .lexer import Lexer
    from .parser import Parser
    lexer = Lexer(source)
    tokens = lexer.run()
    parser = Parser(tokens, source)
    try:
        program = parser.parse_program()
    except FlauxError:
        # Report whichever problem comes first in the file.
        if lexer.first_error and parser.peek().line >= lexer.first_error.line:
            raise lexer.first_error
        raise
    if lexer.first_error:
        raise lexer.first_error
    return Compiler(program, filename, source, program_dir).compile()
