"""What Flaux Studio on the web asks Python (running in the page with
Pyodide). Every answer is JSON text, the same shapes the desktop Studio's
helper (studio/server.py) gives back."""

import json

from ..errors import FlauxError
from ..jsgen import compile_js
from ..studio import blocks as block_editor


def _problem(e, name, source):
    return {"ok": False, "error": e.to_dict(name, source)}


def check(source, name="untitled.flx"):
    try:
        compile_js(source, name)
        return json.dumps({"ok": True})
    except FlauxError as e:
        return json.dumps(_problem(e, name, source))
    except RecursionError:
        return json.dumps({"ok": False, "error": {"message": "This program is nested too deeply for Flaux to read.",
                                                  "line": None, "text": ""}})


def build(source, name="untitled.flx"):
    try:
        return json.dumps({"ok": True, "js": compile_js(source, name)})
    except FlauxError as e:
        return json.dumps(_problem(e, name, source))


def build_c(source, name="untitled.flx"):
    """The program as C code, for Export: TinyCC (in the page) makes it a Windows program."""
    from ..compiler import compile_source
    try:
        c = compile_source(source, name)
    except FlauxError as e:
        return json.dumps(_problem(e, name, source))
    except RecursionError:
        return json.dumps({"ok": False, "error": {"message": "This program is nested too deeply for Flaux to read.",
                                                  "line": None, "text": ""}})
    return json.dumps({"ok": True, "c": c, "graphics": "#define FX_USES_GRAPHICS" in c, "says": "#define FX_SAYS" in c})


def blocks(source):
    try:
        return json.dumps({"ok": True, "workspace": block_editor.to_blocks(source)})
    except FlauxError as e:
        return json.dumps(_problem(e, "untitled.flx", source))


def blockinfo():
    return json.dumps(block_editor.info())
