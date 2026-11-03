"""Shaders: Flaux code that runs on the graphics card.

    shader stripes
        if floor(x * 4) mod 2 = 0
            return "white"
        end
        return rgb(230, 40, 60)
    end

    cube(0, 0, 0, 1, stripes)     # paint a shape with it
    effect(wobble)                # or put the whole screen through one

A shader runs once for every pixel, millions of times a frame, so it has to
be simple: numbers, true/false, colours, `if`, `repeat` and maths. This file
turns one into GLSL (the graphics card's language) for OpenGL 3.3 and WebGL 2
(they share GLSL ES 3 / GLSL 330), checking it on the way and explaining what
a shader can't do.

Two kinds, decided by how the shader is used:
  surface  paints shapes. Knows x, y, z (the point in the world), u, v (where
           on the face, 0 to 1), nx, ny, nz (which way the face points) and
           time. Gives back the colour; light, shadows and fog are added after.
  screen   for effect(...). Knows x, y (the pixel), width, height, time, and
           red, green, blue (that pixel's colour), and can look at other
           pixels with red_at(x, y), green_at(x, y) and blue_at(x, y).

Other names a shader uses are numbers from wherever it's used (like `health`
in `cube(0, 0, 0, 1, glow)`); the compiler passes them in.
"""

from . import graphics
from . import nodes as N
from .errors import FlauxError

INPUTS = {
    "surface": ["x", "y", "z", "u", "v", "nx", "ny", "nz", "time"],
    "screen": ["x", "y", "width", "height", "time", "red", "green", "blue"],
}
INPUT_HELP = {
    "surface": "x, y and z (the point being painted), u and v (where on the face, 0 to 1), "
               "nx, ny and nz (which way the face points) and time (in seconds)",
    "screen": "x and y (the pixel), width and height (of the window), time (in seconds), "
              "red, green and blue (the pixel's colour, 0 to 255), and red_at(x, y), green_at(x, y) "
              "and blue_at(x, y) for other pixels",
}
MAX_OUTSIDE = {"surface": 4, "screen": 8}

NUM, BOOL, COLOR = "number", "true/false value", "colour"

# function -> (GLSL, number of inputs, kind it's allowed in or None for both)
FUNCTIONS = {
    "sqrt": ("sqrt(max({0}, 0.0))", 1, None),
    "sin": ("sin({0})", 1, None),
    "cos": ("cos({0})", 1, None),
    "tan": ("tan({0})", 1, None),
    "log": ("log({0})", 1, None),
    "abs": ("abs({0})", 1, None),
    "floor": ("floor({0})", 1, None),
    "ceil": ("ceil({0})", 1, None),
    "round": ("fx_round({0})", 1, None),
    "min": ("min({0}, {1})", 2, None),
    "max": ("max({0}, {1})", 2, None),
    "distance": ("distance(vec2({0}, {1}), vec2({2}, {3}))", 4, None),
    "red_at": ("red_at({0}, {1})", 2, "screen"),
    "green_at": ("green_at({0}, {1})", 2, "screen"),
    "blue_at": ("blue_at({0}, {1})", 2, "screen"),
}

LOOP_GUARD = 10000   # a loop on the graphics card that never stops would freeze the computer


def glsl_name(name):
    return "v_" + name.replace("__", "_0_")


def glsl_float(value):
    text = repr(float(value))
    if "e" in text or "inf" in text or "nan" in text:
        text = f"{float(value):.9f}"
    return text


class ShaderGen:
    def __init__(self, shader, kind, compiler):
        self.shader = shader
        self.kind = kind
        self.c = compiler            # for err(), src() and the lines of the program
        self.inputs = INPUTS[kind]
        self.vars = {n: NUM for n in self.inputs}
        self.assigned = set()
        for s in self.statements(shader.body):
            if isinstance(s, N.Set) and isinstance(s.target, N.Name):
                self.assigned.add(s.target.name)
            elif isinstance(s, N.RepeatRange):
                self.assigned.add(s.var.name)
        self.outside = []            # names from where the shader is used, in order
        self.out = []
        self.depth = 1
        self.loops = 0
        self.tmp_count = 0
        self.has_return = False

    # ------------------------------------------------------------ helpers

    def statements(self, stmts):
        for s in stmts:
            yield s
            if isinstance(s, N.If):
                for _, body in s.branches:
                    yield from self.statements(body)
                if s.else_body:
                    yield from self.statements(s.else_body)
            elif hasattr(s, "body") and isinstance(s.body, list):
                yield from self.statements(s.body)

    def err(self, node, msg, hint=None, **teach):
        return self.c.err(node, msg, hint=hint, **teach)

    def emit(self, line):
        self.out.append("    " * self.depth + line)

    def why_not(self, node, what, hint=None, example=None):
        return self.err(node, f"Shaders can't use {what}.",
                        hint=hint or "A shader runs on the graphics card, once for every pixel, so it can only "
                                     "use numbers, true/false, colours, `if`, `repeat` and maths.",
                        example=example)

    # ------------------------------------------------------------- output

    def generate(self):
        body = self.shader.body
        self.block(body)
        if not self.has_return:
            raise self.err(self.shader, f"The shader `{self.shader.name}` never gives back a colour.",
                           hint="End it with `return` and the colour for this point:",
                           example=f"shader {self.shader.name}\n    return rgb(255, 200, 0)\nend")
        params = ", ".join(f"float {glsl_name(n)}" for n in self.inputs)
        if self.kind == "surface":
            head = ["#define FX_USER 1", f"vec3 fx_user_surface({params}, vec4 fx_in) {{"]
            outside = [f"    float {glsl_name(n)} = fx_in.{'xyzw'[i]};" for i, n in enumerate(self.outside)]
        else:
            head = [f"vec3 fx_user_screen({params}) {{"]
            outside = [f"    float {glsl_name(n)} = u_vars[{i}];" for i, n in enumerate(self.outside)]
        locals_ = []
        for name, t in self.vars.items():
            if name in self.inputs or name in self.outside:
                continue
            gt = {NUM: "float", BOOL: "bool", COLOR: "vec3"}[t]
            zero = {NUM: "0.0", BOOL: "false", COLOR: "vec3(0.0)"}[t]
            locals_.append(f"    {gt} {glsl_name(name)} = {zero};")
        lines = ["", f"// shader {self.shader.name} (line {self.shader.line})"] + head + outside + locals_
        lines += self.out + ["    return vec3(0.0);", "}", ""]
        return "\n".join(lines)

    # --------------------------------------------------------- statements

    def block(self, stmts):
        for s in stmts:
            method = getattr(self, "s_" + type(s).__name__, None)
            if method is None:
                raise self.why_not(s, f"`{self.c.src(s).split()[0] if self.c.src(s).split() else 'this'}`")
            method(s)

    def nested(self, stmts):
        self.depth += 1
        self.block(stmts)
        self.depth -= 1

    def s_Set(self, s):
        if not isinstance(s.target, N.Name):
            raise self.why_not(s.target, "lists")
        name = s.target.name
        code, t = self.expr(s.value)
        if name in self.inputs and t != NUM:
            raise self.err(s, f"`{name}` is a number the shader is given, so it can only hold numbers.",
                           hint="Use a new name for this:", fix=self.c.replaced(s.target, f"my_{name}"))
        old = self.vars.get(name)
        if old is not None and old != t:
            raise self.err(s.value, f"`{name}` holds {self.a(old)}, so it can't be set to {self.a(t)}.",
                           hint="Use a new name for this value.")
        self.vars[name] = t
        self.emit(f"{glsl_name(name)} = {code};")

    def a(self, t):
        return {NUM: "a number", BOOL: "a true/false value", COLOR: "a colour"}[t]

    def change(self, s, op):
        if not isinstance(s.target, N.Name):
            raise self.why_not(s.target, "lists")
        name = s.target.name
        code, t = self.expr(s.value)
        self.need(s.value, t, NUM)
        known = self.vars.get(name)
        if known is None:
            raise self.err(s.target, f"`{name}` hasn't been set yet, so there's nothing to change.",
                           hint="Give it a starting value first:", example=f"set {name} to 0")
        self.need(s.target, known, NUM)
        self.emit(f"{glsl_name(name)} {op}= {code};")

    def s_Add(self, s):
        self.change(s, "+")

    def s_Subtract(self, s):
        self.change(s, "-")

    def s_If(self, s):
        for i, (cond, body) in enumerate(s.branches):
            code, t = self.expr(cond)
            self.need(cond, t, BOOL)
            self.emit(f"{'if' if i == 0 else '} else if'} ({code}) {{")
            self.nested(body)
        if s.else_body:
            self.emit("} else {")
            self.nested(s.else_body)
        self.emit("}")

    def guard(self):
        self.tmp_count += 1
        return f"fx_g{self.tmp_count}"

    def loop(self, head, body):
        self.emit(head)
        self.loops += 1
        self.nested(body)
        self.loops -= 1
        self.emit("}")

    def s_RepeatTimes(self, s):
        code, t = self.expr(s.count)
        self.need(s.count, t, NUM)
        g = self.guard()
        self.loop(f"for (int {g} = 0; {g} < min(int({code}), {LOOP_GUARD}); {g}++) {{", s.body)

    def s_RepeatWhile(self, s):
        code, t = self.expr(s.cond)
        self.need(s.cond, t, BOOL)
        if s.until:
            code = f"!({code})"
        g = self.guard()
        self.loop(f"for (int {g} = 0; {g} < {LOOP_GUARD} && ({code}); {g}++) {{", s.body)

    def s_RepeatForever(self, s):
        raise self.err(s, "Shaders can't use `repeat forever`.",
                       hint="A shader has to finish quickly for every pixel. Repeat a number of times instead:",
                       example="repeat 10 times\n    # ...\nend")

    def s_RepeatRange(self, s):
        start, st = self.expr(s.start)
        end, et = self.expr(s.end)
        self.need(s.start, st, NUM)
        self.need(s.end, et, NUM)
        step = "1.0"
        if s.step is not None:
            step, pt = self.expr(s.step)
            self.need(s.step, pt, NUM)
            if isinstance(s.step, N.Num) and s.step.value == 0:
                raise self.err(s.step, "Counting by 0 would never finish.", fix="1")
        name = s.var.name
        if self.vars.get(name, NUM) != NUM:
            raise self.err(s.var, f"`{name}` already holds {self.a(self.vars[name])}, so it can't count.",
                           hint="Use a new name to count with.")
        self.vars[name] = NUM
        v, g = glsl_name(name), self.guard()
        self.emit("{")
        self.depth += 1
        self.emit(f"float {g}_end = {end};")
        self.emit(f"float {g}_step = {step};")
        self.emit(f"{v} = {start};")
        self.loop(f"for (int {g} = 0; {g} < {LOOP_GUARD} && ({g}_step > 0.0 ? {v} <= {g}_end : {v} >= {g}_end); "
                  f"{g}++, {v} += {g}_step) {{", s.body)
        self.depth -= 1
        self.emit("}")

    def s_RepeatEach(self, s):
        raise self.why_not(s, "`repeat with ... in`", hint="Shaders don't have lists or text to go through. "
                           "Count with numbers instead:", example="repeat with i from 1 to 5\n    # ...\nend")

    def s_Stop(self, s):
        if not self.loops:
            raise self.err(s, "`stop` only works inside a `repeat`.")
        self.emit("break;")

    def s_Skip(self, s):
        if not self.loops:
            raise self.err(s, "`skip` only works inside a `repeat`.")
        self.emit("continue;")

    def s_Return(self, s):
        if s.value is None:
            raise self.err(s, "A shader gives back a colour, so `return` needs one.",
                           hint="Give back a colour:", fix='return "white"')
        code, t = self.expr(s.value)
        if t == NUM:
            raise self.err(s.value, "A shader gives back a colour, but this is a number.",
                           hint="For a shade of grey, use the number for red, green and blue:",
                           fix=f"rgb({self.c.src(s.value)}, {self.c.src(s.value)}, {self.c.src(s.value)})")
        if t != COLOR:
            raise self.err(s.value, f"A shader gives back a colour, but this is {self.a(t)}.",
                           hint='Give back a colour, like "red" or rgb(255, 0, 0).')
        self.has_return = True
        self.emit(f"return {code};")

    def s_Say(self, s):
        raise self.why_not(s, "`say`", hint="A shader runs on the graphics card, millions of times a frame, so "
                           "it can't show text. To see a number, turn it into a colour, like "
                           "return rgb(number, 0, 0).")

    def s_ExprStmt(self, s):
        raise self.why_not(s, f"`{s.expr.name}(...)` on its own line" if isinstance(s.expr, N.Call) else "this",
                           hint="Shaders work out a colour and give it back with `return`. Drawing and "
                                "other instructions go in the main program.")

    def s_ShaderDef(self, s):
        raise self.err(s, "Shaders can't go inside other shaders.")

    # -------------------------------------------------------- expressions

    def need(self, node, t, want):
        if t != want:
            raise self.err(node, f"This needs {self.a(want)}, but it's {self.a(t)}.")

    def expr(self, e):
        method = getattr(self, "x_" + type(e).__name__, None)
        if method is None:
            raise self.why_not(e, "lists" if isinstance(e, (N.ListLit, N.Index, N.Copies, N.Length)) else "this")
        return method(e)

    def x_Num(self, e):
        return glsl_float(e.value), NUM

    def x_Bool(self, e):
        return ("true" if e.value else "false"), BOOL

    def x_Str(self, e):
        if len(e.parts) == 1 and isinstance(e.parts[0], str):
            name = e.parts[0]
            if graphics.is_color(name):
                r, g, b = graphics.color_rgb(name)
                return f"vec3({glsl_float(r)}, {glsl_float(g)}, {glsl_float(b)})", COLOR
            raise self.err(e, f"Flaux doesn't know the colour \"{name}\".", hint=graphics.COLOR_HELP)
        raise self.why_not(e, "text", hint="The only text a shader understands is a colour name, like "
                                           "\"red\". Make other colours with rgb(red, green, blue).")

    def x_Name(self, e):
        name = e.name
        if name in self.vars:
            return glsl_name(name), self.vars[name]
        if name in self.assigned:
            raise self.err(e, f"`{name}` is used before it's set.",
                           hint=f"Set `{name}` before this line.")
        # a number from wherever the shader is used
        if name not in self.outside:
            if len(self.outside) >= MAX_OUTSIDE[self.kind]:
                raise self.err(e, f"A shader can only use {MAX_OUTSIDE[self.kind]} numbers from the rest of the "
                                  f"program, and `{name}` would be one more.",
                               hint=f"The numbers it knows already are {INPUT_HELP[self.kind]}.")
            self.outside.append(name)
        self.vars[name] = NUM
        return glsl_name(name), NUM

    def x_Unary(self, e):
        code, t = self.expr(e.operand)
        if e.op == "not":
            self.need(e.operand, t, BOOL)
            return f"(!{code})", BOOL
        self.need(e.operand, t, NUM)
        return f"(-{code})", NUM

    def x_Binary(self, e):
        op = e.op
        lc, lt = self.expr(e.left)
        rc, rt = self.expr(e.right)
        if op in ("and", "or"):
            self.need(e.left, lt, BOOL)
            self.need(e.right, rt, BOOL)
            return f"({lc} {'&&' if op == 'and' else '||'} {rc})", BOOL
        if op in ("=", "!="):
            if lt != rt:
                raise self.err(e, f"This compares {self.a(lt)} with {self.a(rt)}, which are never the same.")
            if lt == COLOR:
                return f"({'all' if op == '=' else 'any'}({'equal' if op == '=' else 'notEqual'}({lc}, {rc})))", BOOL
            return f"({lc} {'==' if op == '=' else '!='} {rc})", BOOL
        if lt == COLOR or rt == COLOR:
            raise self.err(e, "Shaders can't do maths with whole colours.",
                           hint="Work with the red, green and blue numbers, then make the colour at the end "
                                "with rgb(red, green, blue).")
        self.need(e.left, lt, NUM)
        self.need(e.right, rt, NUM)
        if op in ("<", ">", "<=", ">="):
            return f"({lc} {op} {rc})", BOOL
        if op in ("+", "-", "*", "/"):
            return f"({lc} {op} {rc})", NUM
        if op == "//":
            return f"floor({lc} / {rc})", NUM
        if op in ("%", "mod"):
            return f"fx_mod({lc}, {rc})", NUM
        if op == "^":
            return f"fx_pow({lc}, {rc})", NUM
        raise self.why_not(e, f"`{op}`")

    def x_Call(self, e):
        name = e.name
        args = [self.expr(a) for a in e.args]
        if name == "rgb":
            if len(args) != 3:
                raise self.err(e, "`rgb` needs 3 numbers: red, green and blue (each 0 to 255).",
                               example="return rgb(255, 136, 0)")
            for a, (_, t) in zip(e.args, args):
                self.need(a, t, NUM)
            return f"vec3({args[0][0]}, {args[1][0]}, {args[2][0]})", COLOR
        info = FUNCTIONS.get(name)
        if info is None:
            if name in self.c.funcs:
                raise self.err(e, f"Shaders can't use your own functions, like `{name}`.",
                               hint="A shader runs on the graphics card, which can't run the rest of your "
                                    "program. Do the maths inside the shader instead.")
            raise self.why_not(e, f"`{name}`", hint="In a shader you can use maths (sqrt, sin, cos, tan, log, abs, "
                               "floor, ceil, round, min, max, distance), rgb(...)" +
                               (" and red_at, green_at, blue_at." if self.kind == "screen" else "."))
        pattern, count, kind = info
        if kind and kind != self.kind:
            raise self.err(e, f"`{name}` only works in a shader used with effect(...).",
                           hint="Shapes don't have other pixels to look at. Use it in a screen effect:",
                           example=f"effect({self.shader.name})")
        if len(args) != count:
            raise self.err(e, f"`{name}` needs {count} input{'s' if count > 1 else ''}, but it was given {len(args)}.")
        for a, (_, t) in zip(e.args, args):
            self.need(a, t, NUM)
        return pattern.format(*[c for c, _ in args]), NUM


def shader_glsl(shader, kind, compiler):
    """GLSL for a shader used as `kind`, and the names it needs from outside."""
    gen = ShaderGen(shader, kind, compiler)
    return gen.generate(), list(gen.outside)
