"""Builds Flaux's Windows apps from flaux_app.cc: FlauxStudio.exe and FlauxPlayer.exe.

    python installer/app/build_app.py OUT_FOLDER [--cxx g++] [--windres windres]

Works on Windows (w64devkit's g++) and on Linux (x86_64-w64-mingw32-g++, from
the g++-mingw-w64-x86-64 package). It downloads Microsoft's WebView2 SDK (for
its WebView2.h) once, from NuGet, checking it's the exact file expected.
"""

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WEBVIEW2_VERSION = "1.0.2903.40"
WEBVIEW2_URL = (f"https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/{WEBVIEW2_VERSION}/"
                f"microsoft.web.webview2.{WEBVIEW2_VERSION}.nupkg")
WEBVIEW2_SHA256 = "ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74"

RESOURCES = """\
1 ICON "{icon}"
1 VERSIONINFO
FILEVERSION {v0},{v1},{v2},0
PRODUCTVERSION {v0},{v1},{v2},0
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "Flaux"
      VALUE "FileDescription", "{description}"
      VALUE "FileVersion", "{version}"
      VALUE "ProductName", "Flaux"
      VALUE "ProductVersion", "{version}"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"""


def tool(names):
    for name in names:
        if name and shutil.which(name):
            return name
    sys.exit(f"Couldn't find any of: {', '.join(n for n in names if n)}")


def webview2_include(cache):
    """The folder with WebView2.h (downloaded and checked the first time)."""
    include = os.path.join(cache, f"webview2-{WEBVIEW2_VERSION}")
    if os.path.isfile(os.path.join(include, "WebView2.h")):
        return include
    print(f"Getting the WebView2 SDK {WEBVIEW2_VERSION} from NuGet...")
    with urllib.request.urlopen(WEBVIEW2_URL, timeout=120) as r:
        data = r.read()
    if hashlib.sha256(data).hexdigest() != WEBVIEW2_SHA256:
        sys.exit("The WebView2 SDK that was downloaded isn't the one expected (its SHA-256 is different).")
    os.makedirs(include, exist_ok=True)
    # (WebView2.h says "EventToken.h"; MinGW has it as eventtoken.h, which only matters where
    # capital letters matter in file names: on Windows this file would include itself)
    if os.name != "nt":
        with open(os.path.join(include, "EventToken.h"), "w") as fh:
            fh.write("#include <eventtoken.h>\n")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name, target in (("build/native/include/WebView2.h", "WebView2.h"),
                             ("build/native/include/WebView2EnvironmentOptions.h", "WebView2EnvironmentOptions.h"),
                             ("LICENSE.txt", "LICENSE-WebView2.txt")):
            with open(os.path.join(include, target), "wb") as fh:
                fh.write(z.read(name))
    return include


def version():
    with open(os.path.join(ROOT, "flauxc", "__init__.py"), encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("__version__"):
                return line.split('"')[1]
    return "0.0.0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--cxx", help="the C++ compiler for 64-bit Windows")
    ap.add_argument("--windres", help="the resource compiler (for the icon)")
    ap.add_argument("--cache", default=os.path.join(tempfile.gettempdir(), "flaux-app-build"))
    args = ap.parse_args()
    windows = os.name == "nt"
    cxx = args.cxx or tool(["g++"] if windows else ["x86_64-w64-mingw32-g++"])
    windres = args.windres or tool(["windres"] if windows else ["x86_64-w64-mingw32-windres"])
    include = webview2_include(args.cache)
    os.makedirs(args.out, exist_ok=True)
    v = version()
    parts = (v.split(".") + ["0", "0", "0"])[:3]
    with tempfile.TemporaryDirectory() as tmp:
        for exe, description, defines in (("FlauxStudio.exe", "Flaux Studio", []),
                                          ("FlauxPlayer.exe", "A game made with Flaux", ["-DFLAUX_PLAYER"])):
            rc = os.path.join(tmp, "app.rc")
            with open(rc, "w", encoding="utf-8") as fh:
                fh.write(RESOURCES.format(icon=os.path.join(ROOT, "assets", "flaux.ico").replace("\\", "/"),
                                          version=v, v0=parts[0], v1=parts[1], v2=parts[2], description=description))
            res = os.path.join(tmp, "app-res.o")
            subprocess.run([windres, rc, "-O", "coff", "-o", res], check=True)
            target = os.path.join(args.out, exe)
            subprocess.run([cxx, "-std=c++17", "-O2", "-s", "-static", "-mwindows", "-municode",
                            "-I", include, "-I", HERE, *defines, os.path.join(HERE, "flaux_app.cc"), res,
                            "-o", target, "-ladvapi32", "-lole32", "-lshell32", "-lshlwapi", "-luser32",
                            "-lversion"], check=True)
            print(f"Made {target} ({os.path.getsize(target) // 1024} KB)")


if __name__ == "__main__":
    main()
