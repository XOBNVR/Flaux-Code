// Runs one compiled Flaux program (JavaScript) with Node, for tests/js_tests.py.
//   node tests/js_runner.js program.js original.flx   (typed answers come from stdin)
"use strict";
const fs = require("fs");
const path = require("path");
const { runProgram } = require(path.join(__dirname, "..", "flauxc", "web", "fx.js"));

const [jsFile, flxFile] = process.argv.slice(2);
const answers = fs.readFileSync(0, "utf8").split("\n");
const lines = fs.readFileSync(flxFile, "utf8").split("\n").map((l) => l.replace(/\s+$/, ""));
const io = {
  write: (stream, text) => (stream === "err" ? process.stderr : process.stdout).write(text),
  ask: async (prompt) => {
    if (prompt) process.stdout.write(prompt.endsWith(" ") ? prompt : prompt + " ");
    return answers.length ? answers.shift().replace(/\r$/, "") : "";
  },
  openWindow: () => { throw new Error("no windows in this test"); },
  keys: new Set(), mouse: { x: 0, y: 0, down: false },
  nextFrame: () => new Promise((r) => setTimeout(r, 0)),
  cancelled: () => false,
  loadPicture: async () => null,
  beep: () => {},
  now: () => Date.now(),
};
runProgram(fs.readFileSync(jsFile, "utf8"), io, { file: path.basename(flxFile), lines })
  .then((code) => process.exit(code === 0 ? 0 : 1));
