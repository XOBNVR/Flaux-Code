"""Turns a Flaux program into blocks for Flaux Studio's block editor.

The result is a Blockly workspace (its JSON "serialization" format). The
page turns blocks back into code itself (see blocks.js), so a program can
switch between Blocks and Code as often as you like.

Anything that has no block of its own becomes a "code" block holding the
exact text, so nothing is ever lost. `# notes` on their own line become note
blocks, and notes at the end of a line are attached to that line's block.
"""

import json
import os

from .. import nodes as N
from ..errors import FlauxError
from ..graphics import COLORS, normalize
from ..lexer import Lexer
from ..parser import Parser

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, "blocks_table.json"), encoding="utf-8") as _fh:
    TABLE = json.load(_fh)
FUNCTION_BLOCKS = {(f["name"], len(f["args"])): f for f in TABLE["functions"]}
KEYS = set(TABLE["keys"])
COLOR_NAMES = [n for n in COLORS if n not in ("grey", "darkgrey", "lightgrey", "skyblue")]

ARITH = {"+": "ADD", "-": "SUB", "*": "MUL", "/": "DIV", "//": "IDIV", "mod": "MOD", "^": "POW"}
COMPARE = {"=": "EQ", "!=": "NEQ", "<": "LT", ">": "GT", "<=": "LTE", ">=": "GTE"}


def function_block_type(name, arity):
    return f"fn_{name}_{arity}"


def info():
    """What the page needs to build its blocks."""
    return {"table": TABLE, "colors": COLOR_NAMES}


def returns_value(func):
    def walk(stmts):
        for s in stmts:
            if isinstance(s, N.Return) and s.value is not None:
                return True
            for body in bodies(s):
                if walk(body):
                    return True
        return False
    return walk(func.body)


def bodies(s):
    if isinstance(s, N.If):
        return [b for _, b in s.branches] + ([s.else_body] if s.else_body else [])
    if isinstance(s, (N.RepeatTimes, N.RepeatWhile, N.RepeatForever, N.RepeatRange, N.RepeatEach)):
        return [s.body]
    return []


def last_line(node):
    """The last line a statement uses (its `end`, for blocks)."""
    return max(getattr(node, "end_line", 0) or 0, getattr(node, "last_line", 0) or 0, node.line)


class ToBlocks:
    def __init__(self, source, program):
        self.source = source
        self.lines = source.split("\n")
        self.program = program
        self.functions = {f.name: f for f in program.funcs}
        self.count = 0
        self.variables = {}
        self.comments, self.trailing = self.find_notes()

    # ---------------------------------------------------------------- notes

    def find_notes(self):
        """Lines that are only a # note, and notes after code on a line."""
        whole, trailing = {}, {}
        for n, line in enumerate(self.lines, 1):
            in_text, escaped = False, False
            for i, ch in enumerate(line):
                if in_text:
                    if escaped:
                        escaped = False
                    elif ch == "\\":
                        escaped = True
                    elif ch == '"':
                        in_text = False
                elif ch == '"':
                    in_text = True
                elif ch == "#":
                    note = line[i + 1:]
                    note = note[1:] if note.startswith(" ") else note
                    if line[:i].strip():
                        trailing[n] = note.rstrip()
                    else:
                        whole[n] = note.rstrip()
                    break
        return whole, trailing

    # --------------------------------------------------------------- basics

    def new_id(self):
        self.count += 1
        return f"b{self.count}"

    def var(self, name):
        if name not in self.variables:
            self.variables[name] = f"v_{name}"
        return {"id": self.variables[name]}

    def block(self, kind, fields=None, inputs=None, extra=None):
        b = {"type": kind, "id": self.new_id()}
        if fields:
            b["fields"] = fields
        if inputs:
            b["inputs"] = {k: v for k, v in inputs.items() if v is not None}
        if extra is not None:
            b["extraState"] = extra
        return b

    def src(self, node):
        line = self.lines[node.line - 1] if 1 <= node.line <= len(self.lines) else ""
        return line[node.col - 1: node.col - 1 + node.length]

    # ----------------------------------------------------------- statements

    def chain(self, stmts, start, end):
        """Blocks joined top to bottom, with note blocks for # lines between them."""
        spans = [(s.line, last_line(s)) for s in stmts]
        items = [(n, self.block("flx_note", {"TEXT": text}))
                 for n, text in sorted(self.comments.items())
                 if start < n < end and not any(a <= n <= b for a, b in spans)]
        for st in stmts:
            made = self.stmt(st)
            for k, b in enumerate(made if isinstance(made, list) else [made]):
                items.append((st.line + k * 1e-3, b))
        items.sort(key=lambda item: item[0])
        for (_, upper), (_, lower) in zip(items, items[1:]):
            upper["next"] = {"block": lower}
        return {"block": items[0][1]} if items else None

    def stmt(self, s):
        method = getattr(self, "s_" + type(s).__name__, None)
        b = method(s) if method else None
        if b is None:  # no block for this: keep the code exactly, one line per block
            text = self.raw_text(s).split("\n")
            b = [self.block("flx_raw_stmt", {"CODE": t.strip()}) for t in text if t.strip()]
            if len(b) == 1:
                b = b[0]
            elif not b:
                b = self.block("flx_raw_stmt", {"CODE": ""})
            if isinstance(b, list):
                return b
        if s.line in self.trailing:
            b["icons"] = {"comment": {"text": self.trailing[s.line], "pinned": False,
                                      "height": 60, "width": 200}}
        return b

    def raw_text(self, s):
        text_lines = self.lines[s.line - 1: last_line(s)]
        indent = min((len(l) - len(l.lstrip()) for l in text_lines if l.strip()), default=0)
        out = [l[indent:] for l in text_lines]
        if s.line in self.trailing:  # the note is kept on the block instead
            first = out[0]
            out[0] = first[: len(first) - len(self.trailing[s.line])].rstrip().rstrip("#").rstrip()
        return "\n".join(out)

    def s_Say(self, s):
        if not s.values:
            return self.block("flx_say_blank")
        if len(s.values) == 1:
            return self.block("flx_say", inputs={"VALUE": self.val(s.values[0])})
        if len(s.values) <= 4:
            return self.block(f"flx_say_{len(s.values)}",
                              inputs={f"VALUE{i}": self.val(v) for i, v in enumerate(s.values)})
        return None

    def s_Set(self, s):
        if isinstance(s.target, N.Name):
            return self.block("variables_set", {"VAR": self.var(s.target.name)}, {"VALUE": self.val(s.value)})
        if isinstance(s.target, N.Index):
            return self.block("flx_set_item", inputs={"INDEX": self.val(s.target.index),
                                                      "LIST": self.val(s.target.base),
                                                      "VALUE": self.val(s.value)})
        return None

    def s_Add(self, s):
        return self.change(s, "flx_add")

    def s_Subtract(self, s):
        return self.change(s, "flx_subtract")

    def change(self, s, kind):
        if isinstance(s.target, N.Name):
            return self.block(kind, {"VAR": self.var(s.target.name)}, {"VALUE": self.val(s.value)})
        if isinstance(s.target, N.Index):  # add 1 to scores[i]
            return self.block(kind + "_item", inputs={"VALUE": self.val(s.value), "INDEX": self.val(s.target.index),
                                                      "LIST": self.val(s.target.base)})
        return None

    def s_If(self, s):
        inputs = {}
        marks = [s.line] + list(s.else_lines) + [s.end_line]
        for i, (cond, body) in enumerate(s.branches):
            inputs[f"IF{i}"] = self.val(cond)
            inputs[f"DO{i}"] = self.chain(body, marks[i], marks[i + 1])
        extra = {}
        if len(s.branches) > 1:
            extra["elseIfCount"] = len(s.branches) - 1
        if s.else_body is not None:
            extra["hasElse"] = True
            inputs["ELSE"] = self.chain(s.else_body, marks[-2], marks[-1])
        return self.block("controls_if", inputs=inputs, extra=extra or None)

    def s_RepeatTimes(self, s):
        return self.block("flx_repeat", inputs={"TIMES": self.val(s.count),
                                                "DO": self.chain(s.body, s.line, s.end_line)})

    def s_RepeatWhile(self, s):
        return self.block("flx_repeat_while", {"MODE": "UNTIL" if s.until else "WHILE"},
                          {"COND": self.val(s.cond), "DO": self.chain(s.body, s.line, s.end_line)})

    def s_RepeatForever(self, s):
        return self.block("flx_forever", inputs={"DO": self.chain(s.body, s.line, s.end_line)})

    def s_RepeatRange(self, s):
        inputs = {"FROM": self.val(s.start), "TO": self.val(s.end),
                  "DO": self.chain(s.body, s.line, s.end_line)}
        if s.step is None:
            return self.block("flx_repeat_range", {"VAR": self.var(s.var.name)}, inputs)
        inputs["BY"] = self.val(s.step)
        return self.block("flx_repeat_range_by", {"VAR": self.var(s.var.name)}, inputs)

    def s_RepeatEach(self, s):
        return self.block("flx_repeat_each", {"VAR": self.var(s.var.name)},
                          {"LIST": self.val(s.iterable), "DO": self.chain(s.body, s.line, s.end_line)})

    def s_Stop(self, s):
        return self.block("flx_stop")

    def s_Skip(self, s):
        return self.block("flx_skip")

    def s_Return(self, s):
        if s.value is None:
            return self.block("flx_return_nothing")
        return self.block("flx_return", inputs={"VALUE": self.val(s.value)})

    def s_ExprStmt(self, s):
        call = s.expr
        if call.name in self.functions:
            return self.call_block(call, "procedures_callnoreturn")
        entry = FUNCTION_BLOCKS.get((call.name, len(call.args)))
        if entry and entry["kind"] == "statement":
            return self.function_block(call, entry)
        return None

    # ---------------------------------------------------------- functions

    def function(self, f):
        gives_back = returns_value(f)
        body = list(f.body)
        final = None
        if gives_back and body and isinstance(body[-1], N.Return) and body[-1].value is not None \
                and body[-1].line not in self.trailing:
            final = body.pop()
        params = [{"name": p.name, "id": self.var(p.name)["id"]} for p in f.params]
        inputs = {"STACK": self.chain(body, f.line, final.line if final else f.end_line)}
        if final is not None:
            inputs["RETURN"] = self.val(final.value)
        kind = "procedures_defreturn" if gives_back else "procedures_defnoreturn"
        b = self.block(kind, {"NAME": f.name}, inputs, {"params": params} if params else None)
        if f.line in self.trailing:
            b["icons"] = {"comment": {"text": self.trailing[f.line], "pinned": False, "height": 60, "width": 200}}
        return b

    def call_block(self, call, kind):
        f = self.functions[call.name]
        inputs = {f"ARG{i}": self.val(a) for i, a in enumerate(call.args)}
        return self.block(kind, inputs=inputs, extra={"name": f.name, "params": [p.name for p in f.params]})

    def function_block(self, call, entry):
        inputs = {}
        for i, (arg, kind) in enumerate(zip(call.args, entry["args"])):
            inputs[f"ARG{i}"] = self.special_arg(arg, kind) or self.val(arg)
        return self.block(function_block_type(entry["name"], len(entry["args"])), inputs=inputs)

    def special_arg(self, arg, kind):
        """Colour and key names get a dropdown block."""
        if not (isinstance(arg, N.Str) and len(arg.parts) == 1 and isinstance(arg.parts[0], str)):
            return None
        text = arg.parts[0]
        if kind == "colour" and normalize(text) in COLOR_NAMES:
            return {"shadow": self.block("flx_colour", {"COLOUR": normalize(text)})}
        if kind == "key" and text.lower() in KEYS:
            return {"shadow": self.block("flx_key", {"KEY": text.lower()})}
        return None

    # --------------------------------------------------------------- values

    def val(self, e):
        method = getattr(self, "e_" + type(e).__name__, None)
        result = method(e) if method else None
        if result is None:
            return {"block": self.block("flx_raw_expr", {"CODE": self.src(e)})}
        return result

    def e_Num(self, e):
        text = self.src(e).strip().strip("()").strip()
        try:
            float(text.replace("_", ""))
        except ValueError:
            text = repr(e.value)
        return {"shadow": self.block("flx_number", {"NUM": text})}

    def e_Str(self, e):
        token = self.src(e)
        first, last = token.find('"'), token.rfind('"')
        if first < 0 or last <= first:
            return None
        return {"shadow": self.block("text", {"TEXT": token[first + 1:last]})}

    def e_Bool(self, e):
        return {"block": self.block("logic_boolean", {"BOOL": "TRUE" if e.value else "FALSE"})}

    def e_Name(self, e):
        return {"block": self.block("variables_get", {"VAR": self.var(e.name)})}

    def e_ListLit(self, e):
        inputs = {f"ADD{i}": self.val(item) for i, item in enumerate(e.items)}
        return {"block": self.block("lists_create_with", inputs=inputs, extra={"itemCount": len(e.items)})}

    def e_Copies(self, e):
        return {"block": self.block("flx_copies", inputs={"COUNT": self.val(e.count), "VALUE": self.val(e.value)})}

    def e_Unary(self, e):
        kind = "flx_not" if e.op == "not" else "flx_negate"
        return {"block": self.block(kind, inputs={"VALUE": self.val(e.operand)})}

    def e_Binary(self, e):
        inputs = {"A": self.val(e.left), "B": self.val(e.right)}
        if e.op in ("and", "or"):
            return {"block": self.block("flx_logic", {"OP": e.op.upper()}, inputs)}
        if e.op in COMPARE:
            return {"block": self.block("flx_compare", {"OP": COMPARE[e.op]}, inputs)}
        return {"block": self.block("flx_arith", {"OP": ARITH[e.op]}, inputs)}

    def e_Index(self, e):
        return {"block": self.block("flx_index", inputs={"INDEX": self.val(e.index), "LIST": self.val(e.base)})}

    def e_Length(self, e):
        return {"block": self.block("flx_length", inputs={"VALUE": self.val(e.operand)})}

    def e_Call(self, e):
        if e.name in self.functions:
            return {"block": self.call_block(e, "procedures_callreturn")}
        if e.name == "length" and len(e.args) == 1:
            return {"block": self.block("flx_length", inputs={"VALUE": self.val(e.args[0])})}
        entry = FUNCTION_BLOCKS.get((e.name, len(e.args)))
        if entry and entry["kind"] == "value":
            return {"block": self.function_block(e, entry)}
        return None

    # ----------------------------------------------------------------- all

    def workspace(self):
        """Each run of top-level code, and each function, is its own stack,
        placed in the same order as in the program."""
        groups = []   # (first line, top block)
        run, run_start, prev_end = [], 0, 0
        items = sorted([(st.line, st) for st in self.program.main] +
                       [(f.line, f) for f in self.program.funcs], key=lambda item: item[0])

        def close_run(end):
            # the code (and notes) between the previous function and `end`
            chain = self.chain(run, run_start if run else prev_end, end)
            if chain:
                first = chain["block"]
                groups.append((run[0].line if run else end - 0.5, first))

        for _, node in items:
            if isinstance(node, N.FuncDef):
                close_run(node.line)
                run = []
                groups.append((node.line, self.function(node)))
                prev_end = node.end_line
            else:
                if not run:
                    run_start = prev_end
                run.append(node)
        close_run(len(self.lines) + 1)

        blocks, y = [], 20
        for _, b in sorted(groups, key=lambda g: g[0]):
            b["x"], b["y"] = 20, y
            y += 44 * self.rows(b) + 50
            blocks.append(b)
        variables = [{"name": name, "id": vid} for name, vid in self.variables.items()]
        return {"blocks": {"languageVersion": 0, "blocks": blocks}, "variables": variables}

    def rows(self, b):
        """Roughly how many rows of blocks a stack takes (for spacing stacks out)."""
        count, todo = 0, [b]
        while todo:
            block = todo.pop()
            count += 1
            for key, inp in (block.get("inputs") or {}).items():
                if key.startswith(("DO", "ELSE", "STACK")) and inp and inp.get("block"):
                    todo.append(inp["block"])
            if block.get("next"):
                todo.append(block["next"]["block"])
        return count + 1


def to_blocks(source):
    """Blocks for a program, or a problem if the program can't be read."""
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
    return ToBlocks(source, program).workspace()
