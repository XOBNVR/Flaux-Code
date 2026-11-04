"""The JavaScript backend: turns a Flaux program into JavaScript, so it can
run in a web browser (Flaux Studio on the web).

The C backend (compiler.py) has already worked out the type of everything;
this backend asks it whenever whole numbers and decimals behave
differently (like `//` and `^`). Every Flaux function becomes an `async`
JavaScript function, so programs can wait for the next frame of a game or
for someone to type an answer without freezing the page.

The JavaScript it makes expects the runtime `fx` (flauxc/web/fx.js).
"""

import json

from . import graphics
from . import nodes as N
from .compiler import SHADER_INPUT, Compiler, compile_source  # noqa: F401  (compile_source re-exported)
from .errors import FlauxError
from .font import FONT_5X7
from .lexer import Lexer
from .parser import Parser
from .types import INT, FLOAT, BOOL, TEXT, SHADER, is_list, resolve


class JsGen:
    def __init__(self, compiler):
        self.c = compiler
        self.out = []
        self.depth = 0
        self.tmp_count = 0

    # ---------------------------------------------------------------- helpers

    def emit(self, line):
        self.out.append("  " * self.depth + line)

    def tmp(self):
        self.tmp_count += 1
        return f"_t{self.tmp_count}"

    def type_of(self, e):
        return self.c.expr(e)[1]

    def zero(self, t):
        t = resolve(t)
        if t in (INT, FLOAT):
            return "0"
        if t == BOOL:
            return "false"
        if t == TEXT:
            return '""'
        if is_list(t):
            return "[]"
        return "0"

    # ---------------------------------------------------------------- program

    def generate(self):
        c = self.c
        if c.uses_graphics:
            colors = {name: f"#{rgb:06x}" for name, rgb in graphics.COLORS.items()}
            self.emit(f"fx.setupGraphics({json.dumps(colors)}, {json.dumps(FONT_5X7)});")
        if c.uses_3d:
            shaders = [{"name": name,
                        "surface": c.shader_uses.get((name, "surface"), (None,))[0],
                        "screen": c.shader_uses.get((name, "screen"), (None,))[0]} for name in c.shaders]
            self.emit(f"fx.setup3d({json.dumps(graphics.glsl_sections())}, {json.dumps(shaders)});")
        for spec in c.used:
            self.function(spec)
        self.function(c.main)
        return "\n".join(self.out) + "\n"

    def function(self, scope):
        self.c.scope = scope
        params = [f"v_{p}" for p in scope.params]
        name = "fx_main" if scope is self.c.main else scope.c_name
        self.emit(f"async function {name}({', '.join(params)}) {{")
        self.depth += 1
        local = [f"v_{n} = {self.zero(t)}" for n, t in scope.vars.items() if n not in scope.params]
        if local:
            self.emit(f"let {', '.join(local)};")
        self.block(scope.body)
        self.depth -= 1
        self.emit("}")

    def block(self, stmts):
        for s in stmts:
            getattr(self, "s_" + type(s).__name__)(s)

    def body(self, stmts, loop=False):
        self.depth += 1
        if loop:  # let the page breathe (and the Stop button work) in long loops
            self.emit("if (++fx.steps > 1000) await fx.breathe();")
        self.block(stmts)
        self.depth -= 1

    # ------------------------------------------------------------- statements

    def s_Say(self, s):
        self.emit(f"fx.say([{', '.join(self.ex(v) for v in s.values)}]);")

    def s_Set(self, s):
        if isinstance(s.target, N.Name):
            self.emit(f"v_{s.target.name} = {self.ex(s.value)};")
        else:
            t = s.target
            self.emit(f"fx.set({self.ex(t.base)}, {self.ex(t.index)}, {self.ex(s.value)}, {t.line});")

    def s_Add(self, s):
        t = s.target
        tt = self.type_of(t)
        vt = self.type_of(s.value)
        v = self.ex(s.value)
        if is_list(tt):
            mode = "extend" if (vt == tt and tt[1] != "?") else "push"
            if mode == "extend":
                self.emit(f"fx.extend({self.ex(t)}, {v});")
            else:
                self.emit(f"{self.ex(t)}.push({v});")
            return
        if isinstance(t, N.Name):
            if tt == TEXT:
                self.emit(f"v_{t.name} = v_{t.name} + fx.str({v});")
            else:
                self.emit(f"v_{t.name} += {v};")
        else:
            op = "text" if tt == TEXT else "num"
            self.emit(f"fx.addAt({self.ex(t.base)}, {self.ex(t.index)}, {v}, '{op}', {t.line});")

    def s_Subtract(self, s):
        t = s.target
        v = self.ex(s.value)
        if isinstance(t, N.Name):
            self.emit(f"v_{t.name} -= {v};")
        else:
            self.emit(f"fx.addAt({self.ex(t.base)}, {self.ex(t.index)}, -({v}), 'num', {t.line});")

    def s_If(self, s):
        for i, (cond, body) in enumerate(s.branches):
            self.emit(("if" if i == 0 else "} else if") + f" ({self.ex(cond)}) {{")
            self.body(body)
        if s.else_body is not None:
            self.emit("} else {")
            self.body(s.else_body)
        self.emit("}")

    def s_RepeatTimes(self, s):
        i, n = self.tmp(), self.tmp()
        self.emit(f"for (let {i} = 0, {n} = {self.ex(s.count)}; {i} < {n}; {i}++) {{")
        self.body(s.body, loop=True)
        self.emit("}")

    def s_RepeatWhile(self, s):
        cond = self.ex(s.cond)
        self.emit(f"while (!({cond})) {{" if s.until else f"while ({cond}) {{")
        self.body(s.body, loop=True)
        self.emit("}")

    def s_RepeatForever(self, s):
        self.emit("while (true) {")
        self.body(s.body, loop=True)
        self.emit("}")

    def s_RepeatRange(self, s):
        v = f"v_{s.var.name}"
        end, step = self.tmp(), self.tmp()
        step_code = self.ex(s.step) if s.step is not None else "1"
        self.emit("{")
        self.depth += 1
        self.emit(f"const {end} = {self.ex(s.end)}, {step} = {step_code};")
        if isinstance(s.step, N.Num) or s.step is None:
            positive = s.step is None or s.step.value > 0
            cond = f"{v} {'<=' if positive else '>='} {end}"
        else:
            self.emit(f'if ({step} === 0) fx.fail({s.step.line}, "Make sure the number after `by` is never 0.", '
                      f'"Counting by 0 would never finish.");')
            cond = f"({step} > 0 ? {v} <= {end} : {v} >= {end})"
        self.emit(f"for ({v} = {self.ex(s.start)}; {cond}; {v} += {step}) {{")
        self.body(s.body, loop=True)
        self.emit("}")
        self.depth -= 1
        self.emit("}")

    def s_RepeatEach(self, s):
        items, k = self.tmp(), self.tmp()
        it = self.type_of(s.iterable)
        self.emit("{")
        self.depth += 1
        source = self.ex(s.iterable)
        self.emit(f"const {items} = {source if is_list(it) else f'Array.from({source})'};")
        self.emit(f"for (let {k} = 0; {k} < {items}.length; {k}++) {{")
        self.depth += 1
        self.emit(f"v_{s.var.name} = {items}[{k}];")
        self.depth -= 1
        self.body(s.body, loop=True)
        self.emit("}")
        self.depth -= 1
        self.emit("}")

    def s_Wait(self, s):
        seconds = {"s": 1, "m": 60, "h": 3600, "d": 86400}[s.unit]
        self.emit(f"await fx.wait({self.ex(s.amount)}, {seconds}, {s.line});")

    def s_Stop(self, s):
        self.emit("break;")

    def s_Skip(self, s):
        self.emit("continue;")

    def s_Return(self, s):
        self.emit("return;" if s.value is None else f"return {self.ex(s.value)};")

    def s_ShaderDef(self, s):
        pass   # shaders run on the graphics card: their GLSL goes to fx.setup3d

    def s_ExprStmt(self, s):
        self.emit(self.ex(s.expr) + ";")

    # ------------------------------------------------------------ expressions

    def ex(self, e):
        return getattr(self, "x_" + type(e).__name__)(e)

    def x_Num(self, e):
        return repr(float(e.value)) if e.is_float else str(e.value)

    def x_Str(self, e):
        if all(isinstance(p, str) for p in e.parts):
            return json.dumps("".join(e.parts))
        parts = [json.dumps(p) if isinstance(p, str) else f"fx.str({self.ex(p)})" for p in e.parts]
        return f"({' + '.join(parts)})"

    def x_Bool(self, e):
        return "true" if e.value else "false"

    def x_Name(self, e):
        return f"v_{e.name}"

    def x_ListLit(self, e):
        return "[" + ", ".join(self.ex(i) for i in e.items) + "]"

    def x_Copies(self, e):
        return f"fx.copies({self.ex(e.count)}, () => ({self.ex(e.value)}), {e.line})"

    def x_Unary(self, e):
        return f"(!{self.ex(e.operand)})" if e.op == "not" else f"(-{self.ex(e.operand)})"

    def x_Length(self, e):
        return f"fx.len({self.ex(e.operand)})"

    def x_Index(self, e):
        return f"fx.get({self.ex(e.base)}, {self.ex(e.index)}, {e.line})"

    def x_Binary(self, e):
        a, b, op = self.ex(e.left), self.ex(e.right), e.op
        if op == "and":
            return f"({a} && {b})"
        if op == "or":
            return f"({a} || {b})"
        if op == "=":
            return f"({a} === {b})"
        if op == "!=":
            return f"({a} !== {b})"
        if op in ("<", ">", "<=", ">="):
            return f"({a} {op} {b})"
        lt, rt = self.type_of(e.left), self.type_of(e.right)
        if op == "+":
            if TEXT in (lt, rt):
                return f"(fx.str({a}) + fx.str({b}))"
            if is_list(lt):
                return f"[...{a}, ...{b}]"
            return f"({a} + {b})"
        if op in ("-", "*"):
            return f"({a} {op} {b})"
        if op == "/":
            return f"fx.div({a}, {b}, {e.line})"
        whole = lt == INT and rt == INT
        if op == "//":
            return f"fx.floorDiv({a}, {b}, {e.line})"
        if op == "mod":
            return f"fx.mod({a}, {b}, {e.line})"
        if op == "^":
            return f"fx.ipow({a}, {b}, {e.line})" if whole else f"Math.pow({a}, {b})"
        raise FlauxError(f"Internal error: the web version can't do `{op}` yet.")

    def x_Call(self, e):
        f = self.c.funcs.get(e.name)
        if f is not None:
            # the same specialised function the C backend uses
            key = (f.name, tuple(self.type_of(a) for a in e.args))
            spec = self.c.specs[key]
            return f"(await {spec.c_name}({', '.join(self.ex(a) for a in e.args)}))"
        name = e.name
        paint_at = SHADER_INPUT.get(name)
        args = []
        for i, a in enumerate(e.args):
            if i == paint_at and isinstance(a, N.Name) and self.c.expr(a, SHADER)[1] == SHADER:
                kind = "screen" if name == "effect" else "surface"
                _, outside = self.c.shader_uses[(a.name, kind)]
                values = [f"(v_{v} ? 1 : 0)" if self.type_of(N.Name(v, line=a.line, col=a.col)) == BOOL else f"v_{v}"
                          for v in outside]
                args.append(f"{{shader: {self.c.shader_ids[a.name]}, values: [{', '.join(values)}]}}")
            else:
                args.append(self.ex(a))
        if name == "random":
            if len(e.args) == 1:
                return f"fx.pick({args[0]}, {e.line})"
            whole = all(self.type_of(a) == INT for a in e.args)
            return f"fx.random{'Int' if whole else 'Float'}({args[0]}, {args[1]}, {e.line})"
        if name in ("ask", "show", "image", "image_width", "image_height"):
            return f"(await fx.b.{name}([{', '.join(args)}], {e.line}))"
        return f"fx.b.{name}([{', '.join(args)}], {e.line})"


def compile_js(source, filename="program.flx"):
    """Flaux source code to JavaScript (raises FlauxError, like compile_source)."""
    lexer = Lexer(source)
    tokens = lexer.run()
    parser = Parser(tokens, source)
    try:
        program = parser.parse_program()
    except FlauxError:
        if lexer.first_error and parser.peek().line >= lexer.first_error.line:
            raise lexer.first_error
        raise
    if lexer.first_error:
        raise lexer.first_error
    compiler = Compiler(program, filename, source)
    compiler.check()
    return JsGen(compiler).generate()
