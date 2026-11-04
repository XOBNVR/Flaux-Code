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

CFLAGS = ["-O3", "-std=gnu11", "-fwrapv", "-w"]
# FLAUX_STRICT_C=1 (the tests): mistakes in the C code that newer C compilers (GCC 14 and later, like
# the one Flaux comes with on Windows) always refuse are refused by older ones too
if os.environ.get("FLAUX_STRICT_C"):
    CFLAGS = [f for f in CFLAGS if f != "-w"] + [
        "-Werror=incompatible-pointer-types", "-Werror=int-conversion", "-Werror=implicit-function-declaration",
        "-Werror=implicit-int", "-Werror=return-type"]
WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""

# When Flaux is installed with FlauxSetup.exe, the C compiler and SDL2 come
# with it, in folders next to the flauxc folder.
# (FLAUX_TOOLS can point somewhere else, which the Windows build's tests use.)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.environ.get("FLAUX_TOOLS") or ROOT
BUNDLED_CC = os.path.join(TOOLS, "toolchain", "bin", "gcc" + EXE)
BUNDLED_SDL = os.path.join(TOOLS, "sdl2")
BUNDLED_MESA = os.path.join(TOOLS, "mesa")   # software OpenGL, for PCs without a good graphics card
BUNDLED_OPENXR = os.path.join(TOOLS, "openxr", "openxr_loader.dll")   # for VR headsets


class CliError(Exception):
    """A problem to tell the user about (and then stop)."""


def fail(msg, code=1):
    raise CliError(msg)


def find_c_compiler():
    if os.environ.get("FLAUX_CC"):
        return os.environ["FLAUX_CC"]
    if os.path.isfile(BUNDLED_CC):
        return BUNDLED_CC
    for cand in ("cc", "gcc", "clang"):
        if shutil.which(cand):
            return cand
    if WINDOWS:
        fail("Flaux needs a C compiler to make programs, and I couldn't find one.\n"
             "The easiest fix is to reinstall Flaux with FlauxSetup.exe, which includes one.")
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
        return compile_source(source, name, os.path.dirname(os.path.abspath(path)))
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
    if os.path.isdir(BUNDLED_SDL):
        return (["-I", os.path.join(BUNDLED_SDL, "include", "SDL2")],
                ["-L", os.path.join(BUNDLED_SDL, "lib"), "-lSDL2"])
    if WINDOWS:
        fail("This program uses graphics, but the SDL2 library that Flaux uses for them is missing.\n"
             "Reinstall Flaux with FlauxSetup.exe to put it back.")
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


def compile_c(c_code, out_path, exported=False):
    """Build the C code into a program. `exported`: a program to share (Flaux Studio's Export):
    on Windows, a game that never says anything has no console window (problems show in a
    message box), and a program with a console waits for Enter before it closes."""
    cc = find_c_compiler()
    cflags, libs = [], []
    if exported and WINDOWS:
        cflags.append("-DFX_EXPORTED")
        if "#define FX_USES_GRAPHICS" in c_code and "#define FX_SAYS" not in c_code:
            cflags += ["-DFX_GUI_APP", "-mwindows"]
    if "#define FX_USES_GRAPHICS" in c_code:
        sdl_cflags, libs = sdl_flags()
        cflags = cflags + sdl_cflags + ["-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "vendor")]
        if "#define FX_USES_3D" in c_code and WINDOWS and os.path.isfile(os.path.join(BUNDLED_MESA, "opengl32.dll")):
            mesa = os.path.abspath(BUNDLED_MESA).replace("\\", "/")
            cflags.append(f'-DFX_SOFTWARE_GL="{mesa}"')
        if "#define FX_USES_XR" in c_code and WINDOWS and os.path.isfile(BUNDLED_OPENXR):
            loader = os.path.abspath(BUNDLED_OPENXR).replace("\\", "/")
            cflags.append(f'-DFX_XR_LOADER="{loader}"')
    with tempfile.TemporaryDirectory() as tmp:
        c_path = os.path.join(tmp, "program.c")
        with open(c_path, "w", encoding="utf-8") as fh:
            fh.write(c_code)
        extra = ["-Wl,--stack,16777216"] if WINDOWS else []  # 16 MB, like Linux, for deep recursion
        result = subprocess.run([cc, *CFLAGS, *cflags, c_path, "-o", out_path, *libs, "-lm", *extra],
                                capture_output=True, text=True, encoding="utf-8", errors="replace",
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    if result.returncode != 0:
        fd, keep = tempfile.mkstemp(prefix="flaux-bug-", suffix=".c")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(c_code)
        errors = "\n".join(result.stderr.splitlines()[:30])
        fail("Flaux made C code that the C compiler didn't accept. This is a bug in Flaux, "
             f"not in your program.\nThe C code was saved to {keep}\n\n{errors}")
    if libs and WINDOWS:
        copy_sdl_dll(os.path.dirname(os.path.abspath(out_path)))


def copy_sdl_dll(folder):
    """Windows programs that use graphics need SDL2.dll next to them."""
    dll = os.path.join(BUNDLED_SDL, "bin", "SDL2.dll")
    target = os.path.join(folder, "SDL2.dll")
    if os.path.isfile(dll) and not os.path.isfile(target):
        shutil.copyfile(dll, target)


def cache_dir():
    if WINDOWS:
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser("~")
        return os.path.join(base, "Flaux", "cache")
    return os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "flaux")


def cached_build(c_code, exported=False):
    """Build into a cache so running the same program again is instant."""
    cache = cache_dir()
    os.makedirs(cache, exist_ok=True)
    digest = hashlib.sha256((c_code + " ".join(CFLAGS) + (" exported" if exported else "")).encode()).hexdigest()[:20]
    exe = os.path.join(cache, f"prog-{digest}{EXE}")
    if not os.path.exists(exe):
        partial = os.path.join(cache, f"partial-{digest}-{os.getpid()}{EXE}")
        compile_c(c_code, partial, exported)
        try:
            os.replace(partial, exe)
        except OSError:  # another copy is running and finished first: use that one
            if not os.path.exists(exe):
                raise
    return exe


def cmd_run(args):
    exe = cached_build(to_c(args.file))
    try:
        result = subprocess.run([exe, *args.args])
    except KeyboardInterrupt:
        sys.exit(130)
    sys.exit(result.returncode)


def cmd_build(args):
    out = args.output or os.path.splitext(os.path.basename(args.file))[0] + EXE
    c_code = to_c(args.file)
    compile_c(c_code, out)
    if WINDOWS:
        extra = " Share it together with SDL2.dll, which is next to it." if "FX_USES_GRAPHICS" in c_code else ""
        print(f"Made {out}. Double-click it to run it.{extra}")
    else:
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


def cmd_studio(args):
    # The app (its own window, made with Tk). `--web`, or no Tk on this computer: the
    # older one, the same page as the website, in a browser window.
    if not (args.web or args.no_window or args.port):
        try:
            from .studio import app
        except ImportError:
            app = None
        if app:
            app.main(args.file)
            return
    from .studio import server
    server.main(args.file, port=args.port, show=not args.no_window, keep_running=args.keep_running)


def main(argv=None):
    # Flaux Studio's shortcut starts us without a console (pythonw.exe)
    if sys.stdout is None:
        sys.stdout = open(os.devnull, "w")
    if sys.stderr is None:
        sys.stderr = open(os.devnull, "w")
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
    if argv and not argv[0].startswith("-") and argv[0] not in ("run", "build", "check", "c", "studio"):
        argv.insert(0, "run")
    p = sub.add_parser("studio", help="open Flaux Studio, the beginner code editor")
    p.add_argument("file", nargs="?")
    p.add_argument("--port", type=int, default=0, help=argparse.SUPPRESS)
    p.add_argument("--no-window", action="store_true", help=argparse.SUPPRESS)
    p.add_argument("--keep-running", action="store_true", help=argparse.SUPPRESS)  # for tests
    p.add_argument("--web", action="store_true", help="the Studio as a web page, in a browser window")
    p.set_defaults(func=cmd_studio)

    args = parser.parse_args(argv)
    if not args.command:
        parser.print_help()
        sys.exit(0)
    try:
        args.func(args)
    except CliError as e:
        print(e, file=sys.stderr)
        sys.exit(1)
