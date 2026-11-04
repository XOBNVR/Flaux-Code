/* Flaux Studio on the web: does the jobs the desktop Studio's helper
 * (studio/server.py) does, but inside the browser.
 *
 * - Python runs in the page (Pyodide) with Flaux's own compiler, so checking,
 *   Apply fix and the block editor work exactly like the desktop app
 * - programs become JavaScript (jsgen.py) and run on fx.js
 * - your programs and pictures are kept in this browser (localStorage)
 * - games draw in the game panel, with keyboard, mouse and sound, and can go
 *   fullscreen (the Fullscreen button, or fullscreen() in the program), drawn at the
 *   full resolution of the screen (see "sharpness"), and
 *   lock the mouse for looking around (lock_mouse(): Pointer Lock API)
 * - Export makes a .zip with the program as a real Windows program (<name>.exe): Flaux
 *   makes it into C, and TinyCC (tcc/, WebAssembly) compiles that right in the page
 *
 * index.html calls FlauxWeb.api(path, data) instead of asking a server.
 */
"use strict";

window.FlauxWeb = (() => {
  const PROGRAMS = "flaux-web-programs", PICTURES = "flaux-web-pictures";
  // Output waiting for the page to show it. A program that says things very
  // fast (in a forever loop) can make more than anyone can read, so only the
  // newest MAX_KEEP characters are kept; the page is told when older ones were
  // skipped. Small pieces are joined, so there are few chunks to hand over.
  const MAX_KEEP = 1000000, JOIN_UP_TO = 16384;
  let examples = null, py = null, pyLoading = null;
  const runs = new Map();
  let nextRun = 1;

  // --------------------------------------------------------------- storage
  function load(key) { try { return JSON.parse(localStorage.getItem(key) || "{}"); } catch (e) { return {}; } }
  function store(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch (e) { return false; }
  }

  // --------------------------------------------------------- Flaux itself
  async function getExamples() {
    if (!examples) examples = await (await fetch("examples.json")).json();
    return examples;
  }

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement("script");
      s.src = src; s.onload = resolve; s.onerror = () => reject(new Error("Couldn't load " + src));
      document.head.appendChild(s);
    });
  }

  // Python (Pyodide) with Flaux's compiler. About 10 MB the first time; the
  // browser keeps it afterwards.
  function python() {
    if (!pyLoading) pyLoading = (async () => {
      window.setWebStatus && window.setWebStatus("Getting Flaux ready (the first time takes a moment)...");
      await loadScript("pyodide/pyodide.js");
      const p = await loadPyodide({ indexURL: new URL("pyodide/", location.href).href });
      const files = await (await fetch("flaux-files.json")).json();
      for (const [name, text] of Object.entries(files)) {
        const full = "/flaux/" + name;
        p.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
        p.FS.writeFile(full, text);
      }
      p.runPython("import sys\nsys.path.insert(0, '/flaux')\nfrom flauxc.web import web_api");
      py = p;
      window.setWebStatus && window.setWebStatus("Ready", "good");
      return p;
    })();
    return pyLoading;
  }

  async function ask(fn, ...args) {
    const p = await python();
    return JSON.parse(p.globals.get("web_api")[fn](...args));
  }

  // ---------------------------------------------------------------- files
  function nameOf(path) { return path ? path.split(/[:/]/).pop() : "untitled.flx"; }
  function safeName(name) {
    name = name.trim().replace(/\.flx$/i, "").replace(/[^A-Za-z0-9 _-]+/g, "").trim().replace(/ /g, "_");
    return (name || "my_program") + ".flx";
  }

  async function state() {
    const ex = await getExamples();
    const mine = Object.keys(load(PROGRAMS)).sort().map((name) => ({ name, path: "mine:" + name }));
    const group = (g) => ex.programs.filter((p) => p.group === g).map((p) => ({ name: p.name, path: "example:" + p.key }));
    const wanted = new URLSearchParams(location.search).get("open");
    return { home: "this browser", mine, lessons: group("lessons"), games: group("games"), examples: group(""),
             open_file: wanted ? "example:" + wanted : null, windows: false, web: true };
  }

  async function open(path) {
    if (path.startsWith("example:")) {
      const ex = await getExamples();
      const found = ex.programs.find((p) => "example:" + p.key === path);
      if (!found) return { ok: false, message: "That example isn't here any more." };
      return { path, name: found.name, content: found.content, example: true };
    }
    if (path.startsWith("mine:")) {
      const programs = load(PROGRAMS), name = path.slice(5);
      if (!(name in programs)) return { ok: false, message: `There's no program called ${name} in this browser.` };
      return { path, name, content: programs[name], example: false };
    }
    return { ok: false, message: "Flaux Studio on the web can only open programs saved in this browser." };
  }

  function save(d) {
    let path = d.path;
    if (d.name) path = "mine:" + safeName(d.name);
    if (!path || path.startsWith("example:"))
      return { ok: false, message: "Examples can't be changed. Save a copy with a new name instead." };
    const programs = load(PROGRAMS);
    programs[path.slice(5)] = d.content;
    if (!store(PROGRAMS, programs)) return { ok: false, message: "This browser is out of room for saving programs." };
    return { path, name: path.slice(5) };
  }

  // -------------------------------------------------------------- pictures
  async function loadPicture(name, programPath) {
    const url = await pictureUrl(name, programPath);
    if (!url) return null;
    return new Promise((resolve) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => resolve(null);
      img.src = url;
    });
  }

  async function pictureUrl(name, programPath) {
    const mine = load(PICTURES);
    if (mine[name]) return mine[name];
    const ex = await getExamples();
    const folder = programPath && programPath.startsWith("example:") ? programPath.slice(8).split("/").slice(0, -1).join("/") : null;
    const key = (folder ? folder + "/" : "") + name;
    if (ex.pictures[key]) return ex.pictures[key];
    const anywhere = Object.keys(ex.pictures).find((k) => k.split("/").pop() === name);
    return anywhere ? ex.pictures[anywhere] : null;
  }

  function addPictures(fileList, done) {
    const mine = load(PICTURES);
    const added = [];
    let left = fileList.length;
    for (const file of fileList) {
      const reader = new FileReader();
      reader.onload = () => {
        mine[file.name] = reader.result;
        added.push(file.name);
        if (--left === 0) done(store(PICTURES, mine) ? added : null);
      };
      reader.readAsDataURL(file);
    }
  }

  // ------------------------------------------------------------- running
  // W, H: the program's window(w, h) size (the canvas itself can have more pixels, see sharpness)
  const game = { canvas: null, W: 800, H: 600, win: null, keys: new Set(),
                 mouse: { x: 0, y: 0, down: false, mx: 0, my: 0 } };

  const KEY_NAMES = { ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down", " ": "space",
    Enter: "enter", Escape: "escape", Shift: "shift", Control: "ctrl", Alt: "alt", Tab: "tab",
    Backspace: "backspace", Delete: "delete", Insert: "insert", Home: "home", End: "end",
    PageUp: "pageup", PageDown: "pagedown" };
  function keyName(e) { return KEY_NAMES[e.key] || (e.key.length === 1 ? e.key.toLowerCase() : e.key.toLowerCase()); }

  // A canvas can only ever draw one way (2D or WebGL), so each window gets a
  // fresh one, with the keys and mouse wired up.
  function freshCanvas() {
    const old = document.getElementById("game-canvas");
    const canvas = old.cloneNode(false);
    old.replaceWith(canvas);
    setUpGame(canvas);
    return canvas;
  }

  function setUpGame(canvas = document.getElementById("game-canvas")) {
    game.canvas = canvas;
    canvas.addEventListener("keydown", (e) => { game.keys.add(keyName(e)); e.preventDefault(); });
    canvas.addEventListener("keyup", (e) => { game.keys.delete(keyName(e)); e.preventDefault(); });
    canvas.addEventListener("blur", () => game.keys.clear());
    // Mouse position in canvas pixels, however big the canvas is shown (in the
    // panel or fullscreen), kept on the canvas.
    const where = (e) => {
      const r = canvas.getBoundingClientRect();
      // how far it moved (locked or not), in screen (CSS) pixels as the browser gives them:
      // not scaled to the canvas, so mouse look turns as fast in the panel as in fullscreen
      game.mouse.mx += e.movementX || 0;
      game.mouse.my += e.movementY || 0;
      if (mouseLocked()) return;        // locked: the pointer stays where it was
      const x = Math.floor((e.clientX - r.left) * game.W / r.width);
      const y = Math.floor((e.clientY - r.top) * game.H / r.height);
      game.mouse.x = Math.max(0, Math.min(game.W - 1, x));
      game.mouse.y = Math.max(0, Math.min(game.H - 1, y));
    };
    canvas.addEventListener("mousemove", where);
    canvas.addEventListener("mousedown", (e) => {
      if (!mouseLocked()) where(e);
      game.mouse.down = game.mouse.pressed = true; canvas.focus();
      if (full.wanted) enterFullscreen();          // the program asked: a click is allowed to do it
      if (look.wanted && !mouseLocked()) askLock(); // the same for lock_mouse() (and after Esc let go)
    });
    // touch screens: a finger is the mouse, and dragging it moves the mouse (for mouse look)
    let last = null;
    const touch = (e) => {
      e.preventDefault();
      if (!e.touches.length) { game.mouse.down = false; last = null; return; }
      const t = e.touches[0];
      if (last) { game.mouse.mx += t.clientX - last.x; game.mouse.my += t.clientY - last.y; }
      last = { x: t.clientX, y: t.clientY };
      where({ clientX: t.clientX, clientY: t.clientY });
      if (e.type === "touchstart") {
        game.mouse.pressed = true;
        canvas.focus();
        if (full.wanted) enterFullscreen();
      }
      game.mouse.down = true;
    };
    canvas.addEventListener("touchstart", touch, { passive: false });
    canvas.addEventListener("touchmove", touch, { passive: false });
    canvas.addEventListener("touchend", touch, { passive: false });
    if (!game.listening) {          // (just once: each window gets a fresh canvas)
      window.addEventListener("mouseup", () => { game.mouse.down = false; });
      setUpFullscreen();
      setUpSharpness();
      game.listening = true;
    }
  }

  // a model (.obj or .mtl) or any other file added with the Pictures button,
  // or one of the examples', as text
  async function loadFile(name, programPath) {
    const url = await pictureUrl(name, programPath);
    if (!url) return null;
    try { const r = await fetch(url); return r.ok ? await r.text() : null; } catch (e) { return null; }
  }

  // ------------------------------------------------------------ sharpness
  // The game is drawn at as many pixels as it's shown at, times the screen's pixel ratio, so it's
  // sharp in fullscreen and on sharp screens: the page tells the runtime (win.resize) and the
  // program keeps its own window(w, h) pixels. Never fewer pixels than the program's own, and at
  // most about a 4K screen's worth (3840 by 2160), so drawing stays quick.
  let sizeWatch = null, exactBox = false;
  function sharpness(entry) {
    if (!game.win || !game.win.resize) return;
    const r = game.canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
    let dw = Math.round(r.width * ratio), dh = Math.round(r.height * ratio);
    // the browser's exact count of screen pixels, when it agrees (some only give the CSS size there)
    const d = entry && entry.devicePixelContentBoxSize && entry.devicePixelContentBoxSize[0];
    if (d && Math.abs(d.inlineSize - dw) <= 2 && Math.abs(d.blockSize - dh) <= 2) { dw = d.inlineSize; dh = d.blockSize; }
    const fit = Math.min(dw / game.W, dh / game.H);
    const s = Math.max(1, Math.min(fit, Math.sqrt(3840 * 2160 / (game.W * game.H))));
    // exactly as many pixels as it's shown at when that's allowed (nothing gets resampled)
    if (s === fit) game.win.resize(dw, dh);
    else game.win.resize(game.W * s, game.H * s);
  }
  function watchSize() {          // (again for each program: the first report comes straight away)
    if (!sizeWatch) return;
    sizeWatch.disconnect();
    try { sizeWatch.observe(game.canvas, { box: "device-pixel-content-box" }); exactBox = true; }
    catch (e) { sizeWatch.observe(game.canvas); exactBox = false; }
  }
  function setUpSharpness() {
    if (window.ResizeObserver) sizeWatch = new ResizeObserver((entries) => sharpness(entries[entries.length - 1]));
    // zooming the page changes the pixel ratio; without device-pixel sizes, look again then
    window.addEventListener("resize", () => { if (!exactBox) sharpness(); });
  }

  // ------------------------------------------------------------ fullscreen
  // The wrapper #game-screen goes fullscreen (black background) and the canvas
  // is sized to fit it, same shape. Browsers without the Fullscreen API (like
  // iPhones) get the wrapper stretched over the page instead, with a ✕ button.
  const full = { fake: false, wanted: false };
  function fullscreenElement() { return document.fullscreenElement || document.webkitFullscreenElement || null; }
  function isFullscreen() { return full.fake || fullscreenElement() === document.getElementById("game-screen"); }
  function fitCanvas() {
    const canvas = game.canvas, box = document.getElementById("game-screen"), W = game.W, H = game.H;
    if (!isFullscreen()) {
      // in the panel: the program's own size, or smaller to fit (same shape). (The canvas can have
      // more pixels than that, so its size is set here rather than coming from its pixels.)
      canvas.style.width = `min(100%, ${W}px, ${52 * W / H}vh)`;
      canvas.style.height = "auto";
      canvas.style.aspectRatio = `${W} / ${H}`;
      box.classList.remove("fake-full");
      return;
    }
    const s = Math.min(box.clientWidth / W, box.clientHeight / H);
    canvas.style.width = W * s + "px";
    canvas.style.height = H * s + "px";
  }
  function wantFullscreen(on) {
    full.wanted = on;
    document.getElementById("btn-fullscreen").classList.toggle("wants", on);
    showTitle();
  }
  function showTitle() {
    const title = document.getElementById("game-title");
    title.textContent = title.dataset.name ? title.dataset.name +
      (full.wanted ? "  (click the game to go fullscreen)"
       : look.wanted && !mouseLocked() ? "  (click the game to look around)" : "  (click here to use the keys)") : "";
  }
  function enterFullscreen() {
    const box = document.getElementById("game-screen");
    wantFullscreen(false);
    if (isFullscreen()) return Promise.resolve(true);
    const ask = box.requestFullscreen ? () => box.requestFullscreen({ navigationUI: "hide" })
      : box.webkitRequestFullscreen ? () => box.webkitRequestFullscreen() : null;
    const fake = () => { full.fake = true; box.classList.add("fake-full"); fitCanvas(); game.canvas.focus(); };
    if (!ask) { fake(); return Promise.resolve(true); }
    let p;
    try { p = ask(); } catch (e) { p = Promise.reject(e); }
    return Promise.resolve(p).then(() => { game.canvas.focus(); return true; }, () => false);
  }
  function leaveFullscreen() {
    wantFullscreen(false);
    if (full.fake) { full.fake = false; fitCanvas(); return; }
    if (fullscreenElement()) {
      try { (document.exitFullscreen || document.webkitExitFullscreen).call(document); } catch (e) { /* already gone */ }
    }
  }
  function toggleFullscreen() { if (isFullscreen()) leaveFullscreen(); else enterFullscreen(); }
  function setUpFullscreen() {
    const changed = () => { game.keys.clear(); fitCanvas(); if (isFullscreen()) game.canvas.focus(); };
    document.addEventListener("fullscreenchange", changed);
    document.addEventListener("webkitfullscreenchange", changed);
    window.addEventListener("resize", () => { if (isFullscreen()) fitCanvas(); });
    document.getElementById("btn-fullscreen").addEventListener("click", toggleFullscreen);
    document.getElementById("game-title").addEventListener("dblclick", toggleFullscreen);
    document.getElementById("game-exit").addEventListener("click", leaveFullscreen);
    // clicking a black bar keeps the keys going to the game
    document.getElementById("game-screen").addEventListener("mousedown", (e) => {
      if (e.target.id === "game-screen") { e.preventDefault(); game.canvas.focus(); }
    });
  }

  // ----------------------------------------------------------- mouse look
  // lock_mouse(): the canvas takes the pointer (Pointer Lock API), so the
  // mouse can move forever. Browsers only allow it after a click, and Esc lets
  // go, so while the program wants it locked, a click on the game locks it again.
  const look = { wanted: false };
  function mouseLocked() { return !!game.canvas && document.pointerLockElement === game.canvas; }
  function askLock() {
    try {
      const p = game.canvas.requestPointerLock();
      if (p && p.catch) p.catch(() => {});        // refused: the next click tries again
    } catch (e) { /* no Pointer Lock here */ }
  }
  function lockMouse(on) {
    look.wanted = on;
    if (on && !mouseLocked()) askLock();
    if (!on && mouseLocked()) try { document.exitPointerLock(); } catch (e) { /* already gone */ }
    showTitle();
  }
  document.addEventListener("pointerlockchange", () => { if (game.canvas) showTitle(); });

  let audio = null, audioTime = 0;
  function beep(pitch, seconds) {
    try {
      if (!audio) audio = new (window.AudioContext || window.webkitAudioContext)();
      if (pitch <= 0 || seconds <= 0) return;
      const start = Math.max(audio.currentTime, audioTime), stop = start + Math.min(seconds, 10);
      const osc = audio.createOscillator(), gain = audio.createGain();
      osc.type = "square"; osc.frequency.value = pitch;
      gain.gain.setValueAtTime(0, start);
      gain.gain.linearRampToValueAtTime(0.08, start + 0.01);
      gain.gain.setValueAtTime(0.08, Math.max(start + 0.01, stop - 0.01));
      gain.gain.linearRampToValueAtTime(0, stop);
      osc.connect(gain).connect(audio.destination);
      osc.start(start); osc.stop(stop);
      audioTime = stop;
    } catch (e) { /* no sound here: stay quiet */ }
  }

  async function run(d) {
    const name = nameOf(d.path);
    const built = await ask("build", d.content, name);
    if (!built.ok) return built;
    const id = String(nextRun++);
    // chunks[k] is chunk number base + k; `given` is how many the page has had
    const r = { chunks: [], base: 0, given: 0, kept: 0, done: false, exit: null, cancelled: false,
                asking: null, answers: [], lastFrame: 0, sleepers: new Set() };
    runs.set(id, r);
    const panel = document.getElementById("game");
    leaveFullscreen();
    lockMouse(false);
    panel.hidden = true;
    document.getElementById("game-buttons").replaceChildren();
    game.keys.clear();
    const io = {
      write: (stream, text) => {
        const last = r.chunks[r.chunks.length - 1];
        // join onto the last chunk if the page hasn't had it yet
        if (last && last[0] === stream && r.base + r.chunks.length - 1 >= r.given && last[1].length < JOIN_UP_TO)
          last[1] += text;
        else r.chunks.push([stream, text]);
        r.kept += text.length;
        while (r.kept > MAX_KEEP && r.chunks.length > 1) {    // too much waiting: drop the oldest
          const [, old] = r.chunks.shift();
          r.kept -= old.length; r.base++;
        }
      },
      sleep: (ms) => new Promise((resolve) => {
        const wake = () => { clearTimeout(timer); r.sleepers.delete(wake); resolve(); };
        const timer = setTimeout(wake, ms);
        r.sleepers.add(wake);
      }),
      ask: (prompt) => {
        if (prompt) io.write("out", prompt.endsWith(" ") ? prompt : prompt + " ");
        if (r.answers.length) return Promise.resolve(r.answers.shift());
        return new Promise((resolve) => { r.asking = resolve; });
      },
      openWindow: (w, h, title) => {
        freshCanvas();
        game.canvas.width = w; game.canvas.height = h;
        game.W = w; game.H = h;
        panel.hidden = false;
        document.getElementById("game-title").dataset.name = title;
        wantFullscreen(false);
        fitCanvas();
        game.canvas.focus();
        game.mouse.mx = game.mouse.my = 0;
        const back = document.createElement("canvas");
        back.width = w; back.height = h;
        game.win = { screen: game.canvas, back };      // the runtime adds win.resize (see sharpness)
        watchSize();
        return game.win;
      },
      keys: game.keys, mouse: game.mouse,
      // fullscreen(): browsers only allow it straight after a click or key, so
      // if it's refused, the button lights up and the next click on the game does it
      fullscreen: () => {
        if (isFullscreen()) return;
        enterFullscreen().then((ok) => { if (!ok && !r.done && !r.cancelled) wantFullscreen(true); });
      },
      lockMouse: (on) => { if (!r.done && !r.cancelled) lockMouse(on); },
      mouseLocked,
      nextFrame: () => new Promise((resolve) => {
        const tick = (now) => {              // 60 frames a second, whatever the screen
          if (r.cancelled || now - r.lastFrame >= 1000 / 60 - 2) { r.lastFrame = now; resolve(); }
          else requestAnimationFrame(tick);
        };
        requestAnimationFrame(tick);
      }),
      cancelled: () => r.cancelled,
      loadPicture: (pic) => loadPicture(pic, d.path),
      loadFile: (file) => loadFile(file, d.path),
      gamepads: () => (navigator.getGamepads ? Array.from(navigator.getGamepads()) : []),
      addButton: (label, onclick) => {          // like "Enter VR", next to the game
        const button = document.createElement("button");
        button.className = "game-button";
        button.textContent = label;
        button.addEventListener("click", onclick);
        document.getElementById("game-buttons").append(button);
      },
      beep,
      now: () => performance.now(),
    };
    const lines = d.content.split("\n").map((l) => l.replace(/\s+$/, ""));
    FlauxRuntime.runProgram(built.js, io, { file: name, lines }).then((code) => {
      r.exit = code === null ? 130 : code;
      r.done = true;
      // Stop or a problem: back to the Studio, so the message can be read. A game
      // that finished normally stays as it is (its last picture is still shown).
      if (String(nextRun - 1) !== id) return;           // a newer run has the game panel now
      lockMouse(false);                                 // the pointer back, however it ended
      if (code !== 0) leaveFullscreen();
      else wantFullscreen(false);
    });
    return { ok: true, run: id };
  }

  function output(d) {
    const r = runs.get(String(d.run));
    if (!r) return { chunks: [], done: true, exit: null };
    // `from` counts every chunk ever made, so it keeps working after old ones are dropped
    const from = Math.max(d.from || 0, r.base);
    const chunks = r.chunks.slice(from - r.base);
    const skipped = (d.from || 0) < r.base;
    r.given = Math.max(r.given, from + chunks.length);
    return { chunks, start: from, skipped, done: r.done, exit: r.exit };
  }

  function input(d) {
    const r = runs.get(String(d.run));
    if (!r || r.done) return { ok: false };
    if (r.asking) { const resolve = r.asking; r.asking = null; resolve(d.text); }
    else r.answers.push(d.text);
    return { ok: true };
  }

  function stop(d) {
    const r = runs.get(String(d.run));
    if (r) {
      r.cancelled = true;
      if (r.asking) { const resolve = r.asking; r.asking = null; resolve(""); }
      for (const wake of [...r.sleepers]) wake();     // stop a `wait` straight away
    }
    return { ok: true };
  }

  // -------------------------------------------------------------- export
  // A .zip with the program as a real Windows program, made right here in the page: Flaux makes
  // the program into C (Python, like the desktop app), and TinyCC (a small C compiler, as
  // WebAssembly: tcc/, made by build_tcc.py) makes that into machine code.
  //   <name>/<name>.exe     the program (a game opens just its own window)
  //   <name>/SDL2.dll       for games (graphics, keys, sound)
  //   <name>/<pictures and models it uses>
  //   <name>/README.txt
  function exportName(path) {
    const name = nameOf(path).replace(/\.flx$/i, "").replace(/[^A-Za-z0-9 _-]+/g, "").trim();
    return !name || name === "untitled" ? "game" : name;
  }
  // the files the program names: pictures, 3D models and the .mtl files (and pictures) they use
  async function filesFor(source, path) {
    const found = {}, wanted = [...source.matchAll(/"([^"\n]+\.(?:png|jpe?g|bmp|gif|obj|mtl))"/gi)].map((m) => m[1]);
    while (wanted.length) {
      const name = wanted.shift();
      if (name in found || /(^|[\\/])\.\.([\\/]|$)/.test(name) || /^[\\/]|^[A-Za-z]:/.test(name)) continue;
      const url = await pictureUrl(name, path);
      if (!url) continue;
      try { found[name] = new Uint8Array(await (await fetch(url)).arrayBuffer()); } catch (e) { continue; }
      if (/\.(obj|mtl)$/i.test(name)) {
        const text = new TextDecoder().decode(found[name]);
        for (const m of text.matchAll(/^\s*(?:mtllib|map_Kd)\s+(.+?)\s*$/gm)) wanted.push(m[1]);
      }
    }
    return found;
  }
  let tccScript = null;
  function loadTcc() {             // tcc/tcc.js, the first time Export is used
    if (!tccScript) tccScript = new Promise((resolve, reject) => {
      const s = document.createElement("script");
      s.src = "tcc/tcc.js";
      s.onload = resolve;
      s.onerror = () => { tccScript = null; reject(new Error("This website doesn't have Flaux's C compiler (the tcc folder).")); };
      document.head.append(s);
    });
    return tccScript;
  }
  // C code -> a Windows program (bytes). `gui`: a window only, no console window.
  async function compileExe(c, graphics, gui) {
    await loadTcc();
    let said = "";
    const tcc = await window.createTcc({ locateFile: (f) => "tcc/" + f, print: (t) => { said += t + "\n"; },
                                         printErr: (t) => { said += t + "\n"; } });
    tcc.FS.writeFile("/program.c", c);
    const args = ["-I/sysroot/include/SDL2", "-I/sysroot/flaux", "-DFX_EXPORTED",
                  ...(gui ? ["-DFX_GUI_APP", "-Wl,-subsystem=gui"] : []),
                  "-o", "/program.exe", "/program.c", ...(graphics ? ["-lSDL2"] : []), "-lshell32", "-lole32", "-luser32"];
    let code;
    try { code = tcc.ccall("flaux_tcc", "number", ["string"], [args.join("\n")]); }
    catch (e) { code = 1; said += String(e && e.message || e); }
    if (code !== 0) throw new Error("Flaux's C compiler didn't accept the program. This is a bug in Flaux, not in your " +
                                    "program.\n" + said.trim());
    return tcc.FS.readFile("/program.exe");
  }
  async function exportGame(d) {
    const source = d.content || "", name = exportName(d.path);
    const built = await ask("build_c", source, nameOf(d.path));
    if (!built.ok) return built;
    let exe;
    try { exe = await compileExe(built.c, built.graphics, built.graphics && !built.says); }
    catch (e) { return { ok: false, message: e.message }; }
    const crlf = (t) => t.replace(/\n/g, "\r\n");
    const files = [[`${name}/${name}.exe`, exe, true]];
    if (built.graphics) {
      const dll = await fetch("tcc/SDL2.dll");
      if (!dll.ok) return { ok: false, message: "This website is missing tcc/SDL2.dll." };
      files.push([`${name}/SDL2.dll`, new Uint8Array(await dll.arrayBuffer())]);
    }
    for (const [file, bytes] of Object.entries(await filesFor(source, d.path)))
      files.push([`${name}/${file.replace(/\\/g, "/")}`, bytes]);
    files.push([`${name}/README.txt`, crlf(`${name}\n${"=".repeat(name.length)}\n\n` +
      `Made with Flaux (https://github.com/XOBNVR/Flaux-Code).\n\n` +
      `To play on Windows, double-click ${name}.exe. It's a real Windows program, in a window of its own.\n` +
      (built.graphics ? `Keep SDL2.dll${files.length > 2 ? " and the pictures" : ""} next to it: to share the game, ` +
                        `share the whole folder (or this zip).\n` : "") +
      `\n(If Windows says it protected your PC, click "More info", then "Run anyway": that happens\n` +
      `with any new program that isn't from a big company.)\n` +
      (built.graphics ? `\nF11 (or Alt+Enter) plays on the whole screen.\n` : ""))]);
    download(name + ".zip", makeZip(files), "application/zip");
    return { ok: true, name: name + ".zip", exe: true };
  }

  // A .zip file (stored, not compressed). files: [path, text or bytes, executable?]
  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(bytes) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  function makeZip(files) {
    const enc = new TextEncoder(), parts = [], central = [];
    const now = new Date();
    const time = (now.getHours() << 11) | (now.getMinutes() << 5) | (now.getSeconds() >> 1);
    const date = ((now.getFullYear() - 1980) << 9) | ((now.getMonth() + 1) << 5) | now.getDate();
    let offset = 0;
    for (const [path, text, executable] of files) {
      const name = enc.encode(path), data = typeof text === "string" ? enc.encode(text) : text;
      const crc = crc32(data);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true);
      local.setUint16(4, 20, true);              // version needed
      local.setUint16(6, 0x0800, true);          // names are UTF-8
      local.setUint16(8, 0, true);               // stored
      local.setUint16(10, time, true); local.setUint16(12, date, true);
      local.setUint32(14, crc, true);
      local.setUint32(18, data.length, true); local.setUint32(22, data.length, true);
      local.setUint16(26, name.length, true); local.setUint16(28, 0, true);
      const head = new DataView(new ArrayBuffer(46));
      head.setUint32(0, 0x02014b50, true);
      head.setUint16(4, (3 << 8) | 20, true);    // made on Unix (so the file mode below counts)
      head.setUint16(6, 20, true);
      head.setUint16(8, 0x0800, true);
      head.setUint16(10, 0, true);
      head.setUint16(12, time, true); head.setUint16(14, date, true);
      head.setUint32(16, crc, true);
      head.setUint32(20, data.length, true); head.setUint32(24, data.length, true);
      head.setUint16(28, name.length, true);
      head.setUint32(38, ((executable ? 0o100755 : 0o100644) << 16) >>> 0, true);
      head.setUint32(42, offset, true);
      parts.push(local, name, data);
      central.push(head, name);
      offset += 30 + name.length + data.length;
    }
    const size = central.reduce((n, p) => n + p.byteLength, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, files.length, true); end.setUint16(10, files.length, true);
    end.setUint32(12, size, true); end.setUint32(16, offset, true);
    return new Blob([...parts, ...central, end], { type: "application/zip" });
  }

  // ----------------------------------------------------------------- api
  async function api(path, d) {
    switch (path) {
      case "/api/ping": case "/api/folder": return { ok: true };
      case "/api/state": return state();
      case "/api/open": return open(d.path);
      case "/api/save": return save(d);
      case "/api/check": return ask("check", d.content || "", nameOf(d.path));
      case "/api/blocks": return ask("blocks", d.content || "");
      case "/api/blockinfo": return ask("blockinfo");
      case "/api/run": return run(d);
      case "/api/output": return output(d);
      case "/api/input": return input(d);
      case "/api/stop": return stop(d);
      case "/api/export": return exportGame(d);
    }
    throw new Error("Flaux Studio on the web doesn't know " + path);
  }

  function download(name, data, type) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(data instanceof Blob ? data : new Blob([data], { type: type || "text/plain" }));
    a.download = name;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  function openFromComputer(file, done) {
    const reader = new FileReader();
    reader.onload = () => {
      const name = safeName(file.name);
      const saved = save({ path: "mine:" + name, content: String(reader.result) });
      done(saved.ok === false ? null : saved.path);
    };
    reader.readAsText(file);
  }

  document.addEventListener("DOMContentLoaded", () => { setUpGame(); python(); });

  return { api, download, openFromComputer, addPictures, picturesSaved: () => Object.keys(load(PICTURES)),
           mouse: () => ({ x: game.mouse.x, y: game.mouse.y }),
           mouseLook: () => ({ wanted: look.wanted, locked: mouseLocked() }),
           gameSize: () => ({ w: game.W, h: game.H }) };
})();
