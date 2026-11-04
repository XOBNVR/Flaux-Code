"""Builds the C compiler that Flaux Studio on the web uses to Export real
Windows programs: TinyCC (a small C compiler that can make Windows .exe files),
made into WebAssembly so it runs in the web page.

    python3 -m flauxc.web.build_tcc OUTPUT_FOLDER

OUTPUT_FOLDER gets:
    tcc.js, tcc.wasm   TinyCC, as WebAssembly (with flaux_tcc.c, next to this file)
    tcc.data           what it reads while compiling: TinyCC's and Windows' C headers,
                       SDL2's headers, the lists of what Windows' DLLs (and SDL2.dll) have,
                       and Flaux's own C headers (stb_image.h, ...)
    SDL2.dll           SDL2 for 64-bit Windows: exported games need it next to them
    LICENSE-*.txt      TinyCC (LGPL 2.1), SDL2 (zlib)

Needs Linux with gcc and make (to build TinyCC for making Windows programs
first), MinGW-w64 (x86_64-w64-mingw32-nm and its Windows libraries, for the
DLL lists) and Emscripten (emcc). On Ubuntu:
    sudo apt-get install gcc make mingw-w64 emscripten
Downloads (checked against the SHA-256s below): TinyCC's source from Ubuntu's
archive, and SDL2 2.32.8 for Windows from NuGet.
"""

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
FLAUXC = os.path.dirname(HERE)

TCC_URL = ("http://archive.ubuntu.com/ubuntu/pool/universe/t/tcc/"
           "tcc_0.9.27+git20200814.62c30a4a.orig.tar.bz2")
TCC_SHA256 = "d63c113bcdd657f51c8ee197a0b0a24203603313e19a52b5f678d40ab7f5df35"
SDL_VERSION = "2.32.8"
SDL_PACKAGES = {
    "sdl2.nuget": "54e83aae770016653b70ee6b8fc8ec3c41aa1a0c6424aa204628fb976fe38359",
    "sdl2.nuget.redist": "b6e52e56d289fb8b83cb9527d0c5f0d80aa8bba0fe086247dbcae8408313c0ae",
}
# Windows DLLs that Flaux programs (and Windows' C library) can use
WINDOWS_DLLS = ["kernel32", "user32", "gdi32", "shell32", "advapi32", "ole32", "winmm", "msvcrt", "shcore"]
MINGW_LIB = "/usr/x86_64-w64-mingw32/lib"


def download(url, sha256):
    print(f"Downloading {url}")
    with urllib.request.urlopen(url, timeout=300) as r:
        data = r.read()
    if hashlib.sha256(data).hexdigest() != sha256:
        sys.exit(f"{url} isn't the file expected (its SHA-256 is different).")
    return data


def run(cmd, **kw):
    subprocess.run(cmd, check=True, **kw)


def build(out):
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        # 1. TinyCC, for making 64-bit Windows programs, on this computer first (for its
        #    library, libtcc1, and to read SDL2.dll's list of functions)
        src = os.path.join(tmp, "tcc")
        with tarfile.open(fileobj=io.BytesIO(download(TCC_URL, TCC_SHA256)), mode="r:bz2") as t:
            t.extractall(src, **({"filter": "data"} if hasattr(tarfile, "data_filter") else {}))
        run(["./configure"], cwd=src, stdout=subprocess.DEVNULL)
        run(["make", "cross-x86_64-win32"], cwd=src, stdout=subprocess.DEVNULL)
        cross = os.path.join(src, "x86_64-win32-tcc")

        # 2. what it reads while compiling (/sysroot in the web page)
        root = os.path.join(tmp, "sysroot")
        include, lib = os.path.join(root, "include"), os.path.join(root, "lib")
        os.makedirs(lib)
        shutil.copytree(os.path.join(src, "include"), include)
        shutil.copytree(os.path.join(src, "win32", "include"), include, dirs_exist_ok=True)
        shutil.copyfile(os.path.join(src, "x86_64-win32-libtcc1.a"), os.path.join(lib, "x86_64-win32-libtcc1.a"))
        for dll in WINDOWS_DLLS:   # what each Windows DLL has, from MinGW's import libraries
            names = subprocess.run(["x86_64-w64-mingw32-nm", "-g", os.path.join(MINGW_LIB, f"lib{dll}.a")],
                                   capture_output=True, text=True, check=True).stdout.split("\n")
            exports = sorted({line.split()[2][len("__imp_"):] for line in names
                              if len(line.split()) == 3 and line.split()[1] == "I" and line.split()[2].startswith("__imp_")})
            with open(os.path.join(lib, f"{dll}.def"), "w") as fh:
                fh.write(f"LIBRARY {dll}.dll\n\nEXPORTS\n" + "\n".join(exports) + "\n")
        sdl = os.path.join(tmp, "sdl")
        for name, sha in SDL_PACKAGES.items():
            url = f"https://api.nuget.org/v3-flatcontainer/{name}/{SDL_VERSION}/{name}.{SDL_VERSION}.nupkg"
            with zipfile.ZipFile(io.BytesIO(download(url, sha))) as z:
                z.extractall(sdl)
        shutil.copytree(os.path.join(sdl, "build", "native", "include"), os.path.join(include, "SDL2"))
        dll = os.path.join(sdl, "build", "native", "bin", "x64", "SDL2.dll")
        shutil.copyfile(dll, os.path.join(out, "SDL2.dll"))
        shutil.copyfile(dll, os.path.join(root, "SDL2.dll"))
        run([cross, "-impdef", "SDL2.dll", "-o", os.path.join("lib", "SDL2.def")], cwd=root)
        os.remove(os.path.join(root, "SDL2.dll"))
        shutil.copytree(os.path.join(FLAUXC, "vendor"), os.path.join(root, "flaux"))

        # 3. TinyCC itself, as WebAssembly, with all of that in tcc.data
        obj = os.path.join(tmp, "tcc.o")
        run(["emcc", "-O2", "-c", "tcc.c", "-o", obj, "-Dmain=tcc_main", "-DTCC_TARGET_PE", "-DTCC_TARGET_X86_64",
             '-DTCC_LIBTCC1="x86_64-win32-libtcc1.a"', '-DCONFIG_TCCDIR="/sysroot"', "-I.", "-Wno-everything"],
            cwd=src)
        run(["emcc", "-O2", obj, os.path.join(HERE, "tcc", "flaux_tcc.c"), "-o", os.path.join(out, "tcc.js"),
             "-sMODULARIZE=1", "-sEXPORT_NAME=createTcc", "-sINVOKE_RUN=0", "-sEXIT_RUNTIME=0",
             "-sALLOW_MEMORY_GROWTH=1", "-sFORCE_FILESYSTEM=1", "-sEXPORTED_FUNCTIONS=_flaux_tcc",
             "-sEXPORTED_RUNTIME_METHODS=ccall,FS", "-sENVIRONMENT=web,worker,node",
             "--preload-file", f"{root}@/sysroot"], cwd=out)

        shutil.copyfile(os.path.join(src, "COPYING"), os.path.join(out, "LICENSE-TinyCC.txt"))
        sdl_license = os.path.join(sdl, "build", "native", "docs", "LICENSE.txt")
        if os.path.isfile(sdl_license):
            shutil.copyfile(sdl_license, os.path.join(out, "LICENSE-SDL2.txt"))
        with open(os.path.join(out, "SOURCE.txt"), "w") as fh:
            fh.write("tcc.js, tcc.wasm and tcc.data are TinyCC (https://bellard.org/tcc/, LGPL 2.1, see\n"
                     f"LICENSE-TinyCC.txt), built from {TCC_URL}\n"
                     "by flauxc/web/build_tcc.py in https://github.com/XOBNVR/Flaux-Code.\n"
                     f"SDL2.dll is SDL {SDL_VERSION} (https://libsdl.org, zlib license, see LICENSE-SDL2.txt).\n")
    size = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out)) / 1e6
    print(f"The C compiler for the web is in {out} ({size:.1f} MB)")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output")
    build(ap.parse_args().output)
