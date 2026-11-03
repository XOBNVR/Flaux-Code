"""The kinds of values Flaux knows about.

Flaux programs never write types, but the compiler works out the exact type
of every variable and expression. That is what lets Flaux turn into fast
native code.

Types are plain Python values so they can be compared and used as dict keys:
    'int'    whole number    (64-bit integer)
    'float'  decimal number  (64-bit floating point)
    'bool'   true / false
    'text'   text
    'void'   nothing (a function that gives nothing back)
    '?'      not worked out yet
    ('list', T)  a list whose items are all of type T
"""

INT = "int"
FLOAT = "float"
BOOL = "bool"
TEXT = "text"
VOID = "void"
UNK = "?"
SHADER = "shader"   # a shader's name, given to a shape or effect(...)


class TypeConflict(Exception):
    pass


def ListT(elem):
    return ("list", elem)


def is_list(t):
    return isinstance(t, tuple) and t[0] == "list"


def is_num(t):
    return t == INT or t == FLOAT


def has_unknown(t):
    if t == UNK or t is None:
        return True
    if is_list(t):
        return has_unknown(t[1])
    return False


def resolve(t):
    """Replace any still-unknown list item types with whole numbers."""
    if is_list(t):
        return ListT(INT if t[1] == UNK else resolve(t[1]))
    return t


def merge(old, new, widen=True):
    """Combine what we knew about a value (old) with something new.

    Whole numbers widen to decimals when `widen` is on. Unknown parts get
    filled in. Anything else that disagrees is a TypeConflict.
    """
    if new is None or new == UNK:
        return old if old is not None else UNK
    if old is None or old == UNK:
        return new
    if old == new:
        return old
    if widen and is_num(old) and is_num(new):
        return FLOAT
    if is_list(old) and is_list(new):
        return ListT(merge(old[1], new[1], widen=False))
    raise TypeConflict()


def describe(t, plural=False):
    """Plain-English name for a type, used in error messages."""
    if t == INT:
        return "whole numbers" if plural else "a whole number"
    if t == FLOAT:
        return "decimal numbers" if plural else "a decimal number"
    if t == BOOL:
        return "true/false values" if plural else "a true/false value"
    if t == TEXT:
        return "text"
    if t == VOID:
        return "nothing"
    if t == SHADER:
        return "shaders" if plural else "a shader"
    if is_list(t):
        if t[1] == UNK:
            return "lists" if plural else "a list"
        return ("lists of " if plural else "a list of ") + describe(t[1], plural=True)
    return "something Flaux can't work out yet"


def mangle(t):
    """Short code for a type, used to build C names. Prefix-free."""
    t = resolve(t)
    if t == INT:
        return "i"
    if t == FLOAT:
        return "f"
    if t == BOOL:
        return "b"
    if t == TEXT:
        return "t"
    if t == VOID:
        return "v"
    if is_list(t):
        return "l" + mangle(t[1])
    raise ValueError(f"cannot mangle {t!r}")
