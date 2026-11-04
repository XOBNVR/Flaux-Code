"""Makes a Flaux program into a Windows game app, the way Flaux Studio on
the web's Export does: the program becomes one web page (player.html with
fx.js and the compiled program), added to the end of FlauxPlayer.exe.

    python installer/app/make_game_exe.py FlauxPlayer.exe game.flx "My Game.exe"

(Pictures and 3D models aren't added here: the web version does that. This is
for testing the player, and for quick games.)
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, ROOT)

from flauxc.jsgen import compile_js  # noqa: E402

WEB = os.path.join(ROOT, "flauxc", "web")


def game_page(source, name):
    """The program as one web page (like web.js's exportGame)."""
    js = compile_js(source, name + ".flx")
    read = lambda f: open(os.path.join(WEB, f), encoding="utf-8").read()  # noqa: E731
    in_script = lambda code: code.replace("</script", "<\\/script").replace("<!--", "<\\!--")  # noqa: E731
    program = {"name": name, "js": js, "source": source, "pictures": {}, "files": {}}
    return (read("player.html")
            .replace("@@TITLE@@", name.replace("&", "&amp;").replace("<", "&lt;"))
            .replace("/*@FX@*/", in_script(read("fx.js")))
            .replace("/*@FX3D@*/", in_script(read("fx3d.js")) if "fx.setup3d(" in js else "")
            .replace("/*@PROGRAM@*/", json.dumps(program).replace("<", "\\u003c")))


def game_exe(player, page):
    """The player, the page, the page's length (8 bytes, little endian), then FLAUXGM1."""
    return player + page + struct.pack("<Q", len(page)) + b"FLAUXGM1"


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    player_path, source_path, out = sys.argv[1:]
    with open(source_path, encoding="utf-8") as fh:
        source = fh.read()
    name = os.path.splitext(os.path.basename(out))[0]
    with open(player_path, "rb") as fh:
        player = fh.read()
    with open(out, "wb") as fh:
        fh.write(game_exe(player, game_page(source, name).encode("utf-8")))
    print(f"Made {out}")


if __name__ == "__main__":
    main()
