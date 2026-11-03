# Flaux

**A programming language that reads like English and runs like C++.**

```
function fib(n)
    if n < 2
        return n
    end
    return fib(n - 1) + fib(n - 2)
end

set name to ask("What's your name?")
say "Hi {name}! fib(40) is {fib(40)}"
```

No types to write, no semicolons, no curly brackets. Flaux works out every
type by itself, compiles to native machine code, and runs about as fast as
C++ and faster than C#.

## Why Flaux

- **Easy to read.** `set`, `say`, `add 5 to score`, `repeat 10 times`,
  `if lives is more than 0`. Every line maps cleanly to a block, so a
  drag-and-drop editor can sit on top later.
- **Fast.** Types are worked out ahead of time, every function is
  specialised for the values it's used with, and the result is compiled
  by an optimising C compiler. No interpreter, no virtual machine, no
  garbage collector.
- **Errors that teach.** Flaux doesn't just say what's wrong. It explains
  how to fix it, and usually shows your own line already corrected:

```
Problem in game.flx, line 2:

  2 | if name = "Sam" or "Bo"
    |                    ^^^^

Each side of `or` must be a complete check, but this is just text.

How to fix it:
  Computers need the whole comparison repeated on each side. Change it to:

  2 | if name = "Sam" or name = "Bo"
```

It spots habits from other languages too (`print(...)`, `x += 1`, `&&`,
`while`, `:` at the end of a line, single quotes) and shows the Flaux way.

## Speed

The same five programs written in Flaux, C++, C# and Python
(see [benchmarks/](benchmarks/) to run them yourself):

| Benchmark | Tests | Flaux | C++ | C# | Python |
|---|---|---|---|---|---|
| fib | recursive function calls | **0.17s** | 0.17s | 0.57s | 12.15s |
| sieve | loops + list reads/writes | **0.29s** | 0.30s | 0.34s | 11.18s |
| collatz | whole-number maths | **0.73s** | 0.79s | 0.92s | 54.09s |
| mandelbrot | decimal maths | **0.26s** | 0.27s | 0.28s | 16.47s |
| lists | growing a big list | **0.21s** | 0.35s | 0.46s | 5.58s |

<sub>Linux x86_64, wall-clock time including start-up, best of 5 runs. C++ built with `g++ -O2`, C# with .NET 8 Release, Python 3.13. Full results: [benchmarks/RESULTS.md](benchmarks/RESULTS.md).</sub>

Flaux and C++ are neck and neck: both end up as the same kind of machine
code. Flaux's small wins come from tricks a C++ programmer could also use by
hand (like turning `n // 2` into a single instruction); Flaux just does them
for you. Flaux beats C# on every test, partly because C# has to start its
runtime and compile code while it runs, and there's no garbage collector.
Flaux is 25–70 times faster than Python.

Flaux still checks every list position and division, so a mistake gives a
clear message instead of a crash. Those checks cost very little.

## Getting started

You need Python 3.8+ and a C compiler (gcc or clang).

```bash
git clone https://github.com/XOBNVR/Flaux-Code.git
cd Flaux-Code
./flaux run examples/hello.flx
```

| Command | What it does |
|---|---|
| `./flaux run game.flx` | compile and run a program |
| `./flaux build game.flx` | make a standalone program you can share |
| `./flaux check game.flx` | look for mistakes without running |
| `./flaux c game.flx` | show the C code Flaux makes |

## A taste of Flaux

```
# Variables and text
set score to 0
add 10 to score
say "Score: {score}"

# Choices
if score is at least 10
    say "Level up!"
else
    say "Keep going"
end

# Loops
repeat with i from 1 to 5
    say i * i
end

# Lists
set fruit to ["apple", "pear"]
add "plum" to fruit
repeat with f in fruit
    say uppercase(f)
end

# Functions work with any kind of value
function double(x)
    return x * 2
end
say double(21)     # 42
say double(1.5)    # 3
```

The full guide is in [docs/LANGUAGE.md](docs/LANGUAGE.md), and there are
more programs in [examples/](examples/).

## How it works

```
your .flx file → lexer → parser → type inference + specialisation → C code → gcc/clang → machine code
```

| File | Job |
|---|---|
| `flauxc/lexer.py` | splits source text into words and symbols |
| `flauxc/parser.py` | builds the syntax tree, catches grammar mistakes |
| `flauxc/compiler.py` | works out every type, specialises functions, writes C |
| `flauxc/runtime.h` | the small C library every program uses (text, lists, safe maths, errors) |
| `flauxc/errors.py` | turns problems into teaching error messages |
| `flauxc/cli.py` | the `flaux` command |

## Tests

```bash
python3 tests/run_tests.py
```

`tests/programs/` holds programs and their exact expected output.
`tests/errors/` holds programs with mistakes, and checks that each error
explains the problem and shows the right fix.

## Status and roadmap

Flaux 0.1 is an early prototype. Things that aren't there yet:

- [ ] Memory clean-up for text and lists (right now long-running programs
      that make lots of text keep using more memory)
- [ ] Functions that can see variables made outside them
- [ ] Removing items from lists, splitting and joining text
- [ ] Records/objects for grouping values (`a player has a name and a score`)
- [ ] Graphics and sound for making games
- [ ] A drag-and-drop block editor that writes Flaux
- [ ] Rewriting the compiler in a faster language (it currently needs Python)
