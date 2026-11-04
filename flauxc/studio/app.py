"""Flaux Studio, the desktop app: a real program with its own window, made
with Tk (the window toolkit that comes with Python; on Windows its buttons
and dialogs are Windows' own). No web page, no web browser.

It does what the web version (index.html) does:
  - the code editor, with colours, line numbers and Tab to indent
  - Blocks mode: build the program from blocks (block_editor.py, drawn
    with Tk too), and switch between blocks and code at any time
  - mistakes underlined while you type, explained, with Apply fix
  - Run (F5) / Stop: what the program says appears on the right, and it can
    ask questions; games open in their own window
  - Open / Save (Windows' own file dialogs), Learn (lessons, games, code you
    can add), Export (a .zip with the real program, <name>.exe on Windows)
  - Night mode, and bigger or smaller text

Everything behind the buttons is the Studio class in server.py, the same
one the web page talks to.

    flaux studio [file.flx]
"""

import json
import os
import re
import sys
import threading
import time

import tkinter as tk
import tkinter.font as tkfont
from tkinter import filedialog, messagebox, ttk

from .. import __version__
from ..errors import FlauxError
from .server import EXAMPLES, Studio

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ICON_ICO = os.path.join(ROOT, "assets", "flaux.ico")
ICON_PNG = os.path.join(ROOT, "assets", "flaux-icon.png")
WINDOWS = sys.platform == "win32"

KEYWORDS = set(("set to add subtract from say if then else end repeat times while until with in by "
                "forever function shader return and or not is true false mod stop skip copies of more less "
                "than at least most equal greater fewer bigger smaller wait").split())
TIME_UNITS = set("second seconds minute minutes hour hours day days".split())
BUILTINS = set(("sqrt sin cos tan log abs floor ceil round min max random text number whole ask "
                "is_number clock length contains uppercase lowercase distance rgb window show clear rect circle line "
                "triangle dot write key_down key_pressed mouse_x mouse_y mouse_down mouse_clicked beep screenshot "
                "fullscreen mouse_moved_x mouse_moved_y lock_mouse unlock_mouse mouse_locked "
                "text_width image image_width image_height fps seconds button_down button_pressed stick_x stick_y "
                "camera sky sun light fog turn cube box ball cylinder cone plane model effect in_vr hand_x hand_y "
                "hand_z").split())
BLOCK_START = re.compile(r"^\s*(if|else|repeat|function|shader)\b")

SNIPPETS = [
    ("Say something", 'say "Hello!"'),
    ("Say on the same line", 'repeat 3 times\n    say.same "hi "\nend\nsay "!"'),
    ("Make a variable", "set score to 0\nadd 1 to score"),
    ("Ask a question", 'set name to ask("What\'s your name?")\nsay "Hi {name}!"'),
    ("If ... else", 'if score > 10\n    say "You win!"\nelse\n    say "Keep going"\nend'),
    ("Repeat", 'repeat 3 times\n    say "hooray!"\nend'),
    ("Count", "repeat with i from 1 to 10\n    say i\nend"),
    ("A list", 'set pets to ["cat", "dog"]\nadd "fish" to pets\nrepeat with pet in pets\n    say pet\nend'),
    ("A function", 'function greet(name)\n    say "Hello, {name}!"\nend\n\ngreet("Sam")'),
    ("Wait", 'say "Ready..."\nwait 1s\nsay "Go!"'),
    ("Random number", "set dice to random(1, 6)\nsay dice"),
    ("Game window", 'window(800, 600, "My Game")\nset x to 400\n\nrepeat forever\n    if key_down("left")\n'
                    '        subtract 5 from x\n    end\n    if key_down("right")\n        add 5 to x\n    end\n'
                    '    clear("navy")\n    circle(x, 300, 30, "yellow")\n    show()\nend'),
    ("Draw shapes", 'rect(50, 50, 200, 100, "blue")\ncircle(400, 300, 40, "red")\n'
                    'triangle(500, 200, 550, 100, 600, 200, "orange")\nwrite("Hello!", 20, 20, "white", 3)'),
    ("A picture", 'image("ship.png", 100, 200, 64, 64)'),
    ("A beep", "beep(440, 0.2)"),
    ("3D world", 'window(800, 600, "3D")\nset angle to 0\n\nrepeat forever\n    sky("sky", "white")\n'
                 '    camera(0, 3, 8, 0, 1, 0)\n    plane(0, 0, 0, 30, 30, "green")\n    turn(0, angle, 0)\n'
                 '    cube(0, 1, 0, 1.5, "red")\n    add 1 to angle\n    show()\nend'),
    ("A shader", 'shader stripes\n    if floor(x * 4) mod 2 = 0\n        return "white"\n    end\n    return "red"\nend\n\n'
                 'cube(0, 1, 0, 2, stripes)'),
    ("Mouse look", 'lock_mouse()\nset turn to 0\nrepeat forever\n    set turn to turn + mouse_moved_x() * 0.004\n'
                   '    show()\nend'),
    ("Game controller", 'if button_down("a")\n    say "jump!"\nend\nadd stick_x() * 5 to x'),
]

THEMES = {
    "day": {"bg": "#f4f5fb", "panel": "#ffffff", "text": "#1d2033", "muted": "#6b6f86", "line": "#dfe1ec",
            "ed_bg": "#ffffff", "ed_text": "#1d2033", "gutter": "#f0f1f7", "gutter_text": "#9a9db0",
            "select": "#cfd8ff", "cursor": "#1d2033", "kw": "#7b2fd0", "builtin": "#0b62c4", "str": "#1a8a3a",
            "interp": "#b35a00", "num": "#c2410c", "com": "#8a8fa5", "problem_bg": "#ffe4e4",
            "run_line": "#fff3c4", "out": "#1d2033", "err": "#c62828", "note": "#6b6f86", "in": "#6a4cff",
            "box_bg": "#fff8db", "box_line": "#e8c75a", "good": "#1a8a3a", "bad": "#c62828"},
    "night": {"bg": "#14161f", "panel": "#1b1e2b", "text": "#e8e9f0", "muted": "#9a9db0", "line": "#2c3044",
              "ed_bg": "#11131b", "ed_text": "#e8e9f0", "gutter": "#171a25", "gutter_text": "#5d6178",
              "select": "#34407a", "cursor": "#e8e9f0", "kw": "#c792ff", "builtin": "#6cb6ff", "str": "#8ee08e",
              "interp": "#ffb86b", "num": "#ff9e64", "com": "#6b7089", "problem_bg": "#4a1f27",
              "run_line": "#4a4120", "out": "#e8e9f0", "err": "#ff8a8a", "note": "#9a9db0", "in": "#9d93ff",
              "box_bg": "#2b2616", "box_line": "#7a6420", "good": "#6fd38a", "bad": "#ff8a8a"},
}


def settings_path():
    base = os.environ.get("APPDATA") if WINDOWS else os.environ.get("XDG_CONFIG_HOME")
    base = base or (os.path.expanduser("~") if WINDOWS else os.path.join(os.path.expanduser("~"), ".config"))
    return os.path.join(base, "Flaux", "studio.json")


def tokens(line):
    """[(kind, start, end)] for one line: com, str, interp, num, kw, builtin (like index.html)."""
    out, i, n = [], 0, len(line)
    waiting = re.match(r"^\s*wait\b", line) is not None
    while i < n:
        ch = line[i]
        if ch == "#":
            out.append(("com", i, n))
            break
        if ch == '"':
            j, start = i + 1, i
            while j < n and line[j] != '"':
                if line[j] == "\\" and j + 1 < n:
                    j += 2
                    continue
                if line[j] == "{":
                    out.append(("str", start, j))
                    close = line.find("}", j)
                    end = n if close == -1 else close + 1
                    out.append(("interp", j, end))
                    j = start = end
                    continue
                j += 1
            if j < n:
                j += 1
            out.append(("str", start, j))
            i = j
            continue
        m = re.match(r"\d[\d_]*(\.\d+)?(e[+-]?\d+)?([smhd](?!\w))?", line[i:], re.I)
        if m and not (i and (line[i - 1].isalnum() or line[i - 1] == "_")):
            out.append(("num", i, i + m.end()))
            i += m.end()
            continue
        m = re.match(r"say\.same(?!\w)", line[i:])
        if m and not (i and (line[i - 1].isalnum() or line[i - 1] == "_")):
            out.append(("kw", i, i + m.end()))
            i += m.end()
            continue
        m = re.match(r"[A-Za-z_]\w*", line[i:])
        if m:
            w = m.group(0)
            if w in KEYWORDS or (waiting and w in TIME_UNITS):
                out.append(("kw", i, i + len(w)))
            elif w in BUILTINS and line[i + len(w):].lstrip().startswith("("):
                out.append(("builtin", i, i + len(w)))
            i += len(w)
            continue
        i += 1
    return out


def Progress_answers(path):
    """(For the self-test) the answers saved in the course's progress file."""
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh).get("answers", {})
    except (OSError, ValueError):
        return {}


def lesson_name(name):
    m = re.match(r"^(\d+)_(.*)\.flx$", name)
    return f"Lesson {int(m.group(1))}: {m.group(2).replace('_', ' ')}" if m else name


class StudioApp:
    def __init__(self, root, open_file=None, home=None):
        self.root = root
        self.studio = Studio(open_file, home=home)
        self.settings = self.load_settings()
        self.night = bool(self.settings.get("night"))
        self.size = int(self.settings.get("size", 13))
        self.path = None           # the program's file (None: not saved yet)
        self.example = False       # an example: Save makes a copy
        self.dirty = False
        self.run_id = None
        self.run_from = 0
        self.problem = None
        self.check_after = None
        self.highlight_after = None
        self.checking = 0          # (newer checks win)
        self.loading = False
        self.mode = "code"         # or "blocks"
        self.blocks_view = None
        self.course_window = None
        self.challenge = None      # the course challenge being done in the editor (a unit's id)
        self.run_said = ""         # what the running program has said (to check a challenge)

        root.title("Flaux Studio")
        root.minsize(760, 480)
        root.geometry(self.settings.get("geometry", "1250x800"))
        self.set_icon()
        self.style = ttk.Style(root)
        self.fonts()
        self.build()
        self.apply_theme()
        self.bind_keys()
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.fill_learn()
        start = self.studio.open_file or self.settings.get("last_file")
        if not (start and os.path.isfile(start)):
            start = os.path.join(self.studio.home, "my_first_program.flx")
        if os.path.isfile(start):
            self.open_path(start, ask=False)
        else:
            self.set_code('say "Hello!"\n')
        self.status(f"Ready. Flaux {__version__}")
        root.after(50, self.place_divider)
        if os.environ.get("FLAUX_STUDIO_TEST"):
            root.after(500, self.self_test)

    # ------------------------------------------------------------ setup

    def load_settings(self):
        try:
            with open(settings_path(), encoding="utf-8") as fh:
                return json.load(fh)
        except (OSError, ValueError):
            return {}

    def save_settings(self):
        data = {"night": self.night, "size": self.size, "geometry": self.root.geometry(),
                "last_file": self.path if self.path and not self.example else None}
        try:
            os.makedirs(os.path.dirname(settings_path()), exist_ok=True)
            with open(settings_path(), "w", encoding="utf-8") as fh:
                json.dump(data, fh)
        except OSError:
            pass

    def set_icon(self):
        try:
            if WINDOWS:
                # its own taskbar button and icon (not Python's)
                import ctypes
                ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("Flaux.Studio")
                self.root.iconbitmap(default=ICON_ICO)
            elif os.path.isfile(ICON_PNG):
                self.icon = tk.PhotoImage(file=ICON_PNG)
                self.root.iconphoto(True, self.icon)
        except (tk.TclError, OSError, AttributeError):
            pass

    def fonts(self):
        families = set(tkfont.families(self.root))
        mono = next((f for f in ("Cascadia Mono", "Consolas", "Menlo", "DejaVu Sans Mono", "Liberation Mono")
                     if f in families), "TkFixedFont")
        ui = next((f for f in ("Segoe UI", "SF Pro Text", "Ubuntu", "DejaVu Sans") if f in families), "TkDefaultFont")
        self.code_font = tkfont.Font(self.root, family=mono, size=self.size)
        self.code_bold = tkfont.Font(self.root, family=mono, size=self.size, weight="bold")
        self.ui_font = tkfont.Font(self.root, family=ui, size=10)
        self.ui_bold = tkfont.Font(self.root, family=ui, size=10, weight="bold")
        self.title_font = tkfont.Font(self.root, family=ui, size=12, weight="bold")

    def build(self):
        root = self.root
        # toolbar
        bar = self.toolbar = ttk.Frame(root, padding=(8, 6), style="Bar.TFrame")
        bar.pack(side="top", fill="x")
        def button(text, cmd, tip=None, style="TButton"):
            b = ttk.Button(bar, text=text, command=cmd, style=style, width=len(text) + 2)
            b.pack(side="left", padx=2)
            return b
        ttk.Label(bar, text="Flaux Studio", font=self.title_font, style="Bar.TLabel").pack(side="left", padx=(2, 12))
        button("New", self.new)
        button("Open", self.open_dialog)
        button("Save", self.save)
        button("Save as", self.save_as)
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)
        self.mode_button = button("▦  Blocks", self.toggle_blocks)
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)
        self.run_button = button("▶  Run", self.run, style="Run.TButton")
        self.stop_button = button("■  Stop", self.stop)
        self.stop_button.state(["disabled"])
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)
        button("⬇  Export", self.export)
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)
        button("★  Course", self.open_course)
        for text, cmd in (("A+", lambda: self.zoom(1)), ("A−", lambda: self.zoom(-1))):
            ttk.Button(bar, text=text, width=4, command=cmd).pack(side="right", padx=2)
        self.night_button = ttk.Button(bar, text="Night", width=7, command=self.toggle_night)
        self.night_button.pack(side="right", padx=2)

        # status bar
        self.status_var = tk.StringVar()
        status = self.status_bar = ttk.Frame(root, padding=(10, 3), style="Bar.TFrame")
        status.pack(side="bottom", fill="x")
        self.status_label = ttk.Label(status, textvariable=self.status_var, style="Status.TLabel")
        self.status_label.pack(side="left")
        self.where_var = tk.StringVar()
        ttk.Label(status, textvariable=self.where_var, style="Muted.Bar.TLabel").pack(side="right")

        panes = self.panes = ttk.PanedWindow(root, orient="horizontal")
        panes.pack(fill="both", expand=True)

        # left: the editor, and the problem box under it
        left = ttk.Frame(panes)
        panes.add(left, weight=3)
        self.main_area = ttk.Frame(left)          # the code, or the blocks
        self.main_area.pack(fill="both", expand=True)
        editor = self.editor_frame = ttk.Frame(self.main_area)
        editor.pack(fill="both", expand=True)
        self.gutter = tk.Canvas(editor, width=44, highlightthickness=0, bd=0)
        self.gutter.pack(side="left", fill="y")
        self.code = tk.Text(editor, width=70, undo=True, autoseparators=True, maxundo=-1, wrap="none", font=self.code_font,
                            bd=0, highlightthickness=0, padx=10, pady=8, tabs=(self.code_font.measure("    "),),
                            insertwidth=2)
        yscroll = ttk.Scrollbar(editor, orient="vertical", command=self.code.yview)
        xscroll = ttk.Scrollbar(editor, orient="horizontal", command=self.code.xview)
        self.code.configure(yscrollcommand=lambda a, b: (yscroll.set(a, b), self.draw_gutter()),
                            xscrollcommand=xscroll.set)
        xscroll.pack(side="bottom", fill="x")
        yscroll.pack(side="right", fill="y")
        self.code.pack(side="left", fill="both", expand=True)

        self.box = tk.Frame(left, bd=0, highlightthickness=1)
        self.box_title = tk.Label(self.box, font=self.ui_bold, anchor="w", justify="left")
        self.box_title.pack(fill="x", padx=12, pady=(10, 2))
        self.box_text = tk.Text(self.box, height=7, wrap="word", font=self.ui_font, bd=0, highlightthickness=0,
                                padx=12, pady=4)
        self.box_text.tag_configure("code", font=self.code_font)
        self.box_text.pack(fill="x")
        box_buttons = tk.Frame(self.box, bd=0)
        box_buttons.pack(fill="x", padx=10, pady=(4, 10))
        self.box_buttons = box_buttons
        self.fix_button = ttk.Button(box_buttons, text="Apply fix", command=self.apply_fix, style="Run.TButton")
        self.goto_button = ttk.Button(box_buttons, text="Show me the line", command=self.goto_problem)
        self.goto_button.pack(side="right")

        # right: what the program says, and Learn
        right = ttk.Notebook(panes)
        self.tabs = right
        panes.add(right, weight=2)
        out = ttk.Frame(right)
        right.add(out, text="  Output  ")
        self.console = tk.Text(out, wrap="word", font=self.code_font, bd=0, highlightthickness=0, padx=10, pady=8,
                               state="disabled", width=44)
        cscroll = ttk.Scrollbar(out, orient="vertical", command=self.console.yview)
        self.console.configure(yscrollcommand=cscroll.set)
        ask = ttk.Frame(out, padding=(6, 6))
        ask.pack(side="bottom", fill="x")
        self.answer = ttk.Entry(ask, font=self.code_font)
        self.answer.pack(side="left", fill="x", expand=True)
        self.answer.bind("<Return>", lambda e: self.send_answer())
        self.send_button = ttk.Button(ask, text="Send", command=self.send_answer)
        self.send_button.pack(side="left", padx=(6, 0))
        self.answer.state(["disabled"])
        self.send_button.state(["disabled"])
        cscroll.pack(side="right", fill="y")
        self.console.pack(fill="both", expand=True)

        learn = ttk.Frame(right, padding=6)
        right.add(learn, text="  Learn  ")
        self.learn_tree = ttk.Treeview(learn, show="tree", selectmode="browse")
        lscroll = ttk.Scrollbar(learn, orient="vertical", command=self.learn_tree.yview)
        self.learn_tree.configure(yscrollcommand=lscroll.set)
        self.snippet_view = tk.Text(learn, height=9, wrap="none", font=self.code_font, bd=0, highlightthickness=0,
                                    padx=8, pady=6, state="disabled")
        learn_buttons = ttk.Frame(learn, padding=(0, 6, 0, 0))
        self.learn_button = ttk.Button(learn_buttons, text="Open", command=self.learn_go, style="Run.TButton")
        self.learn_button.pack(side="left")
        self.learn_hint = ttk.Label(learn_buttons, text="Double-click a lesson or game to open it.",
                                    style="Muted.TLabel")
        self.learn_hint.pack(side="left", padx=10)
        learn_buttons.pack(side="bottom", fill="x")
        self.snippet_view.pack(side="bottom", fill="x", pady=(6, 0))
        lscroll.pack(side="right", fill="y")
        self.learn_tree.pack(fill="both", expand=True)
        self.learn_tree.bind("<<TreeviewSelect>>", lambda e: self.learn_selected())
        self.learn_tree.bind("<Double-1>", lambda e: self.learn_go())

        self.code.bind("<<Modified>>", self.on_modified)
        self.code.bind("<KeyRelease>", lambda e: self.update_where())
        self.code.bind("<ButtonRelease-1>", lambda e: self.update_where())
        self.code.bind("<Configure>", lambda e: self.draw_gutter())

    def place_divider(self):
        """The code gets a bit more than half the window."""
        self.root.update_idletasks()
        try:
            self.panes.sashpos(0, int(self.panes.winfo_width() * 0.56))
        except tk.TclError:
            pass

    def apply_theme(self):
        t = THEMES["night" if self.night else "day"]
        self.t = t
        names = self.style.theme_names()
        if WINDOWS and not self.night and "vista" in names:
            self.style.theme_use("vista")
        elif "clam" in names:
            self.style.theme_use("clam")
        s = self.style
        s.configure(".", font=self.ui_font)
        if self.style.theme_use() == "clam":
            s.configure(".", background=t["bg"], foreground=t["text"], fieldbackground=t["panel"],
                        bordercolor=t["line"], lightcolor=t["panel"], darkcolor=t["line"], troughcolor=t["bg"])
            s.configure("TButton", background=t["panel"], foreground=t["text"], padding=(10, 4))
            s.map("TButton", background=[("active", t["line"]), ("disabled", t["bg"])],
                  foreground=[("disabled", t["muted"])])
            s.configure("TNotebook", background=t["bg"])
            s.configure("TNotebook.Tab", background=t["bg"], foreground=t["muted"], padding=(10, 4))
            s.map("TNotebook.Tab", background=[("selected", t["panel"])], foreground=[("selected", t["text"])])
            s.configure("Treeview", background=t["panel"], fieldbackground=t["panel"], foreground=t["text"],
                        rowheight=int(self.ui_font.metrics("linespace") * 1.6))
            s.map("Treeview", background=[("selected", t["select"])], foreground=[("selected", t["text"])])
            s.configure("TEntry", fieldbackground=t["ed_bg"], foreground=t["ed_text"], insertcolor=t["cursor"])
            s.configure("TScrollbar", background=t["panel"], troughcolor=t["bg"], arrowcolor=t["muted"],
                        bordercolor=t["line"])
            s.configure("TPanedwindow", background=t["bg"])
        s.configure("Run.TButton", font=self.ui_bold)
        s.configure("Bar.TFrame", background=t["bg"])
        s.configure("Bar.TLabel", background=t["bg"], foreground=t["text"])
        s.configure("Muted.Bar.TLabel", background=t["bg"], foreground=t["muted"])
        s.configure("Muted.TLabel", foreground=t["muted"])
        s.configure("Status.TLabel", background=t["bg"], foreground=t["text"])
        s.configure("Treeview", font=self.ui_font)
        self.root.configure(background=t["bg"])
        for w in (self.code, self.box_text, self.snippet_view):
            w.configure(background=t["ed_bg"], foreground=t["ed_text"], insertbackground=t["cursor"],
                        selectbackground=t["select"], selectforeground=t["ed_text"])
        self.console.configure(background=t["ed_bg"], foreground=t["out"], selectbackground=t["select"],
                               insertbackground=t["cursor"])
        self.gutter.configure(background=t["gutter"])
        for kind in ("kw", "builtin", "str", "interp", "num", "com"):
            self.code.tag_configure(kind, foreground=t[kind], font=self.code_bold if kind == "kw" else self.code_font)
            self.snippet_view.tag_configure(kind, foreground=t[kind])
        self.code.tag_configure("problem", background=t["problem_bg"], underline=True)
        self.code.tag_configure("run_line", background=t["run_line"])
        self.code.tag_raise("sel")
        for stream in ("out", "err", "note", "in"):
            self.console.tag_configure(stream, foreground=t[stream])
        self.console.tag_configure("good", foreground=t["good"])
        for w in (self.box, self.box_buttons):
            w.configure(background=t["box_bg"], highlightbackground=t["box_line"], highlightcolor=t["box_line"])
        self.box_title.configure(background=t["box_bg"], foreground=t["text"])
        self.box_text.configure(background=t["box_bg"])
        self.night_button.configure(text="Day" if self.night else "Night")
        if self.blocks_view:
            self.blocks_view.set_theme(t)
        if getattr(self, "course_window", None):
            self.course_window.apply_theme()
        self.draw_gutter()

    def bind_keys(self):
        r, c = self.root, self.code
        r.bind("<F5>", lambda e: (self.run(), "break")[1])
        r.bind("<Shift-F5>", lambda e: (self.stop(), "break")[1])
        r.bind("<Control-s>", lambda e: (self.save(), "break")[1])
        r.bind("<Control-S>", lambda e: (self.save_as(), "break")[1])
        r.bind("<Control-o>", lambda e: (self.open_dialog(), "break")[1])
        r.bind("<Control-n>", lambda e: (self.new(), "break")[1])
        r.bind("<Control-plus>", lambda e: self.zoom(1))
        r.bind("<Control-equal>", lambda e: self.zoom(1))
        r.bind("<Control-minus>", lambda e: self.zoom(-1))
        c.bind("<Tab>", self.on_tab)
        c.bind("<Shift-Tab>", self.on_untab)
        if not WINDOWS:
            c.bind("<ISO_Left_Tab>", self.on_untab)
        c.bind("<Return>", self.on_return)
        c.bind("<Control-y>", lambda e: (self.redo(), "break")[1])
        c.bind("<Control-Z>", lambda e: (self.redo(), "break")[1])

    # ------------------------------------------------------- the editor

    def get_code(self):
        return self.code.get("1.0", "end-1c")

    def set_code(self, text, keep_undo=False):
        self.loading = True
        if keep_undo:
            self.code.edit_separator()
            self.code.delete("1.0", "end")
            self.code.insert("1.0", text)
            self.code.edit_separator()
        else:
            self.code.delete("1.0", "end")
            self.code.insert("1.0", text)
            self.code.edit_reset()
        self.code.edit_modified(False)
        self.loading = False
        self.highlight()
        self.schedule_check(10)
        self.draw_gutter()

    def redo(self):
        try:
            self.code.edit_redo()
        except tk.TclError:
            pass

    def on_modified(self, _event=None):
        if not self.code.edit_modified():
            return
        self.code.edit_modified(False)
        if self.loading:
            return
        if not self.dirty:
            self.dirty = True
            self.update_title()
        self.code.tag_remove("run_line", "1.0", "end")
        if self.highlight_after:
            self.root.after_cancel(self.highlight_after)
        self.highlight_after = self.root.after(80, self.highlight)
        self.schedule_check()
        self.draw_gutter()

    def highlight(self, widget=None):
        widget = widget or self.code
        self.highlight_after = None
        for kind in ("kw", "builtin", "str", "interp", "num", "com"):
            widget.tag_remove(kind, "1.0", "end")
        for n, line in enumerate(widget.get("1.0", "end-1c").split("\n"), 1):
            for kind, a, b in tokens(line):
                widget.tag_add(kind, f"{n}.{a}", f"{n}.{b}")

    def draw_gutter(self):
        g = getattr(self, "gutter", None)
        if g is None:
            return
        g.delete("all")
        t = getattr(self, "t", THEMES["day"])
        lines = int(self.code.index("end-1c").split(".")[0])
        g.configure(width=self.code_font.measure("0" * max(3, len(str(lines)))) + 18)
        width = int(g.cget("width"))
        problem_line = self.problem.get("line") if self.problem else None
        i = self.code.index("@0,0")
        while True:
            info = self.code.dlineinfo(i)
            if info is None:
                break
            n = int(i.split(".")[0])
            color = t["bad"] if n == problem_line else t["gutter_text"]
            g.create_text(width - 8, info[1] + info[3] // 2, anchor="e", text=str(n), fill=color, font=self.code_font)
            nxt = self.code.index(f"{i}+1line")
            if nxt == i:
                break
            i = nxt

    def update_where(self):
        line, col = self.code.index("insert").split(".")
        self.where_var.set(f"Line {line}, column {int(col) + 1}")

    def line_text(self, index="insert"):
        return self.code.get(f"{index} linestart", f"{index} lineend")

    def on_tab(self, _e):
        if self.code.tag_ranges("sel"):
            first = int(self.code.index("sel.first").split(".")[0])
            last = int(self.code.index("sel.last-1c").split(".")[0])
            for n in range(first, last + 1):
                self.code.insert(f"{n}.0", "    ")
        else:
            col = int(self.code.index("insert").split(".")[1])
            self.code.insert("insert", " " * (4 - col % 4))
        return "break"

    def on_untab(self, _e):
        if self.code.tag_ranges("sel"):
            first = int(self.code.index("sel.first").split(".")[0])
            last = int(self.code.index("sel.last-1c").split(".")[0])
        else:
            first = last = int(self.code.index("insert").split(".")[0])
        for n in range(first, last + 1):
            spaces = len(self.code.get(f"{n}.0", f"{n}.4")) - len(self.code.get(f"{n}.0", f"{n}.4").lstrip(" "))
            if spaces:
                self.code.delete(f"{n}.0", f"{n}.{spaces}")
        return "break"

    def on_return(self, _e):
        """A new line starts as far in as this one, and further after `if`, `repeat`, ..."""
        before = self.code.get("insert linestart", "insert")
        indent = re.match(r"\s*", before).group(0)
        if BLOCK_START.match(before) and not before.strip().startswith("#"):
            indent += "    "
        if self.code.tag_ranges("sel"):
            self.code.delete("sel.first", "sel.last")
        self.code.insert("insert", "\n" + indent)
        self.code.see("insert")
        return "break"

    def zoom(self, step):
        self.size = max(8, min(32, self.size + step))
        for f in (self.code_font, self.code_bold):
            f.configure(size=self.size)
        self.code.configure(tabs=(self.code_font.measure("    "),))
        if self.blocks_view:
            self.blocks_view.set_zoom(self.size)
        self.draw_gutter()
        return "break"

    def toggle_night(self):
        self.night = not self.night
        self.apply_theme()

    # ------------------------------------------------------- mistakes

    def schedule_check(self, delay=500):
        if self.check_after:
            self.root.after_cancel(self.check_after)
        self.check_after = self.root.after(delay, self.check)

    def check(self):
        """Look for mistakes (on a helper thread: the window never waits)."""
        self.check_after = None
        self.checking += 1
        ticket, content, path = self.checking, self.get_code(), self.path

        def work():
            result = self.studio.check(content, path)
            self.root.after(0, lambda: self.show_check(ticket, content, result))
        threading.Thread(target=work, daemon=True).start()

    def show_check(self, ticket, content, result):
        if ticket != self.checking or content != self.get_code():
            return
        self.show_problem(None if result.get("ok") else result["error"])

    def show_problem(self, error):
        self.problem = error
        if self.mode == "blocks" and self.blocks_view:     # the problem's line: which block made it
            block = None
            if error and error.get("line"):
                _, line_blocks, _ = self.blocks_view.code()
                if 1 <= error["line"] <= len(line_blocks):
                    block = line_blocks[error["line"] - 1]
            self.blocks_view.show_problem(block)
        self.code.tag_remove("problem", "1.0", "end")
        if not error:
            self.box.pack_forget()
            self.draw_gutter()
            return
        line, col, length = error.get("line"), error.get("col") or 1, error.get("length") or 1
        if line:
            start = f"{line}.{max(0, col - 1)}"
            end = f"{start}+{max(1, length)}c" if self.line_text(start).strip() else f"{line}.0 lineend"
            if not self.code.get(start, end).strip():
                start, end = f"{line}.0", f"{line}.0 lineend"
            self.code.tag_add("problem", start, end)
        self.box_title.configure(text=("⚠  Line %d: " % line if line else "⚠  ") + error.get("message", ""))
        parts = []      # (text, tag)
        if error.get("hint"):
            parts.append((error["hint"], None))
        for label, text in error.get("fixed") or []:
            parts.append((f"   {label} | {text}" if isinstance(label, int) else f"   {text}", "code"))
        if error.get("example"):
            parts.append(("\n".join("   " + l for l in error["example"].split("\n")), "code"))
        self.box_text.configure(state="normal")
        self.box_text.delete("1.0", "end")
        for i, (text, tag) in enumerate(parts):
            self.box_text.insert("end", ("\n" if i else "") + text, tag or ())
        self.box.pack(fill="x", side="bottom", padx=8, pady=8, before=self.main_area)
        self.box_text.update_idletasks()
        try:
            lines = self.box_text.count("1.0", "end", "displaylines")
            lines = lines[0] if isinstance(lines, tuple) else lines
        except tk.TclError:
            lines = None
        self.box_text.configure(height=min(14, max(2, (lines or 4) + 1)), state="disabled")
        if error.get("fixed_source") is not None:
            self.fix_button.pack(side="left")
        else:
            self.fix_button.pack_forget()
        self.box.pack(fill="x", side="bottom", padx=8, pady=8, before=self.main_area)
        self.draw_gutter()

    def apply_fix(self):
        if self.problem and self.problem.get("fixed_source") is not None:
            line = self.problem.get("line") or 1
            self.set_code(self.problem["fixed_source"], keep_undo=True)
            self.reload_blocks()
            self.dirty = True
            self.update_title()
            self.code.mark_set("insert", f"{line}.0 lineend")
            self.code.see("insert")
            self.status("Fixed it. (Ctrl+Z puts it back.)", "good")
            self.schedule_check(10)

    def goto_problem(self):
        if self.problem and self.problem.get("line"):
            self.code.mark_set("insert", f"{self.problem['line']}.{max(0, (self.problem.get('col') or 1) - 1)}")
            self.code.see("insert")
            self.code.focus_set()

    # ------------------------------------------------------- running

    def say(self, text, stream="out"):
        self.console.configure(state="normal")
        self.console.insert("end", text, stream)
        self.console.configure(state="disabled")
        self.console.see("end")

    def run(self):
        if self.run_id:
            self.stop()
        self.tabs.select(0)
        self.console.configure(state="normal")
        self.console.delete("1.0", "end")
        self.console.configure(state="disabled")
        self.code.tag_remove("run_line", "1.0", "end")
        self.run_said = ""
        content, path = self.get_code(), self.path
        self.status("Getting your program ready...")
        self.run_button.state(["disabled"])

        def work():
            result = self.studio.run(content, path)
            self.root.after(0, lambda: self.started(result))
        threading.Thread(target=work, daemon=True).start()

    def started(self, result):
        self.run_button.state(["!disabled"])
        if not result.get("ok"):
            error = result.get("error") or {}
            self.show_problem(error if error.get("line") else self.problem)
            self.say(error.get("text") or error.get("message") or "It didn't start.", "err")
            self.say("\n— Fix the problem, then press Run again —\n", "note")
            self.status("There's a problem in your program.", "bad")
            return
        self.run_id, self.run_from = result["run"], 0
        self.stop_button.state(["!disabled"])
        self.answer.state(["!disabled"])
        self.send_button.state(["!disabled"])
        self.status("Running... (Stop: Shift+F5)")
        self.poll()

    def poll(self):
        if not self.run_id:
            return
        r = self.studio.output(self.run_id, self.run_from)
        if r.get("skipped"):
            self.say("\n(... some output was skipped: the program said things faster than anyone can read)\n", "note")
        for stream, text in r["chunks"]:
            self.say(text, stream)
            if stream == "out":
                self.run_said += text
        self.run_from = r.get("start", self.run_from) + len(r["chunks"])
        if r["done"]:
            self.finished(r["exit"])
        else:
            self.root.after(40, self.poll)

    def finished(self, code):
        self.run_id = None
        self.stop_button.state(["disabled"])
        self.answer.state(["disabled"])
        self.send_button.state(["disabled"])
        if self.challenge:
            self.check_challenge(code)
            if code == 0:
                return
        if code == 0:
            self.say("\n— Finished —\n", "note")
            self.status("Finished.", "good")
            return
        text = self.console.get("1.0", "end")
        m = re.search(r"Problem while running [^\n]*?, line (\d+):", text)
        if m:
            line = int(m.group(1))
            self.code.tag_add("run_line", f"{line}.0", f"{line}.0 lineend+1c")
            self.code.see(f"{line}.0")
        self.say("\n— Stopped —\n", "note")
        self.status("The program stopped." + (" The line it stopped on is lit up." if m else ""), "bad")

    def check_challenge(self, code):
        """The program for a course challenge finished: did it do it?"""
        from .course_window import check_challenge, load_course
        unit = next((u for u in load_course()["units"] if u["id"] == self.challenge), None)
        if not unit:
            return
        ok = check_challenge(unit["challenge"], self.run_said, code)
        if ok:
            self.say(f"\n★ Challenge done: {unit['title']}! ★\n", "good")
            self.status(f"Challenge done: {unit['title']}!", "good")
            self.challenge = None
        elif code == 0:
            self.say("\n— Finished —\n", "note")
            self.say("Not quite yet: for the challenge it should say\n" + unit["challenge"]["expected"] + "\n", "note")
            self.status("Not quite yet. Compare what it said with what the challenge wants.", "bad")
        if self.course_window or ok:
            from .course_window import Progress
            if self.course_window:
                self.course_window.challenge_finished(unit["id"], ok)
            elif ok:
                Progress(os.path.join(os.path.dirname(settings_path()), "course-progress.json")).challenge_done(unit["id"])

    def open_course(self):
        if self.course_window:
            self.course_window.top.deiconify()
            self.course_window.top.lift()
            return
        from .course_window import CourseWindow
        self.course_window = CourseWindow(self)

    def stop(self):
        if self.run_id:
            self.studio.stop(self.run_id)

    def send_answer(self):
        if not self.run_id:
            return
        text = self.answer.get()
        self.answer.delete(0, "end")
        self.say(text + "\n", "in")
        self.studio.send_input(self.run_id, text)

    # --------------------------------------------------------- files

    def update_title(self):
        name = os.path.basename(self.path) if self.path else "untitled"
        self.root.title(f"{'• ' if self.dirty else ''}{name} — Flaux Studio")

    def keep_changes(self):
        """Before closing this program: True if it's fine to go on."""
        if not self.dirty or not self.get_code().strip():
            return True
        answer = messagebox.askyesnocancel("Flaux Studio", "Save the changes to your program first?",
                                           parent=self.root)
        if answer is None:
            return False
        return self.save() if answer else True

    def new(self):
        if not self.keep_changes():
            return
        self.path, self.example, self.dirty = None, False, False
        self.challenge = None
        self.set_code("")
        self.reload_blocks()
        self.update_title()
        self.code.focus_set()
        self.status("A new program. Press Save to give it a name.")

    def open_dialog(self):
        if not self.keep_changes():
            return
        path = filedialog.askopenfilename(parent=self.root, title="Open a Flaux program", initialdir=self.studio.home,
                                          filetypes=[("Flaux programs", "*.flx"), ("All files", "*.*")])
        if path:
            self.open_path(path, ask=False)

    def open_path(self, path, ask=True):
        if ask and not self.keep_changes():
            return
        try:
            info = self.studio.open(path)
        except (OSError, ValueError, UnicodeDecodeError) as e:
            messagebox.showerror("Flaux Studio", f"Flaux Studio couldn't open that file.\n\n{e}", parent=self.root)
            return
        self.path, self.example, self.dirty = info["path"], info["example"], False
        self.challenge = None
        self.set_code(info["content"])
        self.reload_blocks()
        self.code.mark_set("insert", "1.0")
        self.code.see("1.0")
        self.update_title()
        self.status(f"Opened {info['name']}" + (" (an example: Save makes your own copy)" if self.example else ""))

    def save(self):
        if not self.path or self.example:
            return self.save_as()
        try:
            self.studio.save(self.get_code(), self.path)
        except (OSError, ValueError) as e:
            messagebox.showerror("Flaux Studio", f"It wasn't saved.\n\n{e}", parent=self.root)
            return False
        self.dirty = False
        self.update_title()
        self.status(f"Saved {os.path.basename(self.path)}", "good")
        return True

    def save_as(self):
        start = os.path.basename(self.path) if self.path else "my_program.flx"
        path = filedialog.asksaveasfilename(parent=self.root, title="Save your program", initialdir=self.studio.home,
                                            initialfile=start, defaultextension=".flx",
                                            filetypes=[("Flaux programs", "*.flx")])
        if not path:
            return False
        if not path.lower().endswith(".flx"):
            path += ".flx"
        try:
            saved = self.studio.save(self.get_code(), path, from_path=self.path)
        except (OSError, ValueError) as e:
            messagebox.showerror("Flaux Studio", f"It wasn't saved.\n\n{e}", parent=self.root)
            return False
        self.path, self.example, self.dirty = saved["path"], False, False
        self.update_title()
        self.status(f"Saved {saved['name']}", "good")
        self.fill_learn()
        return True

    def export(self):
        content, path = self.get_code(), self.path
        self.status("Making the export (building your program)...")

        def work():
            result = self.studio.export(content, path, show=True)
            self.root.after(0, lambda: self.exported(result))
        threading.Thread(target=work, daemon=True).start()

    def exported(self, r):
        if not r.get("ok"):
            error = r.get("error") or {}
            if error.get("line"):
                self.show_problem(error)
            self.status("Fix the problem in your program first, then export it." if error.get("line")
                        else (error.get("message") or "Export didn't work."), "bad")
            return
        self.tabs.select(0)
        how = "double-click the .exe inside" if WINDOWS else "run play.sh inside"
        self.say(f"\nExported {r['name']}, saved in your programs folder:\n  {r['file']}\n"
                 f"Unzip it anywhere and {how} to run your program.\n", "note")
        self.status(f"Exported {r['name']}", "good")

    # --------------------------------------------------------- blocks

    def toggle_blocks(self):
        if self.mode == "code":
            if self.to_blocks():
                self.editor_frame.pack_forget()
                self.blocks_view.pack(fill="both", expand=True)
                self.mode = "blocks"
                self.mode_button.configure(text="⌨  Code")
                self.root.after(30, lambda: self.panes.sashpos(0, int(self.panes.winfo_width() * 0.74)))
                self.status("Blocks: drag blocks out of the menus on the left. Right-click a block for more.")
                self.schedule_check(10)
        else:
            self.blocks_view.pack_forget()
            self.editor_frame.pack(fill="both", expand=True)
            self.mode = "code"
            self.mode_button.configure(text="▦  Blocks")
            self.place_divider()
            self.code.focus_set()

    def to_blocks(self):
        """Show the program as blocks. False if it can't be (a mistake that stops Flaux reading it)."""
        from .block_editor import BlockEditor
        from .blocks import to_blocks
        try:
            workspace = to_blocks(self.get_code())
        except FlauxError as e:
            self.show_problem(e.to_dict(os.path.basename(self.path or "untitled.flx"), self.get_code()))
            self.status("Fix the problem first: then the program can be shown as blocks.", "bad")
            return False
        except RecursionError:
            self.status("This program is nested too deeply to show as blocks.", "bad")
            return False
        if not self.blocks_view:
            self.blocks_view = BlockEditor(self.main_area, self.blocks_changed, self.t)
            self.blocks_view.set_zoom(self.size)
        self.blocks_view.load(workspace)
        return True

    def reload_blocks(self):
        """The code changed from outside the blocks (Open, New, Apply fix): show it as blocks again."""
        if self.mode == "blocks" and not self.to_blocks():
            self.toggle_blocks()       # (back to the code, where the problem is shown)

    def blocks_changed(self):
        code, _, _ = self.blocks_view.code()
        if code != self.get_code():
            self.set_code(code, keep_undo=True)
            if not self.dirty:
                self.dirty = True
                self.update_title()

    # --------------------------------------------------------- learn

    def fill_learn(self):
        tree = self.learn_tree
        tree.delete(*tree.get_children())
        self.learn_items = {}
        info = self.studio.state()
        sections = [("The Flaux course", [("Lessons, quizzes (1000+ questions) and challenges", ("course", None))]),
                    ("Lessons", [(lesson_name(f["name"]), f["path"]) for f in info["lessons"]]),
                    ("Games", [(f["name"][:-4].replace("_", " "), f["path"]) for f in info["games"]]),
                    ("Examples", [(f["name"][:-4].replace("_", " "), f["path"]) for f in info["examples"]]),
                    ("Your programs", [(f["name"], f["path"]) for f in info["mine"]]),
                    ("Code you can add", [(title, ("snippet", code)) for title, code in SNIPPETS])]
        for title, items in sections:
            head = tree.insert("", "end", text=title, open=title in ("The Flaux course", "Lessons", "Code you can add"))
            for label, what in items:
                self.learn_items[tree.insert(head, "end", text="   " + label)] = what

    def learn_selected(self):
        what = self.learn_items.get((self.learn_tree.selection() or [None])[0])
        if isinstance(what, tuple) and what[0] == "course":
            self.learn_button.configure(text="Open the course")
            self.learn_hint.configure(text="18 units, each with a lesson, a quiz and a challenge.")
            self.snippet_view.configure(state="normal")
            self.snippet_view.delete("1.0", "end")
            self.snippet_view.configure(state="disabled")
            return
        snippet = isinstance(what, tuple)
        self.learn_button.configure(text="Add to my program" if snippet else "Open")
        self.learn_hint.configure(text="It goes where your cursor is." if snippet
                                  else "Double-click a lesson or game to open it.")
        text = what[1] if snippet else ""
        if isinstance(what, str):
            try:
                with open(what, encoding="utf-8") as fh:
                    text = "".join(fh.readlines()[:40])
            except OSError:
                text = ""
        self.snippet_view.configure(state="normal")
        self.snippet_view.delete("1.0", "end")
        self.snippet_view.insert("1.0", text)
        self.highlight(self.snippet_view)
        self.snippet_view.configure(state="disabled")

    def learn_go(self):
        what = self.learn_items.get((self.learn_tree.selection() or [None])[0])
        if isinstance(what, tuple) and what[0] == "course":
            self.open_course()
        elif isinstance(what, tuple):
            self.add_snippet(what[1])
        elif isinstance(what, str):
            self.open_path(what)

    def add_snippet(self, snippet):
        line = self.line_text()
        indent = re.match(r"\s*", line).group(0)
        if line.strip():                      # on a line of its own, after this one
            self.code.mark_set("insert", "insert lineend")
            text = "\n" + "\n".join(indent + l if l else l for l in snippet.split("\n"))
        else:
            self.code.delete("insert linestart", "insert lineend")
            text = "\n".join(indent + l if l else l for l in snippet.split("\n"))
        self.code.edit_separator()
        self.code.insert("insert", text)
        self.code.edit_separator()
        self.code.see("insert")
        self.code.focus_set()

    # --------------------------------------------------------- the rest

    def status(self, text, kind=None):
        self.status_var.set(text)
        t = getattr(self, "t", THEMES["day"])
        self.style.configure("Status.TLabel", background=t["bg"],
                             foreground={"good": t["good"], "bad": t["bad"]}.get(kind, t["text"]))

    def drag_with_mouse(self, src, sx, sy, dst, dx, dy):
        """(For the self-test) press at (sx, sy) on `src`, move to (dx, dy) on `dst`, let go."""
        def at(widget, x, y, kind, **more):
            widget.event_generate(kind, x=int(x), y=int(y), rootx=int(widget.winfo_rootx() + x),
                                  rooty=int(widget.winfo_rooty() + y), **more)
            self.root.update()
        at(src, sx, sy, "<ButtonPress-1>")
        x0, y0 = src.winfo_rootx() + sx, src.winfo_rooty() + sy
        x1, y1 = dst.winfo_rootx() + dx, dst.winfo_rooty() + dy
        for k in range(1, 9):
            rx, ry = x0 + (x1 - x0) * k / 8, y0 + (y1 - y0) * k / 8
            at(src, rx - src.winfo_rootx(), ry - src.winfo_rooty(), "<B1-Motion>", state=0x100)
        at(src, x1 - src.winfo_rootx(), y1 - src.winfo_rooty(), "<ButtonRelease-1>")

    def self_test(self):
        """FLAUX_STUDIO_TEST=<file>: use the Studio like a person would (a mistake, Apply fix, Run,
        Save), write what happened to <file> as JSON, and close. For the tests (tests/studio_app_test.py
        and the Windows installer's)."""
        out, report = os.environ["FLAUX_STUDIO_TEST"], {"title": self.root.title()}

        def wait_for(test, then, seconds=90):
            deadline = time.time() + seconds
            def again():
                if test() or time.time() > deadline:
                    then()
                else:
                    self.root.after(100, again)
            again()

        def typed():
            self.set_code('set score to 10\nrepeat 3 times\n    say.same "hi "\nend\nsay scroe\n')
            wait_for(lambda: self.problem is not None, found)

        def found():
            report["problem"] = self.problem and self.problem.get("message")
            report["problem_line"] = self.problem and self.problem.get("line")
            self.apply_fix()
            wait_for(lambda: self.problem is None and self.check_after is None, fixed, 30)

        def fixed():
            report["fixed_code"] = self.get_code()
            self.run()
            wait_for(lambda: "Finished" in self.console.get("1.0", "end") or "Stopped" in self.console.get("1.0", "end")
                     or "Fix the problem" in self.console.get("1.0", "end"), ran)

        def ran():
            report["output"] = self.console.get("1.0", "end-1c")
            path = os.path.join(self.studio.home, "studio_test.flx")
            self.studio.save(self.get_code(), path)
            self.path, self.example, self.dirty = path, False, False
            report["saved"] = os.path.isfile(path)
            # Blocks mode: the same program as blocks, and a block added from the menus
            from . import blockmodel
            self.toggle_blocks()
            report["blocks_mode"] = self.mode
            report["blocks_code"] = self.get_code()
            view = self.blocks_view
            tail = view.tops[0]
            while tail.next:
                tail = tail.next
            tail.next = blockmodel.Block("flx_say", inputs={"VALUE": blockmodel.text("made with blocks")})
            view.changed()
            report["blocks_added"] = self.get_code()
            # and a block dragged out of the menus with the mouse, under the last block
            view.cats.selection_clear(0, "end")
            view.cats.selection_set(0)
            view.pick_category()
            self.root.update()
            say = [t for t in view.palette_targets if t[4] == "template"][0]
            tail = view.tops[0]
            while tail.next:
                tail = tail.next
            box = view.box_of(tail)
            self.drag_with_mouse(view.palette, say[0] + 10, say[1] + 12, view.board,
                                 int(box[0] - view.board.canvasx(0)) + 12, int(box[3] - view.board.canvasy(0)) + 14)
            report["blocks_dragged"] = self.get_code()
            self.toggle_blocks()
            self.toggle_night()
            report["night"] = self.night
            # the Course: a quiz question answered right and one wrong, then a challenge done in the editor
            self.dirty = False
            self.open_course()
            course = self.course_window
            self.root.update()
            report["course_units"] = len(course.units)
            report["course_questions"] = len(course.course["questions"])
            course.start_quiz()
            self.root.update()
            first = course.quiz[0]
            course.answer_buttons[first["answer"]].invoke()
            report["quiz_right"] = str(course.feedback.cget("text"))
            course.next_question()
            second = course.quiz[1]
            course.choose((second["answer"] + 1) % len(second["choices"]))
            report["quiz_wrong"] = str(course.feedback.cget("text"))
            report["quiz_saved"] = Progress_answers(course.progress.path)
            course.tree.selection_set("say")
            course.show_unit("say")
            course.start_challenge()
            self.set_code(self.get_code() + course.unit["challenge"]["solution"])
            report["challenge_code"] = self.get_code()
            self.run()
            wait_for(lambda: self.run_id is None and "Challenge done" in self.console.get("1.0", "end")
                     or "Stopped" in self.console.get("1.0", "end"), challenged, 60)

        def challenged():
            course = self.course_window
            report["challenge_output"] = self.console.get("1.0", "end-1c")
            report["challenge_done"] = "say" in course.progress.challenges
            report["unit_label"] = course.tree.item("say", "text")
            with open(out, "w", encoding="utf-8") as fh:
                json.dump(report, fh)
            self.dirty = False         # (the test's changes aren't kept: no "Save the changes?")
            self.close()

        typed()

    def close(self):
        if self.course_window:
            self.course_window.close()
        if not self.keep_changes():
            return
        self.save_settings()
        self.studio.stop_all()
        self.root.destroy()


def main(open_file=None, home=None):
    if WINDOWS:
        try:     # sharp text on high-resolution screens (before the window is made)
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (OSError, AttributeError):
            pass
    root = tk.Tk(className="FlauxStudio")
    app = StudioApp(root, open_file, home)
    root.mainloop()
    return app


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
