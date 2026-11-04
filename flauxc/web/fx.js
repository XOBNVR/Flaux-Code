/* Flaux's runtime for the web: what compiled Flaux programs (from jsgen.py)
 * call when they run in a browser. It matches the native C runtime
 * (runtime.h, graphics.h): the same maths, the same number formatting, and
 * the same friendly problem messages.
 *
 * runProgram(js, io, info) runs a compiled program. `io` is how it talks to
 * the page (or to a test):
 *   io.write(stream, text)        stream is "out" or "err"
 *   io.ask(prompt) -> Promise     resolves with the typed answer
 *   io.openWindow(w, h, title)    -> { screen: canvas, back: canvas }, both w by h to start with.
 *                                 The runtime adds win.resize(width, height) to that object: the
 *                                 page calls it to draw at more pixels when the game is shown bigger
 *                                 (fullscreen, a sharp screen). Programs keep their own w-by-h pixels.
 *   io.keys (Set of key names)    io.mouse = { x, y, down, pressed }  (pressed: set on
 *                                 mouse down, so a click quicker than a frame still counts)
 *                                 io.mouse.mx, my: how far it moved since the last frame, in screen
 *                                 (CSS) pixels as the browser gives them, not scaled to the canvas
 *                                 (added up by the page, set back to 0 at each frame)
 *   io.fullscreen()               (optional) asked for by fullscreen()
 *   io.lockMouse(on)              (optional) lock_mouse() / unlock_mouse()
 *   io.mouseLocked() -> bool      (optional) for mouse_locked()
 *   io.nextFrame() -> Promise     waits for the next frame (60 a second)
 *   io.cancelled() -> bool        true after Stop was pressed
 *   io.loadPicture(name) -> Promise<image or null>
 *   io.beep(pitch, seconds)       io.now() (milliseconds)
 *   io.sleep(ms) -> Promise       (optional) waits, but finishes early when
 *                                 Stop is pressed; used by `wait`
 */
(function (root) {
  "use strict";

  class FlauxProblem extends Error {
    constructor(line, message, fix) { super(message); this.line = line; this.fix = fix; this.flaux = true; }
  }
  class FlauxCancelled extends Error {}

  const FIX_DIVIDE = "Check that the number you divide by isn't zero first, like this:\n\n" +
    "      if count != 0\n          say total / count\n      end";
  const KEY_FIX = 'Key names are letters ("a" to "z"), numbers ("0" to "9"), arrows ("left", "right", "up", "down"),\n' +
    '  and "space", "enter", "escape", "shift", "ctrl", "alt", "tab", "backspace" and "f1" to "f12".';
  const COLOUR_FIX = 'Use a colour name like "red", "blue", "dark green" or "sky", a code like "#ff8800",\n' +
    "  or make one with rgb(255, 136, 0).";
  const CRASH = "\nProblem while running the program: it crashed.\n\n" +
    "This almost always means a function kept calling itself and never stopped.\n\n" +
    "How to fix it:\n  Every function that calls itself needs an `if` with a `return` that doesn't call\n" +
    "  itself, so it knows when to stop:\n\n      function count_down(n)\n          if n = 0\n" +
    "              return 0\n          end\n          return count_down(n - 1)\n      end\n";
  const KEYS = new Set([..."abcdefghijklmnopqrstuvwxyz0123456789"].concat(
    ["left", "right", "up", "down", "space", "enter", "escape", "tab", "backspace", "delete", "insert", "home",
     "end", "pageup", "pagedown", "shift", "ctrl", "alt"], Array.from({ length: 12 }, (_, i) => "f" + (i + 1))));

  // ---------------------------------------------------------------- text
  function num(x) {
    if (Number.isNaN(x)) return "not a number";
    if (!Number.isFinite(x)) return x > 0 ? "infinity" : "-infinity";
    if (Number.isInteger(x) && Math.abs(x) < 1e17) return String(x);
    const [mant, e] = x.toExponential().split("e");     // the fewest digits that read back exactly
    const power = Number(e);
    if (power < -5 || power >= 17)
      return mant + "e" + (power < 0 ? "-" : "+") + String(Math.abs(power)).padStart(2, "0");
    const digits = mant.replace("-", "").replace(".", "").length;
    return x.toFixed(Math.max(0, digits - 1 - power));
  }

  function str(x) {
    if (typeof x === "string") return x;
    if (typeof x === "boolean") return x ? "true" : "false";
    if (typeof x === "number") return num(x);
    if (Array.isArray(x)) return "[" + x.map((i) => (typeof i === "string" ? '"' + i + '"' : str(i))).join(", ") + "]";
    return String(x);
  }

  function formatProblem(info, p) {
    let out = `\nProblem while running ${info.file}, line ${p.line}:\n\n`;
    const text = (info.lines || [])[p.line - 1];
    if (text !== undefined) out += `  ${p.line} | ${text}\n\n`;
    out += p.message + "\n";
    if (p.fix) out += "\nHow to fix it:\n  " + p.fix + "\n";
    return out;
  }

  const PAD_FIX = 'Controller buttons are "a", "b", "x", "y", "start", "back", "lb", "rb" (bumpers),\n' +
    '  "lt", "rt" (triggers), "l3", "r3" (pressing a stick) and "up", "down", "left", "right" (the D-pad).';

  // Give the browser (or Node) a turn, without setTimeout's 4 ms minimum.
  let channel = null;
  const wakeUps = [];
  function nextTask() {
    if (typeof setImmediate === "function") return new Promise((resolve) => setImmediate(resolve));
    if (typeof MessageChannel === "function") {
      if (!channel) {
        channel = new MessageChannel();
        channel.port1.onmessage = () => { const resolve = wakeUps.shift(); if (resolve) resolve(); };
      }
      return new Promise((resolve) => { wakeUps.push(resolve); channel.port2.postMessage(0); });
    }
    return new Promise((resolve) => setTimeout(resolve, 0));
  }

  function squash(text) { return String(text).toLowerCase().replace(/[ _-]/g, ""); }
  function lround(x) { return x < 0 ? -Math.round(-x) : Math.round(x); }
  function px(v) { return v > 1e6 ? 1e6 : v < -1e6 ? -1e6 : lround(v); }

  function createRuntime(io) {
    const fx = { steps: 0, str };
    const start = io.now();
    const fail = (line, fix, message) => { throw new FlauxProblem(line, message, fix); };
    fx.fail = fail;

    function indexFail(i, len, line, what) {
      const thing = what === "item" ? "list" : "text";
      if (i === 0) fail(line, `In Flaux, the first ${what} is number 1 (not 0), and the last is number \`length of\` the ${thing}.`,
        `There is no ${what} number 0.`);
      if (len === 0) fail(line, `The ${thing} has nothing in it yet. Make sure it has something in it before reading from it,\n` +
        `  or check its length first:\n\n      if length of my_${thing} > 0\n          say my_${thing}[1]\n      end`,
        `You asked for ${what} number ${i}, but the ${thing} is empty.`);
      fail(line, `${what[0].toUpperCase() + what.slice(1)}s are numbered from 1 up to \`length of\` the ${thing}. ` +
        `Check the position is in range\n  before you use it:\n\n      if position >= 1 and position <= length of my_${thing}\n` +
        `          say my_${thing}[position]\n      end`,
        `You asked for ${what} number ${i}, but there ${len === 1 ? "is" : "are"} only ${len}.`);
    }
    function check(list, i, line, what = "item") {
      if (!Number.isInteger(i) || i < 1 || i > list.length) indexFail(i, list.length, line, what);
    }

    // -------------------------------------------------------------- basics
    // say.same leaves its line unfinished, so the next thing said goes right after it
    let midLine = false;
    fx.say = (values) => { io.write("out", values.map(str).join(" ") + "\n"); midLine = false; };
    fx.saySame = (values) => {
      const text = values.map(str).join(" ");
      if (text) { io.write("out", text); midLine = !text.endsWith("\n"); }
    };
    fx.div = (a, b, line) => { if (b === 0) fail(line, FIX_DIVIDE, "You can't divide by zero."); return a / b; };
    fx.floorDiv = (a, b, line) => { if (b === 0) fail(line, FIX_DIVIDE, "You can't divide by zero."); return Math.floor(a / b); };
    fx.mod = (a, b, line) => {
      if (b === 0) fail(line, "`mod` gives the remainder after dividing, so the number after it can't be 0.", "You can't use mod with zero.");
      let r = a % b;
      if (r !== 0 && (r < 0) !== (b < 0)) r += b;
      return r;
    };
    fx.ipow = (a, b, line) => {
      if (b < 0) fail(line, "Use a decimal number instead, like 2.0 ^ -1 (which gives 0.5).",
        "A whole number to a negative power isn't a whole number.");
      return Math.pow(a, b);
    };
    fx.copies = (n, make, line) => {
      if (n < 0) fail(line, "The number before `copies of` must be 0 or more.", `You can't make ${n} copies of something.`);
      return Array.from({ length: n }, make);
    };
    fx.len = (x) => x.length;
    fx.get = (x, i, line) => { check(x, i, line, typeof x === "string" ? "letter" : "item"); return x[i - 1]; };
    fx.set = (list, i, v, line) => { check(list, i, line); list[i - 1] = v; };
    fx.addAt = (list, i, v, mode, line) => {
      check(list, i, line);
      list[i - 1] = mode === "text" ? list[i - 1] + str(v) : list[i - 1] + v;
    };
    fx.extend = (a, b) => { for (const x of b.slice()) a.push(x); };
    fx.pick = (list, line) => {
      if (!list.length) fail(line, "Make sure the list has items before picking one:\n\n      if length of my_list > 0\n" +
        "          say random(my_list)\n      end", "You can't pick a random item from an empty list.");
      return list[Math.floor(Math.random() * list.length)];
    };
    fx.randomInt = (a, b, line) => {
      if (a > b) fail(line, "Put the smaller number first, like random(1, 6).", `random(${a}, ${b}) has the bigger number first.`);
      return a + Math.floor(Math.random() * (b - a + 1));
    };
    fx.randomFloat = (a, b, line) => {
      if (a > b) fail(line, "Put the smaller number first, like random(0.0, 1.0).", "random(a, b) has the bigger number first.");
      return a + (b - a) * Math.random();
    };
    // Loops call this every 1000 steps. About every 12 ms it gives the page a
    // turn, so clicks (like Stop), drawing and the output panel keep working
    // while a long loop runs.
    let lastBreath = io.now();
    fx.breathe = async () => {
      fx.steps = 0;
      if (io.cancelled()) throw new FlauxCancelled();
      if (io.now() - lastBreath < 12) return;
      await nextTask();
      lastBreath = io.now();
      if (io.cancelled()) throw new FlauxCancelled();
    };

    // `wait 2s`: amount times the unit's seconds. Stop works straight away,
    // even in the middle of a long wait.
    fx.wait = async (amount, unit, line) => {
      if (Number.isNaN(amount))
        fail(line, "Check the calculation before the unit. It must give a number, like `wait 1s`.",
          "`wait` was given something that isn't a number.");
      if (amount < 0)
        fail(line, "Waiting goes forwards only, so the time must be 0 or more. You could check it first:\n\n" +
          "      if delay > 0\n          wait delay seconds\n      end", "You can't wait for a negative amount of time.");
      const end = io.now() + amount * unit * 1000;
      for (;;) {
        if (io.cancelled()) throw new FlauxCancelled();
        const left = end - io.now();
        if (left <= 0) break;
        const ms = Math.min(left, io.sleep ? 60000 : 50);   // short naps if Stop can't wake us
        await (io.sleep ? io.sleep(ms) : new Promise((resolve) => setTimeout(resolve, ms)));
      }
      fx.steps = 0;
      lastBreath = io.now();
    };

    const numberText = /^\s*[+-]?(\d+\.?\d*|\.\d+)(e[+-]?\d+)?\s*$/i;
    function toNumber(t, line) {
      if (!numberText.test(t))
        fail(line, 'number(...) only works on text that looks like a number, like "42" or "3.5".\n' +
          "  If this came from ask(...), you could keep asking until you get a number:\n\n" +
          '      set answer to ask("How old are you?")\n      repeat until is_number(answer)\n' +
          '          set answer to ask("Please type a number:")\n      end', `I can't turn "${t}" into a number.`);
      return parseFloat(t);
    }

    // ---------------------------------------------------- built-in functions
    const b = {
      sqrt: ([x], line) => { if (x < 0) fail(line, "Use abs(...) to make the number positive first, like sqrt(abs(x)).",
        "You can't take the square root of a negative number."); return Math.sqrt(x); },
      sin: ([x]) => Math.sin(x), cos: ([x]) => Math.cos(x), tan: ([x]) => Math.tan(x), log: ([x]) => Math.log(x),
      abs: ([x]) => Math.abs(x), floor: ([x]) => Math.floor(x), ceil: ([x]) => Math.ceil(x), round: ([x]) => lround(x),
      min: (xs) => Math.min(...xs), max: (xs) => Math.max(...xs),
      text: ([x]) => str(x),
      number: ([x], line) => (typeof x === "string" ? toNumber(x, line) : x),
      whole: ([x], line) => typeof x === "string"
        ? (/^\s*[+-]?\d+\s*$/.test(x) ? parseInt(x, 10) : Math.trunc(toNumber(x, line))) : Math.trunc(x),
      is_number: ([t]) => numberText.test(t),
      ask: async ([prompt]) => {
        const answer = await io.ask(prompt === undefined ? "" : str(prompt));
        midLine = false;   // pressing Enter finished the line
        return answer;
      },
      clock: () => (io.now() - start) / 1000,
      length: ([x]) => x.length,
      contains: ([h, n]) => (typeof h === "string" ? h.includes(n) : h.some((v) => v === n)),
      uppercase: ([t]) => t.toUpperCase(), lowercase: ([t]) => t.toLowerCase(),
      distance: ([x1, y1, x2, y2]) => Math.hypot(x1 - x2, y1 - y2),
      rgb: (cs) => "#" + cs.map((v) => Math.max(0, Math.min(255, lround(v))).toString(16).padStart(2, "0")).join(""),
    };
    fx.b = b;

    // ------------------------------------------------------------ graphics
    let colors = {}, font = [], win = null, back = null, ctx = null, W = 0, H = 0, dirty = false;
    // Drawing happens at the canvas's real size: sx, sy canvas pixels per program pixel (1 to
    // start with; more when the page shows the game bigger, see resize). Programs keep their own
    // pixels: rects, dots and text land on whole program pixels (so they stay sharp and blocky),
    // and shapes (triangles, circles, lines) are smooth paths at the full resolution.
    let sx = 1, sy = 1, styled = null;
    let keysNow = new Set(), keysPrev = new Set(), mouseNow = false, mousePrev = false;
    let buttonsNow = 0, buttonsPrev = 0;     // every mouse button: 1 left, 2 middle, 4 right
    let moveX = 0, moveY = 0, movedX = 0, movedY = 0;       // mouse movement (see readInput)
    const pictures = new Map();
    fx.setupGraphics = (c, f) => { colors = c; font = f; };

    function needWindow(line) {
      if (!ctx) fail(line, "Open a window first, at the top of your program, like this:\n\n      window(800, 600)",
        "There's no window to draw in yet.");
    }
    let lastColour = null, lastStyle = null;
    function colour(c, line) {
      if (c === lastColour) return lastStyle;
      const key = squash(c);
      let style = null;
      if (/^#([0-9a-f]{3}|[0-9a-f]{6})$/.test(key)) style = key;
      else if (colors[key]) style = colors[key];
      if (!style) fail(line, COLOUR_FIX, `I don't know the colour "${c}".`);
      lastColour = c; lastStyle = style;
      return style;
    }
    function use(c, line) {
      needWindow(line);
      const style = colour(c === undefined ? "white" : c, line);
      if (style !== styled) { ctx.fillStyle = ctx.strokeStyle = style; styled = style; }
      dirty = true;
    }
    // Whole program pixels [x0, x1) by [y0, y1), with edges on whole canvas pixels: sharp, and
    // boxes side by side meet exactly (rect, dot, the pixels of text).
    function box(x0, y0, x1, y1) {
      const a = Math.round(x0 * sx), b = Math.round(y0 * sy);
      ctx.fillRect(a, b, Math.round(x1 * sx) - a, Math.round(y1 * sy) - b);
    }
    // A filled triangle, at the canvas's real resolution with smooth edges. It's made a little
    // bigger (half a canvas pixel all round), so triangles side by side (like the two halves of a
    // square in a 3D game) overlap a little: their soft edges would otherwise let a thin line of
    // the background show between them.
    function fillTriangle(x1, y1, x2, y2, x3, y3) {
      const ax = x1 * sx, ay = y1 * sy, bx = x2 * sx, by = y2 * sy, cx = x3 * sx, cy = y3 * sy;
      const area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
      if (!area) return;                      // flat: nothing to fill (like the desktop version)
      ctx.beginPath();
      if (isFinite(area)) {
        // each side's outward direction (one way round the triangle or the other)
        const s = area > 0 ? 1 : -1;
        let l = Math.hypot(bx - ax, by - ay);
        const ux = s * (by - ay) / l, uy = s * (ax - bx) / l;     // side a-b
        l = Math.hypot(cx - bx, cy - by);
        const vx = s * (cy - by) / l, vy = s * (bx - cx) / l;     // side b-c
        l = Math.hypot(ax - cx, ay - cy);
        const wx = s * (ay - cy) / l, wy = s * (cx - ax) / l;     // side c-a
        corner(ax, ay, wx, wy, ux, uy, true);
        corner(bx, by, ux, uy, vx, vy, false);
        corner(cx, cy, vx, vy, wx, wy, false);
      } else {                                // (enormous: just as it is)
        ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.lineTo(cx, cy);
      }
      ctx.closePath();
      ctx.fill();
    }
    // A corner of a triangle moved out half a canvas pixel from both of its sides (n1, n2: their
    // outward directions). A sharp corner would stick out a long way, so it's cut off instead.
    function corner(x, y, n1x, n1y, n2x, n2y, first) {
      const d = 1 + n1x * n2x + n1y * n2y;
      if (d > 0.25) {                         // (wider than about 40 degrees)
        const k = 0.5 / d;
        if (first) ctx.moveTo(x + (n1x + n2x) * k, y + (n1y + n2y) * k);
        else ctx.lineTo(x + (n1x + n2x) * k, y + (n1y + n2y) * k);
      } else {
        if (first) ctx.moveTo(x + n1x * 0.5, y + n1y * 0.5); else ctx.lineTo(x + n1x * 0.5, y + n1y * 0.5);
        ctx.lineTo(x + n2x * 0.5, y + n2y * 0.5);
      }
    }
    function fillCircle(cx, cy, r) {          // centre and radius in program pixels (not rounded)
      ctx.beginPath();
      ctx.ellipse(cx * sx, cy * sy, r * sx, r * sy, 0, 0, 2 * Math.PI);
      ctx.fill();
    }
    // A line from the middle of one program pixel to the middle of another, `width` program pixels wide.
    function strokeLine(x1, y1, x2, y2, width, cap) {
      ctx.lineWidth = width * (sx + sy) / 2;
      ctx.lineCap = cap;
      ctx.beginPath();
      ctx.moveTo((x1 + 0.5) * sx, (y1 + 0.5) * sy);
      ctx.lineTo((x2 + 0.5) * sx, (y2 + 0.5) * sy);
      ctx.stroke();
    }
    // After the canvas changes size (its drawing settings are reset then).
    function fresh() {
      sx = back.width / W; sy = back.height / H;
      ctx.imageSmoothingEnabled = false;      // pictures keep sharp pixels, like the desktop version
      styled = null;
    }
    // The page shows the game bigger or smaller (fullscreen, a sharp screen): draw at that many
    // pixels from now on. What's drawn so far is kept, scaled, so a finished picture still shows.
    function resize(width, height) {
      width = Math.max(1, Math.round(width)); height = Math.max(1, Math.round(height));
      if (!back || (width === back.width && height === back.height)) return;
      for (const c of [back, win.screen]) {
        const old = c.ownerDocument.createElement("canvas");
        old.width = c.width; old.height = c.height;
        old.getContext("2d").drawImage(c, 0, 0);
        c.width = width; c.height = height;
        const g = c.getContext("2d");
        g.imageSmoothingEnabled = width % old.width !== 0 || height % old.height !== 0;  // whole times bigger: sharp
        g.drawImage(old, 0, 0, width, height);
      }
      fresh();
    }
    // game controllers (and VR controllers, through fx.extraPads), with the
    // same button names as the desktop app
    const PAD_BUTTONS = ["a", "b", "x", "y", "lb", "rb", "lt", "rt", "back", "start", "l3", "r3", "up", "down", "left", "right"];
    const PAD_ALIASES = { select: "back", view: "back", menu: "start", options: "start", leftbumper: "lb", l1: "lb",
      grip: "lb", rightbumper: "rb", r1: "rb", lefttrigger: "lt", l2: "lt", righttrigger: "rt", r2: "rt", trigger: "rt" };
    let padNow = new Set(), padPrev = new Set(), stick = [[0, 0], [0, 0]];
    function readPads() {
      padPrev = padNow; padNow = new Set(); stick = [[0, 0], [0, 0]];
      const pads = io.gamepads ? io.gamepads() : [];
      for (const p of pads) {
        if (!p) continue;
        p.buttons.forEach((btn, i) => { if (i < 16 && (btn.pressed || btn.value > 0.5)) padNow.add(PAD_BUTTONS[i]); });
        for (let s = 0; s < 2; s++)
          for (let a = 0; a < 2; a++) {
            let v = p.axes[s * 2 + a] || 0;
            if (Math.abs(v) < 0.15) v = 0;
            if (a === 1) v = -v;                 // up is positive
            if (Math.abs(v) > Math.abs(stick[s][a])) stick[s][a] = v;
          }
      }
      if (fx.extraPads) {
        for (const name of fx.extraPads.buttons) padNow.add(name);
        for (let s = 0; s < 2; s++)
          for (let a = 0; a < 2; a++)
            if (Math.abs(fx.extraPads.stick[s][a]) > Math.abs(stick[s][a])) stick[s][a] = fx.extraPads.stick[s][a];
      }
    }
    function padName(name, line) {
      let k = squash(name);
      k = PAD_ALIASES[k] || k;
      if (!PAD_BUTTONS.includes(k)) fail(line, PAD_FIX, `I don't know the controller button "${name}".`);
      return k;
    }
    function stickSide(side, line) {
      const k = side === undefined ? "left" : squash(side);
      if (k !== "left" && k !== "right") fail(line, 'Use stick_x("left") or stick_x("right").',
        `A controller has a "left" and a "right" stick, not "${side}".`);
      return k === "left" ? 0 : 1;
    }
    let windowOpened = 0, fpsValue = 0, fpsFrames = 0, fpsSince = 0, frameCount = 0;
    function countFrame() {
      frameCount++;
      const now = io.now();
      if (!fpsSince) fpsSince = now;
      fpsFrames++;
      if (now - fpsSince >= 500) { fpsValue = fpsFrames * 1000 / (now - fpsSince); fpsFrames = 0; fpsSince = now; }
    }
    function readInput() {
      keysPrev = keysNow; keysNow = new Set(io.keys);
      mousePrev = mouseNow; mouseNow = !!(io.mouse.down || io.mouse.pressed);
      buttonsPrev = buttonsNow; buttonsNow = (io.mouse.buttons || 0) | (io.mouse.clicks || 0) | (mouseNow ? 1 : 0);
      io.mouse.clicks = 0;
      io.mouse.pressed = false;     // a click quicker than a frame still counts
      // mouse movement: whole pixels this frame, the rest is kept for the next one (like graphics.h)
      moveX += io.mouse.mx || 0; moveY += io.mouse.my || 0;
      io.mouse.mx = io.mouse.my = 0;
      movedX = Math.trunc(moveX); movedY = Math.trunc(moveY);
      moveX -= movedX; moveY -= movedY;
      readPads();
    }
    function present() { win.screen.getContext("2d").drawImage(back, 0, 0); dirty = false; }
    function mouseBit(name, line) {
      const k = squash(name);
      if (k === "left" || k === "") return 1;
      if (k === "right") return 4;
      if (k === "middle" || k === "wheel") return 2;
      fail(line, 'Mouse buttons are "left", "right" and "middle", like mouse_clicked("right").',
        `A mouse doesn't have a "${name}" button.`);
    }
    function keyName(name, line) {
      let k = squash(name);
      k = { "return": "enter", esc: "escape", control: "ctrl" }[k] || k;
      if (!KEYS.has(k)) fail(line, KEY_FIX, `I don't know the key "${name}".`);
      return k;
    }
    async function picture(name, line) {
      needWindow(line);
      if (!pictures.has(name)) pictures.set(name, await io.loadPicture(name));
      const img = pictures.get(name);
      if (!img) fail(line, "Add the picture to your program with the Pictures button, and check the name is exactly\n" +
        "  right (including .png at the end).", `I can't find the picture "${name}".`);
      return img;
    }

    Object.assign(b, {
      window: ([w, h, title], line) => {
        if (win) fail(line, "Call window(...) just once, at the start of your program.", "There's already a window open.");
        w = lround(w); h = lround(h);
        if (w < 50 || h < 50 || w > 8000 || h > 8000)
          fail(line, "Pick a size between 50 and 8000, like window(800, 600).", `A window can't be ${w} by ${h} pixels.`);
        win = io.openWindow(w, h, title === undefined ? "Flaux" : str(title), "2d");
        windowOpened = io.now();
        back = win.back; ctx = back.getContext("2d"); W = w; H = h;
        win.resize = resize;
        fresh();
        ctx.fillStyle = "#000000"; ctx.fillRect(0, 0, back.width, back.height); present();
        readInput(); keysPrev = new Set();
      },
      show: async (_, line) => {
        needWindow(line);
        countFrame();
        present();
        await io.nextFrame();
        if (io.cancelled()) throw new FlauxCancelled();
        readInput();
      },
      clear: ([c], line) => { use(c === undefined ? "black" : c, line); ctx.fillRect(0, 0, back.width, back.height); },
      rect: ([x, y, w, h, c], line) => {
        use(c, line);
        if (w < 0) { x += w; w = -w; }
        if (h < 0) { y += h; h = -h; }
        box(px(x), px(y), px(x + w), px(y + h));
      },
      // a circle covers the pixels within `r` of the middle of pixel (x, y), like the desktop version
      circle: ([x, y, r, c], line) => { use(c, line); if (r > 0) fillCircle(x + 0.5, y + 0.5, Math.min(r, 20000) + 0.5); },
      triangle: ([x1, y1, x2, y2, x3, y3, c], line) => { use(c, line); fillTriangle(x1, y1, x2, y2, x3, y3); },
      dot: ([x, y, c], line) => { use(c, line); box(px(x), px(y), px(x) + 1, px(y) + 1); },
      line: ([x1, y1, x2, y2, c, thickness], line) => {
        use(c, line);
        const thin = thickness === undefined || thickness <= 1.5;
        if (!thin) thickness = Math.min(thickness, 40000);
        if (x1 === x2 && y1 === y2) {           // just a dot (or a round blob)
          if (thin) box(px(x1), px(y1), px(x1) + 1, px(y1) + 1);
          else fillCircle(x1 + 0.5, y1 + 0.5, thickness / 2);
        } else if (thin) strokeLine(x1, y1, x2, y2, 1, "square");     // one program pixel wide, ends included
        else strokeLine(x1, y1, x2, y2, thickness, "round");          // a long rectangle with round ends
      },
      write: ([value, x, y, c, size], line) => {
        use(c, line);
        const s = Math.max(1, Math.min(50, size === undefined ? 2 : lround(size)));
        let col = 0, row = 0;
        for (const ch of str(value)) {
          if (ch === "\n") { row++; col = 0; continue; }
          let code = ch.codePointAt(0);
          if (code < 32 || code > 126) code = 63;   // "?"
          const glyph = font[code - 32];
          for (let gx = 0; gx < 5; gx++)
            for (let gy = 0; gy < 7; gy++)
              if ((glyph[gx] >> gy) & 1) {
                const left = px(x) + col * 6 * s + gx * s, top = px(y) + row * 9 * s + gy * s;
                box(left, top, left + s, top + s);
              }
          col++;
        }
      },
      text_width: ([value, size]) => {
        const s = Math.max(1, size === undefined ? 2 : lround(size));
        const widest = Math.max(0, ...str(value).split("\n").map((l) => [...l].length));
        return widest ? (widest * 6 - 1) * s : 0;
      },
      image: async ([name, x, y, w, h, angle], line) => {
        const img = await picture(name, line);
        dirty = true;
        let dw = w === undefined ? img.width : w, dh = h === undefined ? img.height : h;
        const flipX = dw < 0, flipY = dh < 0;
        dw = Math.abs(dw); dh = Math.abs(dh);
        // on whole program pixels, with edges on whole canvas pixels like rect (pictures side by side meet)
        const left = Math.round(px(x) * sx), topY = Math.round(px(y) * sy);
        const width = Math.round(px(x + dw) * sx) - left, height = Math.round(px(y + dh) * sy) - topY;
        ctx.save();
        ctx.translate(left + width / 2, topY + height / 2);
        if (angle) ctx.rotate(angle * Math.PI / 180);
        ctx.scale(flipX ? -1 : 1, flipY ? -1 : 1);
        ctx.drawImage(img, -width / 2, -height / 2, width, height);
        ctx.restore();
      },
      image_width: async ([name], line) => (await picture(name, line)).width,
      image_height: async ([name], line) => (await picture(name, line)).height,
      key_down: ([name], line) => keysNow.has(keyName(name, line)),
      key_pressed: ([name], line) => { const k = keyName(name, line); return keysNow.has(k) && !keysPrev.has(k); },
      mouse_x: () => Math.round(io.mouse.x), mouse_y: () => Math.round(io.mouse.y),
      mouse_down: ([b], line) => (b === undefined ? mouseNow : !!(buttonsNow & mouseBit(b, line))),
      mouse_clicked: ([b], line) => (b === undefined ? mouseNow && !mousePrev
                                     : !!(buttonsNow & mouseBit(b, line)) && !(buttonsPrev & mouseBit(b, line))),
      beep: ([pitch, seconds]) => io.beep(pitch, seconds === undefined ? 0.1 : seconds),
      fullscreen: (_, line) => { needWindow(line); if (io.fullscreen) io.fullscreen(); },
      mouse_moved_x: () => movedX, mouse_moved_y: () => movedY,
      lock_mouse: (_, line) => { needWindow(line); if (io.lockMouse) io.lockMouse(true); },
      unlock_mouse: () => { if (win && io.lockMouse) io.lockMouse(false); },
      mouse_locked: () => !!(win && io.mouseLocked && io.mouseLocked()),
      screenshot: ([name], line) => { needWindow(line); if (io.screenshot) io.screenshot(str(name), back); },
      fps: () => Math.round(fpsValue),
      seconds: () => (io.fast ? frameCount / 60 : (io.now() - windowOpened) / 1000),
      button_down: ([name], line) => padNow.has(padName(name, line)),
      button_pressed: ([name], line) => { const k = padName(name, line); return padNow.has(k) && !padPrev.has(k); },
      stick_x: ([side], line) => stick[stickSide(side, line)][0],
      stick_y: ([side], line) => stick[stickSide(side, line)][1],
    });

    // 3D and shaders: the WebGL 2 engine (fx3d.js) takes over the drawing
    fx.setup3d = (glsl, shaders) => {
      if (!root.FlauxRuntime3D) fail(0, "Load fx3d.js next to fx.js.", "3D isn't available here.");
      root.FlauxRuntime3D.install(fx, b, io, {
        colour, px, readInput, font: () => font, Cancelled: FlauxCancelled,
      }, glsl, shaders);
    };

    fx.finish = () => {
      if (win && dirty) present();
      if (midLine) { io.write("out", "\n"); midLine = false; }   // like the native version
    };
    return fx;
  }

  // Run a compiled program. Gives back 0 (finished), 1 (stopped by a problem) or null (Stop pressed).
  async function runProgram(js, io, info) {
    const fx = createRuntime(io);
    let main;
    try {
      main = new Function("fx", js + "\nreturn fx_main;")(fx);
    } catch (e) {
      io.write("err", "\nProblem: Flaux made JavaScript the browser didn't accept. This is a bug in Flaux.\n" + e.message + "\n");
      return 1;
    }
    try {
      await main();
      await fx.finish();
      return 0;
    } catch (e) {
      if (e instanceof FlauxCancelled) return null;
      if (e && e.flaux) { io.write("err", formatProblem(info, e)); return 1; }
      if (e instanceof RangeError && /call stack/i.test(e.message)) { io.write("err", CRASH); return 1; }
      io.write("err", "\nProblem while running the program: something went wrong inside Flaux (" +
        (e && e.message) + "). This is a bug in Flaux.\n");
      return 1;
    }
  }

  const api = { createRuntime, runProgram, formatProblem, str, num };
  if (typeof module === "object" && module.exports) module.exports = api;
  root.FlauxRuntime = api;
})(typeof window !== "undefined" ? window : globalThis);
