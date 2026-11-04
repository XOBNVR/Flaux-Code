// Runs one compiled Flaux program (JavaScript) with Node, for tests/js_tests.py.
//   node tests/js_runner.js program.js original.flx   (typed answers come from stdin)
// Graphics programs get a pretend window that draws nothing when FLAUX_FRAMES
// is set (like the native test hooks): the program stops after that many
// show()s, and FLAUX_MOUSE_MOVE=dx,dy pretends the mouse moves that far every frame.
"use strict";
const fs = require("fs");
const path = require("path");
const { runProgram } = require(path.join(__dirname, "..", "flauxc", "web", "fx.js"));

const [jsFile, flxFile] = process.argv.slice(2);
const answers = fs.readFileSync(0, "utf8").split("\n");
const lines = fs.readFileSync(flxFile, "utf8").split("\n").map((l) => l.replace(/\s+$/, ""));
const frames = process.env.FLAUX_FRAMES ? +process.env.FLAUX_FRAMES : -1;
const [moveX, moveY] = (process.env.FLAUX_MOUSE_MOVE || "0,0").split(",").map(Number);
let shown = 0, locked = false;
const nothing = () => new Proxy({}, { get: (t, k) => (k in t ? t[k] : () => {}) });  // draws nothing
const fakeCanvas = (w, h) => ({ width: w, height: h, getContext: nothing });
// FLAUX_HOLD_MOUSE=right (or left, middle): pretend those mouse buttons are held
const held = process.env.FLAUX_HOLD_MOUSE || "";
const mouse = { x: 0, y: 0, down: held.includes("left"), mx: moveX, my: moveY,
                buttons: (held.includes("left") ? 1 : 0) | (held.includes("middle") ? 2 : 0) | (held.includes("right") ? 4 : 0) };
const io = {
  write: (stream, text) => (stream === "err" ? process.stderr : process.stdout).write(text),
  ask: async (prompt) => {
    if (prompt) process.stdout.write(prompt.endsWith(" ") ? prompt : prompt + " ");
    return answers.length ? answers.shift().replace(/\r$/, "") : "";
  },
  openWindow: (w, h) => {
    if (frames < 0) throw new Error("no windows in this test");
    return { screen: fakeCanvas(w, h), back: fakeCanvas(w, h) };
  },
  keys: new Set(), mouse,
  lockMouse: (on) => { locked = on; }, mouseLocked: () => locked,
  nextFrame: () => new Promise((r) => setTimeout(() => {
    shown++; mouse.mx += moveX; mouse.my += moveY; r();
  }, 0)),
  cancelled: () => frames >= 0 && shown >= frames,
  loadPicture: async () => null,
  beep: () => {},
  now: () => Date.now(),
};
runProgram(fs.readFileSync(jsFile, "utf8"), io, { file: path.basename(flxFile), lines })
  .then((code) => process.exit(code === 0 || (code === null && frames >= 0) ? 0 : 1));
