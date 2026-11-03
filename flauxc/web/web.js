/* Flaux Studio on the web: does the jobs the desktop Studio's helper
 * (studio/server.py) does, but inside the browser.
 *
 * - Python runs in the page (Pyodide) with Flaux's own compiler, so checking,
 *   Apply fix and the block editor work exactly like the desktop app
 * - programs become JavaScript (jsgen.py) and run on fx.js
 * - your programs and pictures are kept in this browser (localStorage)
 * - games draw in the game panel, with keyboard, mouse and sound
 *
 * index.html calls FlauxWeb.api(path, data) instead of asking a server.
 */
"use strict";

window.FlauxWeb = (() => {
  const PROGRAMS = "flaux-web-programs", PICTURES = "flaux-web-pictures";
  const MAX_CHUNKS = 20000;
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
  const game = { canvas: null, keys: new Set(), mouse: { x: 0, y: 0, down: false } };

  const KEY_NAMES = { ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down", " ": "space",
    Enter: "enter", Escape: "escape", Shift: "shift", Control: "ctrl", Alt: "alt", Tab: "tab",
    Backspace: "backspace", Delete: "delete", Insert: "insert", Home: "home", End: "end",
    PageUp: "pageup", PageDown: "pagedown" };
  function keyName(e) { return KEY_NAMES[e.key] || (e.key.length === 1 ? e.key.toLowerCase() : e.key.toLowerCase()); }

  function setUpGame() {
    const canvas = document.getElementById("game-canvas");
    game.canvas = canvas;
    canvas.addEventListener("keydown", (e) => { game.keys.add(keyName(e)); e.preventDefault(); });
    canvas.addEventListener("keyup", (e) => { game.keys.delete(keyName(e)); e.preventDefault(); });
    canvas.addEventListener("blur", () => game.keys.clear());
    const where = (e) => {
      const r = canvas.getBoundingClientRect();
      game.mouse.x = (e.clientX - r.left) * canvas.width / r.width;
      game.mouse.y = (e.clientY - r.top) * canvas.height / r.height;
    };
    canvas.addEventListener("mousemove", where);
    canvas.addEventListener("mousedown", (e) => { where(e); game.mouse.down = true; canvas.focus(); });
    window.addEventListener("mouseup", () => { game.mouse.down = false; });
  }

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
    const r = { chunks: [], done: false, exit: null, cancelled: false, asking: null, answers: [], lastFrame: 0 };
    runs.set(id, r);
    const panel = document.getElementById("game");
    panel.hidden = true;
    game.keys.clear();
    const io = {
      write: (stream, text) => { if (r.chunks.length < MAX_CHUNKS) r.chunks.push([stream, text]); },
      ask: (prompt) => {
        if (prompt) io.write("out", prompt.endsWith(" ") ? prompt : prompt + " ");
        if (r.answers.length) return Promise.resolve(r.answers.shift());
        return new Promise((resolve) => { r.asking = resolve; });
      },
      openWindow: (w, h, title) => {
        game.canvas.width = w; game.canvas.height = h;
        panel.hidden = false;
        document.getElementById("game-title").textContent = title + "  (click here to use the keys)";
        game.canvas.focus();
        const back = document.createElement("canvas");
        back.width = w; back.height = h;
        return { screen: game.canvas, back };
      },
      keys: game.keys, mouse: game.mouse,
      nextFrame: () => new Promise((resolve) => {
        const tick = (now) => {              // 60 frames a second, whatever the screen
          if (r.cancelled || now - r.lastFrame >= 1000 / 60 - 2) { r.lastFrame = now; resolve(); }
          else requestAnimationFrame(tick);
        };
        requestAnimationFrame(tick);
      }),
      cancelled: () => r.cancelled,
      loadPicture: (pic) => loadPicture(pic, d.path),
      beep,
      now: () => performance.now(),
    };
    const lines = d.content.split("\n").map((l) => l.replace(/\s+$/, ""));
    FlauxRuntime.runProgram(built.js, io, { file: name, lines }).then((code) => {
      r.exit = code === null ? 130 : code;
      r.done = true;
    });
    return { ok: true, run: id };
  }

  function output(d) {
    const r = runs.get(String(d.run));
    if (!r) return { chunks: [], done: true, exit: null };
    return { chunks: r.chunks.slice(d.from || 0), done: r.done, exit: r.exit };
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
    }
    return { ok: true };
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
    }
    throw new Error("Flaux Studio on the web doesn't know " + path);
  }

  function download(name, text) {
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([text], { type: "text/plain" }));
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

  return { api, download, openFromComputer, addPictures, picturesSaved: () => Object.keys(load(PICTURES)) };
})();
