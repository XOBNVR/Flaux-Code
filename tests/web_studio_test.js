// Tests Flaux Studio on the web in a real browser (Playwright).
//
//   python3 -m flauxc.web.build site --pyodide <pyodide folder>
//   python3 -m http.server --directory site 8790 &
//   node tests/web_studio_test.js http://127.0.0.1:8790/
//
// Checks: Flaux starts in the page, a program runs and asks a question, a
// problem while running is explained, Apply fix, Blocks mode, saving in the
// browser, the graphics pixel tests from tests/graphics, pictures, keys,
// the Stop button, every 3D test from tests/graphics3d (WebGL 2), and VR with
// a pretend headset.
"use strict";
const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const url = process.argv[2] || "http://127.0.0.1:8790/";
const graphics = path.join(__dirname, "graphics");
const read = (name) => fs.readFileSync(path.join(graphics, name), "utf8");
const pixels = (src) => src.split("\n").filter((l) => l.startsWith("# pixel:")).map((l) => l.slice(8).trim().split(/\s+/));

// A pretend VR headset (WebXR), so VR can be tested without one: two eyes
// side by side on the page, 1.6 m above the floor, and a right controller
// with its trigger held.
function fakeHeadset() {
  const move = (x, y, z) => new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]);
  const proj = (() => {
    const f = 1 / Math.tan(Math.PI / 4), n = 0.05, far = 1000, m = new Float32Array(16);
    m[0] = f; m[5] = f; m[10] = (far + n) / (n - far); m[11] = -1; m[14] = 2 * far * n / (n - far);
    return m;
  })();
  const views = [{ eye: "left", transform: { matrix: move(-0.03, 1.6, 0) }, projectionMatrix: proj },
                 { eye: "right", transform: { matrix: move(0.03, 1.6, 0) }, projectionMatrix: proj }];
  const button = (on) => ({ pressed: on, value: on ? 1 : 0 });
  const frame = {
    getViewerPose: () => ({ views }),
    getPose: () => ({ transform: { matrix: move(0.2, 1.2, -0.3) } }),
  };
  const session = {
    inputSources: [{ handedness: "right", gripSpace: {},
      gamepad: { buttons: [button(true), button(false), button(false), button(false), button(false), button(false)], axes: [0, 0, 0, 0] } }],
    requestAnimationFrame: (cb) => setTimeout(() => cb(performance.now(), frame), 16),
    updateRenderState() {}, requestReferenceSpace: async () => ({}), addEventListener() {}, end() {},
  };
  Object.defineProperty(navigator, "xr", { value: {
    isSessionSupported: async (mode) => mode === "immersive-vr",
    requestSession: async () => session,
  } });
  WebGL2RenderingContext.prototype.makeXRCompatible = async () => {};
  window.XRWebGLLayer = class {
    constructor(s, gl) { this.framebuffer = null; this.w = gl.drawingBufferWidth; this.h = gl.drawingBufferHeight; }
    getViewport(view) { return { x: view.eye === "left" ? 0 : this.w / 2, y: 0, width: this.w / 2, height: this.h }; }
  };
}

let failures = 0;
function check(ok, what) { console.log((ok ? "OK    " : "FAIL  ") + what); if (!ok) failures++; }

(async () => {
  // WebGL 2 without a graphics card: Chromium's SwiftShader
  const browser = await chromium.launch({ args: ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader"] });
  const page = await browser.newPage({ viewport: { width: 1300, height: 820 } });
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.addInitScript(fakeHeadset);
  const started = Date.now();
  await page.goto(url);
  await page.waitForFunction(() => /Ready|No problems/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
  check(true, `Flaux started in the browser (${((Date.now() - started) / 1000).toFixed(1)} s)`);

  const setCode = (t) => page.evaluate((t) => { code.value = t; state.lastChecked = null; render(); }, t);
  const consoleText = () => page.textContent("#console");
  const runUntil = async (text) => {
    await page.click("#btn-run");
    await page.waitForFunction((t) => document.getElementById("console").textContent.includes(t), text, { timeout: 60000 });
    return consoleText();
  };
  const pixel = (x, y) => page.evaluate(([x, y]) => {
    const d = document.getElementById("game-canvas").getContext("2d").getImageData(x, y, 1, 1).data;
    return "#" + [d[0], d[1], d[2]].map((v) => v.toString(16).padStart(2, "0")).join("");
  }, [x, y]);
  const pixelChecks = async (src, what) => {
    const wrong = [];
    for (const [x, y, want] of pixels(src)) {
      const got = await pixel(+x, +y);
      if (got !== want.toLowerCase()) wrong.push(`(${x},${y}) is ${got}, not ${want}`);
    }
    check(!wrong.length, `${what}: ${pixels(src).length} pixel checks ${wrong.length ? "-- " + wrong.join("; ") : ""}`);
  };

  // a program that asks a question
  await setCode('set name to ask("Name?")\nsay "Hi {name}!"\nrepeat with i from 1 to 3\n    say i * 1.5\nend\n');
  await page.click("#btn-run");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Name?"), null, { timeout: 60000 });
  await page.fill("#input", "Mia");
  await page.press("#input", "Enter");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Finished"), null, { timeout: 60000 });
  check((await consoleText()).includes("Name? Mia\nHi Mia!\n1.5\n3\n4.5"), "a program runs and asks a question");

  // a problem while running
  await setCode("set nums to [1, 2, 3]\nsay nums[5]\n");
  const stopped = await runUntil("Stopped");
  check(stopped.includes("line 2") && stopped.includes("You asked for item number 5, but there are only 3."),
        "a problem while running is explained");

  // a mistake, and Apply fix
  await page.evaluate(() => { code.value = "set score to 10\nsay scroe\n"; check(); });
  await page.waitForSelector("#problem:not([hidden])", { timeout: 60000 });
  await page.click("#p-apply");
  await page.waitForSelector("#problem[hidden]", { state: "attached", timeout: 60000 });
  check((await page.evaluate(() => code.value)) === "set score to 10\nsay score\n", "Apply fix fixes a mistake");

  // blocks
  await page.click("#mode-blocks");
  await page.waitForFunction(() => state.mode === "blocks" && FlauxBlocks.workspace && FlauxBlocks.workspace.getAllBlocks(false).length > 0, null, { timeout: 60000 });
  check(true, "Blocks mode shows the program as blocks");
  await page.click("#mode-code");

  // saving in the browser
  await page.click("#btn-save");
  await page.fill("#save-name", "web test");
  await page.click("#save-ok");
  await page.waitForFunction(() => (localStorage.getItem("flaux-web-programs") || "").includes("web_test.flx"));
  check(true, "a program is saved in the browser");

  // graphics: the same pixels as the desktop tests
  await setCode(read("shapes.flx"));
  await runUntil("Finished");
  await pixelChecks(read("shapes.flx"), "shapes");
  const checker = fs.readFileSync(path.join(graphics, "checker.png")).toString("base64");
  await page.evaluate((png) => localStorage.setItem("flaux-web-pictures",
    JSON.stringify({ "checker.png": "data:image/png;base64," + png })), checker);
  await setCode(read("picture_file.flx"));
  await runUntil("Finished");
  await pixelChecks(read("picture_file.flx"), "pictures");

  // a game: keys move the square, and Stop ends it
  await setCode('window(300, 200)\nset x to 50\nrepeat forever\n    if key_down("right")\n        add 5 to x\n    end\n' +
                '    clear("black")\n    rect(x, 90, 20, 20, "white")\n    show()\nend\n');
  await page.click("#btn-run");
  await page.waitForSelector("#game:not([hidden])", { timeout: 60000 });
  await page.waitForTimeout(300);
  await page.focus("#game-canvas");
  await page.keyboard.down("ArrowRight");
  await page.waitForTimeout(700);
  await page.keyboard.up("ArrowRight");
  await page.waitForTimeout(200);
  const x = await page.evaluate(() => {
    const d = document.getElementById("game-canvas").getContext("2d").getImageData(0, 100, 300, 1).data;
    for (let i = 0; i < 300; i++) if (d[i * 4] > 200) return i;
    return -1;
  });
  check(x > 80, `holding the right arrow moves the square (from x=50 to x=${x})`);
  await page.click("#btn-stop");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Stopped"), null, { timeout: 15000 });
  check(true, "the Stop button stops a game");

  // 3D and shaders: every tests/graphics3d program, with the same pixel checks
  const dir3d = path.join(__dirname, "graphics3d");
  const files = {};
  for (const f of fs.readdirSync(dir3d).filter((f) => /\.(png|obj|mtl)$/.test(f))) {
    const type = f.endsWith(".png") ? "image/png" : "text/plain";
    files[f] = `data:${type};base64,` + fs.readFileSync(path.join(dir3d, f)).toString("base64");
  }
  await page.evaluate((f) => localStorage.setItem("flaux-web-pictures", JSON.stringify(f)), files);
  const glPixel = (x, y) => page.evaluate(([x, y]) => {
    const src = document.getElementById("game-canvas");
    const c = document.createElement("canvas"); c.width = src.width; c.height = src.height;
    const g = c.getContext("2d"); g.drawImage(src, 0, 0);
    const d = g.getImageData(x, y, 1, 1).data;
    return "#" + [d[0], d[1], d[2]].map((v) => v.toString(16).padStart(2, "0")).join("");
  }, [x, y]);
  const near = (a, b) => [1, 3, 5].every((i) => Math.abs(parseInt(a.slice(i, i + 2), 16) - parseInt(b.slice(i, i + 2), 16)) <= 24);
  for (const f of fs.readdirSync(dir3d).filter((f) => f.endsWith(".flx")).sort()) {
    const src = fs.readFileSync(path.join(dir3d, f), "utf8");
    await setCode(src);
    await runUntil("Finished");
    await page.waitForTimeout(100);
    const wrong = [];
    for (const l of src.split("\n").filter((l) => /^# (pixel|near):/.test(l))) {
      const [kind, x, y, want] = l.slice(2).replace(":", "").split(/\s+/);
      const got = await glPixel(+x, +y);
      if (kind === "pixel" ? got !== want.toLowerCase() : !near(got, want.toLowerCase())) wrong.push(`(${x},${y}) is ${got}, not ${want}`);
    }
    check(!wrong.length, `3D ${f}: pixel checks ${wrong.length ? "-- " + wrong.join("; ") : ""}`);
  }

  // VR (with a pretend headset): both eyes are drawn, and VR controller
  // buttons work like a game controller's
  await setCode('window(400, 200)\nrepeat forever\n    sky("black")\n    camera(0, 1.6, 3, 0, 1.6, 0)\n' +
                '    if in_vr() and button_down("rt")\n        cube(0, 1.6, 0, 1, "red")\n    else\n' +
                '        cube(0, 1.6, 0, 1, "blue")\n    end\n    show()\nend\n');
  await page.click("#btn-run");
  await page.waitForSelector("#game-buttons button", { timeout: 60000 });
  check((await page.textContent("#game-buttons")).includes("Enter VR"), "3D programs get an Enter VR button");
  await page.click("#game-buttons button");
  await page.waitForTimeout(1500);
  const left = await glPixel(100, 100), right = await glPixel(300, 100);
  const reddish = (c) => parseInt(c.slice(1, 3), 16) > 100 && parseInt(c.slice(5, 7), 16) < 80;
  check(reddish(left) && reddish(right),
        `in VR, both eyes see the red cube (the trigger is held): left ${left}, right ${right}`);
  await page.click("#btn-stop");

  check(!errors.length, "no errors in the page" + (errors.length ? ": " + errors.join(" | ") : ""));
  await browser.close();
  console.log(failures ? `${failures} problem(s)` : "Flaux Studio on the web works.");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error("The test couldn't finish: " + e.message); process.exit(1); });
