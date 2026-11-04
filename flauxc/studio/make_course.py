"""Makes course.json: the Flaux course (course_units.py) and its quiz questions
(course_questions.py), for the Course window in both Flaux Studios.

    python3 -m flauxc.studio.make_course            check everything, then write course.json
    python3 -m flauxc.studio.make_course --quick    write it without running the programs

Every answer is checked with the real Flaux compiler first: programs whose
output is the answer are run, "which line is the mistake?" is checked against
the line Flaux points at, and so on (see the top of course_questions.py). If
one is wrong, nothing is written and the problem is shown.
"""

import concurrent.futures
import json
import os
import random
import subprocess
import sys
import threading

from ..compiler import compile_source
from ..errors import FlauxError
from .course_questions import make
from .course_units import UNITS

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, "course.json")
LESSONS = os.path.join(ROOT, "examples", "lessons")


def collect():
    """All the questions, in order, without repeats."""
    rng = random.Random(2026)
    found, seen = [], set()
    unit_ids = {u["id"] for u in UNITS}

    def add(unit, question, right, wrong, why, code=None, check=None, mono=False, given=None):
        assert unit in unit_ids, unit
        key = (unit, question, code, right)
        if key in seen:
            return
        seen.add(key)
        wrong = [w for w in dict.fromkeys(wrong) if w != right][:3]
        assert wrong, f"no wrong answers: {question} {code}"
        found.append({"unit": unit, "q": question, "code": code, "right": right, "wrong": wrong, "why": why,
                      "check": check, "mono": mono, "given": given})

    make(add, rng)
    return found


def build(questions):
    """course.json's contents: the units, and the questions with their answers shuffled."""
    rng = random.Random(7)
    counts, out = {}, []
    order = {u["id"]: i for i, u in enumerate(UNITS)}
    for q in sorted(questions, key=lambda q: order[q["unit"]]):
        counts[q["unit"]] = counts.get(q["unit"], 0) + 1
        answers = [q["right"]] + q["wrong"]
        rng.shuffle(answers)
        item = {"id": f"{q['unit']}-{counts[q['unit']]:03d}", "unit": q["unit"], "q": q["q"], "choices": answers,
                "answer": answers.index(q["right"]), "why": q["why"]}
        if q["code"]:
            item["code"] = q["code"]
        if q["mono"]:
            item["mono"] = True
        out.append(item)
    units = []
    for u in UNITS:
        unit = {k: u[k] for k in ("id", "title", "goal", "lesson", "pages", "challenge")}
        unit["questions"] = counts.get(u["id"], 0)
        units.append(unit)
    return {"version": 1, "units": units, "questions": out}


# ------------------------------------------------------------------ checking

def compiles(source):
    try:
        compile_source(source, "question.flx", LESSONS)
        return None
    except FlauxError as e:
        return e


_locks = {}
_locks_lock = threading.Lock()


def run(source, given=None):
    """What the program says (or raises FlauxError)."""
    from ..cli import cached_build
    c_code = compile_source(source, "question.flx", LESSONS)
    with _locks_lock:                       # (two threads never build the same program at once)
        lock = _locks.setdefault(c_code, threading.Lock())
    with lock:
        exe = cached_build(c_code)
    r = subprocess.run([exe], input=(given or "") + "\n", capture_output=True, text=True, timeout=30,
                       encoding="utf-8", errors="replace")
    return r.stdout.replace("\r\n", "\n")


STATEMENTS = ("set ", "say", "add ", "subtract ", "wait", "ask", "if ", "repeat ", "function ", "window(", "line(",
              "dot(", "write(", "image(", "beep(", "clear(", "rect(", "circle(")


def whole_line(answer):
    """An answer that's part of a program, made into a whole one to check: an `if` or `repeat` gets
    something inside and its `end`, and a value gets a `say` in front."""
    if answer.startswith(("if ", "repeat ")):
        return answer + "\n    say 1\nend"
    if not answer.startswith(STATEMENTS):
        return "say " + answer
    return answer


def problem(q):
    """What's wrong with this question's answer (None if it's right)."""
    check, code = q["check"], q["code"]
    if check in ("run", "ends"):
        try:
            said = run(code, q["given"]).rstrip("\n")
        except FlauxError as e:
            return f"the program has a mistake: {e.message}"
        if check == "run" and said != q["right"]:
            return f"it says {said!r}, not {q['right']!r}"
        if check == "ends" and not said.endswith(q["right"]):
            return f"it says {said!r}, which doesn't end with {q['right']!r}"
        if q["right"] == said and any(w == said for w in q["wrong"]):
            return "a wrong answer is right too"
    elif check == "valid":
        prefix = (code + "\n") if code else ""
        e = compiles(prefix + whole_line(q["right"]))
        if e:
            return f"the right answer has a mistake: {e.message}"
        for w in q["wrong"]:
            if not compiles(prefix + whole_line(w)):
                return f"the wrong answer {w!r} works"
    elif check == "line":
        e = compiles(code)
        if not e:
            return "the program has no mistake"
        if str(e.line) != q["right"]:
            return f"Flaux points at line {e.line} ({e.message}), not line {q['right']}"
    elif check == "fails":
        if not compiles(code):
            return "the program has no mistake"
    elif check == "works":
        e = compiles(code)
        if e:
            return f"the program has a mistake: {e.message}"
    return None


def unit_problems():
    found = []
    for u in UNITS:
        for heading, _text, code in u["pages"]:
            e = compiles(code)
            if e and not (u["id"] == "mistakes" and heading == "Flaux helps"):   # (that one's typo is on purpose)
                found.append(f"{u['id']}, page {heading!r}: line {e.line}: {e.message}")
        if u["lesson"] and not os.path.isfile(os.path.join(LESSONS, u["lesson"])):
            found.append(f"{u['id']}: no lesson {u['lesson']}")
        ch = u["challenge"]
        if u["id"] != "mistakes":
            e = compiles(ch["starter"] or "say 1")
            if e:
                found.append(f"{u['id']}: the challenge's starting code: {e.message}")
        elif not compiles(ch["starter"]):
            found.append("mistakes: the challenge's starting code should have mistakes")
        if ch["expected"] is not None:
            try:
                said = run(ch["solution"]).rstrip("\n")
            except FlauxError as e:
                said = f"(mistake: {e.message})"
            if said != ch["expected"]:
                found.append(f"{u['id']}: the challenge's solution says {said!r}, not {ch['expected']!r}")
        else:
            e = compiles(ch["solution"])
            if e:
                found.append(f"{u['id']}: the challenge's solution: {e.message}")
    return found


def check_all(questions, workers=8, progress=True):
    """Every problem found, as text (an empty list if all is well)."""
    found = unit_problems()
    with concurrent.futures.ThreadPoolExecutor(workers) as pool:
        results = list(pool.map(problem, questions))
    for q, p in zip(questions, results):
        if p:
            found.append(f"[{q['unit']}] {q['q']}\n{q['code'] or ''}\n  -> {p}")
    return found


def main():
    questions = collect()
    if "--quick" not in sys.argv:
        found = check_all(questions)
        if found:
            print("\n\n".join(found))
            sys.exit(f"\n{len(found)} problem(s): course.json was not written.")
    data = build(questions)
    with open(OUT, "w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, separators=(",", ":"))
        fh.write("\n")
    per_unit = ", ".join(f"{u['id']} {u['questions']}" for u in data["units"])
    print(f"Wrote {OUT}: {len(data['units'])} units, {len(data['questions'])} questions ({per_unit}).")


if __name__ == "__main__":
    main()
