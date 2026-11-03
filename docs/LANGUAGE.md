# The Flaux Language Guide

Flaux reads like plain English but compiles to fast native code. This guide
covers everything in Flaux 0.1.

- [Running programs](#running-programs)
- [Saying things](#saying-things)
- [Variables](#variables)
- [Kinds of values](#kinds-of-values)
- [Maths](#maths)
- [Text](#text)
- [Making choices: if](#making-choices-if)
- [Checks and comparisons](#checks-and-comparisons)
- [Loops: repeat](#loops-repeat)
- [Waiting](#waiting)
- [Lists](#lists)
- [Functions](#functions)
- [Built-in functions](#built-in-functions)
- [Graphics and games](#graphics-and-games)
- [Comments](#comments)
- [When things go wrong](#when-things-go-wrong)
- [Why Flaux is fast](#why-flaux-is-fast)
- [Special words](#special-words)

---

## Running programs

Flaux programs are text files ending in `.flx`.

```bash
./flaux hello.flx            # run a program
./flaux run hello.flx        # the same thing, written out in full
./flaux build hello.flx      # make a program called "hello" you can share
./flaux check hello.flx      # look for mistakes without running
./flaux c hello.flx          # see the C code Flaux makes
```

You need a C compiler (gcc or clang) installed. Flaux uses it to turn
your program into machine code.

## Saying things

```
say "Hello, world!"
say 42
say 2 + 2
```

Brackets are fine too, if you're used to `print(...)` from other languages:

```
say("Hello, world!")
```

Give `say` several things, separated by commas, and it shows them on one
line with a space between each:

```
say "Score:", 10              # Score: 10
say("Lives:", 3, "Level:", 2) # Lives: 3 Level: 2
say()                         # an empty line
```

You can also put values inside text with `{ }` (see [Text](#text)).

## Variables

A variable is a name that holds a value. Make one with `set ... to ...`:

```
set score to 0
set name to "Sam"
set alive to true
```

Change it with `set` again, or with `add` and `subtract`:

```
set score to 10
add 5 to score          # score is now 15
subtract 1 from score   # score is now 14
```

Names use letters, numbers and `_`, and can't start with a number:
`score`, `player_2`, `highScore`.

A variable keeps **the same kind of value** for the whole program. If
`score` holds a number, it can't later hold text. The one exception: a
whole number can become a decimal.

```
set total to 0
add 0.5 to total     # fine: total becomes a decimal
```

## Kinds of values

| Kind | Examples | Notes |
|---|---|---|
| whole number | `0`, `42`, `-7`, `1_000_000` | 64-bit, up to about 9 quintillion |
| decimal number | `3.14`, `0.5`, `1.5e20` | |
| text | `"hello"`, `"Score: {score}"` | always double quotes |
| true/false | `true`, `false` | |
| list | `[1, 2, 3]`, `["a", "b"]`, `[]` | every item is the same kind |

You never write these kinds down. Flaux works them out by itself.

## Maths

| Write | Means | Example |
|---|---|---|
| `+ - *` | add, subtract, multiply | `2 * (3 + 4)` is `14` |
| `/` | divide (always gives a decimal) | `7 / 2` is `3.5` |
| `//` | whole-number divide (rounds down) | `7 // 2` is `3` |
| `mod` or `%` | remainder | `7 mod 3` is `1` |
| `^` | power | `2 ^ 10` is `1024` |

Dividing by zero stops the program with a clear message instead of
crashing or giving a strange answer.

## Text

Text goes in double quotes. Put any value inside text with `{ }`:

```
set name to "Sam"
set age to 12
say "{name} is {age} years old"
say "Next year {name} will be {age + 1}"
```

Join text with `+`. Numbers get turned into text automatically:

```
say "Score: " + 100
```

Special characters: `\n` (new line), `\t` (tab), `\"` (a quote mark),
`\{` and `\}` (curly brackets), `\\` (a backslash).

Get one letter with `[ ]`. The first letter is number 1:

```
set word to "hello"
say word[1]              # h
say length of word       # 5
```

## Making choices: if

```
if score > 100
    say "Amazing!"
else if score > 50
    say "Good job"
else
    say "Keep trying"
end
```

Every `if` finishes with `end`. Indenting the inside isn't required, but it
makes your program much easier to read. `then` after the check is allowed
if you like it: `if score > 100 then`.

## Checks and comparisons

| Write | Means |
|---|---|
| `a = b` or `a is b` | equal |
| `a != b` or `a is not b` | not equal |
| `a < b` or `a is less than b` | smaller |
| `a > b` or `a is more than b` | bigger |
| `a <= b` or `a is at most b` | smaller or equal |
| `a >= b` or `a is at least b` | bigger or equal |

Join checks with `and`, `or` and `not`:

```
if age >= 13 and age <= 19
    say "teenager"
end

if not finished
    say "still going"
end
```

Each side of `and`/`or` must be a complete check: write
`name = "Sam" or name = "Bo"`, not `name = "Sam" or "Bo"`.

## Loops: repeat

```
repeat 3 times
    say "hip hip hooray"
end

repeat with i from 1 to 10
    say i
end

repeat with i from 10 to 0 by -2     # count down in steps of 2
    say i
end

repeat with fruit in ["apple", "pear"]
    say fruit
end

repeat with letter in "hello"
    say letter
end

repeat while lives > 0
    subtract 1 from lives
end

repeat until done
    ...
end

repeat forever
    ...
end
```

Inside any `repeat`:
- `stop` leaves the loop straight away.
- `skip` jumps to the next time around.

## Waiting

`wait` pauses the program for a while. Glue a unit to the number: `s` for
seconds, `m` for minutes, `h` for hours and `d` for days.

```
wait 1s      # one second
wait 0.5s    # half a second
wait 2m      # two minutes
wait 1h      # an hour
wait 1d      # a day
```

For a variable or a sum, write the unit as a word after it: `seconds`,
`minutes`, `hours` or `days` (`second`, `minute`, `hour` and `day` work too):

```
set delay to 3
wait delay seconds
wait delay * 2 minutes
wait 1 hour
```

`wait 5` on its own is a mistake, because Flaux can't tell if you mean 5
seconds or 5 minutes. The time can be a decimal, and it can't be negative.

Everything the program said before a `wait` is shown straight away. In a
game, the window keeps working while the program waits, and closing it still
ends the program. `wait` doesn't put the picture on screen: call `show()`
first if you want to see what you drew. (For a game loop, use `show()`, which
already waits for the next frame.) In Flaux Studio, the Stop button stops a
wait straight away.

## Lists

```
set nums to [3, 1, 4]
add 1 to nums            # put an item on the end: [3, 1, 4, 1]
add [5, 9] to nums       # add every item of another list
say nums[1]              # the first item: 3
set nums[1] to 100       # change an item
say length of nums       # 6
say contains(nums, 4)    # true
say nums + [2, 6]        # a new, joined list
```

Item numbers start at **1**, like counting on your fingers. Asking for an
item that isn't there stops the program with a clear message.

Make a list of the same value many times with `copies of`:

```
set scores to 10 copies of 0
set grid to 3 copies of (3 copies of ".")    # a 3-by-3 grid
set grid[2][2] to "X"
```

Start an empty list with `[]`. Flaux works out what kind of items it holds
from what you add to it:

```
set names to []
add "Sam" to names       # now Flaux knows names holds text
```

## Functions

A function is your own instruction. Make it with `function`, give its
inputs names in brackets, and finish it with `end`:

```
function greet(name)
    say "Hello, {name}!"
end

greet("Sam")
```

Use `return` to give back a value:

```
function area(width, height)
    return width * height
end

say area(3, 4)       # 12
say area(2.5, 1.5)   # 3.75
```

The same function works with whole numbers, decimals, text or lists. Flaux
makes a separate fast version for each kind it's used with.

Functions can call themselves:

```
function factorial(n)
    if n <= 1
        return 1
    end
    return n * factorial(n - 1)
end
```

Functions can be written anywhere in the file (above or below the code
that uses them), but not inside another block.

**Functions only see their own inputs and variables.** To use a value from
outside, pass it in. To change it, give back the new value:

```
function lose_life(lives)
    return lives - 1
end

set lives to 3
set lives to lose_life(lives)
```

Lists are different: a function that changes a list it was given changes
the caller's list too.

```
function fill(basket)
    add "apple" to basket
end

set basket to []
fill(basket)
say basket     # ["apple"]
```

## Built-in functions

| Function | Gives back | Example |
|---|---|---|
| `ask(prompt)` | the text the user types | `set name to ask("Name?")` |
| `random(a, b)` | a random number from a to b | `random(1, 6)` |
| `random(list)` | a random item | `random(["rock", "paper", "scissors"])` |
| `length(x)` or `length of x` | the size of a list or text | `length of nums` |
| `contains(x, item)` | true if the list or text has it | `contains("hello", "ell")` |
| `text(x)` | x as text | `text(42)` |
| `number(t)` | text as a decimal number | `number("3.5")` |
| `whole(x)` | text or a decimal as a whole number | `whole("42")`, `whole(3.9)` is 3 |
| `is_number(t)` | true if the text is a number | `is_number("42")` |
| `round(x)`, `floor(x)`, `ceil(x)` | rounded whole numbers | `round(2.5)` is 3 |
| `abs(x)` | x without its minus sign | `abs(-5)` |
| `min(a, b, ...)`, `max(a, b, ...)` | the smallest or biggest | `max(3, 9, 4)` |
| `sqrt(x)` | square root | `sqrt(16)` |
| `sin(x)`, `cos(x)`, `tan(x)`, `log(x)` | maths functions (radians) | `sin(0.5)` |
| `uppercase(t)`, `lowercase(t)` | text in capitals or small letters | `uppercase("hi")` |
| `clock()` | seconds since the program started | `say clock()` |
| `distance(x1, y1, x2, y2)` | how far apart two points are | `distance(0, 0, 3, 4)` is 5 |
| `rgb(r, g, b)` | a colour made of red, green and blue (0 to 255) | `rgb(255, 136, 0)` |

## Graphics and games

Graphics need the free SDL2 library, installed once
(`sudo apt install libsdl2-dev` on Linux and WSL, `brew install sdl2` on a
Mac). Programs that don't draw anything don't need it.

### The window

```
window(800, 600)              # 800 pixels wide, 600 tall
window(800, 600, "My Game")   # with a title
```

Open the window before drawing anything. Places are measured in pixels
from the **top-left corner**: `x` goes right, `y` goes **down**. So the
top-left corner is `0, 0` and the bottom-right of an 800 by 600 window is
`800, 600`.

### Fullscreen

Any game can be played on the whole screen. The picture is made bigger to
fit, keeping its shape (with black bars at the sides or top and bottom if
the screen is a different shape), and **your program doesn't change**: the
window is still 800 by 600 to it, and `mouse_x()` and `mouse_y()` still give
places in those pixels.

- **Windows app** (and `flaux run`): press **F11** (or **Alt+Enter**) to switch
  fullscreen on and off. These keys are kept for that, so `key_down("f11")`
  is never true, and neither is `"enter"` while Alt is held.
- **Flaux Studio on the web**: click **&#9974; Fullscreen** under the game (or
  double-click the game's name). **Esc** leaves fullscreen, so in a browser
  the game doesn't get Esc while it's fullscreen. Pressing Stop, or a problem
  in the program, also leaves it.
- **From the program**: `fullscreen()` (after `window(...)`) switches to
  fullscreen. The Windows app does it straight away. Browsers only allow
  fullscreen right after a click, so on the web the Fullscreen button lights
  up and the next click on the game goes fullscreen.

```
window(800, 600, "My Game")
fullscreen()
```

### Drawing

| Function | Draws |
|---|---|
| `clear(colour)` | fills the window (black if you leave out the colour) |
| `rect(x, y, width, height, colour)` | a filled rectangle; `x, y` is the top-left corner |
| `circle(x, y, radius, colour)` | a filled circle; `x, y` is the centre |
| `triangle(x1, y1, x2, y2, x3, y3, colour)` | a filled triangle |
| `line(x1, y1, x2, y2, colour, thickness)` | a line; thickness can be left out |
| `dot(x, y, colour)` | a single pixel |
| `write(value, x, y, colour, size)` | text (or any value); size 2 if left out |
| `text_width(value, size)` | how many pixels wide `write` would make it |
| `screenshot("file.bmp")` | saves what's drawn so far as a picture |
| `fullscreen()` | makes the window fill the screen (see [Fullscreen](#fullscreen)) |

The colour can always be left out (shapes and text are white by default).

Numbers can be whole or decimal: `circle(100.5, 80.25, 10)` is fine.

### The game loop

Games draw a new picture many times a second. Put the drawing in a
`repeat forever` loop and finish each picture with `show()`:

```
repeat forever
    # 1. change things (move, check keys, check if things touch)
    # 2. draw everything, starting with clear(...)
    show()
end
```

`show()` puts the picture on screen, then waits so the loop runs **60 times
a second** on every computer. Closing the window ends the program.

If a program draws a picture **without** a loop, the window stays open
showing it until you close it.

### Keyboard and mouse

| Function | Gives back |
|---|---|
| `key_down(name)` | true while the key is held down |
| `key_pressed(name)` | true only on the frame the key goes down |
| `mouse_x()`, `mouse_y()` | where the mouse is in the window |
| `mouse_down()` | true while the left mouse button is held |
| `mouse_clicked()` | true only on the frame of a click |

Key names: `"a"` to `"z"`, `"0"` to `"9"`, `"left"`, `"right"`, `"up"`,
`"down"`, `"space"`, `"enter"`, `"escape"`, `"tab"`, `"backspace"`,
`"delete"`, `"shift"`, `"ctrl"`, `"alt"`, `"home"`, `"end"`, `"pageup"`,
`"pagedown"`, `"insert"` and `"f1"` to `"f12"`.

Use `key_down` for smooth movement, and `key_pressed` for things that
should happen once per press, like jumping or firing.

### Pictures

```
image("ship.png", x, y)                 # the picture at its own size
image("ship.png", x, y, 64, 64)         # stretched to 64 by 64 pixels
image("ship.png", x, y, -64, 64)        # flipped (a minus width or height flips it)
image("ship.png", x, y, 64, 64, 45)     # turned 45 degrees around its middle
say image_width("ship.png")             # its real size
say image_height("ship.png")
```

`x, y` is the top-left corner. Pictures can be `.png`, `.jpg`, `.bmp` or
`.gif`; PNG pictures can have see-through parts. Put picture files in the
**same folder as your program**. Flaux looks there first, wherever the
program is started from. If the name is misspelled, Flaux says so before the
program runs, and suggests the right name if a similar file is there.
Pictures are loaded once and then reused, so drawing them every frame is
fast. Small pixel art stays sharp when made bigger.

### Sound

```
beep(440, 0.2)    # pitch 440 (the note A) for 0.2 seconds
beep(880)         # a short beep, 0.1 seconds
```

Beeps play one after another, so several in a row make a tune.

### Colours

Colour names: `black`, `white`, `gray`, `dark gray`, `light gray`, `red`,
`dark red`, `orange`, `yellow`, `gold`, `green`, `dark green`, `lime`,
`blue`, `dark blue`, `navy`, `sky`, `light blue`, `cyan`, `teal`,
`purple`, `violet`, `pink`, `magenta`, `brown`, `beige`.
(`grey` works too, and spaces don't matter: `"darkgreen"` is fine.)

Any other colour: `rgb(255, 136, 0)` or a code like `"#ff8800"`.

### Did they touch?

`distance(x1, y1, x2, y2)` says how far apart two points are. Two circles
touch when the distance between their centres is less than their two
radiuses added together:

```
if distance(player_x, player_y, coin_x, coin_y) < 20 + 10
    add 1 to score
end
```

For rectangles, compare their edges:

```
if ball_x > paddle_x and ball_x < paddle_x + 100 and ball_y > 550
    say "hit the paddle"
end
```

### Testing graphics without a screen

These settings help when running graphics programs in automated tests:

| Setting | Does |
|---|---|
| `FLAUX_FRAMES=60` | stop after 60 frames |
| `FLAUX_FAST=1` | don't wait between frames |
| `FLAUX_SCREENSHOT=last.bmp` | save the last frame when stopping |
| `FLAUX_HOLD_KEYS=left,space` | pretend these keys are held down |
| `SDL_VIDEODRIVER=offscreen` | draw without a real screen |

## Comments

Everything after `#` on a line is ignored by Flaux. Use it to leave notes:

```
# This is a comment
set lives to 3   # so is this
```

## When things go wrong

Flaux errors don't just say what's wrong. They teach you how to fix it,
and when Flaux can, they show your line already fixed:

```
Problem in game.flx, line 2:

  2 | if name = "Sam" or "Bo"
    |                    ^^^^

Each side of `or` must be a complete check, but this is just text.

How to fix it:
  Computers need the whole comparison repeated on each side. Change it to:

  2 | if name = "Sam" or name = "Bo"
```

Flaux recognises habits from other languages (`print`, `while`, `x += 1`,
`&&`, `:` at the end of a line, single quotes...) and shows the Flaux way.

Problems that only show up while the program runs, like dividing by zero or
asking for item 7 of a 3-item list, stop the program with the same kind of
message instead of crashing.

## Why Flaux is fast

1. **Flaux works out every type before running.** You never write types,
   but the compiler knows exactly what's in every variable, so the finished
   program never has to check.
2. **Each function is specialised.** `area(3, 4)` and `area(2.5, 1.5)`
   become two separate machine-code functions, one for whole numbers and one
   for decimals.
3. **It compiles to native code.** Flaux turns your program into C and
   uses an optimising C compiler (gcc or clang) to make machine code. There
   is no interpreter, virtual machine or garbage collector.

Flaux still checks list positions and division by zero, which costs very
little. See [benchmarks/RESULTS.md](../benchmarks/RESULTS.md) for how it
compares with C++, C# and Python.

## Special words

These words have special meanings, so they can't be used as names:

`add` `and` `else` `end` `false` `function` `if` `is` `mod` `not` `or`
`repeat` `return` `say` `set` `skip` `stop` `subtract` `true` `wait`

Built-in function names (like `window`, `circle` or `random`) can be used
for variables, but not for your own functions.
