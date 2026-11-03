"""Graphics support: the colour and key names Flaux knows, and the C code
for the graphics runtime (which uses the SDL2 library)."""

import base64
import os

from .font import c_table as font_table
from .window_icon import RGBA as ICON_RGBA, SIZE as ICON_SIZE

HERE = os.path.dirname(os.path.abspath(__file__))
GRAPHICS_RUNTIME_PATH = os.path.join(HERE, "graphics.h")
GL_RUNTIME_PATH = os.path.join(HERE, "gl.h")
GLSL_PATH = os.path.join(HERE, "engine.glsl")
XR_RUNTIME_PATH = os.path.join(HERE, "xr.h")

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

# Game controller buttons, for button_down(...) and button_pressed(...).
BUTTONS = {"a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt", "l3", "r3", "up", "down", "left",
           "right", "select", "view", "menu", "options", "leftbumper", "rightbumper", "lefttrigger",
           "righttrigger", "l1", "r1", "l2", "r2", "grip", "trigger"}

BUTTON_HELP = ('Controller buttons are "a", "b", "x", "y", "start", "back", "lb", "rb" (the bumpers), '
               '"lt", "rt" (the triggers), "l3", "r3" (pressing a stick) and "up", "down", "left", "right" '
               '(the D-pad). On a VR controller, "trigger" and "grip" work too.')

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


def color_rgb(name):
    """(red, green, blue) for a colour name or code, each 0 to 255."""
    key = normalize(name)
    if key in COLORS:
        v = COLORS[key]
        return (v >> 16) & 255, (v >> 8) & 255, v & 255
    h = key[1:]
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def is_button(name):
    return normalize(name) in BUTTONS


def is_key(name):
    return normalize(name) in {normalize(k) for k in KEYS}


def glsl_sections():
    """engine.glsl, split into its named sections."""
    sections, name = {}, None
    with open(GLSL_PATH, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("//@@ "):
                name = line[5:].strip()
                sections[name] = []
            elif name:
                sections[name].append(line)
    return {k: "".join(v) for k, v in sections.items()}


def c_text(text):
    """A C string for some text, one line of it per line of C."""
    lines = text.splitlines(keepends=True) or [""]
    out = []
    for line in lines:
        esc = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        out.append(f'"{esc}"')
    return "\n    ".join(out)


def runtime_c(three_d=False, xr=False):
    """The graphics runtime, with the colour table and font filled in. With
    three_d, the OpenGL runtime (gl.h) replaces the 2D drawing; with xr, VR
    headsets (xr.h) are added too."""
    with open(GRAPHICS_RUNTIME_PATH, encoding="utf-8") as fh:
        code = fh.read()
    if three_d:
        with open(GL_RUNTIME_PATH, encoding="utf-8") as fh:
            gl = fh.read()
        glsl = "\n".join(f"static const char fx_glsl_{name}[] =\n    {c_text(text)};"
                         for name, text in glsl_sections().items())
        code = "#define FX_USES_3D 1\n" + code + "\n" + gl.replace("/*@GLSL@*/", glsl)
        if xr:
            with open(XR_RUNTIME_PATH, encoding="utf-8") as fh:
                code = "#define FX_USES_XR 1\n" + code + "\n" + fh.read()
    colors = ",\n".join(f'    {{"{name}", 0x{rgb:06X}}}' for name, rgb in COLORS.items())
    code = code.replace("/*@COLORS@*/", colors)
    code = code.replace("/*@FONT@*/", font_table())
    pixels = base64.b64decode(ICON_RGBA)
    rows = [", ".join(str(b) for b in pixels[i:i + 32]) for i in range(0, len(pixels), 32)]
    icon = (f"#define FX_ICON_SIZE {ICON_SIZE}\nstatic const uint8_t fx_icon[] = {{\n    "
            + ",\n    ".join(rows) + "\n};")
    code = code.replace("/*@ICON@*/", icon)
    return code
