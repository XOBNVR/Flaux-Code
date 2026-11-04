"""Flaux Studio: a beginner-friendly code editor for Flaux.

It's a small web server on this computer (127.0.0.1 only) plus a page that
opens in its own app window (Microsoft Edge or Chrome in "app mode").
Only Python's standard library is used, so it works with the Python that
comes in the Flaux installer.

    flaux studio                 open Studio
    flaux studio game.flx        open Studio with a file
"""

import codecs
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from . import blocks
from .. import cli
from ..compiler import compile_source
from ..errors import FlauxError

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = cli.ROOT
EXAMPLES = os.path.join(ROOT, "examples")

STARTER = '''# Welcome to Flaux! Press the green Run button (or F5) to run this.
say "Hello! What's your name?"
set name to ask("Name:")
say "Nice to meet you, {name}!"
'''


def programs_folder():
    """Where the user's own programs are kept: Documents/Flaux."""
    docs = os.path.join(os.path.expanduser("~"), "Documents")
    base = docs if os.path.isdir(docs) else os.path.expanduser("~")
    folder = os.path.join(base, "Flaux")
    if not os.path.isdir(folder):
        os.makedirs(folder, exist_ok=True)
        with open(os.path.join(folder, "my_first_program.flx"), "w", encoding="utf-8") as fh:
            fh.write(STARTER)
    return folder


def list_flx(folder):
    try:
        names = sorted(f for f in os.listdir(folder) if f.lower().endswith(".flx"))
    except OSError:
        return []
    return [{"name": n, "path": os.path.join(folder, n)} for n in names]


def safe_name(name):
    """Turn what the user typed into a file name: letters, numbers, - and _."""
    name = name.strip()
    if name.lower().endswith(".flx"):
        name = name[:-4]
    name = re.sub(r"[^A-Za-z0-9 _-]+", "", name).strip().replace(" ", "_")
    return (name or "my_program") + ".flx"


class Run:
    """One running program: its process and everything it has printed."""

    MARKER = "Problem while running"   # how a problem message starts
    MAX_KEEP = 1_000_000
    JOIN_UP_TO = 16384

    def __init__(self, proc):
        self.proc = proc
        # [stream, text]; chunks[k] is chunk number base + k. A program that
        # says things very fast keeps only the newest MAX_KEEP characters
        # waiting, and the page is told older ones were skipped.
        self.chunks = []
        self.base = 0
        self.given = 0           # how many chunks the page has had
        self.kept = 0            # characters in self.chunks
        self.lock = threading.Lock()
        self.exit_code = None
        self.in_problem = False
        self.pending = ""        # text held back for a moment (see take)
        self.pending_since = 0.0
        # Normal output and problem messages come through one pipe so they
        # stay in the order they happened. Problems are spotted by their heading.
        self.readers = [threading.Thread(target=self.read, args=(proc.stdout,), daemon=True)]
        for t in self.readers:
            t.start()

    def read(self, pipe):
        decoder = codecs.getincrementaldecoder("utf-8")("replace")
        while True:
            data = pipe.read1(4096) if hasattr(pipe, "read1") else pipe.read(1)
            if not data:
                break
            with self.lock:
                self.take(decoder.decode(data))
        with self.lock:
            self.take(decoder.decode(b"", final=True), final=True)

    def take(self, text, final=False):
        """Add a new piece of output. Windows can split a problem message's
        heading over several pieces ("Probl" + "em while..."), so the end of a
        piece that might be the start of the heading waits for the next piece."""
        text = (self.pending + text).replace("\r\n", "\n")
        self.pending = ""
        if self.in_problem:
            text = text.replace("\r", "")
            if text:
                self.push("err", text)
            return
        i = text.find(self.MARKER)
        if i >= 0:
            if i:
                self.push("out", text[:i])
            self.push("err", text[i:])
            self.in_problem = True
            return
        keep = 0
        if not final:
            if text.endswith("\r"):
                keep = 1
            else:
                for n in range(min(len(self.MARKER) - 1, len(text)), 0, -1):
                    if self.MARKER.startswith(text[-n:]):
                        keep = n
                        break
        if keep:
            self.pending, text = text[-keep:], text[:-keep]
            self.pending_since = time.time()
        if text:
            self.push("out", text)

    def push(self, stream, text):
        """Add output (call with the lock held). Small pieces the page hasn't
        had yet are joined, and the oldest go if too much is waiting."""
        last = self.chunks[-1] if self.chunks else None
        if (last and last[0] == stream and self.base + len(self.chunks) - 1 >= self.given
                and len(last[1]) < self.JOIN_UP_TO):
            last[1] += text
        else:
            self.chunks.append([stream, text])
        self.kept += len(text)
        while self.kept > self.MAX_KEEP and len(self.chunks) > 1:
            self.kept -= len(self.chunks.pop(0)[1])
            self.base += 1

    def chunks_from(self, start):
        """The chunks from number `start` on (call with the lock held): (first number, chunks, skipped)."""
        first = max(start, self.base)
        chunks = [list(c) for c in self.chunks[first - self.base:]]
        self.given = max(self.given, first + len(chunks))
        return first, chunks, start < self.base

    def flush_pending(self, force=False):
        """Don't hold text back for long (call with the lock held)."""
        if self.pending and (force or time.time() - self.pending_since > 0.25):
            text, self.pending = self.pending.replace("\r", ""), ""
            self.push("err" if self.in_problem else "out", text)

    def add(self, stream, text):
        with self.lock:
            self.push(stream, text)

    def done(self):
        if self.exit_code is None and self.proc.poll() is not None:
            for t in self.readers:
                t.join(timeout=1)
            with self.lock:
                self.flush_pending(force=True)
            self.exit_code = self.proc.returncode
        return self.exit_code is not None

    def stop(self):
        if self.proc.poll() is None:
            self.proc.kill()


class Studio:
    def __init__(self, open_file=None, home=None):
        self.token = secrets.token_urlsafe(18)
        self.home = home or programs_folder()
        self.open_file = os.path.abspath(open_file) if open_file else None
        self.runs = {}
        self.next_run = 1
        self.last_ping = time.time()
        self.started = time.time()

    # ------------------------------------------------------------ files

    def state(self):
        def group(sub):
            return list_flx(os.path.join(EXAMPLES, sub)) if sub else list_flx(EXAMPLES)
        return {
            "home": self.home,
            "mine": list_flx(self.home),
            "lessons": group("lessons"),
            "games": group("games"),
            "examples": group(""),
            "open_file": self.open_file,
            "windows": cli.WINDOWS,
        }

    def is_example(self, path):
        return os.path.abspath(path).startswith(os.path.abspath(EXAMPLES) + os.sep)

    def open(self, path):
        path = os.path.abspath(path)
        if not path.lower().endswith(".flx"):
            raise ValueError("Flaux Studio opens .flx files.")
        with open(path, encoding="utf-8") as fh:
            content = fh.read()
        return {"path": path, "name": os.path.basename(path), "content": content,
                "example": self.is_example(path)}

    def save(self, content, path=None, name=None, from_path=None):
        if name:  # "Save as" a new file in the programs folder
            path = os.path.join(self.home, safe_name(name))
        if not path or not path.lower().endswith(".flx"):
            raise ValueError("Give your program a name first.")
        path = os.path.abspath(path)
        if self.is_example(path):
            raise ValueError("Examples can't be changed. Save a copy with a new name instead.")
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(content)
        # A copy of an example that uses pictures needs the pictures too.
        if from_path and os.path.dirname(os.path.abspath(from_path)) != os.path.dirname(path):
            for pic in re.findall(r'image(?:_width|_height)?\(\s*"([^"]+)"', content):
                src = os.path.join(os.path.dirname(os.path.abspath(from_path)), pic)
                dst = os.path.join(os.path.dirname(path), pic)
                if os.path.isfile(src) and not os.path.exists(dst):
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    shutil.copyfile(src, dst)
        return {"path": path, "name": os.path.basename(path)}

    # ---------------------------------------------------------- compiling

    def program_dir(self, path):
        return os.path.dirname(os.path.abspath(path)) if path else self.home

    def check(self, content, path=None):
        name = os.path.basename(path) if path else "untitled.flx"
        try:
            compile_source(content, name, self.program_dir(path))
            return {"ok": True}
        except FlauxError as e:
            return {"ok": False, "error": e.to_dict(name, content)}
        except RecursionError:
            return {"ok": False, "error": {"message": "This program is nested too deeply for Flaux to read.",
                                           "line": None, "text": "This program is nested too deeply."}}

    def run(self, content, path=None):
        name = os.path.basename(path) if path else "untitled.flx"
        try:
            c_code = compile_source(content, name, self.program_dir(path))
        except FlauxError as e:
            return {"ok": False, "error": e.to_dict(name, content)}
        try:
            exe = cli.cached_build(c_code)
        except cli.CliError as e:
            return {"ok": False, "error": {"message": str(e), "line": None, "text": str(e)}}
        if cli.WINDOWS and "FX_USES_GRAPHICS" in c_code:
            cli.copy_sdl_dll(os.path.dirname(exe))
        env = dict(os.environ, FLAUX_UNBUFFERED="1")
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # no extra black window on Windows
        proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, cwd=self.program_dir(path), env=env,
                                creationflags=flags)
        run_id = str(self.next_run)
        self.next_run += 1
        self.runs[run_id] = Run(proc)
        return {"ok": True, "run": run_id}

    def output(self, run_id, start):
        run = self.runs.get(run_id)
        if not run:
            return {"chunks": [], "done": True, "exit": None}
        done = run.done()
        with run.lock:
            run.flush_pending()
            first, chunks, skipped = run.chunks_from(start)
        return {"chunks": chunks, "start": first, "skipped": skipped, "done": done, "exit": run.exit_code}

    def send_input(self, run_id, text):
        run = self.runs.get(run_id)
        if not run or run.proc.poll() is not None:
            return {"ok": False}
        try:
            run.proc.stdin.write((text + "\n").encode("utf-8"))
            run.proc.stdin.flush()
        except OSError:
            return {"ok": False}
        return {"ok": True}

    def stop(self, run_id):
        run = self.runs.get(run_id)
        if run:
            run.stop()
        return {"ok": True}

    def stop_all(self):
        for run in self.runs.values():
            run.stop()

    # ------------------------------------------------------------ export

    def export(self, content, path=None, show=True):
        """A .zip that runs the program without Flaux Studio, saved in the
        programs folder: the built program, <name>.exe on Windows (plus
        SDL2.dll for games), and the pictures it names. On Windows the .exe
        is all you double-click (see cli.compile_c's `exported`); elsewhere
        play.sh starts it."""
        name = os.path.basename(path) if path else "untitled.flx"
        try:
            c_code = compile_source(content, name, self.program_dir(path))
        except FlauxError as e:
            return {"ok": False, "error": e.to_dict(name, content)}
        try:
            exe = cli.cached_build(c_code, exported=True)
        except cli.CliError as e:
            return {"ok": False, "error": {"message": str(e), "line": None, "text": str(e)}}
        base = re.sub(r"[^A-Za-z0-9 _-]+", "", os.path.splitext(name)[0]).strip()
        base = "game" if not base or base == "untitled" else base
        graphics = "FX_USES_GRAPHICS" in c_code
        target = os.path.join(self.home, base + ".zip")
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
            def add(arc, data=None, file=None, executable=False):
                info = zipfile.ZipInfo(f"{base}/{arc}", time.localtime()[:6])
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3                          # Unix, so the file mode counts
                info.external_attr = (0o100755 if executable else 0o100644) << 16
                if file:
                    with open(file, "rb") as fh:
                        data = fh.read()
                z.writestr(info, data)
            program = base + cli.EXE
            add(program, file=exe, executable=True)
            if cli.WINDOWS and graphics:
                dll = os.path.join(cli.BUNDLED_SDL, "bin", "SDL2.dll")
                if os.path.isfile(dll):
                    add("SDL2.dll", file=dll)
            three_d = "#define FX_USES_3D" in c_code
            if cli.WINDOWS and three_d and os.path.isdir(cli.BUNDLED_MESA):
                # 3D on computers without a good graphics card (the program looks in mesa\ next to it)
                for f in sorted(os.listdir(cli.BUNDLED_MESA)):
                    if os.path.isfile(os.path.join(cli.BUNDLED_MESA, f)):
                        add("mesa/" + f, file=os.path.join(cli.BUNDLED_MESA, f))
            if cli.WINDOWS and "#define FX_USES_XR" in c_code and os.path.isfile(cli.BUNDLED_OPENXR):
                add("openxr_loader.dll", file=cli.BUNDLED_OPENXR)      # VR headsets
            models = set(re.findall(r'"([^"\n]+\.obj)"', content, re.I)) if three_d else set()
            pictures = set(re.findall(r'"([^"\n]+\.(?:png|jpe?g|bmp|gif))"', content, re.I))
            # 3D models, the .mtl files they use, and the pictures those name
            for obj in sorted(models):
                for folder in (self.program_dir(path), os.getcwd()):
                    file = os.path.join(folder, obj)
                    if not os.path.isfile(file):
                        continue
                    with open(file, encoding="utf-8", errors="replace") as fh:
                        for lib in re.findall(r"^\s*mtllib\s+(.+?)\s*$", fh.read(), re.M):
                            pictures.add(os.path.join(os.path.dirname(obj), lib))
                            mtl = os.path.join(os.path.dirname(file), lib)
                            if os.path.isfile(mtl):
                                with open(mtl, encoding="utf-8", errors="replace") as mh:
                                    pictures.update(os.path.join(os.path.dirname(obj), p)
                                                    for p in re.findall(r"^\s*map_Kd\s+(.+?)\s*$", mh.read(), re.M))
                    pictures.add(obj)
                    break
            for pic in sorted(pictures):
                for folder in (self.program_dir(path), os.getcwd()):
                    file = os.path.join(folder, pic)
                    if os.path.isfile(file) and not os.path.isabs(pic) and ".." not in pic.replace("\\", "/").split("/"):
                        add(pic.replace("\\", "/"), file=file)
                        break
            if not cli.WINDOWS:
                add("play.sh", f'#!/bin/sh\ncd "$(dirname "$0")"\n./"{program}"\n', executable=True)
            how = f"double-click {program}" if cli.WINDOWS else "run play.sh"
            add("README.txt", (f"{base}\n{'=' * len(base)}\n\nMade with Flaux (https://github.com/XOBNVR/Flaux-Code).\n\n"
                               f"To play, {how}.\n" +
                               (f"Keep SDL2.dll next to {program} (to share the game, share the whole folder).\n"
                                if cli.WINDOWS and graphics else "") +
                               ("Keep the mesa folder too: it lets the game run on computers without a good\n"
                                "graphics card.\n" if cli.WINDOWS and three_d else "") +
                               ("F11 (or Alt+Enter) switches fullscreen.\n" if graphics else "")
                               ).replace("\n", "\r\n" if cli.WINDOWS else "\n"))
        if show:
            try:
                self.show_folder()
            except OSError:
                pass
        return {"ok": True, "name": base + ".zip", "file": target, "native": True}

    def blocks(self, content):
        """The program as blocks, for the block editor."""
        try:
            return {"ok": True, "workspace": blocks.to_blocks(content)}
        except FlauxError as e:
            return {"ok": False, "error": e.to_dict("untitled.flx", content)}
        except RecursionError:
            return {"ok": False, "error": {"message": "This program is nested too deeply to show as blocks.",
                                           "line": None, "text": ""}}

    def show_folder(self, path=None):
        folder = self.program_dir(path) if path else self.home
        if cli.WINDOWS:
            os.startfile(folder)  # noqa: the File Explorer
        elif sys.platform == "darwin":
            subprocess.Popen(["open", folder])
        else:
            subprocess.Popen(["xdg-open", folder], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return {"ok": True}


def make_handler(studio):
    with open(os.path.join(HERE, "index.html"), encoding="utf-8") as fh:
        page = fh.read()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):  # keep quiet
            pass

        def host_ok(self):
            # Only answer requests addressed to this computer (blocks DNS tricks)
            host = (self.headers.get("Host") or "").split(":")[0]
            return host in ("127.0.0.1", "localhost")

        def send(self, code, body, kind="application/json"):
            data = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", kind + "; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            url = urlparse(self.path)
            if not self.host_ok():
                return self.send(403, "Forbidden", "text/plain")
            if url.path == "/":
                if parse_qs(url.query).get("t", [""])[0] != studio.token:
                    return self.send(403, "<h1>Please open Flaux Studio from its shortcut.</h1>", "text/html")
                return self.send(200, page.replace("@@TOKEN@@", studio.token), "text/html")
            if url.path.startswith("/blockly/"):
                # the block editor's files (Blockly)
                base = os.path.realpath(os.path.join(HERE, "blockly"))
                file = os.path.realpath(os.path.join(base, url.path[len("/blockly/"):]))
                kinds = {".js": "text/javascript", ".svg": "image/svg+xml", ".mp3": "audio/mpeg",
                         ".gif": "image/gif", ".png": "image/png", ".cur": "image/x-icon", ".ogg": "audio/ogg",
                         ".wav": "audio/wav"}
                kind = kinds.get(os.path.splitext(file)[1])
                if file.startswith(base + os.sep) and kind and os.path.isfile(file):
                    with open(file, "rb") as fh:
                        return self.send(200, fh.read(), kind)
                return self.send(404, "Not found", "text/plain")
            if url.path == "/icon.png":
                with open(os.path.join(HERE, "icon.png"), "rb") as fh:
                    return self.send(200, fh.read(), "image/png")
            if url.path == "/course.json":
                with open(os.path.join(HERE, "course.json"), "rb") as fh:
                    return self.send(200, fh.read(), "application/json")
            if url.path == "/blocks.js":
                with open(os.path.join(HERE, "blocks.js"), "rb") as fh:
                    return self.send(200, fh.read(), "text/javascript")
            if url.path == "/picture":
                # Pictures for the Learn tab and the logo
                name = os.path.basename(parse_qs(url.query).get("name", [""])[0])
                file = os.path.join(HERE, name)
                if name.endswith(".png") and os.path.isfile(file):
                    with open(file, "rb") as fh:
                        return self.send(200, fh.read(), "image/png")
            return self.send(404, "Not found", "text/plain")

        def do_POST(self):
            if not self.host_ok() or self.headers.get("X-Flaux-Token") != studio.token:
                return self.send(403, json.dumps({"error": "forbidden"}))
            length = int(self.headers.get("Content-Length") or 0)
            try:
                data = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                return self.send(400, json.dumps({"error": "bad request"}))
            path = urlparse(self.path).path
            try:
                result = self.api(path, data)
            except (OSError, ValueError) as e:
                result = {"ok": False, "message": str(e)}
            if result is None:
                return self.send(404, json.dumps({"error": "unknown"}))
            self.send(200, json.dumps(result))

        def api(self, path, d):
            studio.last_ping = time.time()
            if path == "/api/ping":
                return {"ok": True}
            if path == "/api/state":
                return studio.state()
            if path == "/api/open":
                return studio.open(d["path"])
            if path == "/api/save":
                return studio.save(d.get("content", ""), d.get("path"), d.get("name"), d.get("from_path"))
            if path == "/api/check":
                return studio.check(d.get("content", ""), d.get("path"))
            if path == "/api/run":
                return studio.run(d.get("content", ""), d.get("path"))
            if path == "/api/output":
                return studio.output(str(d.get("run")), int(d.get("from", 0)))
            if path == "/api/input":
                return studio.send_input(str(d.get("run")), d.get("text", ""))
            if path == "/api/stop":
                return studio.stop(str(d.get("run")))
            if path == "/api/folder":
                return studio.show_folder(d.get("path"))
            if path == "/api/blocks":
                return studio.blocks(d.get("content", ""))
            if path == "/api/export":
                return studio.export(d.get("content", ""), d.get("path"))
            if path == "/api/blockinfo":
                return blocks.info()
            return None

    return Handler


def find_app_browser():
    """Edge or Chrome, which can show a page as its own app window."""
    candidates = []
    if cli.WINDOWS:
        for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"),
                     os.environ.get("LOCALAPPDATA")):
            if base:
                candidates += [os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe"),
                               os.path.join(base, "Google", "Chrome", "Application", "chrome.exe")]
    elif sys.platform == "darwin":
        candidates += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                       "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"]
    else:
        candidates += [shutil.which(n) or "" for n in
                       ("microsoft-edge", "google-chrome", "chromium", "chromium-browser")]
    return next((c for c in candidates if c and os.path.isfile(c)), None)


def open_window(url):
    browser = find_app_browser()
    if browser:
        subprocess.Popen([browser, f"--app={url}", "--window-size=1300,850"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        webbrowser.open(url)


def main(open_file=None, port=0, show=True, quiet=False, keep_running=False):
    studio = Studio(open_file)
    server = ThreadingHTTPServer(("127.0.0.1", port), make_handler(studio))
    url = f"http://127.0.0.1:{server.server_address[1]}/?t={studio.token}"
    if not quiet and sys.stdout:
        print(f"Flaux Studio is running at {url}", flush=True)
        print("(Close the Studio window to stop it.)", flush=True)
    if show:
        open_window(url)

    def watchdog():
        # Stop when the Studio window has been closed for a while.
        while True:
            time.sleep(2)
            quiet_for = time.time() - studio.last_ping
            if quiet_for > 20 and time.time() - studio.started > 60:
                studio.stop_all()
                server.shutdown()
                return

    if not keep_running:
        threading.Thread(target=watchdog, daemon=True).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        studio.stop_all()
    return url
