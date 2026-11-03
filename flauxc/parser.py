"""Parser: turns tokens into a syntax tree.

Grammar overview (lowest to highest precedence for expressions):

    expression := or
    or         := and ("or" and)*
    and        := not ("and" not)*
    not        := "not" not | compare
    compare    := copies (compare-op copies)?
                  compare-op: = == != < > <= >= is, is not, is more than,
                              is less than, is at least, is at most
    copies     := sum ("copies" "of" sum)?
    sum        := product (("+" | "-") product)*
    product    := unary (("*" | "/" | "//" | "%" | "mod") unary)*
    unary      := "-" unary | power
    power      := postfix ("^" unary)?
    postfix    := primary ("(" args ")" | "[" expression "]")*
    primary    := number | text | true | false | name | "(" expression ")"
                | "[" items "]" | "length" "of" unary

Errors try to teach: wherever we can, they show the line fixed.
"""

from . import nodes as N
from .errors import FlauxError

# Words that always mean something special and can't be used as names.
RESERVED = {
    "set", "add", "subtract", "say", "if", "else", "end", "repeat", "function",
    "return", "and", "or", "not", "is", "true", "false", "mod", "stop", "skip",
}

COMPARE_OPS = {"=": "=", "==": "=", "!=": "!=", "<": "<", ">": ">", "<=": "<=", ">=": ">="}
NEGATE = {"=": "!=", "!=": "=", "<": ">=", ">": "<=", "<=": ">", ">=": "<"}

# Words from other languages, and the Flaux word to use instead.
OTHER_LANGUAGE_WORDS = {
    "print": "say", "println": "say", "echo": "say", "puts": "say", "console": "say",
    "printf": "say", "cout": "say", "writeln": "say", "display": "say",
    "let": "set", "var": "set", "const": "set", "int": "set", "string": "set", "float": "set",
    "double": "set", "bool": "set", "auto": "set", "local": "set", "dim": "set",
    "def": "function", "func": "function", "fn": "function", "fun": "function",
    "void": "function", "sub": "function", "proc": "function",
    "while": "repeat while", "until": "repeat until", "for": "repeat with",
    "foreach": "repeat with", "loop": "repeat forever", "do": "repeat",
    "elif": "else if", "elsif": "else if", "elseif": "else if",
    "break": "stop", "continue": "skip", "next": "skip", "exit": "stop",
    "endif": "end", "fi": "end", "done": "end", "endwhile": "end", "endfor": "end", "wend": "end",
    "change": "add", "increase": "add", "decrease": "subtract", "minus": "subtract",
}

EXAMPLES = {
    "set": "set score to 0\nset name to \"Sam\"\nset nums to [1, 2, 3]",
    "say": "say \"Hello!\"\nsay(\"Hello!\")\nsay \"Score:\", score\nsay \"Score: {score}\"",
    "add": "add 1 to score\nadd \"Sam\" to names",
    "subtract": "subtract 1 from lives",
    "if": "if score > 10\n    say \"You win!\"\nelse\n    say \"Keep going\"\nend",
    "repeat": "repeat 3 times\n    say \"hi\"\nend\n\nrepeat with i from 1 to 10\n    say i\nend\n\n"
              "repeat while lives > 0\n    subtract 1 from lives\nend\n\nrepeat with name in names\n    say name\nend",
    "function": "function greet(name)\n    say \"Hello, {name}!\"\nend\n\ngreet(\"Sam\")",
    "return": "function double(x)\n    return x * 2\nend",
}


def describe_token(tok):
    if tok.kind == "newline":
        return "the end of the line"
    if tok.kind == "eof":
        return "the end of the file"
    if tok.kind == "string":
        return "some text"
    return f"`{tok.value}`"


class Parser:
    def __init__(self, tokens, source=""):
        self.toks = tokens
        self.i = 0
        self.in_function = False
        self.lines = source.splitlines()
        self.own_functions = {tokens[i + 1].value for i in range(len(tokens) - 1)
                              if tokens[i].kind == "name" and tokens[i].value == "function"
                              and tokens[i + 1].kind == "name"}

    # ------------------------------------------------------------ helpers

    def peek(self, k=0):
        return self.toks[min(self.i + k, len(self.toks) - 1)]

    def next(self):
        tok = self.peek()
        self.i += 1
        return tok

    def at_word(self, word, k=0):
        tok = self.peek(k)
        return tok.kind == "name" and tok.value == word

    def at_op(self, op, k=0):
        tok = self.peek(k)
        return tok.kind == "op" and tok.value == op

    def at_line_end(self):
        return self.peek().kind in ("newline", "eof")

    def line_text(self, line):
        return self.lines[line - 1] if 1 <= line <= len(self.lines) else ""

    def indent_of(self, line):
        text = self.line_text(line)
        return text[: len(text) - len(text.lstrip())]

    def text(self, node):
        """The source text a node came from (if it's all on one line)."""
        line = self.line_text(node.line)
        return line[node.col - 1: node.col - 1 + node.length]

    def error(self, tok, msg, hint=None, length=None, **teach):
        return FlauxError(msg, tok.line, tok.col, length or tok.length, hint, **teach)

    def insert_fix(self, tok, word):
        """The line with `word` inserted just before `tok`."""
        line = self.line_text(tok.line)
        if tok.kind in ("newline", "eof"):
            body = line.split("#")[0].rstrip() if "#" in line and '"' not in line else line.rstrip()
            return body + " " + word
        cut = tok.col - 1
        return line[:cut].rstrip() + " " + word + " " + line[cut:].lstrip()

    def expect_word(self, word, why, example=None):
        if not self.at_word(word):
            tok = self.peek()
            raise self.error(tok, f"{why} I expected `{word}` here, but found {describe_token(tok)}.",
                             hint=f"Add `{word}`:" if tok.kind != "eof" else None,
                             fix_line=self.insert_fix(tok, word) if tok.kind != "eof" else None,
                             example=example)
        return self.next()

    def ran_off_line(self):
        """True if the next token is the end, or is on a later line than the
        last one. Inside unclosed brackets the lexer joins lines together, so
        this is how we notice a bracket that was never closed."""
        tok = self.peek()
        prev = self.toks[max(self.i - 1, 0)]
        return tok.kind in ("newline", "eof") or tok.line != prev.line

    def unclosed(self, opener, close):
        """Error for a bracket that is never closed, with the fix: close it
        straight after the last thing typed."""
        prev = self.toks[max(self.i - 1, 0)]
        text = self.line_text(prev.line)
        end = prev.col - 1 + prev.length
        return self.error(opener, f"This `{opener.value}` is never closed with `{close}`.",
                          hint=f"Every `{opener.value}` needs a matching `{close}`. Add it:",
                          fix_lines=[(prev.line, text[:end] + close + text[end:])])

    def expect_close(self, op, opener, why):
        if not self.at_op(op):
            tok = self.peek()
            if self.ran_off_line():
                raise self.unclosed(opener, op)
            raise self.error(tok, f"{why} I expected `{op}` here, but found {describe_token(tok)}.",
                             hint="Check for a missing comma or bracket.")
        return self.next()

    def end_of_line(self):
        tok = self.peek()
        if tok.kind == "newline":
            self.next()
        elif tok.kind != "eof":
            line = self.line_text(tok.line)
            raise self.error(tok, f"I expected this line to end here, but found {describe_token(tok)}.",
                             hint="Each instruction goes on its own line. If this is a second instruction, "
                                  "move it to the next line:",
                             fix_lines=[(tok.line, line[: tok.col - 1].rstrip()),
                                        ("+", self.indent_of(tok.line) + line[tok.col - 1:].strip())])

    def skip_newlines(self):
        while self.peek().kind == "newline":
            self.next()

    def pos(self, tok):
        return {"line": tok.line, "col": tok.col, "length": tok.length}

    def span(self, start_tok, end_tok=None):
        """Position covering from start_tok up to and including end_tok."""
        end_tok = end_tok or self.toks[max(self.i - 1, 0)]
        if end_tok.line == start_tok.line and end_tok.kind not in ("newline", "eof"):
            length = end_tok.col + end_tok.length - start_tok.col
        else:
            length = start_tok.length
        return {"line": start_tok.line, "col": start_tok.col, "length": max(1, length)}

    def name_token(self, why, example=None):
        tok = self.peek()
        if tok.kind != "name":
            raise self.error(tok, f"{why} I expected a name here, but found {describe_token(tok)}.",
                             hint="Names are made of letters, numbers and _, like `score` or `player_2`.",
                             example=example)
        if tok.value in RESERVED:
            raise self.error(tok, f"`{tok.value}` is a special Flaux word, so it can't be used as a name.",
                             hint="Pick a different name. Change the line to:", fix=f"my_{tok.value}")
        return self.next()

    # ------------------------------------------------------------ program

    def parse_program(self):
        funcs, main = [], []
        self.skip_newlines()
        while self.peek().kind != "eof":
            if self.at_word("function"):
                funcs.append(self.parse_function())
            else:
                main.append(self.parse_statement())
            self.skip_newlines()
        return N.Program(funcs, main, line=1, col=1)

    def missing_end(self, opener, kind):
        """Error for a block that runs off the end of the file."""
        last = opener.line
        for tok in self.toks:
            if tok.kind not in ("newline", "eof"):
                last = max(last, tok.line)
        shown = list(range(opener.line, last + 1))
        if len(shown) > 6:
            shown = shown[:2] + ["..."] + shown[-3:]
        fix = [(n, self.line_text(n)) if n != "..." else ("...", "") for n in shown]
        fix.append(("+", self.indent_of(opener.line) + "end"))
        return self.error(opener, f"This `{opener.value}` needs an `end` to finish it.",
                          hint=f"Every `{kind}` block finishes with a line that just says `end`. "
                               "Add one after the last line that belongs inside the block:",
                          fix_lines=fix)

    def parse_block(self, opener, enders, kind):
        """Parse statements until one of the `enders` words starts a line."""
        body = []
        self.skip_newlines()
        while True:
            tok = self.peek()
            if tok.kind == "eof":
                raise self.missing_end(opener, kind)
            if tok.kind == "name" and tok.value in enders:
                return body
            body.append(self.parse_statement())
            self.skip_newlines()

    def finish_block(self, opener, kind):
        tok = self.peek()
        if not self.at_word("end"):
            raise self.missing_end(opener, kind)
        self.next()
        if self.at_word(kind):  # optional: `end if`, `end repeat`, `end function`
            self.next()
        self.end_of_line()

    def parse_function(self):
        start = self.next()  # 'function'
        if self.in_function:
            raise self.error(start, "Functions can't go inside other functions.",
                             hint="Finish the function above with `end` first, then start this one. "
                                  "Each function starts at the left edge:",
                             example="function first()\n    say \"one\"\nend\n\nfunction second()\n    say \"two\"\nend")
        name_tok = self.name_token("A function needs a name.", example=EXAMPLES["function"])
        params = []
        if self.at_op("("):
            open_tok = self.next()
            seen = set()
            while not self.at_op(")"):
                if self.ran_off_line() and self.toks[self.i - 1] is not open_tok:
                    raise self.unclosed(open_tok, ")")
                p = self.name_token("Inputs to a function are names.",
                                    example="function area(width, height)\n    return width * height\nend")
                if p.value in seen:
                    raise self.error(p, f"This function already has an input called `{p.value}`.",
                                     hint="Each input needs its own name.", fix=f"{p.value}2")
                seen.add(p.value)
                params.append(N.Name(p.value, **self.pos(p)))
                if self.at_op(","):
                    self.next()
                elif not self.at_op(")") and self.ran_off_line():
                    raise self.unclosed(open_tok, ")")
                elif self.peek().kind == "name":
                    raise self.error(self.peek(), "Inputs to a function are separated by commas.",
                                     hint="Add a comma:",
                                     fix_line=self.insert_fix(self.peek(), ",").replace(" , ", ", "))
                elif not self.at_op(")"):
                    self.expect_close(")", open_tok, "Inputs are separated by commas.")
            self.next()
        elif not self.at_line_end():
            words = []
            while not self.at_line_end():
                words.append(self.next())
            names = [w.value for w in words if w.kind == "name" and w.value not in ("of", "with", "and")]
            raise self.error(words[0], "A function's inputs go in round brackets after its name.",
                             hint="Change the line to:",
                             fix_line=self.indent_of(start.line) + f"function {name_tok.value}({', '.join(names)})",
                             example=EXAMPLES["function"])
        pos = self.span(start)
        self.end_of_line()
        self.in_function = True
        body = self.parse_block(start, {"end"}, "function")
        self.in_function = False
        self.finish_block(start, "function")
        return N.FuncDef(name_tok.value, params, body, **pos)

    # ---------------------------------------------------------- statements

    def parse_statement(self):
        tok = self.peek()
        if tok.kind == "name":
            handler = getattr(self, "stmt_" + tok.value, None)
            if handler is not None and tok.value in RESERVED | {"function"}:
                return handler()
            if (tok.value in OTHER_LANGUAGE_WORDS and tok.value not in self.own_functions
                    and not (self.at_op("=", 1) or self.at_word("to", 1))):
                self.other_language(tok)
            self.check_shortcut_maths(tok)
        start = tok
        expr = self.parse_expression()
        if isinstance(expr, N.Call):
            self.end_of_line()
            return N.ExprStmt(expr, **self.span(start))
        if isinstance(expr, N.Binary) and expr.op == "=" and isinstance(expr.left, (N.Name, N.Index)):
            raise self.error(start, "To change a variable, use `set`.",
                             hint="In Flaux, = checks if two things are equal. To put a value in a "
                                  "variable, write `set ... to ...`. Change the line to:",
                             fix_line=self.indent_of(start.line)
                             + f"set {self.text(expr.left)} to {self.text(expr.right)}",
                             length=expr.length)
        if isinstance(expr, N.Name) and self.peek().kind != "newline":
            raise self.error(start, f"I don't understand this line. Flaux doesn't have an instruction called `{start.value}`.",
                             hint="Lines start with an instruction word like set, say, add, subtract, "
                                  "if, repeat or return, or with a function call like greet(\"Sam\").",
                             example="set score to 10\nsay score")
        raise self.error(start, "This line doesn't do anything with its value.",
                         hint="To show it, use `say`. To keep it for later, use `set`:",
                         fix_lines=[(start.line, self.indent_of(start.line) + "say " + self.text(expr))],
                         example=f"set result to {self.text(expr)}",
                         length=expr.length)

    def other_language(self, tok):
        """Someone used a word from another language, like `print` or `while`."""
        flaux = OTHER_LANGUAGE_WORDS[tok.value]
        line = self.line_text(tok.line)
        rest = line[tok.col - 1 + tok.length:].strip()
        indent = self.indent_of(tok.line)
        fix_line = None
        first = flaux.split()[0]

        if first == "say" and rest:
            inner = rest
            if inner.startswith("(") and inner.endswith(")"):
                inner = inner[1:-1].strip()
            if inner.startswith("."):  # console.log(...)
                inner = inner[inner.find("(") + 1: inner.rfind(")")] if "(" in inner else ""
            fix_line = indent + "say " + inner if inner else None
        elif first == "set" and rest:
            name, _, value = rest.partition("=")
            if value.strip():
                name = name.split(":")[0].strip().split()[-1]
                fix_line = indent + f"set {name} to {value.strip()}"
        elif first == "function":
            fix_line = indent + "function " + rest.rstrip(":{ ").replace(" -> ", "")
        elif flaux in ("repeat while", "repeat until"):
            fix_line = indent + flaux + " " + rest.rstrip(":{ ").strip("()")
        elif first == "repeat" and flaux == "repeat with" and rest:
            fix_line = None
        elif flaux == "else if":
            fix_line = indent + "else if " + rest.rstrip(":{ ")
        elif flaux in ("stop", "skip", "end"):
            fix_line = indent + flaux
        elif first in ("add", "subtract"):
            fix_line = None
        example = None
        if first in EXAMPLES:
            example = EXAMPLES[first]
        if flaux == "repeat with":
            example = "repeat with i from 1 to 10\n    say i\nend\n\nrepeat with name in names\n    say name\nend"
            fix_line = self.translate_for(rest, indent)
        raise self.error(tok, f"Flaux doesn't use `{tok.value}`. The Flaux word for this is `{flaux}`.",
                         hint="Change the line to:" if fix_line else "Here's how it looks in Flaux:",
                         fix_line=fix_line, example=None if fix_line else example)

    def translate_for(self, rest, indent):
        """Turn a Python/JS/C# `for` loop into `repeat with`, when we can."""
        import re
        rest = rest.rstrip(":{ ").strip()

        def minus_one(x):  # range(1, 11) stops before 11, so Flaux counts to 10
            return str(int(x) - 1) if x.lstrip("-").isdigit() else f"{x} - 1"

        m = re.fullmatch(r"\(?\s*(?:var |let |const |int |long )?(\w+)\s+(?:in|of)\s+range\((.*)\)\s*\)?", rest)
        if m:
            var, args = m.group(1), [a.strip() for a in m.group(2).split(",")]
            if len(args) == 1:
                return f"{indent}repeat with {var} from 0 to {minus_one(args[0])}"
            if len(args) == 2:
                return f"{indent}repeat with {var} from {args[0]} to {minus_one(args[1])}"
            if len(args) == 3:
                return f"{indent}repeat with {var} from {args[0]} to {minus_one(args[1])} by {args[2]}"
        m = re.fullmatch(r"\(?\s*(?:var |let |const |string |int )?(\w+)\s+(?:in|of)\s+(\w+)\s*\)?", rest)
        if m:
            return f"{indent}repeat with {m.group(1)} in {m.group(2)}"
        m = re.fullmatch(r"\(\s*(?:var |let |int |long )?(\w+)\s*=\s*(\w+)\s*;\s*\1\s*(<=?)\s*([^;]+);\s*\1(?:\+\+|\s*\+=\s*1)\s*\)", rest)
        if m:
            var, start, op, end = m.group(1), m.group(2), m.group(3), m.group(4).strip()
            end = end if op == "<=" else minus_one(end)
            return f"{indent}repeat with {var} from {start} to {end}"
        return None

    def check_shortcut_maths(self, tok):
        """Catch `x += 1`, `x -= 1`, `x++` and `x--` from other languages."""
        op1, op2 = self.peek(1), self.peek(2)
        if op1.kind != "op":
            return
        indent = self.indent_of(tok.line)
        line = self.line_text(tok.line)
        name = tok.value
        if op1.value in "+-" and op2.kind == "op" and op2.value == op1.value:
            word = "add 1 to" if op1.value == "+" else "subtract 1 from"
            raise self.error(tok, f"Flaux doesn't use `{op1.value * 2}`.",
                             hint="Use `add` or `subtract`. Change the line to:",
                             fix_line=f"{indent}{word} {name}", length=tok.length + 2)
        if op1.value in "+-*/" and op2.kind == "op" and op2.value == "=" and op2.col == op1.col + 1:
            value = line[op2.col:].strip()
            fix = {"+": f"add {value} to {name}", "-": f"subtract {value} from {name}",
                   "*": f"set {name} to {name} * {value}", "/": f"set {name} to {name} / {value}"}[op1.value]
            raise self.error(tok, f"Flaux doesn't use `{op1.value}=`.",
                             hint="Change the line to:", fix_line=indent + fix,
                             length=op2.col - tok.col + 1)

    def parse_target(self, why, example):
        tok = self.peek()
        name = self.name_token(why, example=example)
        target = N.Name(name.value, **self.pos(name))
        while self.at_op("["):
            open_tok = self.next()
            index = self.parse_expression()
            close = self.expect_close("]", open_tok, "This `[` was never closed.")
            target = N.Index(target, index, **self.span(tok, close))
        return target

    def stmt_set(self):
        start = self.next()
        target = self.parse_target("`set` needs the name of a variable.", EXAMPLES["set"])
        if self.at_op("=") or self.at_op("=="):
            raise self.error(self.peek(), "In Flaux, `set` uses the word `to` instead of `=`.",
                             hint="Change the line to:", fix="to")
        self.expect_word("to", "`set` needs `to` between the name and the value.", EXAMPLES["set"])
        if self.at_line_end():
            raise self.error(self.peek(), "`set` needs a value after `to`.",
                             hint="Put the value you want to keep after `to`.", example=EXAMPLES["set"])
        value = self.parse_expression()
        pos = self.span(start)
        self.end_of_line()
        return N.Set(target, value, **pos)

    def stmt_add(self):
        start = self.next()
        value = self.parse_expression()
        self.expect_word("to", "`add` needs `to`.", EXAMPLES["add"])
        target = self.parse_target("`add ... to` needs the name of a variable.", EXAMPLES["add"])
        pos = self.span(start)
        self.end_of_line()
        return N.Add(value, target, **pos)

    def stmt_subtract(self):
        start = self.next()
        value = self.parse_expression()
        if self.at_word("to"):
            raise self.error(self.peek(), "`subtract` uses `from` instead of `to`.", fix="from")
        self.expect_word("from", "`subtract` needs `from`.", EXAMPLES["subtract"])
        target = self.parse_target("`subtract ... from` needs the name of a variable.", EXAMPLES["subtract"])
        pos = self.span(start)
        self.end_of_line()
        return N.Subtract(value, target, **pos)

    def stmt_say(self):
        start = self.next()
        if self.at_line_end():
            raise self.error(start, "`say` needs something to say.",
                             hint="Put text in quotes, or a variable, after `say`:", example=EXAMPLES["say"])
        if self.at_op("(") and self.brackets_fill_line():
            # say("hi") or say("Score:", 10), like print(...) in other languages
            open_tok = self.next()
            values = []
            while not self.at_op(")"):
                if self.peek().kind == "eof":
                    raise self.unclosed(open_tok, ")")
                values.append(self.parse_expression())
                if self.at_op(","):
                    self.next()
                elif not self.at_op(")"):
                    tok = self.peek()
                    raise self.error(tok, "The things to say are separated by commas.",
                                     hint="Add a comma:",
                                     fix_line=self.insert_fix(tok, ",").replace(" , ", ", "))
            self.next()
        else:
            values = [self.parse_expression()]
            while self.at_op(","):
                self.next()
                values.append(self.parse_expression())
        pos = self.span(start)
        self.end_of_line()
        return N.Say(values, **pos)

    def brackets_fill_line(self):
        """True if the `(` here closes right at the end of the line, as in
        say("hi"). False for things like say (1 + 2) * 3."""
        depth = 0
        for j in range(self.i, len(self.toks)):
            tok = self.toks[j]
            if tok.kind in ("newline", "eof"):
                return False
            if tok.kind == "op" and tok.value in ("(", "["):
                depth += 1
            elif tok.kind == "op" and tok.value in (")", "]"):
                depth -= 1
                if depth == 0:
                    return self.toks[j + 1].kind in ("newline", "eof")
        return False

    def condition(self, keyword):
        if self.at_line_end():
            raise self.error(self.peek(), f"`{keyword}` needs a check after it.",
                             hint="A check is something that's true or false, like `score > 10`.",
                             example=EXAMPLES["if"])
        return self.parse_expression()

    def stmt_if(self):
        start = self.next()
        branches = []
        cond = self.condition("if")
        if self.at_word("then"):
            self.next()
        self.end_of_line()
        body = self.parse_block(start, {"end", "else"}, "if")
        branches.append((cond, body))
        else_body = None
        while self.at_word("else"):
            else_tok = self.next()
            if self.at_word("if"):
                self.next()
                cond = self.condition("else if")
                if self.at_word("then"):
                    self.next()
                self.end_of_line()
                body = self.parse_block(start, {"end", "else"}, "if")
                branches.append((cond, body))
            else:
                self.end_of_line()
                else_body = self.parse_block(start, {"end", "else"}, "if")
                if self.at_word("else"):
                    raise self.error(self.peek(), "There's already an `else` above, and it has to be the last choice.",
                                     hint="Use `else if` for extra choices, and keep the plain `else` last:",
                                     example="if score > 100\n    say \"Amazing\"\nelse if score > 50\n"
                                             "    say \"Good\"\nelse\n    say \"Keep trying\"\nend")
        pos = {"line": start.line, "col": start.col, "length": 2}
        self.finish_block(start, "if")
        return N.If(branches, else_body, **pos)

    def stmt_repeat(self):
        start = self.next()
        pos = {"line": start.line, "col": start.col, "length": 6}
        if self.at_word("forever"):
            self.next()
            self.end_of_line()
            body = self.loop_body(start)
            return N.RepeatForever(body, **pos)
        if self.at_word("while") or self.at_word("until"):
            until = self.next().value == "until"
            cond = self.condition("repeat until" if until else "repeat while")
            self.end_of_line()
            body = self.loop_body(start)
            return N.RepeatWhile(cond, body, until, **pos)
        if self.at_word("with"):
            self.next()
            var = self.name_token("`repeat with` needs a name for the counter.", example=EXAMPLES["repeat"])
            var_node = N.Name(var.value, **self.pos(var))
            if self.at_word("in"):
                self.next()
                iterable = self.parse_expression()
                self.end_of_line()
                body = self.loop_body(start)
                return N.RepeatEach(var_node, iterable, body, **pos)
            if self.at_op("="):
                raise self.error(self.peek(), "`repeat with` uses `from` and `to` to count.",
                                 hint="Change the line to:", example="repeat with i from 1 to 10\n    say i\nend",
                                 fix="from")
            self.expect_word("from", "`repeat with` needs `from` (to count) or `in` (to go through a list).",
                             "repeat with i from 1 to 10\n    say i\nend\n\nrepeat with item in my_list\n    say item\nend")
            first = self.parse_expression()
            self.expect_word("to", "`repeat with ... from` needs `to`.", "repeat with i from 1 to 10\n    say i\nend")
            last = self.parse_expression()
            step = None
            if self.at_word("by") or self.at_word("step"):
                self.next()
                step = self.parse_expression()
            self.end_of_line()
            body = self.loop_body(start)
            return N.RepeatRange(var_node, first, last, step, body, **pos)
        if self.at_line_end():
            raise self.error(start, "`repeat` needs to know how many times, or until when.",
                             hint="Here are the kinds of `repeat`:", example=EXAMPLES["repeat"] +
                             "\n\nrepeat forever\n    say \"again!\"\nend")
        count = self.parse_expression()
        self.expect_word("times", "A counted repeat ends with `times`.", "repeat 10 times\n    say \"hi\"\nend")
        self.end_of_line()
        body = self.loop_body(start)
        return N.RepeatTimes(count, body, **pos)

    def loop_body(self, start):
        body = self.parse_block(start, {"end"}, "repeat")
        self.finish_block(start, "repeat")
        return body

    def stmt_stop(self):
        tok = self.next()
        self.end_of_line()
        return N.Stop(**self.pos(tok))

    def stmt_skip(self):
        tok = self.next()
        self.end_of_line()
        return N.Skip(**self.pos(tok))

    def stmt_return(self):
        start = self.next()
        value = None
        if not self.at_line_end():
            value = self.parse_expression()
        pos = self.span(start)
        self.end_of_line()
        return N.Return(value, **pos)

    def stmt_function(self):
        raise self.error(self.peek(), "Functions can't go inside an `if`, a `repeat` or another function.",
                         hint="Move the whole function (from `function` down to its `end`) so it starts at "
                              "the left edge, outside any other block. You can still call it from anywhere.",
                         example=EXAMPLES["function"])

    def stmt_end(self):
        raise self.error(self.peek(), "This `end` doesn't match any `if`, `repeat` or `function`.",
                         hint="Every `if`, `repeat` and `function` has exactly one `end`. This one looks "
                              "like one too many, so try deleting this line.")

    def stmt_else(self):
        raise self.error(self.peek(), "This `else` isn't inside an `if`.",
                         hint="`else` goes between an `if` and its `end`. If there's an `end` just above "
                              "this line, that `end` might be closing the `if` too early.",
                         example=EXAMPLES["if"])

    def stmt_is(self):
        raise self.error(self.peek(), "A line can't start with `is`.",
                         hint="`is` compares two things, like `if age is 10`.")

    def stmt_not(self):
        raise self.error(self.peek(), "A line can't start with `not`.",
                         hint="`not` flips a check, like `if not finished`.")

    # --------------------------------------------------------- expressions

    def parse_expression(self):
        return self.parse_or()

    def binary(self, op, left, right):
        return N.Binary(op, left, right, line=left.line, col=left.col,
                        length=self.end_col(right, left) - left.col)

    def end_col(self, node, start):
        if node.line == start.line:
            return node.col + node.length
        return start.col + start.length

    def parse_or(self):
        left = self.parse_and()
        while self.at_word("or"):
            self.next()
            right = self.parse_and()
            left = self.binary("or", left, right)
        return left

    def parse_and(self):
        left = self.parse_not()
        while self.at_word("and"):
            self.next()
            right = self.parse_not()
            left = self.binary("and", left, right)
        return left

    def parse_not(self):
        if self.at_word("not"):
            tok = self.next()
            operand = self.parse_not()
            return N.Unary("not", operand, line=tok.line, col=tok.col,
                           length=self.end_col(operand, tok) - tok.col)
        return self.parse_compare()

    def compare_op(self):
        """If a comparison starts here, consume it and return the op."""
        tok = self.peek()
        if tok.kind == "op" and tok.value in COMPARE_OPS:
            self.next()
            return COMPARE_OPS[tok.value]
        if not self.at_word("is"):
            return None
        self.next()
        negate = False
        if self.at_word("not"):
            self.next()
            negate = True
        op = "="
        if (self.at_word("more") or self.at_word("greater") or self.at_word("bigger")) and self.at_word("than", 1):
            self.next(); self.next()
            op = ">"
        elif (self.at_word("less") or self.at_word("fewer") or self.at_word("smaller")) and self.at_word("than", 1):
            self.next(); self.next()
            op = "<"
        elif self.at_word("at") and self.at_word("least", 1):
            self.next(); self.next()
            op = ">="
        elif self.at_word("at") and self.at_word("most", 1):
            self.next(); self.next()
            op = "<="
        elif self.at_word("equal") and self.at_word("to", 1):
            self.next(); self.next()
        return NEGATE[op] if negate else op

    def parse_compare(self):
        left = self.parse_copies()
        op_tok = self.peek()
        op = self.compare_op()
        if op is None:
            return left
        if self.at_line_end():
            raise self.error(op_tok, "This comparison is missing the thing to compare with.",
                             hint="Put a value on both sides, like `score > 10`.")
        right = self.parse_copies()
        node = self.binary(op, left, right)
        tok = self.peek()
        if (tok.kind == "op" and tok.value in COMPARE_OPS) or self.at_word("is"):
            op2 = self.compare_op()
            third = self.parse_copies()
            mid = self.text(right)
            fixed = (f"{self.text(left)} {self.text(op_tok) if op_tok.kind == 'op' else op} {mid} and "
                     f"{mid} {tok.value if tok.kind == 'op' else op2} {self.text(third)}")
            raise self.error(tok, "Flaux compares two things at a time.",
                             hint="Split it into two checks joined with `and`:",
                             fix_line=self.line_text(left.line)[: left.col - 1] + fixed
                             + self.line_text(left.line)[third.col - 1 + third.length:])
        return node

    def parse_copies(self):
        left = self.parse_sum()
        if self.at_word("copies") and self.at_word("of", 1):
            self.next()
            self.next()
            right = self.parse_sum()
            return N.Copies(left, right, line=left.line, col=left.col,
                            length=self.end_col(right, left) - left.col)
        return left

    def parse_sum(self):
        left = self.parse_product()
        while self.at_op("+") or self.at_op("-"):
            op = self.next()
            self.missing_operand(op)
            right = self.parse_product()
            left = self.binary(op.value, left, right)
        return left

    def missing_operand(self, op):
        if self.at_line_end():
            raise self.error(op, f"Something is missing after `{op.value}`.",
                             hint=f"`{op.value}` needs a value on both sides, like `score {op.value} 1`.")

    def parse_product(self):
        left = self.parse_unary()
        while True:
            if self.at_op("*") or self.at_op("/") or self.at_op("//"):
                op = self.next()
                kind = op.value
            elif self.at_op("%") or self.at_word("mod"):
                op = self.next()
                kind = "mod"
            else:
                return left
            self.missing_operand(op)
            right = self.parse_unary()
            left = self.binary(kind, left, right)

    def parse_unary(self):
        if self.at_op("-"):
            tok = self.next()
            operand = self.parse_unary()
            if isinstance(operand, N.Num):  # fold -5 into a single number
                operand.value = -operand.value
                operand.length = operand.length + (operand.col - tok.col)
                operand.col = tok.col
                return operand
            return N.Unary("-", operand, line=tok.line, col=tok.col,
                           length=self.end_col(operand, tok) - tok.col)
        if self.at_op("+"):
            self.next()
            return self.parse_unary()
        return self.parse_power()

    def parse_power(self):
        base = self.parse_postfix()
        if self.at_op("^"):
            op = self.next()
            self.missing_operand(op)
            exponent = self.parse_unary()
            return self.binary("^", base, exponent)
        return base

    def parse_postfix(self):
        start = self.peek()
        node = self.parse_primary()
        while True:
            if self.at_op("("):
                if not isinstance(node, N.Name):
                    raise self.error(self.peek(), "Only functions can be called with ( ).",
                                     hint="To multiply, use * between the values, like `2 * (3 + 4)`.")
                open_tok = self.next()
                args = []
                while not self.at_op(")"):
                    if self.peek().kind in ("newline", "eof"):
                        raise self.unclosed(open_tok, ")")
                    args.append(self.parse_expression())
                    if not self.at_op(")"):
                        if not self.at_op(","):
                            tok = self.peek()
                            if self.ran_off_line():
                                raise self.unclosed(open_tok, ")")
                            raise self.error(tok, "Inputs to a function are separated by commas.",
                                             hint="Add a comma:",
                                             fix_line=self.insert_fix(tok, ",").replace(" , ", ", "))
                        self.next()
                close = self.next()
                node = N.Call(node.name, args, **self.span(start, close))
            elif self.at_op("["):
                open_tok = self.next()
                index = self.parse_expression()
                close = self.expect_close("]", open_tok, "This `[` was never closed.")
                node = N.Index(node, index, **self.span(start, close))
            else:
                return node

    def parse_primary(self):
        tok = self.peek()
        if tok.kind == "int":
            self.next()
            return N.Num(tok.value, False, **self.pos(tok))
        if tok.kind == "float":
            self.next()
            return N.Num(tok.value, True, **self.pos(tok))
        if tok.kind == "string":
            self.next()
            parts = []
            for part in tok.value:
                if part[0] == "text":
                    parts.append(part[1])
                else:
                    sub = Parser(part[1])
                    sub.lines = self.lines
                    expr = sub.parse_expression()
                    if sub.peek().kind != "eof":
                        extra = sub.peek()
                        raise sub.error(extra, f"I didn't expect {describe_token(extra)} inside these `{{ }}`.",
                                        hint="Inside { } put one value or calculation, like {score} or {score + 1}.")
                    parts.append(expr)
            return N.Str(parts, **self.pos(tok))
        if tok.kind == "op" and tok.value == "(":
            self.next()
            if self.at_op(")"):
                raise self.error(tok, "These brackets are empty.",
                                 hint="Round brackets group a calculation, like (2 + 3) * 4.")
            inner = self.parse_expression()
            close = self.expect_close(")", tok, "This `(` was never closed.")
            if close.line == tok.line:  # the brackets belong to the value, e.g. for fixes
                inner.col, inner.length = tok.col, close.col + 1 - tok.col
            return inner
        if tok.kind == "op" and tok.value == "[":
            self.next()
            items = []
            while not self.at_op("]"):
                if self.peek().kind in ("newline", "eof"):
                    raise self.unclosed(tok, "]")
                items.append(self.parse_expression())
                if not self.at_op("]"):
                    if not self.at_op(","):
                        nxt = self.peek()
                        if self.ran_off_line():
                            raise self.unclosed(tok, "]")
                        raise self.error(nxt, "Items in a list are separated by commas.",
                                         hint="Add a comma:",
                                         fix_line=self.insert_fix(nxt, ",").replace(" , ", ", "))
                    self.next()
            close = self.next()
            return N.ListLit(items, **self.span(tok, close))
        if tok.kind == "name":
            if tok.value in ("true", "false"):
                self.next()
                return N.Bool(tok.value == "true", **self.pos(tok))
            if tok.value == "length" and self.at_word("of", 1):
                self.next()
                self.next()
                operand = self.parse_unary()
                return N.Length(operand, line=tok.line, col=tok.col,
                                length=self.end_col(operand, tok) - tok.col)
            if tok.value in RESERVED:
                raise self.error(tok, f"`{tok.value}` is a special Flaux word and can't be used here.",
                                 hint="Special words like this one start an instruction or join checks "
                                      "together. Check whether something is missing before it.")
            self.next()
            return N.Name(tok.value, **self.pos(tok))
        if tok.kind in ("newline", "eof"):
            raise self.error(tok, "This line ended too early. Something is missing at the end.",
                             length=1)
        if tok.kind == "op" and tok.value == "=":
            raise self.error(tok, "I didn't expect `=` here.",
                             hint="To put a value in a variable, use `set x to 5`. To compare, use "
                                  "`x = 5` inside an `if`.")
        raise self.error(tok, f"I didn't expect {describe_token(tok)} here.")


def parse(tokens, source=""):
    return Parser(tokens, source).parse_program()
