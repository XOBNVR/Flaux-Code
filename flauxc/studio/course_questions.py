"""The course's quiz questions. make_course.py runs make(add, rng) and checks
every answer with the real Flaux compiler before writing course.json.

add(unit, question, right, wrong, why, code=None, check=None, mono=False, given=None)
  right   the right answer (text)
  wrong   wrong answers (a list; up to three are used)
  why     a short explanation, shown after answering
  code    a program shown with the question
  mono    the answers are code or output (shown in the code font)
  given   what the person types, for programs that ask (one answer per line)
  check   how make_course.py makes sure the answer really is right:
    "run"    run `code`: it must say exactly `right`
    "ends"   run `code` (typing `given`): what it says must end with `right`
    "valid"  `right` is code that works and every wrong answer is code with a mistake
             (each one put after `code`, if there is any)
    "line"   `code` has a mistake, on line number `right`
    "fails"  `code` has a mistake (Flaux finds it before running)
    "works"  `code` has no mistakes
"""

NAMES = ["Sam", "Bo", "Mia", "Leo", "Ava", "Max", "Zoe", "Kai", "Ivy", "Ben", "Ana", "Tom", "Lily", "Omar",
         "Nia", "Raj"]
WORDS = ["apple", "house", "tiger", "robot", "pizza", "water", "green", "music", "cloud", "dragon", "rocket",
         "planet", "flaux", "ninja", "panda", "zebra", "lemon", "comet", "pixel", "candy"]
VARS = ["score", "coins", "lives", "points", "apples", "gems", "stars", "level", "speed", "health", "money",
        "steps", "cats", "eggs", "keys", "goals"]
LABELS = ["Score:", "Lives:", "Level:", "Coins:", "Gems:", "Time:", "Speed:", "Stars:"]


def fmt(v):
    """A value the way Flaux's `say` shows it."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        if v == int(v) and abs(v) < 1e15:
            return str(int(v))
        return repr(v)
    if isinstance(v, str):
        return v
    if isinstance(v, list):
        return "[" + ", ".join(f'"{x}"' if isinstance(x, str) else fmt(x) for x in v) + "]"
    raise TypeError(v)


def lines(*vals):
    return "\n".join(fmt(v) for v in vals)


def fdiv(a, b):        # Flaux's // : rounds down
    return a // b


def fmod(a, b):        # Flaux's mod : the remainder, same sign as b (like Python)
    return a % b


def fround(x):         # round half away from zero
    import math
    return int(math.floor(abs(x) + 0.5)) * (1 if x >= 0 else -1)


def make(add, rng):
    say(add, rng)
    variables(add, rng)
    maths(add, rng)
    text(add, rng)
    ask(add, rng)
    choices(add, rng)
    checks(add, rng)
    loops(add, rng)
    lists(add, rng)
    functions(add, rng)
    builtins(add, rng)
    randomness(add, rng)
    graphics(add, rng)
    games(add, rng)
    pictures(add, rng)
    three_d(add, rng)
    shaders(add, rng)
    mistakes(add, rng)


# ----------------------------------------------------------------------- say

def say(add, rng):
    u = "say"
    add(u, "Which instruction shows something in the Output?", "say", ["show", "print", "output"],
        "`say` shows words and numbers in the Output.")
    add(u, "What goes around text in Flaux?", 'Double quotes: "like this"',
        ["Single quotes: 'like this'", "Square brackets: [like this]", "Nothing at all"],
        "Text always goes in double quotes.")
    add(u, "What does `#` start?", "A comment: Flaux skips the rest of the line",
        ["A number", "A list", "A new line in the Output"], "Comments are notes for people. Flaux skips them.")
    add(u, "Which line shows Hello?", 'say "Hello"', ["say Hello", 'say "Hello', "say 'Hello'"],
        "Text needs double quotes at both ends.", check="valid", mono=True)
    add(u, "What does `say()` with nothing in the brackets do?", "Shows an empty line",
        ["Nothing at all", "It's a mistake", "Shows ()"], "`say()` finishes the line without adding anything.")
    add(u, "How is `say.same` different from `say`?", "The next thing said goes on the same line",
        ["It says things twice", "It says the same thing again", "It waits a second first"],
        "`say` finishes its line, `say.same` doesn't.")
    add(u, "What does `say` show between things separated by commas?", "One space",
        ["A comma", "Nothing", "A new line"], '`say "Score:", 10` shows `Score: 10`.')
    add(u, "Do numbers need quotes after `say`?", "No: `say 42` works",
        ["Yes, always", "Only big numbers", "Only decimals"], "Only text needs quotes.")
    add(u, "Which of these is a comment?", "# this is a note", ['say "# note"', "// this is a note", "-- note"],
        "Comments start with `#`.", mono=True)
    add(u, "Is `say(\"Hi\")` (with brackets) allowed?", "Yes, it's the same as say \"Hi\"",
        ["No, it's a mistake", "Only in games", "Only for numbers"], "Brackets are fine after `say`.")
    for w in rng.sample(WORDS, 8):
        add(u, "What does this program show?", w, [f'"{w}"', f"say {w}", w.upper()],
            "`say` shows the text without the quotes.", code=f'say "{w}"', check="run", mono=True)
    for _ in range(10):
        a, b = rng.randint(2, 40), rng.randint(2, 40)
        add(u, "What does this program show?", fmt(a + b), [f"{a}{b}", f"{a} + {b}", fmt(a + b + 1)],
            "Without quotes, Flaux works out the sum before showing it.", code=f"say {a} + {b}", check="run", mono=True)
    for _ in range(6):
        a, b = rng.randint(2, 20), rng.randint(2, 20)
        add(u, "What does this program show?", f"{a} + {b}", [fmt(a + b), f"{a}{b}", f'"{a} + {b}"'],
            "Inside quotes it's just text, so it's shown as it is.", code=f'say "{a} + {b}"', check="run", mono=True)
    for label in LABELS:
        n = rng.randint(0, 99)
        add(u, "What does this program show?", f"{label} {n}", [f"{label}{n}", f"{label}, {n}", f'"{label}" {n}'],
            "Things separated by commas are shown with one space between them.",
            code=f'say "{label}", {n}', check="run", mono=True)
    for _ in range(5):
        a, b, c = rng.randint(1, 9), rng.randint(1, 9), rng.randint(1, 9)
        add(u, "What does this program show?", f"{a} {b} {c}", [f"{a}{b}{c}", f"{a}, {b}, {c}", fmt(a + b + c)],
            "Commas between things put a space between them.", code=f"say {a}, {b}, {c}", check="run", mono=True)
    for sym in ["*", "#", "o", "hi ", "ab", "-", "+", "x"]:
        n = rng.randint(2, 5)
        add(u, "What does this program show?", sym * n + "!",
            [sym * (n - 1) + "!", sym * (n + 1) + "!", "\n".join([sym.strip()] * n) + "\n!"],
            "`say.same` keeps adding to the same line, then `say` finishes it.",
            code=f'repeat {n} times\n    say.same "{sym}"\nend\nsay "!"', check="run", mono=True)
    for a, b in [("Hello", "World"), ("Hello ", "World"), ("Go", "!"), ("3", "2"), ("ab", "cd"), ("Hi, ", "Sam")]:
        add(u, "What does this program show?", a + b, [f"{a}\n{b}", f"{a.strip()} {b}" if a.strip() + " " + b != a + b
                                                         else f"{a}  {b}", b],
            "`say.same` doesn't finish the line, so the next thing goes right after it, with no space added.",
            code=f'say.same "{a}"\nsay "{b}"', check="run", mono=True)
    for a, b in [("Hello", "World"), ("one", "two"), ("Ready", "Go"), ("up", "down"), ("cat", "dog")]:
        add(u, "What does this program show?", f"{a}\n{b}", [a + b, f"{a} {b}", f"{b}\n{a}"],
            "Each `say` finishes its own line.", code=f'say "{a}"\nsay "{b}"', check="run", mono=True)
    for w, v in [("A", "B"), ("yes", "no"), ("on", "off"), ("hi", "bye"), ("1", "2")]:
        add(u, "What does this program show?", w, [f"{w}\n{v}", v, f'{w}   # say "{v}"'],
            "Everything after `#` is a comment, and Flaux skips it.",
            code=f'say "{w}"   # say "{v}"', check="run", mono=True)
    for w in rng.sample(WORDS, 4):
        add(u, "What does this program show?", w, [f"# {w}", f"{w}\n{w}", "Nothing"],
            "A line that starts with `#` is a comment, so only the second line runs.",
            code=f'# say "nothing"\nsay "{w}"', check="run", mono=True)


# ----------------------------------------------------------------- variables

def variables(add, rng):
    u = "variables"
    add(u, "What is a variable?", "A name that holds a value",
        ["A kind of loop", "A mistake in the program", "A colour"], "Like a labelled box with a value inside.")
    add(u, "Which word makes a variable?", "set", ["make", "var", "let"], "`set score to 0` makes `score`.")
    add(u, "Which line makes a variable called `score` that holds 0?", "set score to 0",
        ["score = 0", "set score = 0", "set 0 to score"], "It's `set` name `to` value.", check="valid", mono=True)
    add(u, "Are `Score` and `score` the same variable?", "No: capital letters make a different name",
        ["Yes, capitals don't matter", "Only in functions", "Only for numbers"],
        "Capitals matter in names.")
    add(u, "Is this program right?", "No: `Score` (with a capital) was never made",
        ["Yes, it shows 5", "Yes, it shows Score", "No: names can't hold numbers"],
        "Capitals matter: `Score` and `score` are different names.", code="set score to 5\nsay Score", check="fails")
    add(u, "What can a name NOT start with?", "A number", ["A letter", "A capital letter", "The letter s"],
        "`player2` is fine, `2player` isn't.")
    add(u, "Which of these can be in a name?", "Letters, numbers and _",
        ["Spaces", "- (a minus)", "Quotes"], "Like `player_2` or `highScore`.")
    add(u, "`set total to 0` and then `add 0.5 to total`. What happens?", "Fine: total becomes 0.5",
        ["A mistake: total holds a whole number", "total stays 0", "total becomes 0.0.5"],
        "A whole number can become a decimal. That's the one change allowed.")
    for base in ["player", "level", "team", "box", "car", "room", "card", "ball"]:
        add(u, "Which name is allowed?", f"set {base}_2 to 1",
            [f"set 2{base} to 1", f"set {base} 2 to 1", f'set "{base}" to 1'],
            "Names use letters, numbers and `_`, and can't start with a number.", check="valid", mono=True)
    for v in rng.sample(VARS, 3):
        add(u, "What happens when this runs?", f"Flaux says it's a mistake: {v} holds a number, not text",
            [f"{v} becomes \"ten\"", "It shows ten", "It shows 10"],
            "A variable keeps the same kind of value all the way through.",
            code=f'set {v} to 10\nset {v} to "ten"', check="fails")
    for _ in range(16):
        v, a, b = rng.choice(VARS), rng.randint(1, 50), rng.randint(1, 20)
        add(u, "What does this program show?", fmt(a + b), [fmt(a), fmt(b), fmt(a * b)],
            f"`add {b} to {v}` makes {v} {b} bigger.", code=f"set {v} to {a}\nadd {b} to {v}\nsay {v}",
            check="run", mono=True)
    for _ in range(14):
        v, a = rng.choice(VARS), rng.randint(10, 60)
        b = rng.randint(1, 9)
        add(u, "What does this program show?", fmt(a - b), [fmt(a + b), fmt(a), fmt(b - a)],
            f"`subtract {b} from {v}` makes {v} {b} smaller.", code=f"set {v} to {a}\nsubtract {b} from {v}\nsay {v}",
            check="run", mono=True)
    for _ in range(8):
        v, a, b = rng.choice(VARS), rng.randint(1, 30), rng.randint(31, 60)
        add(u, "What does this program show?", fmt(b), [fmt(a), fmt(a + b), f"{a}\n{b}"],
            "The second `set` replaces the old value.", code=f"set {v} to {a}\nset {v} to {b}\nsay {v}",
            check="run", mono=True)
    for _ in range(8):
        a, b, c = rng.randint(1, 20), rng.randint(1, 9), rng.randint(1, 9)
        add(u, "What does this program show?", fmt(a + b), [fmt(a + b + c), fmt(a + c), fmt(a)],
            "y got its value when it was set. Changing x afterwards doesn't change y.",
            code=f"set x to {a}\nset y to x + {b}\nadd {c} to x\nsay y", check="run", mono=True)
    for _ in range(6):
        a, k = rng.randint(2, 12), rng.randint(2, 5)
        add(u, "What does this program show?", fmt(a * k), [fmt(a + k), fmt(a), fmt(k)],
            "Use a variable's name to use its value.", code=f"set width to {a}\nset area to width * {k}\nsay area",
            check="run", mono=True)
    for _ in range(4):
        a = rng.randint(2, 30)
        add(u, "What does this program show?", fmt(a * 2), [fmt(a), fmt(a + 1), "0"],
            "x is added to itself, so it doubles.", code=f"set x to {a}\nadd x to x\nsay x", check="run", mono=True)
    for _ in range(4):
        a, b = rng.randint(1, 9), rng.randint(10, 19)
        add(u, "What does this program show?", f"{b} {a}", [f"{a} {b}", f"{b} {b}", f"{a} {a}"],
            "`keep` remembers a's old value, so the two values swap places.",
            code=f"set a to {a}\nset b to {b}\nset keep to a\nset a to b\nset b to keep\nsay a, b", check="run", mono=True)
    for n in rng.sample(NAMES, 4):
        add(u, "What does this program show?", f"Hi {n}", ["Hi name", f'Hi "{n}"', "name"],
            "A variable's name is replaced by its value.", code=f'set name to "{n}"\nsay "Hi", name', check="run",
            mono=True)


# --------------------------------------------------------------------- maths

def maths(add, rng):
    u = "maths"
    add(u, "Which sign multiplies?", "*", ["x", "×", "#"], "`6 * 7` is 42.", mono=True)
    add(u, "Which sign divides?", "/", ["\\", "÷", ":"], "`10 / 4` is 2.5.", mono=True)
    add(u, "What does `mod` give?", "The remainder after dividing", ["The biggest number", "A random number",
                                                                    "The number rounded"], "`7 mod 3` is 1.")
    add(u, "What does `//` do?", "Divides and rounds down to a whole number",
        ["Starts a comment", "Divides twice", "Multiplies"], "`7 // 2` is 3.")
    add(u, "What does `^` do?", "A power: 2 ^ 3 is 2 * 2 * 2", ["Multiplies by 2", "Draws an arrow",
                                                              "Adds one"], "`2 ^ 3` is 8.")
    add(u, "Which is worked out first in `2 + 3 * 4`?", "3 * 4", ["2 + 3", "Both at once", "Whatever's on the left"],
        "Multiplying and dividing come before adding and subtracting.", mono=False)
    add(u, "What happens if a program divides by zero?", "It stops with a clear message",
        ["It shows 0", "It shows a huge number", "The computer crashes"],
        "Flaux stops the program and says which line divided by zero.")
    add(u, "How do you make the adding happen first in `2 + 3 * 4`?", "(2 + 3) * 4",
        ["2 + (3 * 4)", "2 + 3 * 4 first", "[2 + 3] * 4"], "Brackets go first.", mono=True)
    add(u, "How do you write one million so it's easy to read?", "1_000_000", ["1,000,000", "1 000 000", "1.000.000"],
        "`_` between digits is allowed and ignored. (With commas, `say 1,000,000` would say three numbers: 1 0 0.)",
        mono=True)
    add(u, "What does this program show?", "1 0 0", ["1000000", "1,000,000", "100"],
        "Commas separate the things `say` shows, so these are three numbers: 1, 000 and 000. Write 1_000_000.",
        code="say 1,000,000", check="run", mono=True)
    add(u, "Is a number even when `n mod 2` is...", "0", ["1", "2", "n"], "Even numbers have no remainder when "
                                                                        "divided by 2.")
    for _ in range(16):
        a, b, c = rng.randint(1, 9), rng.randint(2, 9), rng.randint(2, 9)
        add(u, "What does this program show?", fmt(a + b * c), [fmt((a + b) * c), fmt(a + b + c), fmt(a * b + c)],
            f"`*` comes first: {b} * {c} is {b * c}, plus {a}.", code=f"say {a} + {b} * {c}", check="run", mono=True)
    for _ in range(10):
        a, b, c = rng.randint(1, 9), rng.randint(2, 9), rng.randint(2, 9)
        add(u, "What does this program show?", fmt((a + b) * c), [fmt(a + b * c), fmt(a + b + c), fmt(a * b * c)],
            f"Brackets first: {a} + {b} is {a + b}, times {c}.", code=f"say ({a} + {b}) * {c}", check="run", mono=True)
    for _ in range(6):
        a, b, c = rng.randint(20, 40), rng.randint(2, 9), rng.randint(2, 9)
        add(u, "What does this program show?", fmt(a - b - c), [fmt(a - (b - c)), fmt(a - b + c), fmt(a + b + c)],
            "Left to right: subtract one, then the other.", code=f"say {a} - {b} - {c}", check="run", mono=True)
    for _ in range(14):
        b = rng.randint(2, 7)
        a = b * rng.randint(1, 9) + rng.randint(1, b - 1)
        add(u, "What does this program show?", fmt(fdiv(a, b)), [fmt(a / b), fmt(fdiv(a, b) + 1), fmt(fmod(a, b))],
            f"`//` divides and rounds down: {a} / {b} is {fmt(a / b)}, down to {fdiv(a, b)}.",
            code=f"say {a} // {b}", check="run", mono=True)
    for _ in range(14):
        b = rng.randint(2, 9)
        a = b * rng.randint(1, 9) + rng.randint(0, b - 1)
        wrong = [fmt(fdiv(a, b)), fmt(b - fmod(a, b)) if b - fmod(a, b) != fmod(a, b) else fmt(b), fmt(a - b)]
        add(u, "What does this program show?", fmt(fmod(a, b)), wrong,
            f"{b} goes into {a} {fdiv(a, b)} times ({fdiv(a, b) * b}), with {fmod(a, b)} left over.",
            code=f"say {a} mod {b}", check="run", mono=True)
    for _ in range(10):
        b = rng.choice([2, 4, 5, 8, 10])
        a = b * rng.randint(1, 9) + rng.randint(1, b - 1)
        add(u, "What does this program show?", fmt(a / b), [fmt(fdiv(a, b)), fmt(fround(a / b) + 1), fmt(fmod(a, b))],
            "`/` always gives the exact answer, as a decimal.", code=f"say {a} / {b}", check="run", mono=True)
    for _ in range(5):
        b = rng.randint(2, 9)
        a = b * rng.randint(2, 9)
        add(u, "What does this program show?", fmt(a // b), [f"{a // b}.0", fmt(a // b + 1), fmt(a * b)],
            "When a decimal is a whole number, Flaux shows it without `.0`.", code=f"say {a} / {b}", check="run", mono=True)
    for _ in range(10):
        a, b = rng.randint(2, 5), rng.randint(2, 4)
        add(u, "What does this program show?", fmt(a ** b), [fmt(a * b), fmt(b ** a) if b ** a != a ** b else fmt(a + b),
                                                             fmt(a ** b + a)],
            f"{a} ^ {b} is {a} multiplied by itself {b} times.", code=f"say {a} ^ {b}", check="run", mono=True)
    for _ in range(8):
        a, b = rng.randint(3, 15), rng.randint(3, 15)
        add(u, "What does this program show?", fmt(a * b), [fmt(a + b), fmt(a * b + a), f"{a}{b}"],
            "`*` multiplies.", code=f"say {a} * {b}", check="run", mono=True)
    for n in rng.sample(range(10, 60), 6):
        add(u, "What does this program show?", fmt(n % 2), [fmt(1 - n % 2), fmt(n // 2), "2"],
            f"{n} is {'even' if n % 2 == 0 else 'odd'}, so the remainder after dividing by 2 is {n % 2}.",
            code=f"say {n} mod 2", check="run", mono=True)
    for _ in range(4):
        a = rng.randint(2, 9)
        b = rng.choice([0.5, 1.5, 2.5])
        add(u, "What does this program show?", fmt(a * b), [fmt(a * int(b)), fmt(a + b), fmt(a * b + 1)],
            "A whole number times a decimal gives a decimal.", code=f"say {a} * {fmt(b)}", check="run", mono=True)


# ---------------------------------------------------------------------- text

def text(add, rng):
    u = "text"
    add(u, "What does `{ }` inside text do?", "Puts a value into the text", ["Makes a list",
                                                                           "Starts a comment", "Nothing"],
        '`"Hi {name}"` puts name\'s value in.')
    add(u, "What number is the first letter of a text?", "1", ["0", "-1", "It has no number"],
        "Counting starts at 1, like on your fingers.")
    add(u, "What does `length of word` give?", "How many letters it has", ["The first letter", "The last letter",
                                                                          "The word backwards"],
        '`length of "hello"` is 5.')
    add(u, "How do you put a quote mark `\"` inside text?", '\\"', ['""', "'", "\\'"],
        'Put a backslash before it: `"Say \\"hi\\""`.', mono=True)
    add(u, "What does `\\n` inside text do?", "Starts a new line", ["Shows the letter n", "Ends the program",
                                                                  "Adds a space"], '`say "a\\nb"` shows a and b on two lines.')
    add(u, "What does `+` do with two texts?", "Joins them together", ["Adds their lengths", "Makes a mistake",
                                                                       "Puts a space between them"], '`"ab" + "cd"` is `abcd`.')
    add(u, "What does this program show?", 'Say "hi"', ['Say \\"hi\\"', "Say hi", 'Say ""hi""'],
        '`\\"` is a quote mark inside text.', code='say "Say \\"hi\\""', check="run", mono=True)
    add(u, "What does this program show?", "a\nb", ["a\\nb", "anb", "a b"], "`\\n` starts a new line.",
        code='say "a\\nb"', check="run", mono=True)
    for _ in range(12):
        n, a = rng.choice(NAMES), rng.randint(5, 15)
        add(u, "What does this program show?", f"{n} is {a}", ["{name} is {age}", "name is age", f"{n} is {{age}}"],
            "Each `{ }` is replaced by the value inside it.",
            code=f'set name to "{n}"\nset age to {a}\nsay "{{name}} is {{age}}"', check="run", mono=True)
    for _ in range(8):
        a, b = rng.randint(1, 20), rng.randint(1, 20)
        add(u, "What does this program show?", f"{a} + {b} = {a + b}",
            [f"{a} + {b} = {a} + {b}", "{a} + {b} = {a + b}", f"{a} + {b} = {a}{b}"],
            "Sums inside `{ }` are worked out.",
            code=f'set a to {a}\nset b to {b}\nsay "{{a}} + {{b}} = {{a + b}}"', check="run", mono=True)
    for _ in range(6):
        w, v = rng.sample(WORDS, 2)
        add(u, "What does this program show?", w + v, [f"{w} {v}", f"{w} + {v}", f'"{w}" + "{v}"'],
            "`+` joins text with nothing in between.", code=f'say "{w}" + "{v}"', check="run", mono=True)
    for _ in range(5):
        n = rng.randint(1, 500)
        add(u, "What does this program show?", f"Score: {n}", [f"Score:{n}", f"Score: + {n}", "Score: n"],
            "Numbers are turned into text when they're joined to text.", code=f'say "Score: " + {n}', check="run", mono=True)
    for _ in range(4):
        a, b = rng.randint(1, 9), rng.randint(1, 9)
        add(u, "What does this program show?", f"{a}{b}", [fmt(a + b), f"{a} {b}", f"{a} + {b}"],
            f'"{a}" is text, so `+` joins: the {b} is put after it.', code=f'say "{a}" + {b}', check="run", mono=True)
    for w in rng.sample(WORDS, 15):
        i = rng.randint(1, len(w))
        wrong = [w[i % len(w)], w[i - 2] if i >= 2 else w[-1], w[i - 1].upper()]
        add(u, "What does this program show?", w[i - 1], wrong,
            f"Letters are counted from 1: letter {i} of {w} is {w[i - 1]}.",
            code=f'set word to "{w}"\nsay word[{i}]', check="run", mono=True)
    for w in rng.sample(WORDS, 12):
        add(u, "What does this program show?", fmt(len(w)), [fmt(len(w) - 1), fmt(len(w) + 1), w],
            f"{w} has {len(w)} letters.", code=f'say length of "{w}"', check="run", mono=True)
    for w in rng.sample(WORDS, 4):
        p = w + " " + rng.choice(WORDS)
        add(u, "What does this program show?", fmt(len(p)), [fmt(len(p) - 1), fmt(len(p.replace(' ', '')) - 1), "2"],
            "The space counts as a letter too.", code=f'say length of "{p}"', check="run", mono=True)
    for w in rng.sample(WORDS, 6):
        add(u, "What does this program show?", w.upper(), [w, w.capitalize(), w.lower() + "!"],
            "`uppercase` turns every letter into a capital.", code=f'say uppercase("{w}")', check="run", mono=True)
    for w in rng.sample(WORDS, 4):
        W = w.upper()
        add(u, "What does this program show?", w, [W, w.capitalize(), w.swapcase().capitalize() + "?"],
            "`lowercase` turns every capital into a small letter.", code=f'say lowercase("{W}")', check="run", mono=True)
    for w in rng.sample(WORDS, 10):
        if rng.random() < 0.5:
            i = rng.randint(0, len(w) - 2)
            part, res = w[i:i + 2], True
        else:
            part = rng.choice(["xy", "qq", "zz", "ww", "aaa", "kk"])
            res = part in w
        add(u, "What does this program show?", fmt(res), [fmt(not res), part, "1"],
            f"`contains` says whether {part} is somewhere in {w}.", code=f'say contains("{w}", "{part}")',
            check="run", mono=True)
    for n in rng.sample(NAMES, 4):
        add(u, "What does this program show?", f"Hello, {n}!", ["Hello, {name}!", f"Hello, {n} !", "Hello, name!"],
            "`{name}` is replaced by name's value.", code=f'set name to "{n}"\nsay "Hello, {{name}}!"', check="run",
            mono=True)


# ----------------------------------------------------------------------- ask

def ask(add, rng):
    u = "ask"
    add(u, "What does `ask(...)` give back?", "The text the person typed", ["A number, always", "true or false",
                                                                           "Nothing"],
        "It's always text, even if they typed a number.")
    add(u, "When does `ask` carry on to the next line?", "When the person presses Enter",
        ["Straight away", "After one second", "When they type a letter"], "It waits for the answer.")
    add(u, "Someone types 7 for `ask`. What kind of value does the program get?", "Text: \"7\"",
        ["A whole number: 7", "A decimal: 7.0", "true"], "Use `number(...)` or `whole(...)` to turn it into a number.")
    add(u, "Which turns an answer into a whole number?", "whole(answer)", ["text(answer)", "length of answer",
                                                                           "round answer"],
        "`whole` turns text into a whole number.", mono=True)
    add(u, "Which checks that an answer is a number?", "is_number(answer)", ["number(answer)", "whole(answer)",
                                                                             "contains(answer, 0)"],
        "`is_number` gives true or false.", mono=True)
    add(u, "Where do you type answers in Flaux Studio?", "In the box under the Output", ["In the code", "In a pop-up window",
                                                                                        "In the title bar"],
        "Type there and press Enter (or Send).")
    add(u, "Which line asks for a name and keeps it?", 'set name to ask("Name?")',
        ['ask("Name?") to name', 'set name = ask("Name?")', 'ask name'], "`ask` gives back what was typed.",
        check="valid", mono=True)
    for n in rng.sample(NAMES, 8):
        add(u, f"Someone types {n}. What does the program show at the end?", f"Hi {n}!",
            ["Hi name!", "Hi {name}!", f"Hi {n}"], "`{name}` is replaced by what they typed.",
            code='set name to ask("What\'s your name?")\nsay "Hi {name}!"', check="ends", given=n, mono=True)
    for _ in range(8):
        k = rng.randint(2, 40)
        add(u, f"Someone types {k}. What does the program show at the end?", fmt(k * 2),
            [f"{k}{k}", fmt(k), f"{k} * 2"], "`number(...)` turned the text into a number, so it can be doubled.",
            code='set n to number(ask("A number?"))\nsay n * 2', check="ends", given=str(k), mono=True)
    for _ in range(6):
        a, b = rng.randint(1, 9), rng.randint(1, 9)
        add(u, f"Someone types {a}, then {b}. What does the program show at the end?", f"{a}{b}",
            [fmt(a + b), f"{a} + {b}", f"{a} {b}"],
            "Answers are text, so `+` joins them. To add them as numbers, use `number(...)` first.",
            code='set a to ask("First?")\nset b to ask("Second?")\nsay a + b', check="ends", given=f"{a}\n{b}", mono=True)
    for _ in range(6):
        k = rng.randint(6, 15)
        add(u, f"Someone types {k}. What does the program show at the end?", f"Next year you'll be {k + 1}",
            [f"Next year you'll be {k}1", f"Next year you'll be {k}", "Next year you'll be {age + 1}"],
            "`whole(...)` turns the answer into a whole number, so 1 can be added.",
            code='set age to whole(ask("How old are you?"))\nsay "Next year you\'ll be {age + 1}"',
            check="ends", given=str(k), mono=True)
    for typed, res in [("42", True), ("hello", False), ("3.5", True), ("4b", False), ("-7", True), ("seven", False)]:
        add(u, f"Someone types {typed}. What does the program show at the end?", fmt(res), [fmt(not res), typed, "42"],
            "`is_number` checks if the text is a number.",
            code='set answer to ask("Type something")\nsay is_number(answer)', check="ends", given=typed, mono=True)


# --------------------------------------------------------------------- if

OPS = [(">", "is more than", lambda a, b: a > b), ("<", "is less than", lambda a, b: a < b),
       (">=", "is at least", lambda a, b: a >= b), ("<=", "is at most", lambda a, b: a <= b),
       ("=", "is", lambda a, b: a == b), ("!=", "is not", lambda a, b: a != b)]


def choices(add, rng):
    u = "if"
    add(u, "What does every `if` finish with?", "end", ["stop", "done", "fi"], "Each `if` needs its own `end`.")
    add(u, "When do the lines inside an `if` run?", "Only when its check is true", ["Always", "Never",
                                                                                     "Only when the check is false"],
        "If the check is false, they're skipped.")
    add(u, "What does `else` do?", "Runs when the check is false", ["Runs every time", "Ends the program",
                                                                     "Repeats the if"], "Either the if part or the else part runs.")
    add(u, "How many parts of an `if ... else if ... else` can run?", "Just one",
        ["All of them", "Two", "None, ever"], "The first part whose check is true runs, and the rest are skipped.")
    add(u, "Which check means \"equal\"?", "=", ["==", "===", "equals"], "`=` (or the word `is`) checks if two "
                                                                         "things are equal.", mono=True)
    add(u, "Which check means \"not equal\"?", "!=", ["<>", "=/=", "not="], "`!=` or `is not`.", mono=True)
    add(u, "Is `then` allowed after the check?", "Yes, it's fine: if score > 100 then",
        ["No, it's a mistake", "Only after else", "Only in games"], "`then` is allowed but not needed.")
    add(u, "Why indent the lines inside an `if`?", "So you can see at a glance what's inside it",
        ["Flaux needs it to work", "It makes the program faster", "It's a comment"], "Indenting is for people.")
    add(u, "Which line checks if score is 10?", "if score = 10", ["if score to 10", "if set score to 10",
                                                                  "if score 10"],
        "`=` checks for equal.", code="set score to 10", check="valid", mono=True)
    add(u, "Which line has the mistake?", "3", ["1", "2", "4"], "The `if` has no `end`, so Flaux points at it.",
        code='set x to 5\nsay "start"\nif x > 3\n    say "big"', check="line")
    for _ in range(30):
        op, words, f = rng.choice(OPS)
        a = rng.randint(1, 12)
        b = a + rng.choice([-2, -1, 0, 0, 1, 2])
        res = f(a, b)
        sign = rng.choice([op, words])
        add(u, "What does this program show?", "yes" if res else "no", ["no" if res else "yes", "yes\nno", "Nothing"],
            f"{a} {words} {b} is {fmt(res)}, so the {'if' if res else 'else'} part runs.",
            code=f'set x to {a}\nif x {sign} {b}\n    say "yes"\nelse\n    say "no"\nend', check="run", mono=True)
    for s in rng.sample([20, 49, 50, 51, 75, 99, 100, 101, 150, 0, 60, 110, 30, 88, 200], 15):
        res = "Amazing!" if s > 100 else "Good job" if s > 50 else "Keep trying"
        add(u, "What does this program show?", res, [x for x in ["Amazing!", "Good job", "Keep trying",
                                                                 "Good job\nKeep trying"] if x != res],
            f"The first check that's true wins. {s} > 100 is {fmt(s > 100)}, {s} > 50 is {fmt(s > 50)}.",
            code=f'set score to {s}\nif score > 100\n    say "Amazing!"\nelse if score > 50\n    say "Good job"\n'
                 f'else\n    say "Keep trying"\nend', check="run", mono=True)
    for _ in range(10):
        a, b = rng.randint(1, 20), rng.randint(5, 15)
        res = f"big\ndone" if a > b else "done"
        add(u, "What does this program show?", res, ["done" if a > b else "big\ndone", "big", "Nothing"],
            "The line after `end` always runs. The one inside runs only if the check is true.",
            code=f'set x to {a}\nif x > {b}\n    say "big"\nend\nsay "done"', check="run", mono=True)
    for _ in range(8):
        age = rng.randint(5, 25)
        res = "teen" if 13 <= age <= 19 else "not a teen"
        add(u, "What does this program show?", res, ["not a teen" if res == "teen" else "teen", "Nothing",
                                                     "teen\nnot a teen"],
            "The inner `if` is only checked when the outer one is true.",
            code=f'set age to {age}\nset result to "not a teen"\nif age >= 13\n    if age <= 19\n'
                 f'        set result to "teen"\n    end\nend\nsay result', check="run", mono=True)
    for _ in range(6):
        t = rng.randint(-5, 35)
        res = "hot" if t > 25 else "cold" if t < 10 else "nice"
        add(u, "What does this program show?", res, [x for x in ["hot", "cold", "nice", "Nothing"] if x != res],
            "Only the first part whose check is true runs.",
            code=f'set temp to {t}\nif temp > 25\n    say "hot"\nelse if temp < 10\n    say "cold"\nelse\n'
                 f'    say "nice"\nend', check="run", mono=True)


# --------------------------------------------------------------- and, or, not

def checks(add, rng):
    u = "checks"
    add(u, "When is `a and b` true?", "When both are true", ["When one is true", "When both are false", "Always"],
        "`and` needs both.")
    add(u, "When is `a or b` true?", "When at least one is true", ["Only when both are true", "Never",
                                                                   "Only when both are false"], "`or` needs one.")
    add(u, "What does `not` do?", "Turns true into false and false into true", ["Stops the program",
                                                                               "Makes a number negative", "Nothing"],
        "`not true` is false.")
    add(u, "What kind of value does `3 > 2` give?", "true or false", ["A number", "Text", "A list"],
        "Checks give true or false.")
    add(u, "Which line checks if the name is Sam or Bo?", 'if name = "Sam" or name = "Bo"',
        ['if name = "Sam" or "Bo"', 'if name = "Sam" and "Bo"', 'if name = "Sam", "Bo"'],
        "Each side of `or` must be a whole check.", code='set name to "Bo"', check="valid", mono=True)
    for a, b in [(True, True), (True, False), (False, True), (False, False)]:
        add(u, "What does this program show?", fmt(a and b), [fmt(not (a and b)), "true false", "1"],
            "`and` is true only when both sides are.", code=f"say {fmt(a)} and {fmt(b)}", check="run", mono=True)
        add(u, "What does this program show?", fmt(a or b), [fmt(not (a or b)), "true false", "1"],
            "`or` is true when at least one side is.", code=f"say {fmt(a)} or {fmt(b)}", check="run", mono=True)
    for v in [True, False]:
        add(u, "What does this program show?", fmt(not v), [fmt(v), "not", "0"], "`not` flips true and false.",
            code=f"say not {fmt(v)}", check="run", mono=True)
    for _ in range(14):
        a, b, c, d = (rng.randint(1, 9) for _ in range(4))
        res = a > b and c < d
        add(u, "What does this program show?", fmt(res), [fmt(not res), f"{fmt(a > b)} {fmt(c < d)}", "Nothing"],
            f"{a} > {b} is {fmt(a > b)} and {c} < {d} is {fmt(c < d)}. `and` needs both.",
            code=f"say {a} > {b} and {c} < {d}", check="run", mono=True)
    for _ in range(12):
        a, b, c, d = (rng.randint(1, 9) for _ in range(4))
        res = (a == b) or (c > d)
        add(u, "What does this program show?", fmt(res), [fmt(not res), f"{fmt(a == b)} {fmt(c > d)}", "Nothing"],
            f"{a} = {b} is {fmt(a == b)} and {c} > {d} is {fmt(c > d)}. `or` needs one.",
            code=f"say {a} = {b} or {c} > {d}", check="run", mono=True)
    for _ in range(6):
        a, b = rng.randint(1, 9), rng.randint(1, 9)
        res = not (a > b)
        add(u, "What does this program show?", fmt(res), [fmt(not res), "not", fmt(a - b)],
            f"{a} > {b} is {fmt(a > b)}, and `not` flips it.", code=f"say not ({a} > {b})", check="run", mono=True)
    for _ in range(8):
        a, b = rng.randint(1, 9), rng.randint(1, 9)
        op, words, f = rng.choice(OPS[:4])
        res = f(a, b)
        add(u, "What does this program show?", fmt(res), [fmt(not res), "yes", fmt(a)],
            f"`{words}` means `{op}`.", code=f"say {a} {words} {b}", check="run", mono=True)
    for _ in range(8):
        h, age = rng.randint(100, 140), rng.randint(5, 12)
        res = "ride" if h >= 120 and age >= 8 else "sorry"
        add(u, "What does this program show?", res, ["sorry" if res == "ride" else "ride", "ride\nsorry", "Nothing"],
            f"Both must be true: {h} >= 120 is {fmt(h >= 120)} and {age} >= 8 is {fmt(age >= 8)}.",
            code=f'set height to {h}\nset age to {age}\nif height >= 120 and age >= 8\n    say "ride"\nelse\n'
                 f'    say "sorry"\nend', check="run", mono=True)
    for _ in range(4):
        done = rng.choice([True, False])
        add(u, "What does this program show?", "still going" if not done else "finished",
            ["finished" if not done else "still going", "Nothing", "not finished"],
            "`not finished` is true when finished is false.",
            code=f'set finished to {fmt(done)}\nif not finished\n    say "still going"\nelse\n    say "finished"\nend',
            check="run", mono=True)


# --------------------------------------------------------------------- loops

def loops(add, rng):
    u = "repeat"
    add(u, "Which loop never stops on its own?", "repeat forever", ["repeat 10 times", "repeat with i from 1 to 5",
                                                                    "repeat until done"], "Games use it for the game loop.",
        mono=True)
    add(u, "What does `stop` do inside a loop?", "Leaves the loop straight away", ["Ends the whole program",
                                                                                   "Pauses for a second",
                                                                                   "Starts the loop again"],
        "The program carries on after the loop's `end`.")
    add(u, "What does `skip` do inside a loop?", "Jumps to the next time round", ["Leaves the loop",
                                                                                 "Skips the next line only",
                                                                                 "Ends the program"],
        "The rest of the loop is skipped this time only.")
    add(u, "What's the difference between `repeat while` and `repeat until`?",
        "while keeps going while the check is true; until keeps going until it's true",
        ["There's no difference", "until is faster", "while only works with numbers"], "They're opposites.")
    add(u, "Which line counts from 1 to 10?", "repeat with i from 1 to 10", ["repeat i from 1 to 10",
                                                                             "repeat with i 1 to 10",
                                                                             "repeat 1 to 10 with i"],
        "`repeat with` name `from` start `to` finish.", check="valid", mono=True)
    add(u, "How do you count down from 10 to 0 in steps of 2?", "repeat with i from 10 to 0 by -2",
        ["repeat with i from 10 to 0 by 2", "repeat with i from 0 to 10 by -2", "repeat with i from 10 down 0"],
        "A minus step counts down.", mono=True)
    add(u, "Which line repeats 3 times?", "repeat 3 times", ["repeat 3", "repeat times 3", "3 times repeat"],
        "A counted repeat ends with `times`.", check="valid", mono=True)
    for w in ["hi", "go!", "beep", "yay", "Flaux"]:
        n = rng.randint(2, 4)
        add(u, "What does this program show?", "\n".join([w] * n), ["\n".join([w] * (n + 1)), w, w * n],
            f"The loop runs {n} times, and each `say` gets its own line.",
            code=f'repeat {n} times\n    say "{w}"\nend', check="run", mono=True)
    for _ in range(10):
        a = rng.randint(0, 5)
        b = a + rng.randint(2, 4)
        add(u, "What does this program show?", lines(*range(a, b + 1)),
            [lines(*range(a, b)), lines(*range(a + 1, b + 1)), lines(*range(b, a - 1, -1))],
            f"It counts from {a} up to {b}, including both.",
            code=f"repeat with i from {a} to {b}\n    say i\nend", check="run", mono=True)
    for _ in range(10):
        a, step = rng.randint(0, 3), rng.randint(2, 4)
        b = a + step * rng.randint(2, 3) + rng.randint(0, step - 1)
        vals = list(range(a, b + 1, step))
        add(u, "What does this program show?", lines(*vals),
            [lines(*range(a, b + 1)), lines(*(vals + [vals[-1] + step])), lines(*range(a + step, b + 1, step))],
            f"It starts at {a} and goes up by {step} each time, stopping before going past {b}.",
            code=f"repeat with i from {a} to {b} by {step}\n    say i\nend", check="run", mono=True)
    for _ in range(6):
        step = rng.randint(2, 5)
        top = step * rng.randint(2, 3)
        vals = list(range(top, -1, -step))
        add(u, "What does this program show?", lines(*vals),
            [lines(*reversed(vals)), lines(*vals[:-1]), lines(*range(top, -1, -1))],
            f"A step of -{step} counts down.",
            code=f"repeat with i from {top} to 0 by -{step}\n    say i\nend", check="run", mono=True)
    for n in rng.sample(range(3, 15), 8):
        add(u, "What does this program show?", fmt(n * (n + 1) // 2), [fmt(n), fmt(n * n), fmt(n * (n - 1) // 2)],
            f"It adds 1 + 2 + ... + {n}.",
            code=f"set total to 0\nrepeat with i from 1 to {n}\n    add i to total\nend\nsay total", check="run", mono=True)
    for _ in range(5):
        n, k = rng.randint(3, 6), rng.randint(2, 9)
        add(u, "What does this program show?", fmt(n * k), [fmt(n + k), fmt(k), fmt(n * k + k)],
            f"It adds {k}, {n} times.",
            code=f"set total to 0\nrepeat {n} times\n    add {k} to total\nend\nsay total", check="run", mono=True)
    for w in rng.sample([x for x in WORDS if len(x) <= 5], 6):
        add(u, "What does this program show?", "\n".join(w), [w, "\n".join(reversed(w)), " ".join(w)],
            "`repeat with letter in` goes through the text one letter at a time.",
            code=f'repeat with letter in "{w}"\n    say letter\nend', check="run", mono=True)
    for _ in range(6):
        items = rng.sample(range(1, 10), 3)
        add(u, "What does this program show?", lines(*[x * 2 for x in items]),
            [lines(*items), fmt(sum(items) * 2), lines(*[x + 2 for x in items])],
            "It goes through the list and says each item doubled.",
            code=f"repeat with n in {fmt(items)}\n    say n * 2\nend", check="run", mono=True)
    for _ in range(8):
        k, s = rng.randint(4, 9), rng.randint(1, 3)
        vals = list(range(k, 0, -s))
        add(u, "What does this program show?", lines(*vals), [lines(*(vals + [vals[-1] - s])), lines(*vals[:-1]),
                                                             lines(*reversed(vals))],
            "It keeps going while n is more than 0.",
            code=f"set n to {k}\nrepeat while n > 0\n    say n\n    subtract {s} from n\nend", check="run", mono=True)
    for lim in rng.sample([5, 10, 20, 30, 50, 70, 100, 200], 5):
        n = 1
        while not n > lim:
            n *= 2
        add(u, "What does this program show?", fmt(n), [fmt(n // 2), fmt(lim), fmt(n * 2)],
            f"n doubles until it's more than {lim}.",
            code=f"set n to 1\nrepeat until n > {lim}\n    set n to n * 2\nend\nsay n", check="run", mono=True)
    for k in rng.sample(range(2, 7), 5):
        add(u, "What does this program show?", lines(*range(1, k)), [lines(*range(1, k + 1)), lines(*range(1, 11)),
                                                                     fmt(k)],
            f"When i is {k}, `stop` leaves the loop before it's said.",
            code=f"repeat with i from 1 to 10\n    if i = {k}\n        stop\n    end\n    say i\nend", check="run", mono=True)
    for _ in range(6):
        m = rng.randint(4, 6)
        k = rng.randint(2, m - 1)
        add(u, "What does this program show?", lines(*[i for i in range(1, m + 1) if i != k]),
            [lines(*range(1, m + 1)), lines(*range(1, k)), lines(*range(k + 1, m + 1))],
            f"`skip` jumps over saying {k}, and the loop carries on.",
            code=f"repeat with i from 1 to {m}\n    if i = {k}\n        skip\n    end\n    say i\nend", check="run", mono=True)
    for _ in range(6):
        a, b = rng.randint(2, 4), rng.randint(2, 4)
        add(u, "What does this program show?", "*" * (a * b), ["*" * (a + b), "*" * a, "*" * (a * b + 1)],
            f"The inside loop runs {b} times for each of the {a} times round the outside loop.",
            code=f'repeat {a} times\n    repeat {b} times\n        say.same "*"\n    end\nend\nsay ""', check="run", mono=True)
    for n in range(2, 6):
        add(u, "What does this program show?", "\n".join("*" * r for r in range(1, n + 1)),
            ["\n".join("*" * n for _ in range(n)), "*" * n, "\n".join("*" * r for r in range(n, 0, -1))],
            "Row 1 has 1 star, row 2 has 2 stars, and so on.",
            code=f'repeat with row from 1 to {n}\n    repeat row times\n        say.same "*"\n    end\n    say()\nend',
            check="run", mono=True)


# --------------------------------------------------------------------- lists

def lists(add, rng):
    u = "lists"
    add(u, "What number is the first item of a list?", "1", ["0", "-1", "first"], "Counting starts at 1.")
    add(u, "Can a list hold a number and a text together, like `[1, \"two\"]`?", "No: every item is the same kind",
        ["Yes, any mix", "Only two items", "Only in functions"], "All the items in a list are the same kind.")
    add(u, "Is this program right?", "No: the items must all be the same kind", ["Yes", "Yes, it shows 1",
                                                                                  "No: lists can't hold text"],
        "All the items in a list are the same kind.", code='set things to [1, "two"]', check="fails")
    add(u, "How do you start an empty list?", "[]", ["()", "{}", "empty"], "`set names to []`", mono=True)
    add(u, "What does `add 4 to nums` do to a list?", "Puts 4 on the end", ["Adds 4 to every item",
                                                                            "Puts 4 at the start", "Makes a mistake"],
        "`add` puts an item on the end of a list.")
    add(u, "What happens if you ask for item 10 of a list with 3 items?", "The program stops with a clear message",
        ["It gives 0", "It gives the last item", "It gives nothing"], "Flaux says which item doesn't exist.")
    add(u, "Which makes `[0, 0, 0, 0]`?", "4 copies of 0", ["0 copies of 4", "[0] * 4", "copies(4, 0)"],
        "`4 copies of 0` is 0, four times. (`0 copies of 4` would be an empty list.)", mono=True)
    add(u, "A function changes a list it was given. What happens to the original list?",
        "It changes too", ["It stays the same", "It becomes empty", "It's a mistake"],
        "Lists are shared, so changing one inside a function changes the caller's list.")
    for _ in range(12):
        items = rng.sample(range(1, 40), 4)
        i = rng.randint(1, 4)
        wrong = [fmt(items[i % 4]), fmt(items[i - 2]) if i > 1 else fmt(items[1] + 1), fmt(i)]
        add(u, "What does this program show?", fmt(items[i - 1]), wrong,
            f"Items are counted from 1, so item {i} is {items[i - 1]}.",
            code=f"set nums to {fmt(items)}\nsay nums[{i}]", check="run", mono=True)
    for _ in range(6):
        names = rng.sample(NAMES, 3)
        i = rng.randint(1, 3)
        add(u, "What does this program show?", names[i - 1], [names[i % 3], names[i - 2], f'"{names[i - 1]}"'],
            f"Item {i} is {names[i - 1]}.", code=f"set names to {fmt(names)}\nsay names[{i}]", check="run", mono=True)
    for _ in range(10):
        items = rng.sample(range(1, 20), rng.randint(2, 5))
        k = rng.randint(1, 3)
        add(u, "What does this program show?", fmt(len(items) + k), [fmt(len(items)), fmt(k), fmt(len(items) + k + 1)],
            f"It started with {len(items)} items and {k} were added.",
            code=f"set nums to {fmt(items)}\n" + "".join(f"add {rng.randint(1, 9)} to nums\n" for _ in range(k)) +
                 "say length of nums", check="run", mono=True)
    for _ in range(10):
        items = rng.sample(range(1, 20), 4)
        x = rng.choice(items) if rng.random() < 0.5 else rng.choice([x for x in range(20, 30)])
        add(u, "What does this program show?", fmt(x in items), [fmt(x not in items), fmt(x), "4"],
            f"`contains` says whether {x} is in the list.",
            code=f"set nums to {fmt(items)}\nsay contains(nums, {x})", check="run", mono=True)
    for _ in range(10):
        items = rng.sample(range(1, 20), 3)
        i, x = rng.randint(1, 3), rng.randint(50, 99)
        new = items[:]
        new[i - 1] = x
        other = items[:]
        other.insert(i - 1, x)
        add(u, "What does this program show?", fmt(new), [fmt(items), fmt(other), fmt(items + [x])],
            f"`set nums[{i}] to {x}` changes item {i}.",
            code=f"set nums to {fmt(items)}\nset nums[{i}] to {x}\nsay nums", check="run", mono=True)
    for _ in range(6):
        a, b = rng.sample(range(1, 9), 2), rng.sample(range(1, 9), 2)
        add(u, "What does this program show?", fmt(a + b), [fmt([x + y for x, y in zip(a, b)]), fmt(b + a),
                                                            fmt([a, b]).replace("], [", "], [")],
            "`+` joins two lists into a longer one.", code=f"say {fmt(a)} + {fmt(b)}", check="run", mono=True)
    for _ in range(8):
        n, v = rng.randint(2, 5), rng.randint(0, 9)
        add(u, "What does this program show?", fmt([v] * n), [fmt([n] * v) if v else "[]", fmt([n, v]), fmt([v] * (n + 1))],
            f"`{n} copies of {v}` is a list of {n} {v}s.", code=f"say {n} copies of {v}", check="run", mono=True)
    for _ in range(5):
        a, b = rng.sample(range(1, 9), 2), rng.sample(range(1, 9), 2)
        add(u, "What does this program show?", fmt(a + b), [fmt(a), fmt(a + [sum(b)]), fmt(b + a)],
            "Adding a list to a list adds every item of it.",
            code=f"set nums to {fmt(a)}\nadd {fmt(b)} to nums\nsay nums", check="run", mono=True)
    for _ in range(6):
        items = rng.sample(range(1, 20), 4)
        add(u, "What does this program show?", fmt(sum(items)), [fmt(len(items)), fmt(max(items)), fmt(sum(items) + 1)],
            "It adds every item to total.",
            code=f"set total to 0\nrepeat with n in {fmt(items)}\n    add n to total\nend\nsay total", check="run", mono=True)
    for _ in range(4):
        items = rng.sample(range(1, 30), 5)
        add(u, "What does this program show?", fmt(max(items)), [fmt(min(items)), fmt(items[-1]), fmt(items[0])],
            "biggest only changes when an item is bigger than it, so it ends as the biggest item.",
            code=f"set nums to {fmt(items)}\nset biggest to nums[1]\nrepeat with n in nums\n    if n > biggest\n"
                 f"        set biggest to n\n    end\nend\nsay biggest", check="run", mono=True)
    for _ in range(4):
        items = rng.sample(range(1, 9), 3)
        add(u, "What does this program show?", fmt(items[-1]), [fmt(items[0]), fmt(len(items)), fmt(items[1])],
            "`nums[length of nums]` is the last item.",
            code=f"set nums to {fmt(items)}\nsay nums[length of nums]", check="run", mono=True)
    for _ in range(4):
        n = rng.randint(2, 4)
        add(u, "What does this program show?", fmt([["."] * n for _ in range(n)]).replace("'", '"'),
            [fmt(["."] * n), fmt(["."] * (n * n)), str(n)],
            f"A list of {n} lists, each with {n} dots: a grid.",
            code=f'say {n} copies of ({n} copies of ".")', check="run", mono=True)
    for n in rng.sample(NAMES, 4):
        add(u, "What does this program show?", f'["{n}"]', ["[]", n, f'["", "{n}"]'],
            "An empty list gets its first item.", code=f'set names to []\nadd "{n}" to names\nsay names', check="run",
            mono=True)


# ----------------------------------------------------------------- functions

def functions(add, rng):
    u = "functions"
    add(u, "Which word makes your own instruction?", "function", ["def", "make", "instruction"],
        "`function name(inputs)` ... `end`.")
    add(u, "What does `return` do?", "Gives a value back and leaves the function", ["Starts the function again",
                                                                                   "Says the value",
                                                                                   "Ends the program"],
        "Where the function was used gets that value.")
    add(u, "Can a function see variables made outside it?", "No: pass them in as inputs",
        ["Yes, all of them", "Only numbers", "Only lists"], "Functions only see their own inputs and variables.")
    add(u, "Where can functions be written?", "Anywhere in the file, but not inside another block",
        ["Only at the top", "Only at the bottom", "Inside a repeat"], "Above or below the code that uses them.")
    add(u, "What is it called when a function uses itself?", "Recursion", ["Repetition", "A loop", "A mistake"],
        "Like factorial(n) using factorial(n - 1).")
    add(u, "Which line has the mistake?", "4", ["1", "2", "3"], "`double` needs 1 input, but it was given 2.",
        code="function double(n)\n    return n * 2\nend\nsay double(3, 4)", check="line")
    for name, k, op in [("double", 2, "*"), ("triple", 3, "*"), ("add_ten", 10, "+"), ("half", 2, "/"),
                        ("times_four", 4, "*"), ("minus_one", 1, "-"), ("add_five", 5, "+"), ("times_ten", 10, "*")]:
        for _ in range(2):
            v = rng.randint(2, 20) * (2 if op == "/" else 1)
            res = {"*": v * k, "+": v + k, "-": v - k, "/": v / k}[op]
            add(u, "What does this program show?", fmt(res), [fmt(v), fmt(res + 1), fmt(k)],
                f"{name}({v}) gives back {v} {op} {k}.",
                code=f"function {name}(n)\n    return n {op} {k}\nend\n\nsay {name}({v})", check="run", mono=True)
    for _ in range(8):
        w, h = rng.randint(2, 9), rng.randint(2, 9)
        add(u, "What does this program show?", fmt(w * h), [fmt(w + h), fmt(2 * (w + h)), fmt(w)],
            "area gives back width times height.",
            code=f"function area(width, height)\n    return width * height\nend\n\nsay area({w}, {h})", check="run", mono=True)
    for n in rng.sample(NAMES, 6):
        add(u, "What does this program show?", f"Hello, {n}!", ["Hello, name!", f"Hello, {n}", "greet(name)"],
            "The input `name` gets the value it was given.",
            code=f'function greet(name)\n    say "Hello, {{name}}!"\nend\n\ngreet("{n}")', check="run", mono=True)
    for _ in range(6):
        v = rng.randint(1, 10)
        add(u, "What does this program show?", fmt((v + 1) * 2), [fmt(v * 2 + 1), fmt(v + 1), fmt(v * 2)],
            "The inside one runs first: add_one, then double.",
            code=f"function add_one(n)\n    return n + 1\nend\n\nfunction double(n)\n    return n * 2\nend\n\n"
                 f"say double(add_one({v}))", check="run", mono=True)
    for n in [3, 4, 5, 6]:
        import math
        add(u, "What does this program show?", fmt(math.factorial(n)), [fmt(n * (n - 1)), fmt(n), fmt(math.factorial(n - 1))],
            f"factorial({n}) is {' * '.join(str(i) for i in range(n, 0, -1))}.",
            code="function factorial(n)\n    if n <= 1\n        return 1\n    end\n    return n * factorial(n - 1)\nend\n\n"
                 f"say factorial({n})", check="run", mono=True)
    for v in rng.sample(range(-9, 10), 6):
        res = "minus" if v < 0 else "zero" if v == 0 else "plus"
        add(u, "What does this program show?", res, [x for x in ["minus", "zero", "plus", "minus\nplus"] if x != res],
            "`return` leaves the function straight away, so only one answer comes back.",
            code=f'function sign(n)\n    if n < 0\n        return "minus"\n    end\n    if n = 0\n        return "zero"\n'
                 f'    end\n    return "plus"\nend\n\nsay sign({v})', check="run", mono=True)
    for _ in range(4):
        v = rng.randint(2, 9)
        add(u, "What does this program show?", fmt(v), [fmt(100), fmt(v + 100), "Nothing"],
            "The function changed its own copy of the number. The x outside is still the same.",
            code=f"function change(x)\n    set x to 100\n    return x\nend\n\nset x to {v}\nset y to change(x)\nsay x",
            check="run", mono=True)
    for _ in range(4):
        v = rng.randint(2, 9)
        add(u, "What does this program show?", fmt(v - 1), [fmt(v), "0", fmt(v - 2)],
            "The new value comes back with `return`, and `set` keeps it.",
            code=f"function lose_life(lives)\n    return lives - 1\nend\n\nset lives to {v}\nset lives to lose_life(lives)\n"
                 f"say lives", check="run", mono=True)
    for fruit in ["apple", "pear", "plum"]:
        add(u, "What does this program show?", f'["{fruit}"]', ["[]", fruit, f'["{fruit}", "{fruit}"]'],
            "A function that changes a list it was given changes the original list.",
            code=f'function fill(basket)\n    add "{fruit}" to basket\nend\n\nset basket to []\nfill(basket)\nsay basket',
            check="run", mono=True)
    for _ in range(3):
        a, b = rng.randint(2, 9), rng.randint(2, 9)
        add(u, "What does this program show?", fmt(max(a, b)), [fmt(min(a, b)), fmt(a + b), "true"],
            "It gives back whichever is bigger.",
            code=f"function bigger(a, b)\n    if a > b\n        return a\n    end\n    return b\nend\n\nsay bigger({a}, {b})",
            check="run", mono=True)


# ----------------------------------------------------------------- built-ins

def builtins(add, rng):
    u = "builtins"
    add(u, "Which rounds DOWN to a whole number?", "floor", ["ceil", "round", "abs"], "`floor(2.9)` is 2.", mono=True)
    add(u, "Which rounds UP to a whole number?", "ceil", ["floor", "round", "whole"], "`ceil(2.1)` is 3.", mono=True)
    add(u, "Which takes away a minus sign?", "abs", ["sqrt", "min", "floor"], "`abs(-5)` is 5.", mono=True)
    add(u, "Which gives the square root?", "sqrt", ["abs", "root", "square"], "`sqrt(16)` is 4.", mono=True)
    add(u, "Which turns a number into text?", "text", ["number", "whole", "write"], "`text(42)` is \"42\".", mono=True)
    add(u, "Which says how far apart two points are?", "distance", ["length", "abs", "min"],
        "`distance(0, 0, 3, 4)` is 5.", mono=True)
    for x in rng.sample([1.2, 2.6, 3.5, 4.4, 5.5, 7.8, 9.1, 0.4, 2.5, 6.49, -1.5, -2.3], 10):
        r = fround(x)
        add(u, "What does this program show?", fmt(r), [fmt(r + 1), fmt(r - 1), fmt(x)],
            "`round` goes to the nearest whole number (.5 goes away from zero).",
            code=f"say round({fmt(x)})", check="run", mono=True)
    import math
    for x in rng.sample([1.2, 2.9, 3.5, 4.01, 7.7, -2.5, 9.99, 0.5], 6):
        add(u, "What does this program show?", fmt(math.floor(x)), [fmt(math.floor(x) + 1), fmt(fround(x)) if fround(x) != math.floor(x) else fmt(math.floor(x) - 1), fmt(x)],
            "`floor` always rounds down.", code=f"say floor({fmt(x)})", check="run", mono=True)
    for x in rng.sample([1.2, 2.9, 3.5, 4.01, 7.1, -2.5, 9.99, 0.5], 6):
        add(u, "What does this program show?", fmt(math.ceil(x)), [fmt(math.ceil(x) - 1), fmt(math.ceil(x) + 1), fmt(x)],
            "`ceil` always rounds up.", code=f"say ceil({fmt(x)})", check="run", mono=True)
    for x in rng.sample([-5, -12, 7, -3, 0, -100, 42, -1], 6):
        add(u, "What does this program show?", fmt(abs(x)), [fmt(-abs(x)) if x else "1", fmt(x * 2) if x else "-1", fmt(abs(x) + 1)],
            "`abs` takes away the minus sign (and leaves other numbers alone).", code=f"say abs({x})", check="run", mono=True)
    for _ in range(12):
        nums = rng.sample(range(1, 50), 3)
        if rng.random() < 0.5:
            add(u, "What does this program show?", fmt(max(nums)), [fmt(min(nums)), fmt(nums[0]), fmt(sum(nums))],
                "`max` gives the biggest.", code=f"say max({', '.join(map(str, nums))})", check="run", mono=True)
        else:
            add(u, "What does this program show?", fmt(min(nums)), [fmt(max(nums)), fmt(nums[-1]), fmt(sum(nums))],
                "`min` gives the smallest.", code=f"say min({', '.join(map(str, nums))})", check="run", mono=True)
    for n in rng.sample([4, 9, 16, 25, 36, 49, 64, 81, 100, 144], 8):
        r = int(n ** 0.5)
        add(u, "What does this program show?", fmt(r), [fmt(n // 2), fmt(n * n), fmt(r + 1)],
            f"{r} times {r} is {n}.", code=f"say sqrt({n})", check="run", mono=True)
    for s in ["42", "7", "100", "15"]:
        add(u, "What does this program show?", fmt(int(s) + 1), [s + "1", s, "1"],
            "`whole` turned the text into a number, so adding works.", code=f'say whole("{s}") + 1', check="run", mono=True)
    for x in [3.9, 7.2, 1.5, -2.7]:
        add(u, "What does this program show?", fmt(int(x)), [fmt(fround(x)) if fround(x) != int(x) else fmt(int(x) + 1),
                                                             fmt(x), fmt(math.ceil(x)) if math.ceil(x) != int(x) else fmt(int(x) - 1)],
            "`whole` drops everything after the point.", code=f"say whole({fmt(x)})", check="run", mono=True)
    for s in ["3.5", "1.25", "0.5", "2.75"]:
        add(u, "What does this program show?", fmt(float(s) * 2), [s + s, s, fmt(float(s) + 2)],
            "`number` turns text into a decimal number.", code=f'say number("{s}") * 2', check="run", mono=True)
    for s, res in [("42", True), ("4.5", True), ("hi", False), ("12a", False), ("-3", True), ("", False), ("x9", False),
                   ("100", True)]:
        add(u, "What does this program show?", fmt(res), [fmt(not res), s or '""', "0"],
            "`is_number` says whether the text is a number.", code=f'say is_number("{s}")', check="run", mono=True)
    for a, b in [(4, 2), (10, 5), (3, 33), (7, 1)]:
        add(u, "What does this program show?", f"{a}{b}", [fmt(a + b), f"{a} {b}", f"{a} + {b}"],
            "`text` turns numbers into text, and `+` joins text.", code=f"say text({a}) + text({b})", check="run", mono=True)
    for (dx, dy, d) in [(3, 4, 5), (6, 8, 10), (5, 12, 13), (8, 15, 17), (0, 7, 7), (9, 12, 15)]:
        x, y = rng.randint(0, 20), rng.randint(0, 20)
        add(u, "What does this program show?", fmt(d), [fmt(dx + dy), fmt(d + 1), fmt(abs(dx - dy))],
            f"The points are {dx} across and {dy} up apart, so {d} apart in a straight line.",
            code=f"say distance({x}, {y}, {x + dx}, {y + dy})", check="run", mono=True)


# --------------------------------------------------------- random and waiting

def randomness(add, rng):
    u = "random"
    add(u, "What does `random(1, 6)` give?", "A whole number from 1 to 6 (both included)",
        ["Always 1", "A decimal between 1 and 6", "A number from 1 to 5"], "Like rolling a dice.")
    add(u, "What does `random([\"rock\", \"paper\", \"scissors\"])` give?", "One of the three, picked at random",
        ["All three", "Always \"rock\"", "A number from 1 to 3"], "`random(list)` picks an item.")
    add(u, "Why is `wait 5` a mistake?", "It doesn't say 5 what: seconds? minutes?",
        ["5 is too long", "wait needs brackets", "It isn't a mistake"], "Write `wait 5s` or `wait 5m`.")
    add(u, "How do you wait for a variable's number of seconds?", "wait delay seconds",
        ["wait delays", "wait delay", "wait(delay)"], "For a variable, write the unit as a word.",
        code="set delay to 3", check="valid", mono=True)
    add(u, "Does `wait` show what a game drew?", "No: call show() first", ["Yes, always", "Only in fullscreen",
                                                                         "Only on the web"],
        "`wait` only pauses. `show()` puts the picture on the screen.")
    add(u, "Can the time in `wait` be a decimal, like `wait 0.5s`?", "Yes: that's half a second",
        ["No, only whole numbers", "Only in games", "Only in minutes"], "`wait 0.5s` waits half a second.")
    for unit, word, wrongs in [("s", "seconds", ["minutes", "milliseconds", "hours"]),
                               ("m", "minutes", ["seconds", "months", "hours"]),
                               ("h", "hours", ["minutes", "seconds", "days"]),
                               ("d", "days", ["hours", "minutes", "seconds"])]:
        n = rng.randint(2, 9)
        add(u, f"How long does `wait {n}{unit}` wait?", f"{n} {word}", [f"{n} {w}" for w in wrongs],
            f"`{unit}` means {word}.")
        add(u, "Which line waits " + f"{n} {word}?", f"wait {n}{unit}", [f"wait {n}", f'wait "{n}{unit}"', f"wait({n})"],
            "Glue the unit to the number.", check="valid", mono=True)
    for _ in range(14):
        a = rng.randint(0, 10)
        b = a + rng.randint(3, 10)
        never = rng.choice([b + 1, a - 1, b + 5])
        inside = rng.sample(range(a, b + 1), 3)
        add(u, f"Which number can `random({a}, {b})` never give?", fmt(never), [fmt(x) for x in inside],
            f"It gives whole numbers from {a} to {b}, and both ends can come up.")
    for _ in range(6):
        a, b = rng.randint(1, 5), rng.randint(6, 12)
        add(u, f"Which can `random({a}, {b})` give?", f"Any whole number from {a} to {b}",
            [f"Only {a} or {b}", f"Numbers from {a} to {b - 1}", f"Decimals like {a}.5"],
            "Both ends are included.")
    add(u, "Two dice: which gives their total?", "random(1, 6) + random(1, 6)", ["random(2, 12)", "random(1, 12)",
                                                                                 "random(1, 6) * 2"],
        "Each dice is its own random number. (random(2, 12) makes 2 just as likely as 7, but real dice don't.)",
        mono=True)


# ------------------------------------------------------------------ graphics

def graphics(add, rng):
    u = "graphics"
    add(u, "In a window, where is x = 0, y = 0?", "The top-left corner", ["The middle", "The bottom-left corner",
                                                                          "The top-right corner"],
        "Places are measured from the top-left.")
    add(u, "When y gets bigger, a shape moves...", "Down", ["Up", "Right", "Left"], "In 2D, y goes down the window.")
    add(u, "When x gets bigger, a shape moves...", "Right", ["Left", "Up", "Down"], "x goes across, to the right.")
    add(u, "Where is a rect's x, y?", "Its top-left corner", ["Its middle", "Its bottom-left corner",
                                                              "Its bottom-right corner"], "`rect(x, y, width, height)`.")
    add(u, "Where is a circle's x, y?", "Its centre", ["Its top-left corner", "Its edge", "Its bottom"],
        "`circle(x, y, radius)`.")
    add(u, "Which fills the whole window with one colour?", "clear(colour)", ["fill(colour)", "rect(colour)",
                                                                              "background(colour)"],
        "`clear(\"sky\")` fills it with sky blue.", mono=True)
    add(u, "Which draws words in the window?", "write", ["say", "text", "print"], "`write(\"Hi\", 10, 10)`.", mono=True)
    add(u, "How many points does `triangle` need?", "3", ["2", "4", "1"], "x and y for each of its 3 corners.")
    add(u, "What colour is the default when you leave it out?", "White", ["Black", "Red", "Nothing is drawn"],
        "Shapes and text are white by default.")
    add(u, "What has to come before drawing anything?", "window(...)", ["show()", "clear()", "repeat forever"],
        "Open the window first.", mono=True)
    add(u, "How big are the numbers in rgb(...)?", "0 to 255", ["0 to 100", "0 to 1", "1 to 10"],
        "0 is none of that colour, 255 is the most.")
    add(u, "Which of these is a colour code?", '"#ff8800"', ['"ff-88-00"', '"rgb ff8800"', '"orange#"'],
        "A `#` and then 6 digits (red, green, blue).", mono=True)
    for r, g, b, name, wrongs in [(255, 0, 0, "Red", ["Green", "Blue", "Black"]), (0, 255, 0, "Green", ["Red", "Blue", "White"]),
                                  (0, 0, 255, "Blue", ["Red", "Green", "Black"]), (255, 255, 255, "White", ["Black", "Gray", "Yellow"]),
                                  (0, 0, 0, "Black", ["White", "Gray", "Blue"]), (255, 255, 0, "Yellow", ["Purple", "Cyan", "Orange"]),
                                  (255, 0, 255, "Magenta (pink-purple)", ["Yellow", "Cyan", "Green"]),
                                  (0, 255, 255, "Cyan (light blue-green)", ["Magenta", "Yellow", "Red"]),
                                  (128, 128, 128, "Gray", ["White", "Black", "Brown"])]:
        add(u, f"What colour is `rgb({r}, {g}, {b})`?", name, wrongs, "Red, green and blue light mixed together.")
    for name, (r, g, b) in [("red", (255, 0, 0)), ("yellow", (255, 255, 0)), ("white", (255, 255, 255))]:
        others = [x for x in ["rgb(255, 0, 0)", "rgb(255, 255, 0)", "rgb(255, 255, 255)", "rgb(0, 0, 255)"]
                  if x != f"rgb({r}, {g}, {b})"]
        add(u, f"Which makes {name}?", f"rgb({r}, {g}, {b})", others[:3], "Red, green, blue, each from 0 to 255.", mono=True)
    for _ in range(10):
        x, w, y, h = rng.randint(10, 300), rng.randint(20, 200), rng.randint(10, 200), rng.randint(20, 150)
        add(u, f"`rect({x}, {y}, {w}, {h})`: how far across is its right edge?", fmt(x + w),
            [fmt(x), fmt(w), fmt(x + h) if x + h != x + w else fmt(x + w + 1)],
            f"It starts at x {x} and is {w} wide.")
    for _ in range(8):
        x, w, y, h = rng.randint(10, 300), rng.randint(20, 200), rng.randint(10, 200), rng.randint(20, 150)
        add(u, f"`rect({x}, {y}, {w}, {h})`: how far down is its bottom edge?", fmt(y + h),
            [fmt(y), fmt(h), fmt(y - h) if y - h != y + h else fmt(y + h + 1)],
            f"It starts at y {y} and is {h} tall, and y goes down.")
    for _ in range(6):
        x, y, r = rng.randint(100, 400), rng.randint(100, 300), rng.randint(10, 60)
        add(u, f"`circle({x}, {y}, {r})`: how far across is its left edge?", fmt(x - r), [fmt(x), fmt(x + r), fmt(r)],
            f"The centre is at {x} and it reaches {r} out each way.")
    for _ in range(6):
        x, y, r = rng.randint(100, 400), rng.randint(100, 300), rng.randint(10, 60)
        add(u, f"`circle({x}, {y}, {r})`: how far down is its top edge?", fmt(y - r), [fmt(y), fmt(y + r), fmt(r)],
            "y goes down, so the top is the centre's y minus the radius.")
    for w, h in [(800, 600), (640, 480), (1024, 768), (400, 300), (1280, 720)]:
        corner, ans = rng.choice([("bottom-right", f"{w}, {h}"), ("top-right", f"{w}, 0"), ("bottom-left", f"0, {h}")])
        add(u, f"In `window({w}, {h})`, which x, y is the {corner} corner?", ans,
            [c for c in [f"{w}, {h}", f"{w}, 0", f"0, {h}", "0, 0"] if c != ans],
            "x goes right from 0 to the width, y goes down from 0 to the height.")
    for w, h in [(800, 600), (640, 480), (1000, 500), (400, 400)]:
        add(u, f"In `window({w}, {h})`, where is the middle?", f"{w // 2}, {h // 2}", [f"{w}, {h}", "0, 0", f"{h // 2}, {w // 2}"],
            "Half the width across, half the height down.")
    add(u, "Which line draws a line 5 pixels thick?", "line(0, 0, 100, 100, \"red\", 5)",
        ["line(0, 0, 100, 100, 5)", "line(5, 0, 0, 100, 100)", "thick_line(0, 0, 100, 100, 5)"],
        "The thickness comes after the colour.", mono=True)
    add(u, "Which draws a single pixel?", "dot(x, y, colour)", ["pixel(x, y)", "circle(x, y, 0)", "point(x, y)"],
        "`dot` draws one pixel.", mono=True)
    add(u, "What does `text_width(\"Hi\", 3)` tell you?", "How many pixels wide write would make it",
        ["How many letters it has", "Its height", "Its colour"], "Handy for putting words in the middle.")


# --------------------------------------------------------------------- games

def games(add, rng):
    u = "games"
    add(u, "How many times a second does a loop with `show()` run?", "60", ["30", "100", "As fast as it can"],
        "`show()` waits so it's the same speed on every computer.")
    add(u, "What does `show()` do?", "Puts the picture on the screen and waits for the next frame",
        ["Opens the window", "Clears the window", "Ends the game"], "Finish each picture with `show()`.")
    add(u, "Which is true only on the frame a key goes down?", "key_pressed", ["key_down", "key_up", "key_held"],
        "Use it for jumping or firing.", mono=True)
    add(u, "Which is true the whole time a key is held?", "key_down", ["key_pressed", "key_clicked", "key_once"],
        "Use it for smooth movement.", mono=True)
    add(u, "Which should you use to jump once per press?", "key_pressed(\"space\")", ["key_down(\"space\")",
                                                                                      "mouse_x()", "show()"],
        "`key_down` would jump again every frame while it's held.", mono=True)
    add(u, "Which gives where the mouse is across the window?", "mouse_x()", ["mouse_y()", "mouse_down()",
                                                                             "mouse_moved_x()"], "", mono=True)
    add(u, "Which is true only on the frame of a click?", "mouse_clicked()", ["mouse_down()", "mouse_x()",
                                                                             "key_pressed(\"mouse\")"],
        "`mouse_down()` is true the whole time it's held.", mono=True)
    add(u, "How do you check the right mouse button?", "mouse_down(\"right\")", ["mouse_right()", "mouse_down(2)",
                                                                                 "right_down()"],
        "Give the button's name.", mono=True)
    add(u, "Why start each frame with `clear(...)`?", "To rub out the last picture", ["To open the window",
                                                                                    "To make it faster",
                                                                                    "To end the game"],
        "Otherwise moving shapes leave a trail.")
    add(u, "Which key switches a running Flaux game to fullscreen?", "F11", ["F5", "Esc", "Tab"],
        "F11 (or Alt+Enter) switches on and off.")
    add(u, "With no controller plugged in, what does `button_down(\"a\")` give?", "false", ["true", "A mistake", "0"],
        "Every button is up and the sticks are 0.")
    add(u, "What do `stick_x()` and `stick_y()` give?", "A number from -1 to 1", ["true or false", "0 to 255",
                                                                                 "The stick's name"],
        "Pushing right and up gives positive numbers.")
    add(u, "What does `lock_mouse()` do?", "Hides the pointer and keeps it in the window",
        ["Stops the mouse working", "Clicks the mouse", "Moves the mouse to the middle"], "For mouse look in 3D games.")
    for _ in range(12):
        a, s, n = rng.randint(0, 100), rng.randint(1, 8), rng.randint(5, 60)
        add(u, f"x starts at {a}, and each frame the game does `add {s} to x`. Where is x after {n} frames?",
            fmt(a + s * n), [fmt(a + s), fmt(a + n), fmt(s * n)], f"{s} more each frame, {n} times: {a} + {s} × {n}.")
    for k in [2, 3, 5, 10]:
        add(u, f"How many frames does a game draw in {k} seconds?", fmt(60 * k), [fmt(k), fmt(30 * k), fmt(100 * k)],
            "60 frames every second.")
    for _ in range(10):
        dx, dy, d = rng.choice([(3, 4, 5), (6, 8, 10), (5, 12, 13), (9, 12, 15), (12, 16, 20), (15, 20, 25)])
        r1, r2 = rng.randint(2, 15), rng.randint(2, 15)
        x, y = rng.randint(50, 200), rng.randint(50, 200)
        res = d < r1 + r2
        add(u, f"Two circles: radius {r1} at {x}, {y} and radius {r2} at {x + dx}, {y + dy}. Do they touch?",
            fmt(res), [fmt(not res), "Only if they're the same colour", "Only in fullscreen"],
            f"The centres are {d} apart and the radiuses add up to {r1 + r2}.",
            code=f"say distance({x}, {y}, {x + dx}, {y + dy}) < {r1} + {r2}", check="run", mono=True)


# ------------------------------------------------------------------ pictures

def pictures(add, rng):
    u = "pictures"
    add(u, "Where should a picture file go?", "In the same folder as the program", ["Anywhere on the computer",
                                                                                     "In the Windows folder",
                                                                                     "Inside the program's text"],
        "Flaux looks next to the program first.")
    add(u, "Which kinds of picture files work?", "PNG, JPG, BMP and GIF", ["Only PNG", "Only BMP", "MP3 and WAV"],
        "PNG pictures can have see-through parts.")
    add(u, "Where is a picture's x, y in `image(...)`?", "Its top-left corner", ["Its middle", "Its bottom",
                                                                                 "Its right edge"], "Like `rect`.")
    add(u, "What does a minus width do in `image(\"ship.png\", x, y, -64, 64)`?", "Flips the picture",
        ["Makes it invisible", "Makes it smaller", "It's a mistake"], "A minus width or height flips it.")
    add(u, "What does the last number in `image(\"ship.png\", x, y, 64, 64, 45)` do?", "Turns it 45 degrees",
        ["Makes it 45 pixels", "Fades it", "Moves it right 45"], "It turns around its middle.")
    add(u, "What happens if a picture's name is misspelled?", "Flaux says so before the program runs",
        ["The game crashes later", "A blank square is drawn", "Nothing"], "It even suggests the right name.")
    add(u, "Is it slow to draw a picture every frame?", "No: it's loaded once and reused", ["Yes, very slow",
                                                                                           "Only big pictures",
                                                                                           "It's not allowed"],
        "Drawing pictures every frame is fast.")
    add(u, "Which gives a picture's real width?", "image_width(\"ship.png\")", ["width(\"ship.png\")",
                                                                               "length of \"ship.png\"",
                                                                               "image(\"ship.png\").width"], "",
        mono=True)
    add(u, "What does `beep(440, 0.2)` play?", "The note A for 0.2 seconds", ["440 beeps", "A sound for 440 seconds",
                                                                              "Nothing"], "Pitch, then how long.")
    add(u, "Which beep sounds higher?", "beep(880)", ["beep(220)", "beep(440)", "They're all the same"],
        "Bigger numbers are higher notes.", mono=True)
    add(u, "Three beeps in a row: what happens?", "They play one after another, like a tune",
        ["They all play at once", "Only the last one plays", "It's a mistake"], "")
    add(u, "How long is `beep(660)` with no time given?", "0.1 seconds", ["1 second", "660 seconds", "Forever"],
        "A short beep.")
    for _ in range(5):
        w, h = rng.randint(2, 8) * 16, rng.randint(2, 8) * 16
        add(u, f"`image(\"ship.png\", 10, 10, {w}, {h})`: how wide is it drawn?", f"{w} pixels",
            [f"{h} pixels", "Its own size", "10 pixels"], "The width comes after x and y.")
    for _ in range(4):
        x, w = rng.randint(10, 300), rng.randint(2, 6) * 16
        add(u, f"`image(\"ship.png\", {x}, 50, {w}, {w})`: how far across is its right edge?", fmt(x + w),
            [fmt(x), fmt(w), fmt(x + w // 2)], "x is the left edge, plus the width.")


# ------------------------------------------------------------------------ 3D

def three_d(add, rng):
    u = "3d"
    add(u, "In 3D, which way does y go?", "Up", ["Down", "Towards you", "Right"], "In 2D y goes down, in 3D it goes up.")
    add(u, "In 3D, which way does z go?", "Towards you", ["Up", "Away from you", "Left"], "")
    add(u, "Where is a 3D shape placed from?", "Its middle", ["Its bottom", "Its top-left corner", "Its front"], "")
    add(u, "What does `camera(x, y, z, look_x, look_y, look_z)` set?", "Where you see from and what you look at",
        ["The size of the window", "The sun", "The sky colour"], "")
    add(u, "With no `camera(...)`, where do you look from?", "0, 3, 10, looking at 0, 0, 0", ["0, 0, 0", "Straight down",
                                                                                               "Nowhere: it's a mistake"],
        "A bit up and back from the middle.")
    add(u, "What does `sun(0, 0, 0)` do?", "No sun: night", ["A sun in the middle", "A very bright sun",
                                                            "It's a mistake"], "Only the sky's glow and lamps are left.")
    add(u, "What does `fog(30)` do?", "Things fade away until they're gone 30 steps away",
        ["Makes 30 clouds", "Turns the sky gray", "Slows the game"], "")
    add(u, "How many lamps (`light`) can there be at once?", "8", ["1", "100", "As many as you like"], "")
    add(u, "What can a shape's paint be?", "A colour, a picture file or a shader", ["Only a colour", "Only a number",
                                                                                     "Only white"], "")
    add(u, "Which draws a flat floor?", "plane", ["box", "floor", "rect"], "`plane(x, y, z, width, depth, paint)`.",
        mono=True)
    add(u, "Which shape stands up and has a point at the top?", "cone", ["cylinder", "ball", "plane"], "", mono=True)
    add(u, "Which file kind is a 3D model?", ".obj", [".png", ".flx", ".mp3"], "From Blender: File, Export, "
                                                                             "Wavefront (.obj).", mono=True)
    add(u, "Can you draw 2D text on top of a 3D world?", "Yes: write, rect and the rest go on top",
        ["No", "Only in VR", "Only with a shader"], "")
    add(u, "What does `sky(\"navy\", \"orange\")` with two colours make?", "A sky fading from navy at the top to orange at the horizon",
        ["Half the sky navy, half orange", "A mistake", "Navy clouds"], "")
    for _ in range(8):
        y = rng.choice([0, 1, 2, 3, 0.5, 1.5])
        s = rng.choice([1, 2, 3, 4])
        add(u, f"`cube(0, {fmt(y)}, 0, {s})`: how high is its bottom?", fmt(y - s / 2), [fmt(y), fmt(y + s / 2), fmt(y - s)],
            f"It's placed by its middle, so the bottom is half its size ({fmt(s / 2)}) below {fmt(y)}.")
    for _ in range(6):
        y = rng.choice([0, 1, 2, 3, 0.5])
        s = rng.choice([1, 2, 4])
        add(u, f"`cube(0, {fmt(y)}, 0, {s})`: how high is its top?", fmt(y + s / 2), [fmt(y), fmt(y - s / 2), fmt(y + s)],
            "Half its size above its middle.")
    for _ in range(5):
        r = rng.choice([0.5, 1, 1.5, 2])
        add(u, f"A ball of radius {fmt(r)} sitting on the floor (y = 0): what's its y?", fmt(r), ["0", fmt(r * 2), fmt(r / 2)],
            "Its middle is one radius above the floor.")
    for _ in range(4):
        s = rng.choice([1, 2, 3])
        add(u, f"Cubes of size {s} stacked 3 high on the floor: how high is the top?", fmt(3 * s), [fmt(s), fmt(2 * s), fmt(3 * s + 1)],
            "Three cubes, each that tall.")


# ------------------------------------------------------------------- shaders

def shaders(add, rng):
    u = "shaders"
    add(u, "Where does a shader run?", "On the graphics card, once for every pixel", ["Once per program",
                                                                                      "On the internet",
                                                                                      "In the Output"], "")
    add(u, "What does a shader give back?", "A colour", ["A list", "Some text", "Nothing"], "`return rgb(...)`.")
    add(u, "Where is a shader written?", "Outside any function, with shader name ... end", ["Inside the game loop",
                                                                                            "In a separate file",
                                                                                            "Inside a function"], "")
    add(u, "How do you use a shader on a shape?", "Put its name where the colour goes: cube(0, 0, 0, 1, glow)",
        ["glow(cube)", "shader(cube)", "cube.glow()"], "")
    add(u, "How do you put a shader over the whole screen?", "effect(name), before show()", ["sky(name)",
                                                                                            "window(name)",
                                                                                            "clear(name)"], "")
    add(u, "What does `time` mean inside a shader?", "Seconds since the window opened", ["The time of day",
                                                                                         "How long it took to draw",
                                                                                         "The frame number"], "")
    add(u, "In an `effect`, what are `red`, `green` and `blue`?", "The pixel's colour, 0 to 255", ["Colour names",
                                                                                                  "Always 0",
                                                                                                  "Mistakes"], "")
    add(u, "What happens if a shader's loop goes on too long?", "It stops after 10,000 times",
        ["The computer freezes", "The program crashes", "It goes on forever"], "So a mistake can't freeze the computer.")
    add(u, "A shader uses a name like `power` that it didn't set. Where does it come from?",
        "From where the shader is used, in the program", ["It's always 0", "It's a mistake", "From the Output"], "")
    add(u, "Do lights, shadows and fog still work on shapes painted with a shader?", "Yes, they're added after",
        ["No", "Only fog", "Only lights"], "")
    can = ["sin and cos", "if", "set", "rgb(...)", "repeat ... times", "sqrt", "distance", "return"]
    cannot = ["say", "lists", "text", "your own functions", "ask", "write"]
    for c in can:
        add(u, "Which of these can a shader use?", c, rng.sample(cannot, 3),
            "Shaders can use numbers, checks, loops and maths, but not lists, text, `say` or your own functions.")
    for c in cannot:
        add(u, "Which of these can a shader NOT use?", c, rng.sample(can, 3),
            "The graphics card can't run lists, text, `say`, `ask`, `write` or your own functions.")
    add(u, "Is this program right?", "No: shaders can't use say", ["Yes", "Yes, it says hi on every pixel",
                                                                    "No: shaders need a name"],
        "Flaux says so before running.", code='shader bad\n    say "hi"\n    return "red"\nend', check="fails")


# ------------------------------------------------------------------ mistakes

MISTAKE_ANSWERS = ["The if has no end", "The text is missing its closing quote", "wait needs a unit, like 5s",
                   "Use set x to 5", "scroe is misspelled", "repeat needs times", "You can't add text to a number",
                   "Nothing"]


def mistakes(add, rng):
    u = "mistakes"
    add(u, "Flaux underlines a line in red. What should you do first?", "Read the message: it says what's wrong",
        ["Delete the line", "Start a new program", "Press Run again and again"], "It often has an Apply fix button too.")
    add(u, "What does \"I don't know what `scroe` is\" usually mean?", "A name is misspelled",
        ["The computer is broken", "A number is too big", "A missing picture"], "Check the spelling.")
    add(u, "The program stops while running, and a line is lit up. What does that mean?",
        "That's the line it stopped on", ["That line is fine", "That's the last line", "Flaux crashed"], "")
    for prog, ans, why in [
        ('set x to 5\nif x > 3\n    say "big"', "The if has no end", "Every `if` needs an `end`."),
        ('say "hello', "The text is missing its closing quote", "Text needs a `\"` at both ends."),
        ("wait 5", "wait needs a unit, like 5s", "5 what? Seconds or minutes?"),
        ("x = 5", "Use set x to 5", "Variables are made with `set`."),
        ("set score to 1\nsay scroe", "scroe is misspelled", "It should be `score`."),
        ("repeat 3\n    say 1\nend", "repeat needs times", "`repeat 3 times`."),
        ('set n to 3\nadd "a" to n', "You can't add text to a number", "n holds a number."),
    ]:
        others = [w for w in MISTAKE_ANSWERS if w != ans]
        add(u, "What's wrong with this program?", ans, rng.sample(others, 3),
            why, code=prog, check="fails")
    # which line has the mistake? (a misspelled name, somewhere)
    for _ in range(18):
        v = rng.choice(VARS)
        typo = v[:-2] + v[-1] + v[-2] if v[-1] != v[-2] else v + "s"
        n = rng.randint(3, 5)
        bad = rng.randint(2, n)
        prog = [f"set {v} to {rng.randint(1, 9)}"]
        for i in range(2, n + 1):
            name = typo if i == bad else v
            prog.append(rng.choice([f"add {rng.randint(1, 9)} to {name}", f"say {name}", f"say {name} * 2",
                                    f"subtract 1 from {name}"]))
        add(u, "Which line has the mistake?", str(bad), [str(i) for i in range(1, n + 2) if i != bad],
            f"Line {bad} says `{typo}`, but the variable is `{v}`.", code="\n".join(prog), check="line")
    for _ in range(6):
        n = rng.randint(2, 4)
        lines_ = [f'say "line {i}"' for i in range(1, n + 1)]
        bad = rng.randint(1, n)
        lines_[bad - 1] = f'say "line {bad}'
        add(u, "Which line has the mistake?", str(bad), [str(i) for i in range(1, n + 2) if i != bad],
            f"Line {bad}'s text has no closing quote.", code="\n".join(lines_), check="line")
    for _ in range(5):
        n = rng.randint(1, 3)
        pre = [f"say {i}" for i in range(1, n + 1)]
        add(u, "Which line has the mistake?", str(n + 1), [str(i) for i in range(1, n + 4) if i != n + 1],
            "`wait` needs a unit, like `wait 2s`.", code="\n".join(pre + ["wait 2"]), check="line")
    for _ in range(4):
        v = rng.choice(VARS)
        add(u, "Which line has the mistake?", "2", ["1", "3"],
            f"{v} holds a whole number, so it can't hold text too.",
            code=f'set {v} to 5\nset {v} to "five"\nsay {v}', check="line")
    for _ in range(4):
        n = rng.randint(1, 3)
        add(u, "Which line has the mistake?", str(n + 1), [str(i) for i in range(1, n + 3) if i != n + 1],
            "A counted repeat ends with `times`.",
            code="\n".join([f"say {i}" for i in range(1, n + 1)] + ["repeat 3", "    say 0", "end"]), check="line")
    for _ in range(4):
        add(u, "Which line has the mistake?", "1", ["2", "3"], "Every item in a list must be the same kind.",
            code=f'set things to [{rng.randint(1, 9)}, "{rng.choice(WORDS)}"]\nsay things\nsay length of things',
            check="line")
    add(u, "Is this program right?", "Yes", ["No: missing end", "No: x isn't made", "No: say needs quotes"],
        "It makes x, checks it, and every block has its `end`.",
        code='set x to 5\nif x > 3\n    say "big"\nend', check="works")
    add(u, "Is this program right?", "Yes", ["No: wait needs brackets", "No: missing end", "No: s isn't allowed"],
        "`wait 2s` waits two seconds.", code='say "ready"\nwait 2s\nsay "go"', check="works")
