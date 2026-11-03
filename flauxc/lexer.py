"""Lexer: turns Flaux source text into a list of tokens.

Token kinds:
    int, float   numbers (underscores allowed: 1_000_000, and 1.5e20)
    string       text in double quotes; value is a list of parts:
                   ("text", "plain text") or ("expr", [tokens], line, col)
                 so "Hi {name}!" becomes text, an expression, then text.
    name         words: variable names, function names and keywords
    op           symbols like + - * / ( ) [ ] , = < > <= >= != //
    newline      end of a line (newlines inside ( ) or [ ] are ignored)
    eof          end of the file

Many errors here catch habits from other languages (`:` at the end of a
line, `&&`, single quotes, curly brackets) and show the Flaux way instead.
"""

import re

from .errors import FlauxError


class Token:
    __slots__ = ("kind", "value", "line", "col", "length")

    def __init__(self, kind, value, line, col, length=1):
        self.kind = kind
        self.value = value
        self.line = line
        self.col = col
        self.length = length

    def __repr__(self):
        return f"Token({self.kind}, {self.value!r}, {self.line}:{self.col})"


TWO_CHAR_OPS = {"<=", ">=", "!=", "==", "//"}
ONE_CHAR_OPS = set("+-*/^%()[],=<>")
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\", "{": "{", "}": "}"}

# Words from other languages that start a block ending in `:`.
FOREIGN_BLOCK_WORDS = {"for", "foreach", "while", "until", "def", "func", "fn", "fun",
                       "elif", "elsif", "elseif", "loop", "do"}

BLOCK_EXAMPLE = """\
if score > 10
    say "You win!"
end"""


class Lexer:
    def __init__(self, src, line=1, col=1, inner=False, full_src=None):
        self.src = src
        self.full_lines = (full_src if full_src is not None else src).splitlines()
        self.i = 0
        self.line = line
        self.col = col
        self.inner = inner  # True when lexing an expression inside "{...}"
        self.tokens = []
        self.first_error = None  # see run()
        self.depth = 0  # how many ( or [ are open
        # names of the program's own functions, so `function loop()` isn't
        # mistaken for a word from another language
        self.own_functions = set(re.findall(r"^\s*function\s+(\w+)", "\n".join(self.full_lines), re.M))

    # ------------------------------------------------------------ helpers

    def error(self, msg, line=None, col=None, length=1, hint=None, **teach):
        raise FlauxError(msg, line or self.line, col or self.col, length, hint, **teach)

    def line_text(self, line=None):
        line = line or self.line
        return self.full_lines[line - 1] if 1 <= line <= len(self.full_lines) else ""

    def peek(self, k=0):
        j = self.i + k
        return self.src[j] if j < len(self.src) else ""

    def advance(self):
        ch = self.src[self.i]
        self.i += 1
        if ch == "\n":
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def add(self, kind, value, line, col, length):
        self.tokens.append(Token(kind, value, line, col, length))

    # --------------------------------------------------------------- main

    def run(self):
        """Split the source into tokens.

        A problem with a symbol doesn't stop us straight away: we note the
        first one, skip the rest of that line, and keep going. That way the
        parser can still report an earlier problem first, so errors always
        come in the order you read the program."""
        while self.i < len(self.src):
            try:
                self.step()
            except FlauxError as e:
                if self.inner:
                    raise
                if self.first_error is None:
                    self.first_error = e
                self.depth = 0
                while self.i < len(self.src) and self.peek() != "\n":
                    self.advance()
        if not self.inner:
            if self.tokens and self.tokens[-1].kind != "newline":
                self.add("newline", None, self.line, self.col, 1)
            self.add("eof", None, self.line, self.col, 1)
        return self.tokens

    def step(self):
        """Read one token (or skip one space, comment or newline)."""
        ch = self.peek()
        if ch == "\n":
            if (self.depth == 0 and not self.inner and self.tokens
                    and self.tokens[-1].kind != "newline"):
                self.add("newline", None, self.line, self.col, 1)
            self.advance()
        elif ch in " \t\r":
            self.advance()
        elif ch == "#":
            while self.i < len(self.src) and self.peek() != "\n":
                self.advance()
        elif ch.isdigit():
            self.number()
        elif ch.isascii() and (ch.isalpha() or ch == "_"):
            self.name()
        elif ch == '"':
            self.string()
        else:
            two = ch + self.peek(1)
            line, col = self.line, self.col
            if two in TWO_CHAR_OPS:
                self.advance()
                self.advance()
                self.add("op", two, line, col, 2)
            elif ch in ONE_CHAR_OPS:
                self.advance()
                if ch in "([":
                    self.depth += 1
                elif ch in ")]":
                    self.depth = max(0, self.depth - 1)
                self.add("op", ch, line, col, 1)
            elif ch == ":" and self.foreign_block_colon():
                self.advance()  # the parser will translate the whole line
            else:
                self.bad_symbol(ch)

    def foreign_line(self):
        """True if this line starts with a block word from another language,
        like `for`, `while` or `def`. The parser translates the whole line."""
        first = re.match(r"\s*([A-Za-z_]+)", self.line_text())
        return (first is not None and first.group(1) in FOREIGN_BLOCK_WORDS
                and first.group(1) not in self.own_functions)

    def foreign_block_colon(self):
        """A `:` ending a line like `for pet in pets:` or `while x < 5:`."""
        after = self.line_text()[self.col:]
        return after.split("#")[0].strip() == "" and self.foreign_line()

    def bad_symbol(self, ch):
        if ch in ":;{}" and self.foreign_line():
            # e.g. `for (int i = 0; i < 10; i++) {`: skip the rest of the line
            # and let the parser show the whole line in Flaux.
            while self.i < len(self.src) and self.peek() != "\n":
                self.advance()
            return
        self._bad_symbol(ch)

    def _bad_symbol(self, ch):
        """A symbol Flaux doesn't use. Most come from other languages, so
        explain the Flaux way and show the fixed line."""
        text = self.line_text()
        start = self.col - 1
        after = text[start + 1:]
        before = text[:start]
        indent = text[: len(text) - len(text.lstrip())]

        if ch in "“”‘’":
            fixed = text.replace("“", '"').replace("”", '"')
            self.error("This is a curly quote mark, which Flaux doesn't understand.",
                       hint='Use plain straight quotes ("). Phones and word processors often swap '
                            "them for curly ones without telling you. Change the line to:",
                       fix_line=fixed)
        if ch == "'":
            self.error("Flaux uses double quotes for text, not single quotes.",
                       hint='Put text between double quotes ("), like "hello". Change the line to:',
                       fix_line=re.sub(r"'([^']*)'", r'"\1"', text))
        if ch == ":":
            if after.split("#")[0].strip() == "":
                self.error("Flaux doesn't use `:` at the end of lines.",
                           hint="Blocks in Flaux just start on the next line and finish with `end`. "
                                "Remove the colon:",
                           fix_line=before.rstrip(), example=BLOCK_EXAMPLE)
            self.error("Flaux doesn't use `:`.")
        if ch == ";":
            parts = [p.strip() for p in text.split(";") if p.strip()]
            self.error("Flaux doesn't use `;`.",
                       hint="Put each instruction on its own line instead:",
                       fix_lines=[(self.line, indent + parts[0])] + [("+", indent + p) for p in parts[1:]])
        if ch == "{":
            if after.split("#")[0].strip() == "":
                self.error("Flaux uses `end` instead of curly brackets to finish a block.",
                           hint="Remove the `{`, and put `end` where the `}` would go:",
                           fix_line=before.rstrip(), example=BLOCK_EXAMPLE)
            self.error("Curly brackets only work inside text, to put a value in it.",
                       example='say "Your score is {score}"')
        if ch == "}":
            if text.strip() == "}":
                self.error("Flaux uses `end` instead of `}` to finish a block.",
                           fix_line=indent + "end", example=BLOCK_EXAMPLE)
            self.error("Curly brackets only work inside text, to put a value in it.",
                       example='say "Your score is {score}"')
        if ch == "&":
            n = 2 if self.peek(1) == "&" else 1
            self.error("Flaux uses the word `and` instead of `&&`.", length=n,
                       hint="Change the line to:", fix="and")
        if ch == "|":
            n = 2 if self.peek(1) == "|" else 1
            self.error("Flaux uses the word `or` instead of `||`.", length=n, fix="or")
        if ch == "!":
            self.error("Flaux uses the word `not` instead of `!`.", fix="not ",
                       hint="(To check that two things are different, `!=` works too.) Change the line to:")
        if ch == ".":
            if self.peek(1).isdigit():
                self.error("Decimal numbers need a digit before the dot.", fix="0.")
            call = re.match(r"((?:[A-Za-z_]+\.)*)([A-Za-z_]+)\((.*)\)\s*;?\s*(#.*)?$", after)
            word = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", before)
            if call and word:
                method, inside = call.group(2), call.group(3).strip()
                start_col = self.col - len(word.group(1))
                whole = word.group(1) + "." + call.group(1) + method
                if method in ("log", "WriteLine", "Write", "println", "print", "printf", "info"):
                    self.error(f"Flaux uses `say` instead of `{whole}`.",
                               col=start_col, length=len(whole),
                               hint="Change the line to:", fix_line=indent + "say " + inside)
                if method in ("append", "push", "Add", "add", "insert") and inside:
                    self.error(f"To put something in a list, Flaux uses `add ... to ...`.",
                               col=start_col, length=len(whole),
                               hint="Change the line to:",
                               fix_line=indent + f"add {inside} to {word.group(1)}")
            m = re.match(r"\.(length|size|count|len)\b(\(\))?", after and "." + after)
            word = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", before)
            if m and word:
                name = word.group(1)
                self.error(f"To get the length of `{name}`, use `length of {name}`.",
                           col=self.col - len(name), length=len(name) + len(m.group(0)),
                           fix=f"length of {name}")
            self.error("Flaux doesn't use dots, except inside decimal numbers like 3.5.",
                       hint="Things like `.length` or `.append` are written as words in Flaux:",
                       example="say length of nums\nadd 5 to nums")
        self.error(f"Flaux doesn't understand the symbol `{ch}`.",
                   hint="Flaux uses these symbols: + - * / // ^ % = != < > <= >= ( ) [ ] , "
                        "and \" for text. Most other things are words, like `and`, `or` and `not`.")

    def number(self):
        line, col, start = self.line, self.col, self.i
        while self.peek().isdigit() or (self.peek() == "_" and self.peek(1).isdigit()):
            self.advance()
        is_float = False
        if self.peek() == "." and self.peek(1).isdigit():
            is_float = True
            self.advance()
            while self.peek().isdigit() or (self.peek() == "_" and self.peek(1).isdigit()):
                self.advance()
        # scientific notation: 1.5e20, 3e-4
        if self.peek() in ("e", "E") and (self.peek(1).isdigit() or
                                          (self.peek(1) in ("+", "-") and self.peek(2).isdigit())):
            is_float = True
            self.advance()
            if self.peek() in "+-":
                self.advance()
            while self.peek().isdigit():
                self.advance()
        if self.peek().isascii() and (self.peek().isalpha() or self.peek() == "_"):
            digits_end = self.i
            while self.peek().isascii() and (self.peek().isalnum() or self.peek() == "_"):
                self.advance()
            word = self.src[start:self.i]
            digits = self.src[start:digits_end]
            self.error("Names can't start with a number.", line, col, self.i - start,
                       hint="Put the number at the end of the name instead. Change the line to:",
                       fix=word[len(digits):] + digits)
        text = self.src[start:self.i]
        clean = text.replace("_", "")
        if is_float:
            self.add("float", float(clean), line, col, len(text))
        else:
            value = int(clean)
            if value > 2**63 - 1:
                self.error("This number is too big for a whole number.", line, col, len(text),
                           hint="Whole numbers go up to 9223372036854775807. For bigger numbers, "
                                "use a decimal by adding .0 (decimals can go much higher, but "
                                "they keep only about 16 digits exactly):",
                           fix=clean[0] + "." + clean[1:7].rstrip("0").ljust(1, "0") + f"e{len(clean) - 1}")
            self.add("int", value, line, col, len(text))

    def name(self):
        line, col, start = self.line, self.col, self.i
        while self.peek().isascii() and (self.peek().isalnum() or self.peek() == "_"):
            self.advance()
        word = self.src[start:self.i]
        self.add("name", word, line, col, len(word))

    def string(self):
        line, col = self.line, self.col
        self.advance()  # opening quote
        parts = []
        buf = []
        while True:
            ch = self.peek()
            if ch == "" or ch == "\n":
                text = self.line_text(line)
                self.error('This text is missing its closing quote `"`.', line, col,
                           max(1, len(text.rstrip()) - col + 1),
                           hint='Text starts and ends with a double quote ("). Add one at the end:',
                           fix_line=text.rstrip() + '"')
            if ch == '"':
                self.advance()
                break
            if ch == "\\":
                esc_col = self.col
                self.advance()
                nxt = self.peek()
                if nxt in ESCAPES:
                    self.advance()
                    buf.append(ESCAPES[nxt])
                else:
                    self.error(f"`\\{nxt}` isn't a special character Flaux knows.",
                               self.line, esc_col, 2,
                               hint='Inside text, \\ starts a special character: \\n (new line), '
                                    '\\t (tab), \\" (quote mark), \\{ and \\} (curly brackets), and \\\\ for '
                                    "a real backslash. If you wanted a backslash, double it:",
                               fix="\\\\" + nxt)
                continue
            if ch == "{":
                if buf:
                    parts.append(("text", "".join(buf)))
                    buf = []
                parts.append(self.interpolation())
                continue
            if ch == "}":
                self.error("This `}` doesn't have a matching `{`.",
                           hint="To put a real } in text, write \\}:", fix="\\}")
            buf.append(self.advance())
        if buf or not parts:
            parts.append(("text", "".join(buf)))
        self.add("string", parts, line, col, max(1, self.col - col))

    def interpolation(self):
        open_line, open_col = self.line, self.col
        self.advance()  # {
        start = self.i
        expr_line, expr_col = self.line, self.col
        while True:
            ch = self.peek()
            if ch == "" or ch == "\n" or ch == '"' and "}" not in self.src[self.i:].split("\n")[0]:
                self.error("This `{` inside text is never closed with `}`.", open_line, open_col,
                           hint="Inside text, { } puts a value into the text. Close it with }, "
                                "or write \\{ if you wanted a real curly bracket.",
                           example='say "You have {lives} lives left"')
            if ch == '"':
                self.error("You can't put text in quotes inside `{ }`.",
                           hint="Make a variable for the inner text first, then put the variable inside { }:",
                           example='set greeting to "Hi"\nsay "{greeting}, Sam!"')
            if ch == "}":
                break
            self.advance()
        inner_src = self.src[start:self.i]
        self.advance()  # }
        if not inner_src.strip():
            self.error("There's nothing inside these `{ }`.", open_line, open_col, 2,
                       hint="Inside text, { } shows the value of a variable or a calculation. "
                            "To show real curly brackets, write \\{ and \\}.",
                       example='say "Score: {score}"\nsay "Next level: {level + 1}"')
        tokens = Lexer(inner_src, expr_line, expr_col, inner=True,
                       full_src="\n".join(self.full_lines)).run()
        tokens.append(Token("eof", None, self.line, self.col - 1, 1))
        return ("expr", tokens, expr_line, expr_col)


def tokenize(src):
    return Lexer(src).run()
