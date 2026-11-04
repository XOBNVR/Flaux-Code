# Flaux Studio for the web

This folder is **Flaux Studio**, a beginner code editor for the Flaux
programming language, running entirely in the web browser. It's a plain
static website: no server code, no database, no build step. Upload the files
as they are and it works.

Flaux itself is developed at https://github.com/XOBNVR/Flaux-Code. These files
are **built from that repository**, so make lasting changes there and rebuild
(see "Updating"), not by editing these files by hand.

## What's in this folder

| Path | What it is |
|---|---|
| `index.html` | the page: Flaux Studio (editor, Blocks mode, output, lessons) |
| `web.js` | the page's helper: loads Python, runs programs, keeps files in the browser |
| `fx.js` | the runtime that compiled Flaux programs use (maths, text, 2D graphics, keys, mouse, controllers, sound) |
| `fx3d.js` | 3D, shaders and VR/AR: the same engine as the Windows app, drawn with WebGL 2 and WebXR |
| `player.html` | the page an exported program becomes (**Export** button: `web.js` fills in `fx.js` (and `fx3d.js` plus the GPU code for 3D programs), the compiled program and its pictures) |
| `FlauxPlayer.exe` | Flaux's game player for Windows: a real app with its own window (WebView2, no Electron; source in `installer/app` in the Flaux repository). **Export** puts it in the zip as `<name>.exe`, with the game page added to the end of the file, so a game is one `.exe` people double-click. Keep it on the website (if it's missing, Export gives `game.html` and a `Play <name>.bat` instead). |
| `blocks.js`, `blockly/` | the block editor (built on Google's Blockly) |
| `pyodide/` | Python for web pages (Pyodide 314.0.7). It runs Flaux's real compiler in the browser |
| `flaux-files.json` | the Flaux compiler's Python source files, loaded into Pyodide |
| `examples.json`, `examples/` | the lessons, example games and their pictures |
| `icon.png` | the Flaux logo (also the browser tab icon) |
| `.htaccess` | file types for Apache servers (ignored by other hosts) |
| `.nojekyll` | tells GitHub Pages to serve every file as it is |
| `CLAUDE.md` | this file |

## How it works

1. `index.html` loads `web.js`, which starts Pyodide (WebAssembly Python)
   and copies Flaux's compiler into it from `flaux-files.json`.
2. Checking code, the "Apply fix" button and Blocks mode all use that real
   compiler, so the error messages are exactly the same as the desktop app's.
3. To run a program, the compiler turns it into JavaScript, and it runs in
   the page on `fx.js`. Games draw on a `<canvas>` above the output.
   Its Fullscreen button puts that canvas on the whole screen (Fullscreen API),
   scaled to fit with black bars; mouse positions are turned back into canvas pixels.
   The canvas has as many pixels as it's shown at (times the screen's pixel ratio,
   up to about 4K; `web.js` watches its size and calls the runtime's `win.resize`),
   so games are drawn at the screen's full resolution while programs keep their
   own `window(w, h)` coordinates.
   `lock_mouse()` locks the pointer to the canvas (Pointer Lock API, after a click;
   Esc lets go) and `mouse_moved_x/y()` add up `movementX/Y` between frames, in CSS
   pixels as given (not scaled to the canvas, so turning feels the same in the panel
   and fullscreen). On touch screens a finger is the mouse, and dragging it is mouse movement.
   3D programs and shaders draw with WebGL 2 (`fx3d.js`), also at the full resolution;
   the GPU code (GLSL) comes from the compiler (`engine.glsl`, inside `flaux-files.json`).
4. A 3D program gets **Enter VR** / **Enter AR** buttons next to the game when
   the browser has WebXR and a headset (or an AR phone) is there.
5. People's programs and pictures are kept in their own browser
   (`localStorage` keys `flaux-web-programs` and `flaux-web-pictures`).
   Nothing is sent anywhere.

## Hosting it

Any static web host works: Netlify, Cloudflare Pages, GitHub Pages, Vercel,
or an ordinary web server (Apache, nginx).

- **It must be served over http:// or https://.** Opening `index.html`
  straight from the disk (`file://`) won't work, because browsers block the
  downloads it needs. To try it on your own computer:
  `python3 -m http.server 8000` in this folder, then open
  http://localhost:8000/.
- **`.wasm` files must be served as `application/wasm`.** Almost every host
  does this already; `.htaccess` sets it for Apache.
- **No special headers are needed** (no COOP/COEP, no SharedArrayBuffer).
- **VR and AR need https://.** Browsers only allow WebXR on secure pages
  (or localhost). Without https the Enter VR button doesn't appear;
  everything else still works. If you put the Studio inside an `<iframe>`,
  give it `allow="xr-spatial-tracking"` too.
- **3D needs WebGL 2**, which every current browser has (Chrome, Edge,
  Firefox, Safari 15+, the Meta Quest browser).
- **It can live in a subfolder**, like `https://example.com/flaux/`. Every
  path in the site is relative.
- **Size:** about 15 MB in total. The first visit downloads about 10 MB
  (mostly `pyodide/`); browsers cache it after that. Turn on compression
  (gzip or brotli) if the host offers it.
- Keep the whole `pyodide/` folder together: its files must all be the same
  version.

### Handy links

- `index.html?open=lessons/01_say.flx` opens a lesson straight away
  (any path from `examples.json`, like `games/space_dodge.flx`).

## Checking it works after uploading

1. Open the page. The status bar at the bottom says "Getting Flaux
   ready...", then "Ready" (a few seconds the first time).
2. Press **Run**: the starter program says Hello.
3. Type `say scroe` on a new line: a yellow problem box appears with an
   **Apply fix** button.
4. Click **Blocks**: the program appears as blocks.
5. Open **Learn**, then **Games**, then **star catcher**, and press
   **Run**. The game appears above the output; click it and use the arrow keys.

If step 1 never says Ready, open the browser's developer console. It's
almost always a file that didn't upload, or `.wasm` served with the wrong
file type.

## Updating

In a copy of https://github.com/XOBNVR/Flaux-Code:

```bash
npm install --no-save --prefix /tmp/pw pyodide@314.0.7
python3 -m flauxc.web.build flaux-web --pyodide /tmp/pw/node_modules/pyodide
```

That makes a fresh copy of this folder in `flaux-web/`. Upload it in place of
the old files. (Every passing build on GitHub also attaches a ready-made
`FlauxStudio-web.zip` to the newest release.)

Where things come from, if you want to change them:

| To change... | Edit (in the Flaux-Code repository) |
|---|---|
| the page, its look and its text | `flauxc/studio/index.html` (shared with the desktop app) |
| the block editor | `flauxc/studio/blocks.js`, `flauxc/studio/blocks_table.json` |
| what happens in the browser | `flauxc/web/web.js`, `flauxc/web/fx.js` |
| 3D, VR and AR in the browser | `flauxc/web/fx3d.js` (and `flauxc/engine.glsl`, shared with the Windows app) |
| Flaux to JavaScript | `flauxc/jsgen.py` |
| this file | `flauxc/web/site-CLAUDE.md` |

Tests (run them before uploading a new version):

```bash
python3 tests/js_tests.py                       # every test program on the web runtime
python3 -m http.server --directory flaux-web 8790 &
node tests/web_studio_test.js http://127.0.0.1:8790/   # the whole web Studio, in a browser
```

## Things to know

- **Speed:** programs run as JavaScript, which is fine for learning and games
  but slower than the Windows app (which compiles to machine code). 3D is
  drawn by the graphics card in both, so 3D games run well in the browser.
- **Models:** `.obj` and `.mtl` files can be added with the Pictures button,
  like pictures.
- **Saving:** programs are saved per browser and per website address. If
  the site moves to a new address, people should download their programs
  first (Open, then Download this program).
- **The "Windows app" button** links to the newest installer on GitHub:
  `https://github.com/XOBNVR/Flaux-Code/releases/latest/download/FlauxSetup.exe`.

## Licences

- Flaux: see the Flaux-Code repository.
- Blockly: Apache 2.0 (`blockly/LICENSE`).
- Pyodide: Mozilla Public License 2.0, including CPython's standard library
  (Python Software Foundation License). See https://github.com/pyodide/pyodide.
