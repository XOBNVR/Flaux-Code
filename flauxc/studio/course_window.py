"""The Course window in Flaux Studio (the desktop app): the course's units,
each with a lesson to read, a quiz, and a challenge to do in the editor.
The units and questions are in course.json (made by make_course.py); what
you've done is kept in course-progress.json, next to Flaux Studio's settings.
"""

import json
import os
import random
import re

import tkinter as tk
from tkinter import ttk

HERE = os.path.dirname(os.path.abspath(__file__))
QUIZ_LENGTH = 10


def load_course():
    with open(os.path.join(HERE, "course.json"), encoding="utf-8") as fh:
        return json.load(fh)


class Progress:
    """Which questions were answered right (the last answer counts) and which challenges are done."""

    def __init__(self, path):
        self.path = path
        self.answers, self.challenges = {}, []
        try:
            with open(path, encoding="utf-8") as fh:
                data = json.load(fh)
            self.answers = dict(data.get("answers", {}))
            self.challenges = list(data.get("challenges", []))
        except (OSError, ValueError, AttributeError):
            pass

    def save(self):
        try:
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            with open(self.path, "w", encoding="utf-8") as fh:
                json.dump({"answers": self.answers, "challenges": self.challenges}, fh)
        except OSError:
            pass

    def answered(self, qid, right):
        self.answers[qid] = 1 if right else 0
        self.save()

    def challenge_done(self, unit):
        if unit not in self.challenges:
            self.challenges.append(unit)
            self.save()

    def right_in(self, questions):
        return sum(1 for q in questions if self.answers.get(q["id"]) == 1)


def pick_questions(questions, progress, count=QUIZ_LENGTH, rng=random):
    """New questions first, then ones answered wrong, then the rest, each group shuffled."""
    new = [q for q in questions if q["id"] not in progress.answers]
    wrong = [q for q in questions if progress.answers.get(q["id"]) == 0]
    right = [q for q in questions if progress.answers.get(q["id"]) == 1]
    picked = []
    for group in (new, wrong, right):
        group = group[:]
        rng.shuffle(group)
        picked += group[:count - len(picked)]
    rng.shuffle(picked)
    return picked


class CourseWindow:
    def __init__(self, app):
        from .app import settings_path
        self.app = app
        self.course = load_course()
        self.units = self.course["units"]
        self.by_unit = {u["id"]: [] for u in self.units}
        for q in self.course["questions"]:
            self.by_unit[q["unit"]].append(q)
        self.progress = Progress(os.path.join(os.path.dirname(settings_path()), "course-progress.json"))
        self.unit = self.units[0]
        self.quiz, self.quiz_at, self.quiz_right, self.chosen = [], 0, 0, None

        top = self.top = tk.Toplevel(app.root)
        top.title("Flaux Course")
        top.geometry("1080x720")
        top.minsize(820, 520)
        try:
            top.transient(app.root)
        except tk.TclError:
            pass
        self.build()
        self.apply_theme()
        self.fill_units()
        self.tree.selection_set(self.units[0]["id"])
        self.show_unit(self.units[0]["id"])
        top.protocol("WM_DELETE_WINDOW", self.close)

    # ------------------------------------------------------------- layout

    def build(self):
        app, top = self.app, self.top
        panes = ttk.PanedWindow(top, orient="horizontal")
        panes.pack(fill="both", expand=True)

        left = ttk.Frame(panes, padding=10)
        panes.add(left, weight=1)
        ttk.Label(left, text="The Flaux course", font=app.title_font).pack(anchor="w")
        self.total_label = ttk.Label(left, style="Muted.TLabel", wraplength=260, justify="left")
        self.total_label.pack(anchor="w", pady=(2, 8))
        ttk.Button(left, text="★  Quiz me on everything", command=self.mixed_quiz).pack(side="bottom", fill="x",
                                                                                     pady=(8, 0))
        self.tree = ttk.Treeview(left, show="tree", selectmode="browse")
        self.tree.column("#0", width=260)
        self.tree.pack(fill="both", expand=True)
        self.tree.bind("<<TreeviewSelect>>", lambda e: self.tree.selection() and self.show_unit(self.tree.selection()[0]))

        right = ttk.Frame(panes, padding=(6, 10, 10, 10))
        panes.add(right, weight=3)
        self.unit_title = ttk.Label(right, font=app.title_font)
        self.unit_title.pack(anchor="w")
        self.unit_goal = ttk.Label(right, style="Muted.TLabel")
        self.unit_goal.pack(anchor="w", pady=(0, 6))
        tabs = self.tabs = ttk.Notebook(right)
        tabs.pack(fill="both", expand=True)

        # Learn
        learn = ttk.Frame(tabs, padding=4)
        tabs.add(learn, text="  Learn  ")
        buttons = ttk.Frame(learn, padding=(0, 8, 0, 0))
        buttons.pack(side="bottom", fill="x")
        ttk.Button(buttons, text="Quiz me  ▶", style="Run.TButton", command=lambda: self.start_quiz()).pack(side="right")
        self.lesson_button = ttk.Button(buttons, text="Open the lesson program", command=self.open_lesson)
        self.lesson_button.pack(side="left")
        self.page = tk.Text(learn, wrap="word", bd=0, highlightthickness=0, padx=16, pady=12, font=app.ui_font,
                            spacing1=2, spacing3=4, cursor="arrow")
        scroll = ttk.Scrollbar(learn, orient="vertical", command=self.page.yview)
        self.page.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.page.pack(fill="both", expand=True)

        # Quiz
        quiz = self.quiz_tab = ttk.Frame(tabs, padding=14)
        tabs.add(quiz, text="  Quiz  ")
        # (the result and the Next button stay at the bottom, so long answers never push them away)
        bottom = ttk.Frame(quiz)
        bottom.pack(side="bottom", fill="x")
        self.quiz_head = ttk.Label(quiz, style="Muted.TLabel")
        self.quiz_head.pack(anchor="w")
        self.question = ttk.Label(quiz, font=app.title_font, wraplength=640, justify="left")
        self.question.pack(anchor="w", pady=(6, 8))
        self.q_code = tk.Text(quiz, height=4, wrap="none", font=app.code_font, bd=0, highlightthickness=1,
                              padx=10, pady=8)
        self.answer_frame = ttk.Frame(quiz)
        self.answer_frame.pack(fill="x", pady=(4, 6))
        self.answer_frame.columnconfigure(0, weight=1, uniform="a")
        self.answer_frame.columnconfigure(1, weight=1, uniform="a")
        self.answer_buttons = []
        for i in range(4):
            b = tk.Button(self.answer_frame, anchor="nw", justify="left", relief="flat", bd=0, padx=14, pady=8,
                          highlightthickness=1, cursor="hand2", command=lambda i=i: self.choose(i))
            self.answer_buttons.append(b)
        self.feedback = ttk.Label(bottom, font=app.ui_bold, wraplength=640, justify="left")
        self.feedback.pack(anchor="w", pady=(6, 0))
        self.why = ttk.Label(bottom, wraplength=640, justify="left")
        self.why.pack(anchor="w", pady=(2, 8))
        quiz_buttons = ttk.Frame(bottom)
        quiz_buttons.pack(anchor="w", fill="x")
        self.next_button = ttk.Button(quiz_buttons, text="Next  ▶", style="Run.TButton", command=self.next_question)
        self.again_button = ttk.Button(quiz_buttons, text="10 more questions", style="Run.TButton",
                                       command=lambda: self.start_quiz(self.quiz_unit))
        self.back_button = ttk.Button(quiz_buttons, text="Back to the lesson", command=lambda: self.tabs.select(0))

        # Challenge
        ch = ttk.Frame(tabs, padding=14)
        tabs.add(ch, text="  Challenge  ")
        self.ch_state = ttk.Label(ch, font=app.ui_bold)
        self.ch_state.pack(anchor="w")
        self.ch_task = ttk.Label(ch, font=app.title_font, wraplength=640, justify="left")
        self.ch_task.pack(anchor="w", pady=(6, 10))
        self.ch_how = ttk.Label(ch, wraplength=640, justify="left", style="Muted.TLabel")
        self.ch_how.pack(anchor="w")
        ch_buttons = ttk.Frame(ch, padding=(0, 12, 0, 8))
        ch_buttons.pack(anchor="w", fill="x")
        ttk.Button(ch_buttons, text="Start the challenge in the editor", style="Run.TButton",
                   command=self.start_challenge).pack(side="left")
        self.solution_button = ttk.Button(ch_buttons, text="Show me a way to do it", command=self.show_solution)
        self.solution_button.pack(side="left", padx=8)
        self.solution = tk.Text(ch, height=8, wrap="none", font=app.code_font, bd=0, highlightthickness=1,
                                padx=10, pady=8)

    def apply_theme(self):
        t = self.app.t
        self.top.configure(background=t["bg"])
        self.page.configure(background=t["panel"], foreground=t["text"], selectbackground=t["select"])
        self.page.tag_configure("h1", font=self.app.title_font, spacing1=14, spacing3=4)
        self.page.tag_configure("goal", foreground=t["muted"])
        self.page.tag_configure("code", font=self.app.code_font, foreground=t["builtin"])
        self.page.tag_configure("block", font=self.app.code_font, background=t["ed_bg"], foreground=t["ed_text"],
                                lmargin1=24, lmargin2=24, rmargin=24, spacing1=0, spacing3=0)
        for w in (self.q_code, self.solution):
            w.configure(background=t["ed_bg"], foreground=t["ed_text"], highlightbackground=t["line"],
                        selectbackground=t["select"])
        for w in (self.page, self.q_code, self.solution):
            for kind in ("kw", "builtin", "str", "interp", "num", "com"):
                w.tag_configure(kind, foreground=t[kind])
            w.tag_raise("sel")
        self.paint_answers()

    # -------------------------------------------------------------- units

    def unit_label(self, u):
        qs = self.by_unit[u["id"]]
        right = self.progress.right_in(qs)
        done = u["id"] in self.progress.challenges
        mark = "✓ " if right >= 0.8 * len(qs) and done else "   "
        return f"{mark}{self.units.index(u) + 1}. {u['title']}   {right}/{len(qs)}"

    def fill_units(self):
        for u in self.units:
            if self.tree.exists(u["id"]):
                self.tree.item(u["id"], text=self.unit_label(u))
            else:
                self.tree.insert("", "end", iid=u["id"], text=self.unit_label(u))
        right = self.progress.right_in(self.course["questions"])
        self.total_label.configure(text=f"{len(self.units)} units, {len(self.course['questions'])} questions.\n"
                                        f"You've got {right} right and done {len(self.progress.challenges)} "
                                        f"challenge{'s' if len(self.progress.challenges) != 1 else ''}.")

    def show_unit(self, uid):
        self.unit = next(u for u in self.units if u["id"] == uid)
        u = self.unit
        self.unit_title.configure(text=f"{self.units.index(u) + 1}. {u['title']}")
        self.unit_goal.configure(text=u["goal"])
        self.fill_page()
        self.show_quiz_start()
        self.fill_challenge()

    # -------------------------------------------------------------- learn

    def fill_page(self):
        u, p = self.unit, self.page
        p.configure(state="normal")
        p.delete("1.0", "end")
        for heading, text, code in u["pages"]:
            p.insert("end", heading + "\n", "h1")
            for i, part in enumerate(re.split(r"`([^`]*)`", text)):
                p.insert("end", part, "code" if i % 2 else ())
            p.insert("end", "\n")
            start = int(p.index("end-1c").split(".")[0])
            p.insert("end", "\n" + code + "\n\n", "block")
            self.highlight_lines(p, start + 1, code)
        p.configure(state="disabled")
        self.lesson_button.configure(state="normal" if u["lesson"] else "disabled")

    def highlight_lines(self, widget, first_line, code):
        from .app import tokens
        for n, line in enumerate(code.split("\n")):
            for kind, a, b in tokens(line):
                widget.tag_add(kind, f"{first_line + n}.{a}", f"{first_line + n}.{b}")

    def open_lesson(self):
        if self.unit["lesson"]:
            from .server import EXAMPLES
            self.app.open_path(os.path.join(EXAMPLES, "lessons", self.unit["lesson"]))

    # --------------------------------------------------------------- quiz

    def show_quiz_start(self):
        qs = self.by_unit[self.unit["id"]]
        right = self.progress.right_in(qs)
        self.quiz = []
        self.quiz_head.configure(text="")
        self.question.configure(text=f"{len(qs)} questions about {self.unit['title'].lower()}. You've got {right} right "
                                     f"so far.\nA quiz is {QUIZ_LENGTH} of them: new ones first, then the ones you got "
                                     f"wrong.")
        self.q_code.pack_forget()
        for b in self.answer_buttons:
            b.grid_forget()
        self.feedback.configure(text="")
        self.why.configure(text="")
        self.next_button.pack_forget()
        self.back_button.pack_forget()
        self.again_button.configure(text="Start the quiz")
        self.quiz_unit = self.unit["id"]
        self.again_button.pack(side="left")

    def mixed_quiz(self):
        self.start_quiz(None)

    def start_quiz(self, unit="this"):
        if unit == "this":
            unit = self.unit["id"]
        self.quiz_unit = unit
        pool = self.by_unit[unit] if unit else self.course["questions"]
        self.quiz = pick_questions(pool, self.progress)
        self.quiz_at, self.quiz_right = 0, 0
        self.tabs.select(1)
        self.show_question()

    def show_question(self):
        q = self.quiz[self.quiz_at]
        self.chosen = None
        title = next(u["title"] for u in self.units if u["id"] == q["unit"])
        self.quiz_head.configure(text=f"Question {self.quiz_at + 1} of {len(self.quiz)}   ·   {title}   ·   "
                                      f"{self.quiz_right} right so far")
        self.question.configure(text=q["q"].replace("`", ""))
        self.q_code.pack_forget()
        self.answer_frame.pack_forget()
        if q.get("code"):
            self.q_code.configure(state="normal", height=min(14, q["code"].count("\n") + 1))
            self.q_code.delete("1.0", "end")
            self.q_code.insert("1.0", q["code"])
            self.highlight_lines(self.q_code, 1, q["code"])
            self.q_code.configure(state="disabled")
            self.q_code.pack(anchor="w", fill="x", pady=(0, 8))
        self.answer_frame.pack(fill="x", pady=(4, 6))
        for i, b in enumerate(self.answer_buttons):
            b.grid_forget()
            if i < len(q["choices"]):
                b.configure(text=q["choices"][i], state="normal",
                            font=self.app.code_font if q.get("mono") else self.app.ui_font)
                b.grid(row=i // 2, column=i % 2, sticky="nsew", padx=3, pady=3)
        self.paint_answers()
        self.feedback.configure(text="")
        self.why.configure(text="")
        self.next_button.pack_forget()
        self.again_button.pack_forget()
        self.back_button.pack_forget()

    def paint_answers(self):
        t = self.app.t
        q = self.quiz[self.quiz_at] if self.quiz and self.quiz_at < len(self.quiz) else None
        for i, b in enumerate(self.answer_buttons):
            bg, fg, line = t["panel"], t["text"], t["line"]
            if q is not None and self.chosen is not None:
                if i == q["answer"]:
                    bg, fg, line = t["good"], "#ffffff", t["good"]
                elif i == self.chosen:
                    bg, fg, line = t["bad"], "#ffffff", t["bad"]
            b.configure(background=bg, foreground=fg, activebackground=t["select"], activeforeground=t["text"],
                        highlightbackground=line, disabledforeground=fg, wraplength=320)

    def choose(self, i):
        if self.chosen is not None or not self.quiz:
            return
        q = self.quiz[self.quiz_at]
        self.chosen = i
        right = i == q["answer"]
        self.progress.answered(q["id"], right)
        if right:
            self.quiz_right += 1
        title = next(u["title"] for u in self.units if u["id"] == q["unit"])
        self.quiz_head.configure(text=f"Question {self.quiz_at + 1} of {len(self.quiz)}   ·   {title}   ·   "
                                      f"{self.quiz_right} right so far")
        for b in self.answer_buttons:
            b.configure(state="disabled")
        self.paint_answers()
        t = self.app.t
        self.feedback.configure(text="✓  Right!" if right else "✗  Not quite. The right answer is lit up in green.",
                                foreground=t["good"] if right else t["bad"])
        self.why.configure(text=q["why"].replace("`", ""))
        last = self.quiz_at + 1 >= len(self.quiz)
        self.next_button.configure(text="See how you did  ▶" if last else "Next  ▶")
        self.next_button.pack(side="left")
        self.fill_units()

    def next_question(self):
        self.quiz_at += 1
        if self.quiz_at < len(self.quiz):
            self.show_question()
            return
        n = len(self.quiz)
        cheer = ("Perfect!" if self.quiz_right == n else "Great work!" if self.quiz_right >= 0.8 * n
                 else "Good going!" if self.quiz_right >= 0.5 * n else "Keep practising: the ones you missed come back.")
        self.quiz_head.configure(text="Quiz finished")
        self.question.configure(text=f"You got {self.quiz_right} of {n} right. {cheer}")
        self.q_code.pack_forget()
        for b in self.answer_buttons:
            b.grid_forget()
        self.feedback.configure(text="")
        self.why.configure(text="")
        self.next_button.pack_forget()
        self.again_button.configure(text="10 more questions")
        self.again_button.pack(side="left")
        if self.quiz_unit:
            self.back_button.pack(side="left", padx=8)

    # ---------------------------------------------------------- challenge

    def fill_challenge(self):
        u = self.unit
        ch = u["challenge"]
        done = u["id"] in self.progress.challenges
        t = self.app.t
        self.ch_state.configure(text="✓  Challenge done!" if done else "Challenge", foreground=t["good"] if done else t["muted"])
        self.ch_task.configure(text=ch["task"].replace("`", ""))
        how = ("Press Start: the challenge goes into the editor. Write your program, then press Run. When it says the "
               "right thing, the challenge is done.") if ch["expected"] is not None else \
              ("Press Start: the challenge goes into the editor. Write your program, then press Run. When it runs "
               "without a problem, the challenge is done.")
        self.ch_how.configure(text=how)
        self.solution.pack_forget()
        self.solution_button.configure(state="normal")

    def start_challenge(self):
        app = self.app
        if app.mode == "blocks":
            app.toggle_blocks()
        if not app.keep_changes():
            return
        ch = self.unit["challenge"]
        starter = ch["starter"] or ""
        header = "# Challenge: " + ch["task"].replace("`", "") + "\n"
        app.path, app.example, app.dirty = None, False, False
        app.set_code(header + "\n" + starter)
        app.update_title()
        app.challenge = self.unit["id"]
        app.root.lift()
        app.code.focus_set()
        app.code.mark_set("insert", "end-1c")
        app.status(f"Challenge: {self.unit['title']}. Press Run when your program is ready.")

    def challenge_finished(self, unit, ok):
        if ok:
            self.progress.challenge_done(unit)
        self.fill_units()
        if self.unit["id"] == unit:
            self.fill_challenge()

    def show_solution(self):
        code = self.unit["challenge"]["solution"].rstrip("\n")
        self.solution.configure(state="normal", height=min(14, code.count("\n") + 1))
        self.solution.delete("1.0", "end")
        self.solution.insert("1.0", code)
        self.highlight_lines(self.solution, 1, code)
        self.solution.configure(state="disabled")
        self.solution.pack(anchor="w", fill="x")

    def close(self):
        self.app.course_window = None
        self.top.destroy()


def check_challenge(challenge, said, exit_code):
    """Did the program do the challenge? `said` is everything it said."""
    if exit_code != 0:
        return False
    if challenge["expected"] is None:
        return True
    clean = "\n".join(line.rstrip() for line in said.replace("\r\n", "\n").strip("\n").split("\n"))
    return clean == challenge["expected"]
