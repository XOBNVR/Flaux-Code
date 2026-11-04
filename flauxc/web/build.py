"""Builds Flaux Studio for the web: a folder of plain files any web host can
serve. The GitHub build zips it as FlauxStudio-web.zip and attaches it
to each release.

    python3 -m flauxc.web.build OUTPUT_FOLDER --pyodide PYODIDE_FOLDER [--player FlauxPlayer.exe]

PYODIDE_FOLDER holds Pyodide's core files (from the "pyodide" npm package):
pyodide.js, pyodide.asm.mjs, pyodide.asm.wasm, python_stdlib.zip and
pyodide-lock.json. They're copied in, so the site works without any other
website.

FlauxPlayer.exe (made by installer/app/build_app.py) is the Windows game
player: Export puts it in the zip as <name>.exe, with the game inside, so a
game is a real Windows app. Without it, Export gives game.html and a .bat.
"""

import argparse
import glob
import json
import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
FLAUXC = os.path.dirname(HERE)
ROOT = os.path.dirname(FLAUXC)
STUDIO = os.path.join(FLAUXC, "studio")
PYODIDE_FILES = ["pyodide.js", "pyodide.mjs", "pyodide.asm.mjs", "pyodide.asm.wasm", "python_stdlib.zip",
                 "pyodide-lock.json"]


def python_files():
    """Flaux's compiler, for Python in the page (Pyodide)."""
    files = {}
    wanted = (glob.glob(os.path.join(FLAUXC, "*.py")) + glob.glob(os.path.join(FLAUXC, "web", "*.py"))
              + [os.path.join(STUDIO, "__init__.py"), os.path.join(STUDIO, "blocks.py"),
                 os.path.join(STUDIO, "blocks_table.json"), os.path.join(FLAUXC, "engine.glsl")])
    for path in wanted:
        with open(path, encoding="utf-8") as fh:
            files[os.path.relpath(path, ROOT).replace(os.sep, "/")] = fh.read()
    return files


def examples(out):
    """Every lesson, game and example (and the pictures they use)."""
    programs, pictures = [], {}
    base = os.path.join(ROOT, "examples")
    for path in sorted(glob.glob(os.path.join(base, "**", "*.flx"), recursive=True)):
        key = os.path.relpath(path, base).replace(os.sep, "/")
        if key.startswith("launchers/"):
            continue
        group = key.split("/")[0] if "/" in key else ""
        with open(path, encoding="utf-8") as fh:
            programs.append({"key": key, "name": os.path.basename(path), "group": group, "content": fh.read()})
    files = [p for ext in ("png", "obj", "mtl") for p in glob.glob(os.path.join(base, "**", "*." + ext), recursive=True)]
    for path in sorted(files):
        key = os.path.relpath(path, base).replace(os.sep, "/")
        target = os.path.join(out, "examples", key)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copyfile(path, target)
        pictures[key] = "examples/" + key
    return {"programs": programs, "pictures": pictures}


def page():
    """Flaux Studio's page, switched into its web version."""
    with open(os.path.join(STUDIO, "index.html"), encoding="utf-8") as fh:
        html = fh.read()
    html = html.replace("@@TOKEN@@", "web").replace('<html lang="en">', '<html lang="en" class="web">', 1)
    html = html.replace("<!--@WEB@-->", '<script src="fx.js"></script>\n<script src="fx3d.js"></script>\n'
                                        '<script src="web.js"></script>')
    html = html.replace("<title>Flaux Studio</title>", "<title>Flaux Studio</title>\n"
                        '<meta name="description" content="Write and run Flaux programs in your browser.">')
    return html


def build(out, pyodide, player=None):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(page())
    for name in ("blocks.js", "icon.png"):
        shutil.copyfile(os.path.join(STUDIO, name), os.path.join(out, name))
    for name in ("fx.js", "fx3d.js", "web.js", "player.html"):
        shutil.copyfile(os.path.join(HERE, name), os.path.join(out, name))
    if player:
        shutil.copyfile(player, os.path.join(out, "FlauxPlayer.exe"))
    shutil.copytree(os.path.join(STUDIO, "blockly"), os.path.join(out, "blockly"), dirs_exist_ok=True)
    with open(os.path.join(out, "flaux-files.json"), "w", encoding="utf-8") as fh:
        json.dump(python_files(), fh)
    with open(os.path.join(out, "examples.json"), "w", encoding="utf-8") as fh:
        json.dump(examples(out), fh)
    if pyodide:
        os.makedirs(os.path.join(out, "pyodide"), exist_ok=True)
        for name in PYODIDE_FILES:
            src = os.path.join(pyodide, name)
            if os.path.exists(src):
                shutil.copyfile(src, os.path.join(out, "pyodide", name))
    open(os.path.join(out, ".nojekyll"), "w").close()   # serve every file as it is (GitHub Pages)
    with open(os.path.join(out, ".htaccess"), "w") as fh:   # file types, for Apache servers
        fh.write("AddType application/wasm .wasm\nAddType text/javascript .mjs .js\nAddType application/octet-stream .exe\n"
                 "AddType application/json .json\n")
    shutil.copyfile(os.path.join(HERE, "site-CLAUDE.md"), os.path.join(out, "CLAUDE.md"))
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs) / 1e6
    print(f"Flaux Studio for the web is in {out} ({size:.1f} MB)")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output")
    ap.add_argument("--pyodide", help="folder with Pyodide's core files")
    ap.add_argument("--player", help="FlauxPlayer.exe, so Export makes a .exe (installer/app/build_app.py)")
    args = ap.parse_args()
    build(args.output, args.pyodide, args.player)
