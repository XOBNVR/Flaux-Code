"""Friendly error messages for Flaux.

Every problem Flaux finds is a FlauxError. Errors don't just say what is
wrong, they teach how to fix it:

    Problem in game.flx, line 2:

      2 | say scroe
        |     ^^^^^

    I don't know what `scroe` is.

    How to fix it:
      You made `score` on line 1. Did you mean that? Change the line to:

      2 | say score

Parts of an error:
    message    what is wrong, in one sentence
    hint       how to fix it, in plain words
    fix        replacement text for the underlined part (we show the fixed line)
    fix_line   a whole replacement line
    fix_lines  several lines to show: [(line number or "+", text), ...]
               "+" marks a line that should be added
    example    a few lines of working Flaux code that show the idea
    see        (line number, words) to point at another line, e.g. where a
               function was made
"""

import textwrap

RED = "\033[1;31m"
GREEN = "\033[32m"
CYAN = "\033[36m"
BOLD = "\033[1m"
DIM = "\033[2m"
RESET = "\033[0m"


class FlauxError(Exception):
    def __init__(self, message, line=None, col=None, length=1, hint=None, fix=None,
                 fix_line=None, fix_lines=None, example=None, see=None):
        super().__init__(message)
        self.message = message
        self.line = line
        self.col = col
        self.length = max(1, length or 1)
        self.hint = hint
        self.fix = fix
        self.fix_line = fix_line
        self.fix_lines = fix_lines
        self.example = example
        self.see = see

    def fixed_lines(self, lines):
        """Work out the corrected line(s) to show, if any."""
        if self.fix_lines:
            return self.fix_lines
        if self.fix_line is not None and self.line:
            return [(self.line, self.fix_line)]
        if self.fix is not None and self.line and self.col and 1 <= self.line <= len(lines):
            text = lines[self.line - 1]
            start = self.col - 1
            return [(self.line, text[:start] + self.fix + text[start + self.length:])]
        return None

    def apply_fix(self, source):
        """The whole program with this error's fix applied, or None if the
        error doesn't come with a fix. Used by Flaux Studio's Apply fix button."""
        lines = source.split("\n")
        fixed = self.fixed_lines(lines)
        if not fixed:
            return None
        out = list(lines)
        offset = 0          # how many lines we've added so far
        insert_at = None    # where the next "+" line goes (0-based, in `out`)
        for i, (label, text) in enumerate(fixed):
            if label == "...":
                continue
            if label == "+":
                if insert_at is None:  # a "+" before any numbered line: add it above the next one
                    upcoming = next((n for n, _ in fixed[i:] if isinstance(n, int)), len(lines) + 1)
                    insert_at = upcoming - 1 + offset
                out.insert(insert_at, text)
                insert_at += 1
                offset += 1
            else:
                index = label - 1 + offset
                if 0 <= index < len(out):
                    out[index] = text
                insert_at = index + 1
        return "\n".join(out)

    def to_dict(self, filename, source):
        """Everything about this error, for Flaux Studio."""
        lines = source.splitlines()
        fixed = self.fixed_lines(lines)
        see = None
        if self.see and 1 <= self.see[0] <= len(lines):
            see = {"line": self.see[0], "words": self.see[1], "text": lines[self.see[0] - 1]}
        return {
            "message": self.message, "line": self.line, "col": self.col, "length": self.length,
            "hint": self.hint, "example": self.example, "see": see,
            "fixed": [[label, text] for label, text in fixed] if fixed else None,
            "fixed_source": self.apply_fix(source),
            "text": self.render(filename, source),
        }

    def render(self, filename, source, color=False):
        def paint(code, text):
            return f"{code}{text}{RESET}" if color else text

        lines = source.splitlines()
        fixed = self.fixed_lines(lines)
        numbers = [self.line or 0] + [n for n, _ in (fixed or []) if isinstance(n, int)]
        if self.see:
            numbers.append(self.see[0])
        width = max(len(str(n)) for n in numbers)

        def gutter(label, text, code=None):
            body = text.expandtabs(4).rstrip()
            label = str(label).rjust(width)
            sep = paint(DIM, f"{label} |")
            return f"  {sep} {paint(code, body) if code else body}"

        out = []
        if self.line and 1 <= self.line <= len(lines):
            out.append(paint(RED, "Problem") + paint(BOLD, f" in {filename}, line {self.line}:"))
            out.append("")
            text = lines[self.line - 1]
            out.append(gutter(self.line, text))
            if self.col:
                shown = text.expandtabs(4).rstrip()
                prefix = text[: self.col - 1].expandtabs(4)
                room = max(1, len(shown) - len(prefix))
                carets = "^" * min(self.length, room)
                out.append(f"  {paint(DIM, ' ' * width + ' |')} {' ' * len(prefix)}{paint(RED, carets)}")
        else:
            out.append(paint(RED, "Problem") + paint(BOLD, f" in {filename}:"))
        out.append("")
        out.append(self.message)

        if self.hint or fixed or self.example or self.see:
            out.append("")
            out.append(paint(BOLD, "How to fix it:"))
            hint = self.hint
            if fixed and not hint:
                hint = "Change the line to:" if len(fixed) == 1 else "Change it to:"
            if self.example and not hint:
                hint = "For example:"
            if hint:
                for para in hint.split("\n"):
                    out.append(textwrap.fill(para, width=78, initial_indent="  ",
                                             subsequent_indent="  "))
            if fixed:
                out.append("")
                for label, text in fixed:
                    if label == "+":
                        out.append(gutter("+", text, GREEN))
                    elif label == "...":
                        out.append(f"  {paint(DIM, ' ' * width + ' |')} ...")
                    else:
                        out.append(gutter(label, text, GREEN if label == self.line or len(fixed) == 1 else None))
            if self.example:
                out.append("")
                for ex in self.example.strip("\n").split("\n"):
                    out.append("      " + paint(CYAN, ex))
            if self.see and 1 <= self.see[0] <= len(lines):
                out.append("")
                out.append("  " + self.see[1])
                out.append(gutter(self.see[0], lines[self.see[0] - 1]))
        return "\n".join(out)
