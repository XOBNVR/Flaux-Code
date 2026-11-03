"""The `flaux` command.

    flaux hello.flx               compile and run a program (same as `flaux run`)
    flaux run  hello.flx          compile and run a program
    flaux build hello.flx         make a standalone program you can share
    flaux check hello.flx         just look for mistakes
    flaux c    hello.flx          show the C code Flaux makes (for the curious)
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

from . import __version__
from .compiler import compile_source
from .errors import FlauxError

CFLAGS = ["-O2", "-std=gnu11", "-fwrapv", "-w"]


def fail(msg, code=1):
    print(msg, file=sys.stderr)
    sys.exit(code)


def find_c_compiler():
    for cand in (os.environ.get("FLAUX_CC"), "cc", "gcc", "clang"):
        if cand and shutil.which(cand):
            return cand
    fail("Flaux needs a C compiler (gcc or clang) to make programs, and I couldn't find one.\n"
         "On Ubuntu/Debian: sudo apt install gcc\n"
         "On macOS: xcode-select --install")


def read_source(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return fh.read()
    except FileNotFoundError:
        fail(f"I can't find the file `{path}`.")
    except IsADirectoryError:
        fail(f"`{path}` is a folder, not a Flaux file.")
    except UnicodeDecodeError:
        fail(f"`{path}` doesn't look like a text file.")


def to_c(path):
    source = read_source(path)
    name = os.path.basename(path)
    try:
        return compile_source(source, name)
    except FlauxError as e:
        fail(e.render(name, source))
    except RecursionError:
        fail(f"{name} is nested too deeply for Flaux to read.")


SDL_HELP = """This program uses graphics, so Flaux needs the free SDL2 library, and it isn't installed.
Install it with one command, then try again:

    Ubuntu, Debian, WSL:  sudo apt install libsdl2-dev
    Fedora:               sudo dnf install SDL2-devel
    Arch:                 sudo pacman -S sdl2
    macOS (Homebrew):     brew install sdl2"""


def sdl_flags():
    """Compiler and linker flags for SDL2, from sdl2-config or pkg-config."""
    for tool in (["sdl2-config"], ["pkg-config", "sdl2"]):
        if not shutil.which(tool[0]):
            continue
        try:
            cflags = subprocess.run(tool + ["--cflags"], capture_output=True, text=True, check=True).stdout.split()
            libs = subprocess.run(tool + ["--libs"], capture_output=True, text=True, check=True).stdout.split()
            return cflags, libs
        except subprocess.CalledProcessError:
            continue
    fail(SDL_HELP)


def compile_c(c_code, out_path):
    cc = find_c_compiler()
    cflags, libs = [], []
    if "#define FX_USES_GRAPHICS" in c_code:
        cflags, libs = sdl_flags()
    with tempfile.TemporaryDirectory() as tmp:
        c_path = os.path.join(tmp, "program.c")
        with open(c_path, "w", encoding="utf-8") as fh:
            fh.write(c_code)
        result = subprocess.run([cc, *CFLAGS, *cflags, c_path, "-o", out_path, *libs, "-lm"],
                                capture_output=True, text=True)
    if result.returncode != 0:
        fd, keep = tempfile.mkstemp(prefix="flaux-bug-", suffix=".c")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(c_code)
        errors = "\n".join(result.stderr.splitlines()[:30])
        fail("Flaux made C code that the C compiler didn't accept. This is a bug in Flaux, "
             f"not in your program.\nThe C code was saved to {keep}\n\n{errors}")


def cached_build(c_code):
    """Build into a cache so running the same program again is instant."""
    cache = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "flaux")
    os.makedirs(cache, exist_ok=True)
    digest = hashlib.sha256((c_code + " ".join(CFLAGS)).encode()).hexdigest()[:20]
    exe = os.path.join(cache, f"prog-{digest}")
    if not os.path.exists(exe):
        partial = f"{exe}.{os.getpid()}.tmp"
        compile_c(c_code, partial)
        os.replace(partial, exe)
    return exe


def cmd_run(args):
    exe = cached_build(to_c(args.file))
    try:
        result = subprocess.run([exe, *args.args])
    except KeyboardInterrupt:
        sys.exit(130)
    sys.exit(result.returncode)


def cmd_build(args):
    out = args.output or os.path.splitext(os.path.basename(args.file))[0]
    compile_c(to_c(args.file), out)
    print(f"Made {out}. Run it with: ./{out}" if os.sep not in out else f"Made {out}.")


def cmd_check(args):
    to_c(args.file)
    print(f"No problems found in {args.file}.")


def cmd_c(args):
    code = to_c(args.file)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as fh:
            fh.write(code)
    else:
        sys.stdout.write(code)


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="flaux",
        description="Flaux: a programming language that reads like English and runs like C.")
    parser.add_argument("--version", action="version", version=f"Flaux {__version__}")
    sub = parser.add_subparsers(dest="command", metavar="command")

    p = sub.add_parser("run", help="compile and run a Flaux program")
    p.add_argument("file")
    p.add_argument("args", nargs=argparse.REMAINDER)
    p.set_defaults(func=cmd_run)

    p = sub.add_parser("build", help="make a standalone program")
    p.add_argument("file")
    p.add_argument("-o", "--output", help="name of the program to make")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("check", help="look for mistakes without running")
    p.add_argument("file")
    p.set_defaults(func=cmd_check)

    p = sub.add_parser("c", help="show the C code Flaux makes")
    p.add_argument("file")
    p.add_argument("-o", "--output", help="write the C code to this file")
    p.set_defaults(func=cmd_c)

    # Shortcut: `flaux game.flx` means `flaux run game.flx`.
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and not argv[0].startswith("-") and argv[0] not in ("run", "build", "check", "c"):
        argv.insert(0, "run")
    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        sys.exit(0)
    args.func(args)
