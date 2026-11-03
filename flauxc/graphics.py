"""Graphics support: the colour and key names Flaux knows, and the C code
for the graphics runtime (which uses the SDL2 library)."""

import os

from .font import c_table as font_table

GRAPHICS_RUNTIME_PATH = os.path.join(os.path.dirname(__file__), "graphics.h")

# Colour names (spaces, dashes and underscores are ignored: "dark green",
# "dark_green" and "darkgreen" all work).
COLORS = {
    "black": 0x000000,
    "white": 0xFFFFFF,
    "gray": 0x808080,
    "grey": 0x808080,
    "darkgray": 0x404040,
    "darkgrey": 0x404040,
    "lightgray": 0xC0C0C0,
    "lightgrey": 0xC0C0C0,
    "red": 0xE8303A,
    "darkred": 0x8B0000,
    "orange": 0xFF8C1A,
    "yellow": 0xFFE135,
    "gold": 0xFFC800,
    "green": 0x2EBD4E,
    "darkgreen": 0x1B6B2F,
    "lime": 0x9BE81E,
    "blue": 0x2F6FED,
    "darkblue": 0x14306E,
    "navy": 0x0F1B4C,
    "sky": 0x7EC8F7,
    "skyblue": 0x7EC8F7,
    "lightblue": 0xADD8F6,
    "cyan": 0x2AD4E0,
    "teal": 0x168F8F,
    "purple": 0x8E44C9,
    "violet": 0xB57EF0,
    "pink": 0xFF7EB6,
    "magenta": 0xE62BC9,
    "brown": 0x8B5A2B,
    "beige": 0xEADBB8,
}

# Key names you can use with key_down(...) and key_pressed(...).
KEYS = (
    {chr(c) for c in range(ord("a"), ord("z") + 1)}
    | {str(d) for d in range(10)}
    | {f"f{n}" for n in range(1, 13)}
    | {"left", "right", "up", "down", "space", "enter", "return", "escape", "esc", "tab",
       "backspace", "delete", "insert", "home", "end", "pageup", "pagedown",
       "shift", "ctrl", "control", "alt"}
)

COLOR_HELP = ('Use a colour name like "red", "blue", "dark green" or "sky", a code like '
              '"#ff8800", or make one with rgb(255, 136, 0).')
KEY_HELP = ('Key names are letters ("a" to "z"), numbers ("0" to "9"), arrows ("left", "right", '
            '"up", "down"), and "space", "enter", "escape", "shift", "ctrl", "alt", "tab", '
            '"backspace" and "f1" to "f12".')


def normalize(name):
    return "".join(ch for ch in name.lower() if ch not in " _-")


def is_color(name):
    key = normalize(name)
    if key in COLORS:
        return True
    hexpart = key[1:]
    return key.startswith("#") and len(hexpart) in (3, 6) and all(c in "0123456789abcdef" for c in hexpart)


def is_key(name):
    return normalize(name) in {normalize(k) for k in KEYS}


def runtime_c():
    """The graphics runtime, with the colour table and font filled in."""
    with open(GRAPHICS_RUNTIME_PATH, encoding="utf-8") as fh:
        code = fh.read()
    colors = ",\n".join(f'    {{"{name}", 0x{rgb:06X}}}' for name, rgb in COLORS.items())
    code = code.replace("/*@COLORS@*/", colors)
    code = code.replace("/*@FONT@*/", font_table())
    return code
