"""Blocks mode in Flaux Studio, the desktop app: a block editor drawn with Tk
(no web page). The blocks and their code are in blockmodel.py; code becomes
blocks with blocks.py, the same as the web version.

  - menus on the left (Say & Ask, Variables, Control, ...): drag a block out
  - drag a block to move it, with the blocks under it; it snaps under
    another block, into a block's mouth (repeat, if, ...), or into a space
    for a value; drop it on the menus to throw it away
  - click a box to type in it, or pick from it; click a variable to pick
    another or make a new one
  - right-click a block: duplicate, delete, add "else" to an if, items to a list
"""

import tkinter as tk
import tkinter.font as tkfont
from tkinter import simpledialog, ttk

from . import blockmodel as M

ROW = 30          # the height of a row of words
VAL_H = 24        # the height of a value block with nothing inside it
INDENT = 20       # how far in a mouth's blocks are
BAR = 12          # the bar under a block with a mouth
SNAP = 34         # how close a block must be to snap into place


def lighter(hex_colour, k):
    r, g, b = (int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))
    r, g, b = (int(c + (255 - c) * k) for c in (r, g, b))
    return f"#{r:02x}{g:02x}{b:02x}"


def darker(hex_colour, k):
    r, g, b = (int(hex_colour[i:i + 2], 16) for i in (1, 3, 5))
    return f"#{int(r * (1 - k)):02x}{int(g * (1 - k)):02x}{int(b * (1 - k)):02x}"


class Layout:
    """Where everything goes on one block (sizes in pixels)."""

    def __init__(self, editor, b):
        self.b = b
        cat, self.shape, rows = M.spec(b)
        self.rows = []     # [("row", y, h, parts)] parts: [(x, w, kind, name, child, extra)]; ("mouth", y, h, name, stack)
        f, bold = editor.field_font, editor.bold
        if self.shape == "value":
            x, h, parts = 6, VAL_H, []
            for part in rows[0]:
                x, h = self.part(editor, part, x, h, parts)
            self.w, self.h = max(x + 4, 28), h
            self.rows.append(("row", 0, h, parts))
            return
        y, w = 0, 120
        for row in rows:
            if isinstance(row, tuple) and row[0] == "s":
                stack = b.inputs.get(row[1])
                sw, sh = editor.stack_size(stack)
                h = max(sh, 22)
                self.rows.append(("mouth", y, h, row[1], stack))
                w = max(w, INDENT + sw + 10)
                y += h
                continue
            x, h, parts = 10, ROW, []
            for part in row:
                x, h = self.part(editor, part, x, h, parts)
            self.rows.append(("row", y, h, parts))
            w = max(w, x + 8)
            y += h
        if any(r[0] == "mouth" for r in self.rows):
            y += BAR
        self.w, self.h = w, y
        del f, bold

    def part(self, editor, part, x, h, parts):
        b = self.b
        if isinstance(part, str):
            w = editor.bold.measure(part)
            parts.append((x, w, "words", part, None, None))
            return x + w + 6, h
        kind = part[0]
        if kind == "v":
            child = b.inputs.get(part[1])
            if child:
                lay = editor.layout(child)
                parts.append((x, lay.w, "value", part[1], child, lay))
                return x + lay.w + 6, max(h, lay.h + 8)
            parts.append((x, 30, "slot", part[1], None, None))
            return x + 36, max(h, VAL_H + 6)
        if kind == "var":
            text = b.fields.get("VAR", "") or "?"
            w = editor.field_font.measure(text) + 26
            parts.append((x, w, "var", "VAR", None, text))
            return x + w + 6, h
        if kind == "f":
            text = b.fields.get(part[1], "")
            w = max(editor.field_font.measure(text) + 14, 26)
            parts.append((x, w, "field", part[1], None, text))
            return x + w + 6, h
        if kind == "d":
            value = b.fields.get(part[1], part[2][0][1] if part[2] else "")
            shown = next((s for s, v in part[2] if v == value), value)
            w = editor.field_font.measure(shown) + 26
            parts.append((x, w, "drop", part[1], None, (shown, part[2])))
            return x + w + 6, h
        return x, h


class BlockEditor(ttk.Frame):
    def __init__(self, master, on_change, theme):
        super().__init__(master)
        self.on_change = on_change
        self.t = theme
        self.tops = []                 # the blocks on the board, at the top of their stacks
        self.problem = None            # the block with a problem (a red outline)
        self.drag = None
        self.press = None
        self.edit_box = None
        self.targets = []              # [(x1, y1, x2, y2, kind, block, name, extra)], last drawn last
        self.palette_targets = []
        self.zoom = 10
        self.make_fonts()
        self.category = "Say & Ask"

        self.cats = tk.Listbox(self, width=13, activestyle="none", exportselection=False, bd=0,
                               highlightthickness=0, font=self.bold)
        for name in M.CATEGORIES:
            self.cats.insert("end", "  " + name)
        self.cats.selection_set(0)
        self.cats.bind("<<ListboxSelect>>", lambda e: self.pick_category())
        self.cats.pack(side="left", fill="y")
        self.palette = tk.Canvas(self, width=250, highlightthickness=0, bd=0)
        self.palette.pack(side="left", fill="y")
        right = ttk.Frame(self)
        right.pack(side="left", fill="both", expand=True)
        self.preview = tk.Text(right, height=7, wrap="none", bd=0, highlightthickness=0, padx=10, pady=6,
                               state="disabled")
        self.preview.pack(side="bottom", fill="x")
        ttk.Label(right, text="  Your blocks as Flaux code:", style="Muted.TLabel").pack(side="bottom", anchor="w")
        self.board = tk.Canvas(right, highlightthickness=0, bd=0, scrollregion=(0, 0, 2000, 2000))
        ys = ttk.Scrollbar(right, orient="vertical", command=self.board.yview)
        xs = ttk.Scrollbar(right, orient="horizontal", command=self.board.xview)
        self.board.configure(yscrollcommand=ys.set, xscrollcommand=xs.set)
        xs.pack(side="bottom", fill="x")
        ys.pack(side="right", fill="y")
        self.board.pack(fill="both", expand=True)

        for canvas in (self.board, self.palette):
            canvas.bind("<ButtonPress-1>", self.on_press)
            canvas.bind("<B1-Motion>", self.on_motion)
            canvas.bind("<ButtonRelease-1>", self.on_release)
        self.board.bind("<ButtonPress-3>", self.on_menu)
        self.board.bind("<ButtonPress-2>", self.on_menu)       # (a Mac's right button)
        self.board.bind("<MouseWheel>", lambda e: self.board.yview_scroll(-1 if e.delta > 0 else 1, "units"))
        self.board.bind("<Button-4>", lambda e: self.board.yview_scroll(-1, "units"))
        self.board.bind("<Button-5>", lambda e: self.board.yview_scroll(1, "units"))
        self.set_theme(theme)

    # ----------------------------------------------------------- setting up

    def make_fonts(self):
        families = set(tkfont.families(self))
        ui = next((f for f in ("Segoe UI", "SF Pro Text", "Ubuntu", "DejaVu Sans") if f in families), "TkDefaultFont")
        self.bold = tkfont.Font(self, family=ui, size=self.zoom, weight="bold")
        self.field_font = tkfont.Font(self, family=ui, size=self.zoom)
        mono = next((f for f in ("Cascadia Mono", "Consolas", "DejaVu Sans Mono") if f in families), "TkFixedFont")
        self.code_font = tkfont.Font(self, family=mono, size=self.zoom)

    def set_theme(self, theme):
        self.t = theme
        self.board.configure(background=theme["ed_bg"])
        self.palette.configure(background=theme["panel"])
        self.cats.configure(background=theme["bg"], foreground=theme["text"], selectbackground=theme["select"],
                            selectforeground=theme["text"])
        self.preview.configure(background=theme["panel"], foreground=theme["text"], font=self.code_font)
        self.redraw()

    def pick_category(self):
        sel = self.cats.curselection()
        if sel:
            self.category = self.cats.get(sel[0]).strip()
            self.draw_palette()

    # --------------------------------------------------------- the program

    def load(self, workspace):
        self.tops = M.from_workspace(workspace)
        self.problem = None
        self.redraw()
        self.board.yview_moveto(0)
        self.board.xview_moveto(0)

    def code(self):
        return M.generate(self.tops)

    def changed(self):
        self.redraw()
        self.on_change()

    def show_problem(self, block):
        self.problem = block
        self.redraw()
        if block:
            box = self.box_of(block)
            if box:
                self.board.see = None
                region = self.board.bbox("all")
                if region:
                    h = max(1, region[3])
                    self.board.yview_moveto(max(0, box[1] - 60) / h)

    def variables(self):
        names = set()
        for top in self.tops:
            for b in top.walk():
                if b.fields.get("VAR"):
                    names.add(b.fields["VAR"])
                for p in b.extra.get("params", []):
                    names.add(p["name"] if isinstance(p, dict) else p)
        return sorted(names)

    def functions(self):
        out = []
        for top in self.tops:
            if top.type in ("procedures_defnoreturn", "procedures_defreturn"):
                out.append((top.fields.get("NAME", ""), [p["name"] for p in top.extra.get("params", [])],
                            top.type == "procedures_defreturn"))
        return out

    # -------------------------------------------------------------- sizes

    def layout(self, b):
        return Layout(self, b)

    def stack_size(self, b):
        w = h = 0
        while b:
            lay = self.layout(b)
            w, h = max(w, lay.w), h + lay.h
            b = b.next
        return w, h

    # ------------------------------------------------------------ drawing

    def redraw(self):
        c = self.board
        c.delete("all")
        self.targets = []
        right = bottom = 600
        for top in self.tops:
            if top is (self.drag or {}).get("block"):
                continue
            w, h = self.draw_stack(c, top, top.x, top.y, self.targets)
            right, bottom = max(right, top.x + w + 300), max(bottom, top.y + h + 300)
        if self.drag:      # the block being dragged, on top of everything (and where it would go)
            top = self.drag["block"]
            spot = self.drag.get("spot")
            if spot:
                x, y = spot[0], spot[1]
                c.create_rectangle(x, y - 2, x + 120, y + 3, fill=self.t["text"], outline="")
            self.draw_stack(c, top, top.x, top.y, [])
        c.configure(scrollregion=(0, 0, right, bottom))
        self.draw_palette()
        self.update_preview()

    def update_preview(self):
        code, _, _ = self.code()
        self.preview.configure(state="normal")
        self.preview.delete("1.0", "end")
        self.preview.insert("1.0", code)
        self.preview.configure(state="disabled")

    def draw_stack(self, c, b, x, y, targets):
        w = h = 0
        while b:
            lay = self.draw_block(c, b, x, y + h, targets)
            w, h = max(w, lay.w), h + lay.h
            b = b.next
        return w, h

    def draw_block(self, c, b, x, y, targets):
        lay = self.layout(b)
        col = M.colour(b)
        edge = "#e53935" if b is self.problem else darker(col, 0.25)
        width = 3 if b is self.problem else 1
        if lay.shape == "value":
            c.create_rectangle(x, y, x + lay.w, y + lay.h, fill=col, outline=edge, width=width)
            targets.append((x, y, x + lay.w, y + lay.h, "block", b, None, None))
            self.draw_parts(c, b, x, y, lay.rows[0][2], lay.rows[0][3], targets)
            return lay
        # a statement block: its rows, and mouths with stacks inside
        targets.append((x, y, x + lay.w, y + lay.h, "block", b, None, None))
        for row in lay.rows:
            if row[0] == "row":
                _, ry, rh, parts = row
                c.create_rectangle(x, y + ry, x + lay.w, y + ry + rh, fill=col, outline="")
            else:
                _, ry, rh, name, stack = row
                c.create_rectangle(x, y + ry, x + INDENT, y + ry + rh, fill=col, outline="")
        if lay.h > lay.rows[-1][1] + lay.rows[-1][2]:   # the bar at the bottom
            c.create_rectangle(x, y + lay.h - BAR, x + max(lay.w, 80), y + lay.h, fill=col, outline="")
        c.create_rectangle(x, y, x + lay.w, y + lay.h, outline=edge, width=width)
        for row in lay.rows:
            if row[0] == "row":
                _, ry, rh, parts = row
                self.draw_parts(c, b, x, y + ry, rh, parts, targets)
            else:
                _, ry, rh, name, stack = row
                targets.append((x + INDENT, y + ry, x + INDENT + 120, y + ry + 12, "mouth", b, name, None))
                if stack:
                    self.draw_stack(c, stack, x + INDENT, y + ry, targets)
                else:
                    c.create_rectangle(x + INDENT, y + ry + 2, x + INDENT + 60, y + ry + rh - 2,
                                       fill=self.t["ed_bg"], outline=lighter(col, 0.4), dash=(3, 3))
        targets.append((x, y + lay.h - 8, x + lay.w, y + lay.h + 8, "after", b, None, None))
        return lay

    def draw_parts(self, c, b, x, y, h, parts, targets):
        mid = y + h / 2
        col = M.colour(b)
        for px, pw, kind, name, child, extra in parts:
            if kind == "words":
                c.create_text(x + px, mid, text=name, anchor="w", fill="white", font=self.bold)
            elif kind == "value":
                self.draw_block(c, child, x + px, mid - extra.h / 2, targets)
            elif kind == "slot":
                c.create_rectangle(x + px, mid - VAL_H / 2, x + px + 30, mid + VAL_H / 2,
                                   fill=darker(col, 0.3), outline=lighter(col, 0.3))
                targets.append((x + px, mid - VAL_H / 2, x + px + 30, mid + VAL_H / 2, "slot", b, name, None))
            elif kind in ("field", "var", "drop"):
                shown = extra if kind != "drop" else extra[0]
                fill = "white" if kind == "field" else lighter(col, 0.75)
                c.create_rectangle(x + px, mid - 11, x + px + pw, mid + 11, fill=fill, outline=darker(col, 0.2))
                c.create_text(x + px + 7, mid, text=shown, anchor="w", fill="#1d2033", font=self.field_font)
                if kind != "field":
                    c.create_text(x + px + pw - 10, mid, text="▾", fill="#1d2033", font=self.field_font)
                targets.append((x + px, mid - 11, x + px + pw, mid + 11, kind, b, name, extra))

    def draw_palette(self):
        p = self.palette
        p.delete("all")
        self.palette_targets = []
        blocks = M.palette(self.variables() or ["score"], self.functions()).get(self.category, [])
        y = 12
        if self.category == "Variables":
            p.create_text(12, y + 8, text="Make a variable: click here", anchor="w", fill=self.t["text"],
                          font=self.bold)
            self.palette_targets.append((0, y, 280, y + 20, "newvar", None, None, None))
            y += 34
        for b in blocks:
            spot = []
            w, h = self.draw_stack(p, b, 12, y, spot)
            self.palette_targets.append((12, y, 12 + w, y + h, "template", b, None, None))
            y += h + 12
        p.configure(scrollregion=(0, 0, 290, y))

    def box_of(self, block):
        for x1, y1, x2, y2, kind, b, _, _ in self.targets:
            if kind == "block" and b is block:
                return x1, y1, x2, y2
        return None

    # ---------------------------------------------------------- the mouse

    def at(self, targets, x, y, kinds):
        for t in reversed(targets):
            if t[4] in kinds and t[0] <= x <= t[2] and t[1] <= y <= t[3]:
                return t
        return None

    def board_xy(self, event):
        bx = event.x_root - self.board.winfo_rootx()
        by = event.y_root - self.board.winfo_rooty()
        return self.board.canvasx(bx), self.board.canvasy(by)

    def on_press(self, event):
        self.close_edit()
        self.press = (event.widget, event.x_root, event.y_root)
        if event.widget is self.palette:
            x, y = self.palette.canvasx(event.x), self.palette.canvasy(event.y)
            hit = self.at(self.palette_targets, x, y, ("template", "newvar"))
            if hit and hit[4] == "newvar":
                self.new_variable()
                self.press = None
            elif hit:
                self.press = (event.widget, event.x_root, event.y_root, hit, x - hit[0], y - hit[1])
            return
        x, y = self.board_xy(event)
        hit = self.at(self.targets, x, y, ("field", "var", "drop", "block"))
        self.press = (event.widget, event.x_root, event.y_root, hit, x, y)

    def on_motion(self, event):
        if not self.press or len(self.press) < 4 or not self.press[3]:
            return
        widget, x0, y0, hit = self.press[:4]
        if not self.drag:
            if abs(event.x_root - x0) + abs(event.y_root - y0) < 5:
                return
            if widget is self.palette:
                b = hit[5].copy()
                bx, by = self.board_xy(event)
                b.x, b.y = bx - self.press[4], by - self.press[5]
                self.tops.append(b)
                self.drag = {"block": b, "dx": self.press[4], "dy": self.press[5], "new": True}
            else:
                b = hit[5]
                if hit[4] != "block":       # (a field: drag the block it's on)
                    pass
                bx, by = self.press[4], self.press[5]
                box = self.box_of(b)
                self.detach(b)
                b.x, b.y = box[0], box[1]
                self.tops.append(b)
                self.drag = {"block": b, "dx": bx - box[0], "dy": by - box[1]}
        b = self.drag["block"]
        x, y = self.board_xy(event)
        b.x, b.y = x - self.drag["dx"], y - self.drag["dy"]
        self.drag["spot"] = self.drop_spot(b)
        self.redraw()

    def on_release(self, event):
        press, self.press = self.press, None
        if self.drag:
            b = self.drag.pop("block")
            spot = self.drop_spot(b)
            self.drag = None
            over_palette = event.x_root < self.palette.winfo_rootx() + self.palette.winfo_width()
            self.tops.remove(b)
            if not over_palette:       # (dropped on the menus: thrown away)
                if spot:
                    self.attach(b, spot)
                else:
                    self.tops.append(b)
            self.changed()
            return
        if press and len(press) >= 4 and press[3] and press[0] is self.board:
            hit = press[3]
            if hit[4] == "field":
                self.edit_field(hit)
            elif hit[4] == "drop":
                self.pick(hit, event)
            elif hit[4] == "var":
                self.pick_variable(hit, event)

    # ---------------------------------------------------- moving blocks

    def parent_of(self, target):
        """Where `target` hangs: ("top", None, None), ("next", block, None) or ("input", block, name)."""
        if target in self.tops:
            return "top", None, None
        for top in self.tops:
            for b in top.walk():
                if b.next is target:
                    return "next", b, None
                for name, child in b.inputs.items():
                    if child is target:
                        return "input", b, name
        return None, None, None

    def detach(self, b):
        kind, owner, name = self.parent_of(b)
        if kind == "top":
            self.tops.remove(b)
        elif kind == "next":
            owner.next = None
        elif kind == "input":
            owner.inputs[name] = None

    def drop_spot(self, b):
        """Where the dragged block would go: (x, y, kind, block, name), or None."""
        shape = M.spec(b)[1]
        best, best_d = None, SNAP
        if shape == "value":
            for x1, y1, x2, y2, kind, owner, name, _ in self.targets:
                if kind == "slot":
                    d = abs(b.x - x1) + abs(b.y + VAL_H / 2 - (y1 + y2) / 2)
                    if d < best_d:
                        best, best_d = (x1, y2 + 2, "slot", owner, name), d
            return best
        if shape == "top":
            return None
        for x1, y1, x2, y2, kind, owner, name, _ in self.targets:
            if kind == "after" and M.spec(owner)[1] in ("statement", "end", "top") and not M.is_value(owner):
                if M.spec(owner)[1] == "top":
                    continue
                py = (y1 + y2) / 2
                d = abs(b.x - x1) + abs(b.y - py)
                if d < best_d:
                    best, best_d = (x1, py, "after", owner, None), d
            elif kind == "mouth":
                d = abs(b.x - x1) + abs(b.y - y1)
                if d < best_d:
                    best, best_d = (x1, y1, "mouth", owner, name), d
        for top in self.tops:        # on top of a stack: it goes above it
            if top is b or M.is_value(top) or M.spec(top)[1] == "top":
                continue
            _, h = self.stack_size(b)
            d = abs(b.x - top.x) + abs(b.y + h - top.y)
            if d < best_d:
                best, best_d = (top.x, top.y, "before", top, None), d
        return best

    def attach(self, b, spot):
        _, _, kind, owner, name = spot
        tail = b
        while tail.next:
            tail = tail.next
        if kind == "slot":
            owner.inputs[name] = b
        elif kind == "after":
            tail.next, owner.next = owner.next, b
        elif kind == "mouth":
            tail.next, owner.inputs[name] = owner.inputs.get(name), b
        elif kind == "before":
            i = self.tops.index(owner)
            b.x, b.y = owner.x, owner.y
            tail.next = owner
            self.tops[i] = b

    # ------------------------------------------------------ typing, picking

    def close_edit(self):
        if self.edit_box:
            box, commit = self.edit_box
            self.edit_box = None
            commit()
            box.destroy()

    def edit_field(self, hit):
        x1, y1, x2, y2, _, b, name, text = hit
        entry = tk.Entry(self.board, font=self.field_font, bd=1, relief="solid")
        entry.insert(0, b.fields.get(name, ""))
        entry.select_range(0, "end")
        window = self.board.create_window(x1, y1, window=entry, anchor="nw", width=max(80, x2 - x1 + 40), height=y2 - y1)

        def commit():
            value = entry.get()
            if b.type == "flx_number" and not value.strip():
                value = "0"
            if value != b.fields.get(name, ""):
                b.fields[name] = value
                self.after_idle(self.changed)
            self.board.delete(window)

        self.edit_box = (entry, commit)
        entry.focus_set()
        entry.bind("<Return>", lambda e: (self.close_edit(), "break")[1])
        entry.bind("<Escape>", lambda e: (self.board.delete(window), entry.destroy(), setattr(self, "edit_box", None)))

    def pick(self, hit, event):
        b, name, (shown, options) = hit[5], hit[6], hit[7]
        menu = tk.Menu(self, tearoff=0)
        for label, value in options:
            menu.add_command(label=label, command=lambda v=value: (b.fields.__setitem__(name, v), self.changed()))
        menu.tk_popup(event.x_root, event.y_root)

    def pick_variable(self, hit, event):
        b = hit[5]
        menu = tk.Menu(self, tearoff=0)
        for v in self.variables():
            menu.add_command(label=v, command=lambda v=v: (b.fields.__setitem__("VAR", v), self.changed()))
        menu.add_separator()
        menu.add_command(label="Make a variable...", command=lambda: self.new_variable(b))
        menu.tk_popup(event.x_root, event.y_root)

    def new_variable(self, b=None):
        name = simpledialog.askstring("Make a variable", "Name of the new variable:", parent=self)
        if not name or not name.strip():
            return
        name = M.ident(name)
        if b is None:     # put a `set` block for it on the board, so it's there
            b = M.Block("variables_set", {"VAR": name}, {"VALUE": M.number(0)})
            b.x, b.y = self.board.canvasx(30), self.board.canvasy(30)
            self.tops.append(b)
        else:
            b.fields["VAR"] = name
        self.changed()

    def on_menu(self, event):
        x, y = self.board_xy(event)
        hit = self.at(self.targets, x, y, ("block",))
        if not hit:
            return
        b = hit[5]
        menu = tk.Menu(self, tearoff=0)
        menu.add_command(label="Duplicate", command=lambda: self.duplicate(b))
        if b.type == "controls_if":
            menu.add_command(label="Add else if", command=lambda: self.grow(b, "elseIfCount", 1))
            if int(b.extra.get("elseIfCount", 0)):
                menu.add_command(label="Remove the last else if", command=lambda: self.grow(b, "elseIfCount", -1))
            if b.extra.get("hasElse"):
                menu.add_command(label="Remove else", command=lambda: self.set_extra(b, "hasElse", False))
            else:
                menu.add_command(label="Add else", command=lambda: self.set_extra(b, "hasElse", True))
        if b.type == "lists_create_with":
            menu.add_command(label="Add an item", command=lambda: self.grow(b, "itemCount", 1))
            if int(b.extra.get("itemCount", 0)):
                menu.add_command(label="Remove the last item", command=lambda: self.grow(b, "itemCount", -1))
        menu.add_separator()
        menu.add_command(label="Delete", command=lambda: self.delete(b))
        menu.tk_popup(event.x_root, event.y_root)

    def duplicate(self, b):
        copy = b.copy()
        copy.next = None
        box = self.box_of(b)
        copy.x, copy.y = (box[0] + 30, box[3] + 10) if box else (40, 40)
        self.tops.append(copy)
        self.changed()

    def delete(self, b):
        kind, owner, name = self.parent_of(b)
        follow = b.next if not M.is_value(b) else None
        if kind == "top":
            i = self.tops.index(b)
            if follow:
                follow.x, follow.y = b.x, b.y
                self.tops[i] = follow
            else:
                self.tops.pop(i)
        elif kind == "next":
            owner.next = follow
        elif kind == "input":
            owner.inputs[name] = follow
        self.changed()

    def grow(self, b, key, step):
        n = max(0, int(b.extra.get(key, 0)) + step)
        if step < 0:
            gone = f"IF{n + 1}" if key == "elseIfCount" else f"ADD{n}"
            b.inputs.pop(gone, None)
            b.inputs.pop(f"DO{n + 1}", None) if key == "elseIfCount" else None
        b.extra[key] = n
        self.changed()

    def set_extra(self, b, key, value):
        b.extra[key] = value
        if not value:
            b.inputs.pop("ELSE", None)
        self.changed()

    def set_zoom(self, size):
        self.zoom = max(8, min(20, size - 3))
        for f in (self.bold, self.field_font, self.code_font):
            f.configure(size=self.zoom)
        self.redraw()
