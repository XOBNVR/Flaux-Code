"""The syntax tree: what a Flaux program looks like after parsing.

Every node remembers where it came from (line, col, length) so error
messages can point at the exact spot.
"""


class Node:
    fields = ()

    def __init__(self, *args, line=0, col=0, length=1):
        if len(args) != len(self.fields):
            raise TypeError(f"{type(self).__name__} takes {self.fields}")
        for name, value in zip(self.fields, args):
            setattr(self, name, value)
        self.line = line
        self.col = col
        self.length = length

    def __repr__(self):
        inner = ", ".join(f"{f}={getattr(self, f)!r}" for f in self.fields)
        return f"{type(self).__name__}({inner})"


# ---------------------------------------------------------------- expressions

class Num(Node):
    fields = ("value", "is_float")


class Str(Node):
    fields = ("parts",)  # list of str (plain text) or expression nodes


class Bool(Node):
    fields = ("value",)


class Name(Node):
    fields = ("name",)


class ListLit(Node):
    fields = ("items",)


class Copies(Node):
    fields = ("count", "value")  # `10 copies of 0`


class Unary(Node):
    fields = ("op", "operand")  # '-' or 'not'


class Binary(Node):
    fields = ("op", "left", "right")


class Call(Node):
    fields = ("name", "args")


class Index(Node):
    fields = ("base", "index")  # base[index], counting from 1


class Length(Node):
    fields = ("operand",)  # `length of x`


# ----------------------------------------------------------------- statements

class Set(Node):
    fields = ("target", "value")  # set target to value


class Add(Node):
    fields = ("value", "target")  # add value to target


class Subtract(Node):
    fields = ("value", "target")  # subtract value from target


class Say(Node):
    fields = ("values",)  # say a, b  or  say(a, b): shown with spaces between


class SaySame(Say):
    fields = ("values",)  # say.same a, b: the same, but the line isn't finished


class If(Node):
    fields = ("branches", "else_body")  # branches: list of (condition, body)


class RepeatTimes(Node):
    fields = ("count", "body")


class RepeatWhile(Node):
    fields = ("cond", "body", "until")  # until=True means `repeat until`


class RepeatForever(Node):
    fields = ("body",)


class RepeatRange(Node):
    fields = ("var", "start", "end", "step", "body")  # step may be None


class RepeatEach(Node):
    fields = ("var", "iterable", "body")


class Wait(Node):
    fields = ("amount", "unit")  # wait 2s / wait delay seconds; unit is "s", "m", "h" or "d"


class Stop(Node):
    fields = ()


class Skip(Node):
    fields = ()


class Return(Node):
    fields = ("value",)  # may be None


class ExprStmt(Node):
    fields = ("expr",)


class FuncDef(Node):
    fields = ("name", "params", "body")  # params: list of Name nodes


class Program(Node):
    fields = ("funcs", "main")
