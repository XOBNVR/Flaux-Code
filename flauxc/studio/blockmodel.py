"""Blocks for Flaux Studio, the desktop app (block_editor.py draws them):
what blocks are, what each kind looks like, and blocks -> Flaux code.

Code -> blocks is blocks.py (the same one the web version uses), so both
versions agree on every block. The code this makes is exactly what the web
version's blocks.js makes, so a program goes through blocks and back
unchanged (tests/run_tests.py checks every example).
"""

import json
import os
import re

from ..graphics import COLORS

HERE = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, "blocks_table.json"), encoding="utf-8") as _fh:
    TABLE = json.load(_fh)
CATEGORIES = TABLE["categories"]
FUNCTIONS = TABLE["functions"]
FUNCTION_TYPES = {f"fn_{f['name']}_{len(f['args'])}": f for f in FUNCTIONS}
COLOR_NAMES = sorted(COLORS)


class Block:
    """One block: its kind, its fields (words and numbers you type or pick), its
    inputs (blocks plugged in: values, or the first block of a stack inside it),
    the block under it, and anything extra (how many else-ifs, a function's inputs)."""

    _next_id = 1

    def __init__(self, type, fields=None, inputs=None, extra=None, next=None, comment=None):
        self.type = type
        self.fields = dict(fields or {})
        self.inputs = dict(inputs or {})
        self.extra = dict(extra or {})
        self.next = next
        self.comment = comment
        self.parent = None          # (set by the editor while it draws)
        self.x = self.y = 0
        self.id = f"n{Block._next_id}"
        Block._next_id += 1

    def copy(self):
        b = Block(self.type, self.fields, {k: v.copy() if v else None for k, v in self.inputs.items()},
                  json.loads(json.dumps(self.extra)), self.next.copy() if self.next else None, self.comment)
        b.x, b.y = self.x, self.y
        return b

    def walk(self):
        """This block, everything plugged into it, and everything under it."""
        yield self
        for child in self.inputs.values():
            if child:
                yield from child.walk()
        if self.next:
            yield from self.next.walk()


# ------------------------------------------------------------ workspace -> blocks

def from_workspace(ws):
    """The blocks in a workspace from blocks.py (Blockly's saved form): the top blocks."""
    names = {v["id"]: v["name"] for v in ws.get("variables", [])}

    def make(d):
        if not d:
            return None
        fields = {}
        for k, v in (d.get("fields") or {}).items():
            fields[k] = names.get(v["id"], v.get("name", "")) if isinstance(v, dict) else str(v)
        inputs = {}
        for k, v in (d.get("inputs") or {}).items():
            inputs[k] = make(v.get("block") or v.get("shadow"))
        comment = ((d.get("icons") or {}).get("comment") or {}).get("text")
        b = Block(d["type"], fields, inputs, d.get("extraState"), make((d.get("next") or {}).get("block")), comment)
        b.x, b.y = d.get("x", 0), d.get("y", 0)
        return b

    return [make(d) for d in ws["blocks"]["blocks"]]


# ---------------------------------------------------------------- the look
#
# A block's look: a list of rows. Each row is a list of parts:
#   "words"            words on the block
#   ("v", NAME)        a space for a value block
#   ("f", NAME)        a box to type in
#   ("d", NAME, opts)  a box to pick from: opts are [(shown, value)]
#   ("var",)           a variable's name (pick one, or make one)
# and a row can be ("s", NAME): a mouth holding a stack of blocks.

ARITH = [("+", "ADD"), ("−", "SUB"), ("×", "MUL"), ("÷", "DIV"), ("÷ whole", "IDIV"), ("mod", "MOD"), ("^", "POW")]
COMPARE = [("=", "EQ"), ("≠", "NEQ"), ("<", "LT"), (">", "GT"), ("≤", "LTE"), ("≥", "GTE")]
UNITS = [("seconds", "s"), ("minutes", "m"), ("hours", "h"), ("days", "d")]

SPECS = {   # type: (category, "statement" | "value" | "end" | "top", rows)
    "flx_say": ("Say & Ask", "statement", [["say", ("v", "VALUE")]]),
    "flx_say_blank": ("Say & Ask", "statement", [["say an empty line"]]),
    "flx_say_same": ("Say & Ask", "statement", [["say", ("v", "VALUE"), "and stay on this line"]]),
    "flx_say_2": ("Say & Ask", "statement", [["say", ("v", "VALUE0"), ("v", "VALUE1")]]),
    "flx_say_3": ("Say & Ask", "statement", [["say", ("v", "VALUE0"), ("v", "VALUE1"), ("v", "VALUE2")]]),
    "flx_say_4": ("Say & Ask", "statement", [["say", ("v", "VALUE0"), ("v", "VALUE1"), ("v", "VALUE2"),
                                              ("v", "VALUE3")]]),
    "variables_set": ("Variables", "statement", [["set", ("var",), "to", ("v", "VALUE")]]),
    "variables_get": ("Variables", "value", [[("var",)]]),
    "flx_add": ("Variables", "statement", [["add", ("v", "VALUE"), "to", ("var",)]]),
    "flx_subtract": ("Variables", "statement", [["subtract", ("v", "VALUE"), "from", ("var",)]]),
    "flx_set_item": ("Lists", "statement", [["set item", ("v", "INDEX"), "of", ("v", "LIST"), "to", ("v", "VALUE")]]),
    "flx_add_item": ("Lists", "statement", [["add", ("v", "VALUE"), "to item", ("v", "INDEX"), "of", ("v", "LIST")]]),
    "flx_subtract_item": ("Lists", "statement", [["subtract", ("v", "VALUE"), "from item", ("v", "INDEX"), "of",
                                                  ("v", "LIST")]]),
    "flx_repeat": ("Control", "statement", [["repeat", ("v", "TIMES"), "times"], ("s", "DO")]),
    "flx_repeat_range": ("Control", "statement", [["repeat with", ("var",), "from", ("v", "FROM"), "to", ("v", "TO")],
                                                  ("s", "DO")]),
    "flx_repeat_range_by": ("Control", "statement", [["repeat with", ("var",), "from", ("v", "FROM"), "to",
                                                      ("v", "TO"), "by", ("v", "BY")], ("s", "DO")]),
    "flx_repeat_each": ("Control", "statement", [["repeat with", ("var",), "in", ("v", "LIST")], ("s", "DO")]),
    "flx_repeat_while": ("Control", "statement", [["repeat", ("d", "MODE", [("while", "WHILE"), ("until", "UNTIL")]),
                                                   ("v", "COND")], ("s", "DO")]),
    "flx_forever": ("Control", "statement", [["repeat forever"], ("s", "DO")]),
    "flx_wait": ("Control", "statement", [["wait", ("v", "VALUE"), ("d", "UNIT", UNITS)]]),
    "flx_stop": ("Control", "end", [["stop repeating"]]),
    "flx_skip": ("Control", "end", [["skip to the next time round"]]),
    "flx_return": ("Functions", "end", [["give back", ("v", "VALUE")]]),
    "flx_return_nothing": ("Functions", "end", [["leave the function"]]),
    "flx_note": ("Notes", "statement", [["#", ("f", "TEXT")]]),
    "flx_raw_stmt": ("Notes", "statement", [["code", ("f", "CODE")]]),
    "flx_raw_expr": ("Notes", "value", [["code", ("f", "CODE")]]),
    "flx_number": ("Maths", "value", [[("f", "NUM")]]),
    "text": ("Text", "value", [[("f", "TEXT")]]),
    "logic_boolean": ("Checks", "value", [[("d", "BOOL", [("true", "TRUE"), ("false", "FALSE")])]]),
    "flx_arith": ("Maths", "value", [[("v", "A"), ("d", "OP", ARITH), ("v", "B")]]),
    "flx_compare": ("Checks", "value", [[("v", "A"), ("d", "OP", COMPARE), ("v", "B")]]),
    "flx_logic": ("Checks", "value", [[("v", "A"), ("d", "OP", [("and", "AND"), ("or", "OR")]), ("v", "B")]]),
    "flx_not": ("Checks", "value", [["not", ("v", "VALUE")]]),
    "flx_negate": ("Maths", "value", [["−", ("v", "VALUE")]]),
    "flx_index": ("Lists", "value", [["item", ("v", "INDEX"), "of", ("v", "LIST")]]),
    "flx_length": ("Lists", "value", [["length of", ("v", "VALUE")]]),
    "flx_copies": ("Lists", "value", [[("v", "COUNT"), "copies of", ("v", "VALUE")]]),
    "flx_colour": ("Drawing", "value", [[("d", "COLOUR", [(c, c) for c in COLOR_NAMES])]]),
    "flx_key": ("Keys & Mouse", "value", [[("d", "KEY", [(k, k) for k in TABLE["keys"]])]]),
    "flx_shader": ("3D", "top", [["shader", ("f", "NAME")], ("s", "DO")]),
}


def spec(b):
    """(category, shape, rows) for a block (some depend on the block: if/else, lists, functions)."""
    t = b.type
    if t in SPECS:
        return SPECS[t]
    if t == "controls_if":
        rows = [["if", ("v", "IF0")], ("s", "DO0")]
        for i in range(1, int(b.extra.get("elseIfCount", 0)) + 1):
            rows += [["else if", ("v", f"IF{i}")], ("s", f"DO{i}")]
        if b.extra.get("hasElse"):
            rows += [["else"], ("s", "ELSE")]
        return "Control", "statement", rows
    if t == "lists_create_with":
        n = int(b.extra.get("itemCount", 0))
        return "Lists", "value", [(["list of"] + [("v", f"ADD{i}") for i in range(n)]) if n else ["empty list"]]
    if t in ("procedures_defnoreturn", "procedures_defreturn"):
        params = [p["name"] for p in b.extra.get("params", [])]
        head = ["function", ("f", "NAME")] + ([("with inputs: " + ", ".join(params))] if params else [])
        rows = [head, ("s", "STACK")]
        if t == "procedures_defreturn":
            rows.append(["give back", ("v", "RETURN")])
        return "Functions", "top", rows
    if t in ("procedures_callnoreturn", "procedures_callreturn"):
        params = b.extra.get("params", [])
        row = [b.extra.get("name", "?")]
        for i, p in enumerate(params):
            row += [("with " if i == 0 else "") + p, ("v", f"ARG{i}")]
        return "Functions", ("statement" if t == "procedures_callnoreturn" else "value"), [row]
    if t == "procedures_ifreturn":
        return "Functions", "statement", [["if", ("v", "CONDITION"), "give back", ("v", "VALUE")]]
    f = FUNCTION_TYPES.get(t)
    if f:
        row = []
        for piece in re.split(r"(%\d+)", f["label"]):
            if re.fullmatch(r"%\d+", piece):
                row.append(("v", f"ARG{int(piece[1:]) - 1}"))
            elif piece.strip():
                row.append(piece.strip())
        return f["category"], ("statement" if f["kind"] == "statement" else "value"), [row]
    return "Notes", "statement", [[f"(a block Flaux doesn't know: {t})"]]


def colour(b):
    return CATEGORIES.get(spec(b)[0], "#7a7f99")


def is_value(b):
    return spec(b)[1] == "value"


# --------------------------------------------------------------- blocks -> code

def ident(name):
    s = re.sub(r"[^A-Za-z0-9_]+", "_", str(name).strip()).strip("_")
    return "unnamed" if not s else ("_" + s if s[0].isdigit() else s)


def text_code(raw):
    t = re.sub(r'(^|[^\\])"', r'\1\\"', str(raw))
    t = re.sub(r'(^|[^\\])"', r'\1\\"', t)
    if re.search(r'(^|[^\\])(\\\\)*\\$', t):
        t += "\\"
    return '"' + t + '"'


def generate(tops):
    """Flaux code for the top blocks (like blocks.js's generate): (code, the block for each line,
    problems as [(block, message)])."""
    lines, problems = [], []

    def empty(b):
        problems.append((b, "This block has an empty space. Drag a block into it."))
        return "___"

    def value(b, name, need=0):
        child = b.inputs.get(name)
        if not child:
            return empty(b)
        code, prec = expr(child)
        return f"({code})" if prec < need else code

    def args(b):
        out, i = [], 0
        while f"ARG{i}" in b.inputs:
            out.append(value(b, f"ARG{i}"))
            i += 1
        return ", ".join(out)

    def var(b):
        return ident(b.fields.get("VAR", ""))

    def expr(b):
        t = b.type
        if t == "flx_number":
            n = b.fields.get("NUM", "0").strip() or "0"
            return n, (8 if n.startswith("-") else 10)
        if t == "text":
            return text_code(b.fields.get("TEXT", "")), 10
        if t == "logic_boolean":
            return ("true" if b.fields.get("BOOL") == "TRUE" else "false"), 10
        if t == "variables_get":
            return var(b), 10
        if t == "lists_create_with":
            items = [value(b, f"ADD{i}") for i in range(int(b.extra.get("itemCount", 0))) if b.inputs.get(f"ADD{i}")]
            return "[" + ", ".join(items) + "]", 10
        if t == "flx_arith":
            op = b.fields.get("OP", "ADD")
            if op == "POW":
                return f"{value(b, 'A', 10)} ^ {value(b, 'B', 8)}", 9
            sym = {"ADD": "+", "SUB": "-", "MUL": "*", "DIV": "/", "IDIV": "//", "MOD": "mod"}[op]
            p = 6 if op in ("ADD", "SUB") else 7
            return f"{value(b, 'A', p)} {sym} {value(b, 'B', p + 1)}", p
        if t == "flx_compare":
            sym = {"EQ": "=", "NEQ": "!=", "LT": "<", "GT": ">", "LTE": "<=", "GTE": ">="}[b.fields.get("OP", "EQ")]
            return f"{value(b, 'A', 5)} {sym} {value(b, 'B', 5)}", 4
        if t == "flx_logic":
            if b.fields.get("OP") == "AND":
                return f"{value(b, 'A', 2)} and {value(b, 'B', 3)}", 2
            return f"{value(b, 'A', 1)} or {value(b, 'B', 2)}", 1
        if t == "flx_not":
            return f"not {value(b, 'VALUE', 3)}", 3
        if t == "flx_negate":
            return f"-{value(b, 'VALUE', 8)}", 8
        if t == "flx_index":
            return f"{value(b, 'LIST', 10)}[{value(b, 'INDEX')}]", 10
        if t == "flx_length":
            return f"length of {value(b, 'VALUE', 8)}", 8
        if t == "flx_copies":
            return f"{value(b, 'COUNT', 6)} copies of {value(b, 'VALUE', 6)}", 5
        if t == "flx_colour":
            return f'"{b.fields.get("COLOUR", "white")}"', 10
        if t == "flx_key":
            return f'"{b.fields.get("KEY", "space")}"', 10
        if t == "flx_raw_expr":
            return (b.fields.get("CODE", "").strip() or empty(b)), 0
        if t == "procedures_callreturn":
            return f"{ident(b.extra.get('name', ''))}({args(b)})", 10
        if t.startswith("fn_"):
            return f"{t[3:t.rindex('_')]}({args(b)})", 10
        problems.append((b, "Flaux doesn't know this block."))
        return "___", 10

    def body(b, name, indent):
        c = b.inputs.get(name)
        while c:
            stmt(c, indent)
            c = c.next

    def stmt(b, indent):
        first = len(lines)

        def emit(text, more=0):
            lines.append(["    " * (indent + more) + text, b])

        t = b.type
        if t == "flx_say":
            emit(f"say {value(b, 'VALUE')}")
        elif t == "flx_say_blank":
            emit("say()")
        elif t == "flx_say_same":
            emit(f"say.same {value(b, 'VALUE')}")
        elif t in ("flx_say_2", "flx_say_3", "flx_say_4"):
            parts, i = [], 0
            while f"VALUE{i}" in b.inputs or i < int(t[-1]):
                parts.append(value(b, f"VALUE{i}"))
                i += 1
            emit("say " + ", ".join(parts))
        elif t == "flx_add_item":
            emit(f"add {value(b, 'VALUE')} to {value(b, 'LIST', 10)}[{value(b, 'INDEX')}]")
        elif t == "flx_subtract_item":
            emit(f"subtract {value(b, 'VALUE')} from {value(b, 'LIST', 10)}[{value(b, 'INDEX')}]")
        elif t == "variables_set":
            emit(f"set {var(b)} to {value(b, 'VALUE')}")
        elif t == "flx_add":
            emit(f"add {value(b, 'VALUE')} to {var(b)}")
        elif t == "flx_subtract":
            emit(f"subtract {value(b, 'VALUE')} from {var(b)}")
        elif t == "flx_set_item":
            emit(f"set {value(b, 'LIST', 10)}[{value(b, 'INDEX')}] to {value(b, 'VALUE')}")
        elif t == "controls_if":
            emit(f"if {value(b, 'IF0')}")
            body(b, "DO0", indent + 1)
            for i in range(1, int(b.extra.get("elseIfCount", 0)) + 1):
                emit(f"else if {value(b, f'IF{i}')}")
                body(b, f"DO{i}", indent + 1)
            if b.extra.get("hasElse"):
                emit("else")
                body(b, "ELSE", indent + 1)
            emit("end")
        elif t == "procedures_ifreturn":
            emit(f"if {value(b, 'CONDITION')}")
            emit(f"return {value(b, 'VALUE')}" if b.inputs.get("VALUE") else "return", 1)
            emit("end")
        elif t == "flx_repeat":
            emit(f"repeat {value(b, 'TIMES')} times")
            body(b, "DO", indent + 1)
            emit("end")
        elif t == "flx_repeat_range":
            emit(f"repeat with {var(b)} from {value(b, 'FROM')} to {value(b, 'TO')}")
            body(b, "DO", indent + 1)
            emit("end")
        elif t == "flx_repeat_range_by":
            emit(f"repeat with {var(b)} from {value(b, 'FROM')} to {value(b, 'TO')} by {value(b, 'BY')}")
            body(b, "DO", indent + 1)
            emit("end")
        elif t == "flx_repeat_each":
            emit(f"repeat with {var(b)} in {value(b, 'LIST')}")
            body(b, "DO", indent + 1)
            emit("end")
        elif t == "flx_repeat_while":
            emit(f"repeat {'until' if b.fields.get('MODE') == 'UNTIL' else 'while'} {value(b, 'COND')}")
            body(b, "DO", indent + 1)
            emit("end")
        elif t == "flx_forever":
            emit("repeat forever")
            body(b, "DO", indent + 1)
            emit("end")
        elif t == "flx_wait":
            unit, child = b.fields.get("UNIT", "s"), b.inputs.get("VALUE")
            n = child.fields.get("NUM", "").strip() if child and child.type == "flx_number" else None
            if n and re.fullmatch(r"\d[\d_]*(\.\d+)?", n):
                emit(f"wait {n}{unit}")
            else:
                emit(f"wait {value(b, 'VALUE')} {dict(s='seconds', m='minutes', h='hours', d='days')[unit]}")
        elif t == "flx_stop":
            emit("stop")
        elif t == "flx_skip":
            emit("skip")
        elif t == "flx_return":
            emit(f"return {value(b, 'VALUE')}")
        elif t == "flx_return_nothing":
            emit("return")
        elif t == "flx_note":
            emit("# " + b.fields.get("TEXT", ""))
        elif t == "flx_raw_stmt":
            if b.fields.get("CODE", "").strip():
                emit(b.fields["CODE"].strip())
        elif t in ("procedures_defnoreturn", "procedures_defreturn"):
            params = [ident(p["name"]) for p in b.extra.get("params", [])]
            emit(f"function {ident(b.fields.get('NAME', ''))}({', '.join(params)})")
            body(b, "STACK", indent + 1)
            if t == "procedures_defreturn" and b.inputs.get("RETURN"):
                emit(f"return {value(b, 'RETURN')}", 1)
            emit("end")
        elif t == "procedures_callnoreturn":
            emit(f"{ident(b.extra.get('name', ''))}({args(b)})")
        elif t == "flx_shader":
            emit(f"shader {ident(b.fields.get('NAME', ''))}")
            body(b, "DO", indent + 1)
            emit("end")
        elif t.startswith("fn_"):
            emit(f"{t[3:t.rindex('_')]}({args(b)})")
        else:
            problems.append((b, "Flaux doesn't know this block."))
        if b.comment and len(lines) > first:
            lines[first][0] += "  # " + re.sub(r"\s*\n\s*", " ", b.comment)

    for i, top in enumerate(t for t in tops if not is_value(t)):
        if i and lines and lines[-1][0] != "":
            lines.append(["", None])
        c = top
        while c:
            stmt(c, 0)
            c = c.next
    code = "\n".join(text for text, _ in lines) + ("\n" if lines else "")
    return code, [b for _, b in lines], problems


# ------------------------------------------------------------------- the menus

def number(n):
    return Block("flx_number", {"NUM": str(n)})


def text(t):
    return Block("text", {"TEXT": t})


def palette(variables, functions):
    """The blocks in each menu: {category: [Block]}. `functions`: [(name, params, gives_back)]."""
    def fns(category):
        out = []
        for f in FUNCTIONS:
            if f["category"] != category or f.get("toolbox") is False:
                continue
            inputs = {}
            for i, d in enumerate(f.get("defaults") or []):
                kind = f["args"][i]
                inputs[f"ARG{i}"] = (Block("flx_colour", {"COLOUR": d}) if kind == "colour"
                                     else Block("flx_key", {"KEY": d}) if kind == "key"
                                     else number(d) if kind == "num" else text(d))
            out.append(Block(f"fn_{f['name']}_{len(f['args'])}", inputs=inputs))
        return out

    first = variables[0] if variables else "score"
    var_blocks = [Block("variables_set", {"VAR": first}, {"VALUE": number(0)}),
                  Block("flx_add", {"VAR": first}, {"VALUE": number(1)}),
                  Block("flx_subtract", {"VAR": first}, {"VALUE": number(1)})]
    var_blocks += [Block("variables_get", {"VAR": v}) for v in variables]
    fn_blocks = [Block("procedures_defnoreturn", {"NAME": "do_something"}),
                 Block("procedures_defreturn", {"NAME": "work_out"}, {"RETURN": number(0)})]
    for name, params, gives_back in functions:
        fn_blocks.append(Block("procedures_callreturn" if gives_back else "procedures_callnoreturn",
                               extra={"name": name, "params": list(params)}))
    fn_blocks += [Block("flx_return", inputs={"VALUE": number(0)}), Block("flx_return_nothing")]
    return {
        "Say & Ask": [Block("flx_say", inputs={"VALUE": text("Hello!")}),
                      Block("flx_say_2", inputs={"VALUE0": text("Score:"), "VALUE1": number(10)}),
                      Block("flx_say_same", inputs={"VALUE": text("hi ")}), Block("flx_say_blank")] + fns("Say & Ask"),
        "Variables": var_blocks,
        "Control": [Block("controls_if", inputs={"IF0": Block("flx_compare", {"OP": "GT"},
                                                                {"A": number(5), "B": number(3)})}),
                    Block("controls_if", extra={"hasElse": True}),
                    Block("flx_repeat", inputs={"TIMES": number(10)}),
                    Block("flx_repeat_range", {"VAR": "i"}, {"FROM": number(1), "TO": number(10)}),
                    Block("flx_repeat_each", {"VAR": "item"}),
                    Block("flx_repeat_while", {"MODE": "WHILE"}),
                    Block("flx_forever"),
                    Block("flx_repeat_range_by", {"VAR": "i"}, {"FROM": number(10), "TO": number(1), "BY": number(-1)}),
                    Block("flx_wait", {"UNIT": "s"}, {"VALUE": number(1)}),
                    Block("flx_stop"), Block("flx_skip")],
        "Checks": [Block("flx_compare", {"OP": "EQ"}, {"A": number(1), "B": number(2)}),
                   Block("flx_logic", {"OP": "AND"}), Block("flx_not"), Block("logic_boolean", {"BOOL": "TRUE"})],
        "Maths": [number(0), Block("flx_arith", {"OP": "ADD"}, {"A": number(1), "B": number(1)}),
                  Block("flx_negate")] + fns("Maths"),
        "Text": [text("hello")] + fns("Text"),
        "Lists": [Block("lists_create_with", inputs={"ADD0": number(1), "ADD1": number(2), "ADD2": number(3)},
                        extra={"itemCount": 3}),
                  Block("lists_create_with", extra={"itemCount": 0}),
                  Block("flx_index", inputs={"INDEX": number(1)}), Block("flx_length"),
                  Block("flx_copies", inputs={"COUNT": number(10), "VALUE": number(0)}),
                  Block("flx_set_item", inputs={"INDEX": number(1)}),
                  Block("flx_add_item", inputs={"VALUE": number(1), "INDEX": number(1)}),
                  Block("flx_subtract_item", inputs={"VALUE": number(1), "INDEX": number(1)})] + fns("Lists"),
        "Functions": fn_blocks,
        "Drawing": fns("Drawing") + [Block("flx_colour", {"COLOUR": "red"})],
        "3D": fns("3D") + [Block("flx_shader", {"NAME": "glow"},
                                 {"DO": Block("flx_return", inputs={"VALUE": Block("flx_colour", {"COLOUR": "gold"})})})],
        "Keys & Mouse": fns("Keys & Mouse") + [Block("flx_key", {"KEY": "space"})],
        "Sound": fns("Sound"),
        "Notes": [Block("flx_note", {"TEXT": "a note to myself"}), Block("flx_raw_stmt", {"CODE": 'say "hi"'}),
                  Block("flx_raw_expr", {"CODE": "1 + 2"})],
    }
