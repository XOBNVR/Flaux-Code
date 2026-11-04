"""The Flaux course: what each unit teaches. make_course.py turns this, and the
questions it makes, into course.json, which both Flaux Studios (the desktop
app and the web page) show in their Course window.

Each unit has:
  id, title, goal  (one line: what you can do after it)
  pages            sections of [heading, text, code]: the explanation, with an example
  lesson           the lesson program in examples/lessons that goes with it (or None)
  challenge        a small task: what to do, the code to start from, and what the finished
                   program should say (None for programs that ask, draw or play: then
                   running it without a problem is enough). `solution` is checked by the
                   tests to really say `expected`.
"""

UNITS = [
    {
        "id": "say", "title": "Saying things", "lesson": "01_say.flx",
        "goal": "Make your program show words and numbers.",
        "pages": [
            ["Your first instruction",
             "`say` shows something in the Output. Text goes in double quotes. Numbers don't need quotes, "
             "and Flaux works out sums before showing them.",
             'say "Hello, world!"\nsay 42\nsay 2 + 2'],
            ["Several things at once",
             "Give `say` several things separated by commas. They're shown on one line, with a space "
             "between each. Brackets are fine too: `say(\"hi\")`.",
             'say "Score:", 10\nsay("Lives:", 3, "Level:", 2)'],
            ["Staying on the same line",
             "`say` finishes its line, so the next thing starts on a new one. `say.same` doesn't: the next "
             "thing said goes right after it. It adds exactly what you give it, so put the spaces in yourself.",
             'say.same "Loading"\nsay.same "..."\nsay " done!"'],
            ["Notes for people",
             "Everything after `#` on a line is a comment: Flaux skips it. Use comments to explain your code.",
             '# This line is skipped\nsay "This one runs"   # and this note is skipped too'],
        ],
        "challenge": {
            "task": "Make the program say your robot's name on one line, then `Beep boop!` on the next.",
            "starter": '# Say "My robot is Bolt" and then "Beep boop!"\n',
            "expected": "My robot is Bolt\nBeep boop!",
            "solution": 'say "My robot is Bolt"\nsay "Beep boop!"\n',
        },
    },
    {
        "id": "variables", "title": "Variables", "lesson": "02_variables.flx",
        "goal": "Keep values in named boxes and change them.",
        "pages": [
            ["A name for a value",
             "A variable is a name that holds a value. Make one with `set ... to ...`, and use its name "
             "wherever you want its value.",
             'set score to 0\nset name to "Sam"\nsay name, score'],
            ["Changing it",
             "Use `set` again to give it a new value, or `add` and `subtract` to change a number.",
             "set score to 10\nadd 5 to score\nsubtract 1 from score\nsay score     # 14"],
            ["Good names",
             "Names use letters, numbers and `_`, and can't start with a number: `score`, `player_2` and "
             "`highScore` are fine, `2player` isn't. Capitals matter: `Score` and `score` are different.",
             "set player_2 to 5\nsay player_2"],
            ["One kind of value each",
             "A variable keeps the same kind of value all the way through: if `score` holds a number, it "
             "can't hold text later. (A whole number can become a decimal, though.)",
             "set total to 0\nadd 0.5 to total\nsay total     # 0.5"],
        ],
        "challenge": {
            "task": "Start `coins` at 3. Add 4 coins, then spend 2 (subtract them), then say how many are left.",
            "starter": "set coins to 3\n# add 4, subtract 2, then say coins\n",
            "expected": "5",
            "solution": "set coins to 3\nadd 4 to coins\nsubtract 2 from coins\nsay coins\n",
        },
    },
    {
        "id": "maths", "title": "Maths", "lesson": "03_maths.flx",
        "goal": "Add, subtract, multiply, divide and more.",
        "pages": [
            ["The four sums",
             "`+` adds, `-` subtracts, `*` multiplies and `/` divides. `/` always gives a decimal: `7 / 2` "
             "is `3.5`.",
             "say 3 + 4\nsay 10 - 3\nsay 6 * 7\nsay 7 / 2"],
            ["Which goes first",
             "Like in school maths, `*` and `/` are done before `+` and `-`. Brackets go first of all.",
             "say 2 + 3 * 4      # 14\nsay (2 + 3) * 4    # 20"],
            ["Whole-number dividing",
             "`//` divides and rounds down to a whole number. `mod` (or `%`) gives what's left over: the "
             "remainder. `7 // 2` is `3` and `7 mod 2` is `1`.",
             "say 17 // 5     # 3\nsay 17 mod 5    # 2"],
            ["Powers",
             "`^` is a power: `2 ^ 3` is `2 * 2 * 2`, which is `8`.",
             "say 2 ^ 10      # 1024"],
        ],
        "challenge": {
            "task": "A pizza has 8 slices and 3 friends share it equally. Say how many whole slices each "
                    "gets (use `//`), then how many are left over (use `mod`).",
            "starter": "set slices to 8\nset friends to 3\n",
            "expected": "2\n2",
            "solution": "set slices to 8\nset friends to 3\nsay slices // friends\nsay slices mod friends\n",
        },
    },
    {
        "id": "text", "title": "Text", "lesson": "04_text.flx",
        "goal": "Join text, put values inside it, and look at its letters.",
        "pages": [
            ["Values inside text",
             "Put any value inside text with `{ }`. You can even do sums in there.",
             'set name to "Sam"\nset age to 12\nsay "{name} is {age}"\nsay "Next year: {age + 1}"'],
            ["Joining text",
             "`+` joins text together. Numbers are turned into text when they're joined to it.",
             'say "Hello, " + "world"\nsay "Score: " + 100'],
            ["Letters",
             "`word[1]` is the first letter (counting starts at 1). `length of word` is how many letters it has.",
             'set word to "hello"\nsay word[1]\nsay length of word'],
            ["Capitals and searching",
             "`uppercase` and `lowercase` change the letters. `contains(text, part)` says if the part is in it.",
             'say uppercase("hi")\nsay contains("hello", "ell")'],
        ],
        "challenge": {
            "task": "Set `pet` to \"Rex\" and `legs` to 4, then say `Rex has 4 legs` using `{ }`.",
            "starter": 'set pet to "Rex"\nset legs to 4\n',
            "expected": "Rex has 4 legs",
            "solution": 'set pet to "Rex"\nset legs to 4\nsay "{pet} has {legs} legs"\n',
        },
    },
    {
        "id": "ask", "title": "Asking questions", "lesson": "05_ask.flx",
        "goal": "Let the person using your program type answers.",
        "pages": [
            ["ask",
             "`ask(\"question\")` shows the question, waits for the person to type an answer and press Enter, "
             "and gives back what they typed, as text. In Flaux Studio, type it in the box under the Output.",
             'set name to ask("What\'s your name?")\nsay "Hi {name}!"'],
            ["Numbers from answers",
             "What `ask` gives back is always text, even if they typed a number. `number(...)` turns it into "
             "a decimal number and `whole(...)` into a whole number, so you can do sums with it.",
             'set age to whole(ask("How old are you?"))\nsay "Next year you\'ll be {age + 1}"'],
            ["Checking the answer",
             "`is_number(text)` says if the text is a number, so you can ask again if it isn't.",
             'set answer to ask("Pick a number")\nif is_number(answer)\n    say number(answer) * 2\nelse\n'
             '    say "That isn\'t a number"\nend'],
        ],
        "challenge": {
            "task": "Ask for a favourite food, then say `Yum, ` and the food.",
            "starter": "# ask for a favourite food\n",
            "expected": None,
            "solution": 'set food to ask("Favourite food?")\nsay "Yum, " + food\n',
        },
    },
    {
        "id": "if", "title": "Making choices", "lesson": "06_if.flx",
        "goal": "Do different things depending on a check.",
        "pages": [
            ["if",
             "The lines between `if` and `end` only run when the check is true.",
             'set score to 120\nif score > 100\n    say "Amazing!"\nend'],
            ["else",
             "`else` runs when the check is false. `else if` adds another check. Only one part runs: the "
             "first one whose check is true.",
             'set score to 70\nif score > 100\n    say "Amazing!"\nelse if score > 50\n    say "Good job"\n'
             'else\n    say "Keep trying"\nend'],
            ["Comparing",
             "`=` (or `is`) checks if two things are equal, `!=` (or `is not`) if they're different. "
             "`<`, `>`, `<=` and `>=` compare sizes, and you can write them in words: `is less than`, "
             "`is more than`, `is at most`, `is at least`.",
             'set lives to 0\nif lives = 0\n    say "Game over"\nend'],
            ["Indenting",
             "Indenting the inside of an `if` (Tab) isn't needed, but it shows at a glance what's inside. "
             "Every `if` ends with its own `end`.",
             'set age to 15\nif age >= 13\n    if age <= 19\n        say "teenager"\n    end\nend'],
        ],
        "challenge": {
            "task": "`temperature` is 30. Say `Hot` if it's more than 25, otherwise say `Not hot`.",
            "starter": "set temperature to 30\n",
            "expected": "Hot",
            "solution": 'set temperature to 30\nif temperature > 25\n    say "Hot"\nelse\n    say "Not hot"\nend\n',
        },
    },
    {
        "id": "checks", "title": "and, or, not", "lesson": None,
        "goal": "Join checks together.",
        "pages": [
            ["true and false",
             "A check like `3 > 2` gives `true` or `false`. You can say it, or keep it in a variable.",
             "say 3 > 2\nset finished to false\nsay finished"],
            ["and",
             "`a and b` is true only when both are true.",
             'set age to 15\nif age >= 13 and age <= 19\n    say "teenager"\nend'],
            ["or",
             "`a or b` is true when at least one of them is true. Each side must be a whole check: "
             "`name = \"Sam\" or name = \"Bo\"`, not `name = \"Sam\" or \"Bo\"`.",
             'set name to "Bo"\nif name = "Sam" or name = "Bo"\n    say "Hi, friend"\nend'],
            ["not",
             "`not` turns true into false and false into true.",
             'set finished to false\nif not finished\n    say "still going"\nend'],
        ],
        "challenge": {
            "task": "A ride needs you to be at least 120 tall AND at least 8 years old. With `height` 130 and "
                    "`age` 7, say `You can ride` or `Sorry`.",
            "starter": "set height to 130\nset age to 7\n",
            "expected": "Sorry",
            "solution": 'set height to 130\nset age to 7\nif height >= 120 and age >= 8\n    say "You can ride"\n'
                        'else\n    say "Sorry"\nend\n',
        },
    },
    {
        "id": "repeat", "title": "Loops", "lesson": "07_repeat.flx",
        "goal": "Do things again and again.",
        "pages": [
            ["Repeat a number of times",
             "The lines between `repeat ... times` and `end` run that many times.",
             'repeat 3 times\n    say "hip hip hooray"\nend'],
            ["Counting",
             "`repeat with i from 1 to 5` runs with `i` being 1, then 2, up to 5. Add `by` to count in "
             "steps (a minus step counts down).",
             "repeat with i from 1 to 5\n    say i\nend\nrepeat with i from 10 to 0 by -5\n    say i\nend"],
            ["Going through things",
             "`repeat with item in ...` goes through every item of a list, or every letter of a text.",
             'repeat with fruit in ["apple", "pear"]\n    say fruit\nend'],
            ["While and until",
             "`repeat while` keeps going as long as its check is true. `repeat until` keeps going until "
             "its check becomes true.",
             "set lives to 3\nrepeat while lives > 0\n    say lives\n    subtract 1 from lives\nend"],
            ["stop and skip",
             "Inside a loop, `stop` leaves it straight away, and `skip` jumps to the next time round.",
             "repeat with i from 1 to 10\n    if i = 4\n        stop\n    end\n    say i\nend"],
        ],
        "challenge": {
            "task": "Add up all the numbers from 1 to 10 with a loop, and say the total.",
            "starter": "set total to 0\n",
            "expected": "55",
            "solution": "set total to 0\nrepeat with i from 1 to 10\n    add i to total\nend\nsay total\n",
        },
    },
    {
        "id": "lists", "title": "Lists", "lesson": "08_lists.flx",
        "goal": "Keep many values together in order.",
        "pages": [
            ["Making a list",
             "A list is values in square brackets. Every item is the same kind.",
             'set nums to [3, 1, 4]\nset names to ["Sam", "Bo"]\nsay nums'],
            ["Items",
             "`nums[1]` is the first item: counting starts at 1, like on your fingers. Change an item with "
             "`set nums[1] to ...`.",
             "set nums to [3, 1, 4]\nsay nums[1]\nset nums[1] to 100\nsay nums"],
            ["Adding and counting",
             "`add x to list` puts an item on the end. `length of list` is how many items it has, and "
             "`contains(list, x)` says if it's there.",
             "set nums to [3, 1]\nadd 4 to nums\nsay length of nums\nsay contains(nums, 4)"],
            ["Many copies",
             "`5 copies of 0` makes `[0, 0, 0, 0, 0]`. Start an empty list with `[]`.",
             'set scores to 3 copies of 0\nset names to []\nadd "Sam" to names\nsay scores, names'],
        ],
        "challenge": {
            "task": "Make a list of the numbers 4, 8 and 15, add 16 to the end, then say the list and how "
                    "many items it has, each on its own line.",
            "starter": "",
            "expected": "[4, 8, 15, 16]\n4",
            "solution": "set nums to [4, 8, 15]\nadd 16 to nums\nsay nums\nsay length of nums\n",
        },
    },
    {
        "id": "functions", "title": "Functions", "lesson": "09_functions.flx",
        "goal": "Make your own instructions.",
        "pages": [
            ["Your own instruction",
             "`function name(inputs)` ... `end` makes a new instruction. Use it by writing its name with "
             "the inputs in brackets.",
             'function greet(name)\n    say "Hello, {name}!"\nend\n\ngreet("Sam")\ngreet("Bo")'],
            ["Giving back a value",
             "`return` gives a value back to where the function was used, and leaves the function.",
             "function area(width, height)\n    return width * height\nend\n\nsay area(3, 4)"],
            ["Only their own inputs",
             "A function only sees its inputs and its own variables. To use a value from outside, pass it "
             "in; to change it, give back the new value. (Lists are different: changing a list you were "
             "given changes the original.)",
             "function lose_life(lives)\n    return lives - 1\nend\n\nset lives to 3\nset lives to lose_life(lives)\nsay lives"],
            ["Calling itself",
             "A function can use itself. This is called recursion.",
             "function factorial(n)\n    if n <= 1\n        return 1\n    end\n    return n * factorial(n - 1)\nend\n\nsay factorial(5)"],
        ],
        "challenge": {
            "task": "Write a function `triple(n)` that gives back `n * 3`, and use it to say `triple(7)`.",
            "starter": "",
            "expected": "21",
            "solution": "function triple(n)\n    return n * 3\nend\n\nsay triple(7)\n",
        },
    },
    {
        "id": "builtins", "title": "Built-in functions", "lesson": None,
        "goal": "Use the instructions that come with Flaux.",
        "pages": [
            ["Rounding",
             "`round(x)` rounds to the nearest whole number, `floor(x)` rounds down and `ceil(x)` rounds up.",
             "say round(2.6)\nsay floor(2.6)\nsay ceil(2.1)"],
            ["Biggest, smallest, size",
             "`min(...)` and `max(...)` give the smallest and the biggest. `abs(x)` takes away the minus sign. "
             "`sqrt(x)` is the square root.",
             "say max(3, 9, 4)\nsay min(3, 9, 4)\nsay abs(-5)\nsay sqrt(16)"],
            ["Changing kinds",
             "`text(x)` turns a value into text, `number(t)` turns text into a decimal and `whole(x)` into "
             "a whole number (it drops what's after the point: `whole(3.9)` is `3`).",
             'say text(42) + "!"\nsay number("3.5") * 2\nsay whole(3.9)'],
            ["How far",
             "`distance(x1, y1, x2, y2)` says how far apart two points are.",
             "say distance(0, 0, 3, 4)"],
        ],
        "challenge": {
            "task": "Say the biggest of 12, 7 and 30, then 7.6 rounded to the nearest whole number.",
            "starter": "",
            "expected": "30\n8",
            "solution": "say max(12, 7, 30)\nsay round(7.6)\n",
        },
    },
    {
        "id": "random", "title": "Random and waiting", "lesson": "10_random.flx",
        "goal": "Surprise the player, and take your time.",
        "pages": [
            ["Random numbers",
             "`random(1, 6)` gives a random whole number from 1 to 6, both included: like a dice.",
             "say random(1, 6)"],
            ["Random items",
             "`random(list)` picks a random item from the list.",
             'say random(["rock", "paper", "scissors"])'],
            ["Waiting",
             "`wait` pauses. Glue a unit to the number: `s` seconds, `m` minutes, `h` hours, `d` days. For a "
             "variable, write the unit as a word: `wait delay seconds`. `wait 5` alone is a mistake: 5 what?",
             'say "Ready..."\nwait 1s\nsay "Go!"'],
        ],
        "challenge": {
            "task": "Roll two dice (two random numbers from 1 to 6) and say their total.",
            "starter": "",
            "expected": None,
            "solution": "set a to random(1, 6)\nset b to random(1, 6)\nsay a + b\n",
        },
    },
    {
        "id": "graphics", "title": "Drawing", "lesson": "11_graphics.flx",
        "goal": "Open a window and draw shapes and text.",
        "pages": [
            ["The window",
             "`window(800, 600)` opens a window 800 pixels wide and 600 tall. Places are measured from the "
             "top-left corner: `x` goes right and `y` goes DOWN.",
             'window(800, 600, "My picture")'],
            ["Shapes",
             "`rect(x, y, width, height, colour)` draws a rectangle from its top-left corner. "
             "`circle(x, y, radius, colour)` draws a circle around its centre. There's also `line`, "
             "`triangle` and `dot`.",
             'window(400, 300)\nclear("sky")\nrect(0, 250, 400, 50, "green")\ncircle(320, 60, 30, "yellow")'],
            ["Words",
             "`write(value, x, y, colour, size)` draws text in the window.",
             'window(400, 300)\nwrite("Hello!", 20, 20, "white", 4)'],
            ["Colours",
             "Use a colour name like `\"red\"` or `\"dark green\"`, `rgb(255, 136, 0)` (red, green and blue "
             "from 0 to 255) or a code like `\"#ff8800\"`.",
             'window(300, 200)\nclear(rgb(20, 20, 60))\ncircle(150, 100, 50, "#ff8800")'],
        ],
        "challenge": {
            "task": "Draw a house: a square for the walls and a triangle for the roof.",
            "starter": 'window(400, 400)\nclear("sky")\n',
            "expected": None,
            "solution": 'window(400, 400)\nclear("sky")\nrect(100, 200, 200, 150, "brown")\n'
                        'triangle(80, 200, 320, 200, 200, 100, "red")\n',
        },
    },
    {
        "id": "games", "title": "Games", "lesson": None,
        "goal": "Make things move, with the keyboard and the mouse.",
        "pages": [
            ["The game loop",
             "A game draws a new picture many times a second. Put the drawing in `repeat forever` and end "
             "each picture with `show()`. `show()` waits so the loop runs 60 times a second.",
             'window(400, 300)\nset x to 0\nrepeat forever\n    add 2 to x\n    clear()\n    circle(x, 150, 20, "red")\n    show()\nend'],
            ["Keys",
             "`key_down(\"left\")` is true while the key is held: use it for smooth moving. "
             "`key_pressed(\"space\")` is true only on the frame the key goes down: use it for jumping or firing.",
             'window(400, 300)\nset x to 200\nrepeat forever\n    if key_down("left")\n        subtract 4 from x\n    end\n'
             '    if key_down("right")\n        add 4 to x\n    end\n    clear()\n    rect(x, 250, 40, 20, "blue")\n    show()\nend'],
            ["The mouse",
             "`mouse_x()` and `mouse_y()` say where the mouse is. `mouse_down()` is true while the left button "
             "is held, and `mouse_clicked()` only on the frame of a click. Add `\"right\"` for the right button.",
             'window(400, 300)\nrepeat forever\n    clear()\n    circle(mouse_x(), mouse_y(), 10, "yellow")\n    show()\nend'],
            ["Did they touch?",
             "Two circles touch when the distance between their centres is less than their two radiuses "
             "added together.",
             "set score to 0\nset px to 100\nset py to 100\nset cx to 120\nset cy to 110\n"
             "if distance(px, py, cx, cy) < 20 + 10\n    add 1 to score\nend\nsay score"],
        ],
        "challenge": {
            "task": "Make a ball that moves across the window from left to right.",
            "starter": 'window(400, 300)\nset x to 0\nrepeat forever\n    clear()\n    show()\nend\n',
            "expected": None,
            "solution": 'window(400, 300)\nset x to 0\nrepeat forever\n    add 3 to x\n    clear()\n'
                        '    circle(x, 150, 20, "red")\n    show()\nend\n',
        },
    },
    {
        "id": "pictures", "title": "Pictures and sound", "lesson": "12_pictures.flx",
        "goal": "Draw picture files and make beeps.",
        "pages": [
            ["Pictures",
             "`image(\"ship.png\", x, y)` draws a picture file with its top-left corner at `x, y`. Put the "
             "file in the same folder as your program. PNG, JPG, BMP and GIF work.",
             'window(400, 300)\nimage("ship.png", 100, 100)'],
            ["Bigger, flipped, turned",
             "Give a width and height to stretch it. A minus width or height flips it. A last number turns it "
             "that many degrees.",
             'window(400, 300)\nimage("ship.png", 50, 50, 64, 64)\nimage("ship.png", 150, 50, -64, 64)\n'
             'image("ship.png", 250, 50, 64, 64, 45)'],
            ["Beeps",
             "`beep(pitch, seconds)` plays a note. 440 is the note A; bigger numbers are higher. Beeps play "
             "one after another, so several make a tune.",
             "beep(440, 0.2)\nbeep(660, 0.2)\nbeep(880, 0.4)"],
        ],
        "challenge": {
            "task": "Play a little tune of three beeps that go up.",
            "starter": "",
            "expected": None,
            "solution": "beep(262, 0.2)\nbeep(330, 0.2)\nbeep(392, 0.4)\n",
        },
    },
    {
        "id": "3d", "title": "3D", "lesson": "13_3d.flx",
        "goal": "Build a 3D world with shapes, a camera and light.",
        "pages": [
            ["Three directions",
             "In 3D, `x` goes right, `y` goes UP and `z` comes towards you. Sizes are in steps: a cube of "
             "size 1 is one step wide. Every shape is placed by its middle.",
             'window(800, 600)\nrepeat forever\n    cube(0, 0.5, 0, 1, "red")\n    show()\nend'],
            ["Shapes",
             "`cube`, `box`, `ball`, `cylinder`, `cone` and `plane` (a flat floor). The last input is the "
             "paint: a colour, a picture file or a shader.",
             'window(800, 600)\nrepeat forever\n    plane(0, 0, 0, 20, 20, "green")\n    ball(0, 1, 0, 1, "orange")\n    show()\nend'],
            ["The camera",
             "`camera(x, y, z, look_x, look_y, look_z)` is where you see from and the point you look at.",
             'window(800, 600)\nrepeat forever\n    camera(5, 4, 5, 0, 0, 0)\n    cube(0, 0.5, 0, 1, "red")\n    show()\nend'],
            ["Sky, sun and lamps",
             "`sky(colour)` paints the sky, `sun(x, y, z)` is where sunlight comes from (it makes shadows), "
             "`light(...)` adds a lamp and `fog(distance)` fades things far away.",
             'window(800, 600)\nrepeat forever\n    sky("navy")\n    sun(0, 0, 0)\n    light(0, 2, 0, "orange")\n'
             '    plane(0, 0, 0, 20, 20, "gray")\n    show()\nend'],
        ],
        "challenge": {
            "task": "Make a little 3D scene: a green floor with a tower of 3 cubes on it.",
            "starter": 'window(800, 600)\nrepeat forever\n    plane(0, 0, 0, 20, 20, "green")\n    show()\nend\n',
            "expected": None,
            "solution": 'window(800, 600)\nrepeat forever\n    plane(0, 0, 0, 20, 20, "green")\n'
                        '    repeat with i from 1 to 3\n        cube(0, i - 0.5, 0, 1, "red")\n    end\n    show()\nend\n',
        },
    },
    {
        "id": "shaders", "title": "Shaders", "lesson": "14_shaders.flx",
        "goal": "Paint with the graphics card.",
        "pages": [
            ["A tiny program for every pixel",
             "A shader runs on the graphics card, once for every pixel, and gives back a colour. Write it "
             "outside any function, with `shader name` ... `end`.",
             "shader glow\n    set brightness to sin(time * 3) * 0.5 + 0.5\n    return rgb(255 * brightness, 120, 40)\nend"],
            ["Using it",
             "Use a shader's name in place of a colour on a shape, or for the whole screen with `effect(name)` "
             "before `show()`.",
             "shader stripes\n    if floor(y * 4) mod 2 = 0\n        return \"white\"\n    end\n    return \"red\"\nend\n\n"
             "window(800, 600)\nrepeat forever\n    cube(0, 0.5, 0, 1, stripes)\n    show()\nend"],
            ["What a shader knows",
             "On shapes: `x`, `y`, `z` (the point), `u`, `v` (where on the face) and `time`. In `effect`: "
             "`x`, `y`, `width`, `height` and the pixel's `red`, `green` and `blue`. Shaders can't use lists, "
             "text, `say` or your own functions.",
             "shader night_vision\n    return rgb(0, (red + green + blue) / 3, 0)\nend"],
        ],
        "challenge": {
            "task": "Make a shader that paints a cube in a colour that changes with `time`, and use it.",
            "starter": "window(800, 600)\nrepeat forever\n    show()\nend\n",
            "expected": None,
            "solution": "shader pulse\n    return rgb(128 + 127 * sin(time), 50, 200)\nend\n\nwindow(800, 600)\n"
                        "repeat forever\n    cube(0, 0.5, 0, 1, pulse)\n    show()\nend\n",
        },
    },
    {
        "id": "mistakes", "title": "Finding mistakes", "lesson": None,
        "goal": "Read Flaux's messages and fix your program.",
        "pages": [
            ["Flaux helps",
             "When something's wrong, Flaux Studio underlines the line, says what's wrong in plain words and "
             "often has an Apply fix button. Read the message: it usually says exactly what to change.",
             "set score to 10\nsay scroe     # I don't know what `scroe` is. Did you mean `score`?"],
            ["The usual suspects",
             "A missing `end`, a misspelled name, a missing quote `\"`, `wait 5` without a unit, or "
             "`x = 5` instead of `set x to 5`.",
             'set x to 5\nif x > 3\n    say "big"\nend'],
            ["Problems while running",
             "Some problems only show up while the program runs, like dividing by zero or asking for item 10 "
             "of a list with 3. The program stops and the line it stopped on is lit up.",
             "set nums to [1, 2, 3]\nsay nums[3]     # fine: nums[10] would stop the program"],
        ],
        "challenge": {
            "task": "This program has three mistakes. Fix them so it says `3`, `2`, `1` and `Liftoff!`.",
            "starter": 'set count to 3\nrepeat while count > 0\n    say cont\n    subtract 1 from count\n'
                       'say "Liftoff!\n',
            "expected": "3\n2\n1\nLiftoff!",
            "solution": 'set count to 3\nrepeat while count > 0\n    say count\n    subtract 1 from count\nend\n'
                        'say "Liftoff!"\n',
        },
    },
]
