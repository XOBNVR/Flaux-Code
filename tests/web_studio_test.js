// Tests Flaux Studio on the web in a real browser (Playwright).
//
//   python3 -m flauxc.web.build site --pyodide <pyodide folder>
//   python3 -m http.server --directory site 8790 &
//   node tests/web_studio_test.js http://127.0.0.1:8790/
//
// Checks: Flaux starts in the page, a program runs and asks a question, say.same, a
// problem while running is explained, Apply fix, Blocks mode, saving in the
// browser, the Course (a quiz, and a challenge), the graphics pixel tests from tests/graphics, pictures, keys,
// fullscreen (size, keys and mouse positions, drawn at the screen's resolution,
// triangles without seams), Export, the Stop button, and
// mouse look (lock_mouse() asks for pointer lock, again on a click; mouse
// movement; the pointer is let go at Stop) in the Studio; Export making real Windows programs
// (run with Wine when it's there);
// every 3D test from tests/graphics3d (WebGL 2), 3D in fullscreen (at the
// screen's resolution) and exported, touch, and VR with a pretend headset.
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const { execFileSync } = require("child_process");
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
  // the colour of program pixel (x, y): the canvas can have more pixels than the program's own,
  // so this looks at the middle of that pixel
  const pixel = (x, y) => page.evaluate(([x, y]) => {
    const c = document.getElementById("game-canvas"), g = FlauxWeb.gameSize();
    const d = c.getContext("2d").getImageData(Math.floor((x + 0.5) * c.width / g.w),
                                              Math.floor((y + 0.5) * c.height / g.h), 1, 1).data;
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

  // say.same: the line grows while the program runs ("hi", then "hi hi", ...), then a say finishes it
  await setCode('say "Start"\nrepeat 3 times\n    say.same "hi "\n    wait 0.4s\nend\nsay "!"\nsay "Next line"\n');
  await page.click("#btn-run");
  await page.waitForFunction(() => /Start\nhi hi $/.test(document.getElementById("console").textContent), null, { timeout: 60000 });
  const midway = await consoleText();
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Finished"), null, { timeout: 60000 });
  const same = await consoleText();
  check(midway.endsWith("Start\nhi hi ") && same.includes("Start\nhi hi hi !\nNext line\n"),
        `say.same stays on the same line (${JSON.stringify(midway.slice(-14))} while running, then ${JSON.stringify(same.slice(0, 30))})`);

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

  // the Course: a quiz question answered right and one wrong, then a challenge done in the editor
  const savedPath = await page.evaluate(() => state.path);
  await page.click("#btn-course");
  await page.waitForFunction(() => document.querySelectorAll("#c-units button").length === 18, null, { timeout: 30000 });
  const courseSize = await page.evaluate(() => Course.data().questions.length);
  check(courseSize >= 1000, `the Course opens (18 units, ${courseSize} questions)`);
  await page.click('.c-tabs button[data-ctab="quiz"]');
  await page.click("#c-foot button");
  let answer = await page.evaluate(() => Course.current().answer);
  await page.click(`.answers button:nth-child(${answer + 1})`);
  const rightText = await page.textContent(".c-feedback");
  await page.click("#c-foot button");
  answer = await page.evaluate(() => Course.current().answer);
  await page.click(`.answers button:nth-child(${(answer + 1) % 2 + 1})`);
  const wrongText = await page.textContent(".c-feedback");
  check(rightText.includes("Right") && wrongText.includes("Not quite"),
        `a quiz question answered right, and one wrong (${JSON.stringify(rightText)}, ${JSON.stringify(wrongText)})`);
  const saved = await page.evaluate(() => Object.values(JSON.parse(localStorage.getItem("flaux-course")).answers).sort());
  check(JSON.stringify(saved) === "[0,1]", `the answers are kept in the browser (${JSON.stringify(saved)})`);
  await page.evaluate(() => Course.pickUnit("say"));
  await page.click('.c-tabs button[data-ctab="challenge"]');
  await page.click("#c-foot button");
  await page.waitForFunction(() => !document.getElementById("course-dialog").open);
  const started_ = await page.evaluate(() => code.value);
  await setCode(started_ + 'say "My robot is Bolt"\nsay "Beep boop!"\n');
  await page.click("#btn-run");
  await page.waitForFunction(() => /Challenge done|Stopped|Finished/.test(document.getElementById("console").textContent), null, { timeout: 60000 });
  const challengeOut = await consoleText();
  const chDone = await page.evaluate(() => JSON.parse(localStorage.getItem("flaux-course")).challenges);
  check(started_.startsWith("# Challenge:") && challengeOut.includes("Challenge done: Saying things") && chDone.includes("say"),
        `a challenge done in the editor (${JSON.stringify(challengeOut.slice(-60))})`);
  await page.evaluate(async (p) => { state.dirty = false; await openFile(p); }, savedPath);   // (back to the saved program)

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
    const c = document.getElementById("game-canvas"), k = c.width / FlauxWeb.gameSize().w;
    const d = c.getContext("2d").getImageData(0, Math.floor(100.5 * c.height / FlauxWeb.gameSize().h), c.width, 1).data;
    for (let i = 0; i < c.width; i++) if (d[i * 4] > 200) return Math.floor(i / k);
    return -1;
  });
  check(x > 80, `holding the right arrow moves the square (from x=50 to x=${x})`);

  // fullscreen: the canvas fills the screen, same shape, and clicks give canvas pixels
  await page.click("#btn-fullscreen");
  await page.waitForFunction(() => document.fullscreenElement && document.fullscreenElement.id === "game-screen", null, { timeout: 5000 });
  await page.waitForTimeout(200);
  const vp = page.viewportSize();
  const box = await page.evaluate(() => { const r = document.getElementById("game-canvas").getBoundingClientRect();
    return { x: r.left, y: r.top, w: r.width, h: r.height, focus: document.activeElement.id }; });
  const s = Math.min(vp.width / 300, vp.height / 200);
  check(Math.abs(box.w - 300 * s) < 1 && Math.abs(box.h - 200 * s) < 1 && box.focus === "game-canvas",
        `fullscreen fits the canvas to the screen (${box.w}x${box.h} at ${box.x},${box.y})`);
  await page.mouse.move(box.x + box.w * 0.25, box.y + box.h * 0.75);
  await page.waitForTimeout(100);
  const mouse = await page.evaluate(() => FlauxWeb.mouse());
  check(mouse && Math.abs(mouse.x - 75) <= 1 && Math.abs(mouse.y - 150) <= 1, `fullscreen mouse is in canvas pixels (${JSON.stringify(mouse)})`);
  // drawn at the screen's full resolution: as many canvas pixels as it's shown at, and the
  // square (a rect) still has sharp edges: every pixel across it is all white or all black
  await page.waitForFunction((w) => document.getElementById("game-canvas").width === w, Math.round(box.w), { timeout: 5000 })
    .catch(() => {});
  const sharp = await page.evaluate(() => {
    const c = document.getElementById("game-canvas"), g = FlauxWeb.gameSize();
    const d = c.getContext("2d").getImageData(0, Math.floor(100.5 * c.height / g.h), c.width, 1).data;
    let white = 0, between = 0;
    for (let i = 0; i < c.width; i++) {
      const v = d[i * 4];
      if (v === 255) white++; else if (v !== 0) between++;
    }
    return { w: c.width, h: c.height, white, between, k: c.width / g.w };
  });
  check(sharp.w === Math.round(box.w) && sharp.h === Math.round(box.h),
        `fullscreen draws at the screen's resolution (${sharp.w}x${sharp.h} pixels for the 300x200 game)`);
  check(Math.abs(sharp.white - 20 * sharp.k) <= 1 && sharp.between === 0,
        `rects stay sharp in fullscreen (${sharp.white} white pixels across the 20-pixel square, ${sharp.between} in between)`);
  await page.evaluate(() => document.exitFullscreen());
  await page.waitForFunction(() => !document.fullscreenElement && document.getElementById("game-canvas").style.width.startsWith("min("));
  await page.waitForFunction(() => document.getElementById("game-canvas").width === 300, null, { timeout: 5000 }).catch(() => {});
  check(await page.evaluate(() => document.getElementById("game-canvas").width) === 300,
        "leaving fullscreen puts the game back in the panel, at its own size");
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
  // program pixel (x, y) of a WebGL canvas (the middle of it, if the canvas has more pixels)
  const glPixel = (x, y) => page.evaluate(([x, y]) => {
    const src = document.getElementById("game-canvas"), size = FlauxWeb.gameSize();
    const c = document.createElement("canvas"); c.width = src.width; c.height = src.height;
    const g = c.getContext("2d"); g.drawImage(src, 0, 0);
    const d = g.getImageData(Math.floor((x + 0.5) * src.width / size.w), Math.floor((y + 0.5) * src.height / size.h), 1, 1).data;
    return "#" + [d[0], d[1], d[2]].map((v) => v.toString(16).padStart(2, "0")).join("");
  }, [x, y]);
  const near = (a, b) => [1, 3, 5].every((i) => Math.abs(parseInt(a.slice(i, i + 2), 16) - parseInt(b.slice(i, i + 2), 16)) <= 24);
  for (const f of fs.readdirSync(dir3d).filter((f) => f.endsWith(".flx")).sort()) {
    const src = fs.readFileSync(path.join(dir3d, f), "utf8");
    // (a pretend screen size or mouse is for the desktop app's tests: the browser has its own below)
    if (/^# (screen|mouse|mouse_move):/m.test(src)) continue;
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

  // triangles side by side (a skewed mesh, white on black, like the faces of a 3D game) show no
  // seams: in the panel, and in fullscreen, where they're drawn at the screen's resolution
  await setCode('window(320, 200)\nrepeat forever\n    clear("black")\n    repeat with i from 0 to 9\n' +
    '        repeat with j from 0 to 4\n            set x to 40 + i * 24 + j * 5\n            set y to 30 + j * 24 + i * 3\n' +
    '            triangle(x, y, x + 24, y + 3, x + 29, y + 27, "white")\n' +
    '            triangle(x, y, x + 29, y + 27, x + 5, y + 24, "white")\n        end\n    end\n    show()\nend\n');
  const seams = () => page.evaluate(() => {
    const c = document.getElementById("game-canvas"), g = FlauxWeb.gameSize(), k = c.width / g.w, ky = c.height / g.h;
    const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
    // inside the mesh (corner 40,30, sides 240,30 and 25,120), at least 2 canvas pixels from its edges
    const det = 240 * 120 - 30 * 25, mu = (2 / k) * Math.hypot(25, 120) / det, mv = (2 / k) * Math.hypot(240, 30) / det;
    let inside = 0, bad = 0, darkest = 765;
    for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) {
      const px = (x + 0.5) / k - 40, py = (y + 0.5) / ky - 30;
      const u = (px * 120 - py * 25) / det, v = (240 * py - 30 * px) / det;
      if (u < mu || u > 1 - mu || v < mv || v > 1 - mv) continue;
      const i = (y * c.width + x) * 4, sum = d[i] + d[i + 1] + d[i + 2];
      inside++;
      if (sum < 740) bad++;           // some of the black background shows through
      darkest = Math.min(darkest, sum);
    }
    return { w: c.width, h: c.height, inside, bad, darkest };
  });
  await page.click("#btn-run");
  await page.waitForSelector("#game:not([hidden])", { timeout: 60000 });
  await page.waitForTimeout(300);
  let mesh = await seams();
  check(mesh.inside > 20000 && mesh.bad === 0,
        `no seams between triangles in the panel (${mesh.w}x${mesh.h}: ${mesh.bad} of ${mesh.inside} pixels, darkest ${mesh.darkest}/765)`);
  await page.click("#btn-fullscreen");
  await page.waitForFunction(() => document.fullscreenElement && document.getElementById("game-canvas").width > 1000, null, { timeout: 5000 });
  await page.waitForTimeout(300);
  mesh = await seams();
  check(mesh.inside > 300000 && mesh.bad === 0,
        `no seams between triangles in fullscreen (${mesh.w}x${mesh.h}: ${mesh.bad} of ${mesh.inside} pixels, darkest ${mesh.darkest}/765)`);
  await page.evaluate(() => document.exitFullscreen());
  await page.waitForFunction(() => !document.fullscreenElement);
  await page.click("#btn-stop");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Stopped"), null, { timeout: 15000 });

  // Export: a zip with the game as a real Windows program: Flaux makes it into C, and TinyCC
  // (tcc/, WebAssembly) compiles that in the page. With Wine here, the .exe is run too.
  const unzip = (z) => {                    // stored files, one after another
    const files = {};
    for (let at = 0; z.readUInt32LE(at) === 0x04034b50;) {
      const size = z.readUInt32LE(at + 18), nlen = z.readUInt16LE(at + 26), xlen = z.readUInt16LE(at + 28);
      files[z.slice(at + 30, at + 30 + nlen).toString()] = z.slice(at + 30 + nlen + xlen, at + 30 + nlen + xlen + size);
      at += 30 + nlen + xlen + size;
    }
    return files;
  };
  // press Export and wait for the zip (if it doesn't come, say what the Studio said instead)
  const exportZip = async () => {
    const [d] = await Promise.all([page.waitForEvent("download", { timeout: 120000 }).catch(async () => {
      throw new Error("Export gave no zip. The Studio said: " + await page.textContent("#status") + " | " +
                      (await page.textContent("#console")).slice(-600));
    }), page.click("#btn-export")]);
    return [d];
  };
  const subsystem = (exe) => exe.readUInt16LE(exe.readUInt32LE(0x3C) + 0x5C);   // 2: a window only, 3: a console
  const hasWine = (() => { try { execFileSync("wine", ["--version"], { stdio: "ignore" }); return true; } catch (e) { return false; } })();
  const runExported = (files, base, extraEnv) => {      // unzip into a folder and run <base>.exe with Wine
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "flaux-export-"));
    for (const [n, data] of Object.entries(files)) {
      fs.mkdirSync(path.join(dir, path.dirname(n)), { recursive: true });
      fs.writeFileSync(path.join(dir, n), data);
    }
    const shot = path.join(dir, "shot.bmp");
    let out = "";
    try {
      out = execFileSync("wine", [path.join(dir, base, base + ".exe")], { cwd: path.join(dir, base), timeout: 120000,
        env: { ...process.env, WINEDEBUG: "-all", SDL_VIDEODRIVER: "dummy", SDL_AUDIODRIVER: "dummy", FLAUX_FAST: "1",
               FLAUX_FRAMES: "2", FLAUX_SCREENSHOT: "Z:" + shot, ...extraEnv }, input: "" }).toString();
    } catch (e) { out = "failed: " + (e.stdout || "") + (e.stderr || "") + e.message; }
    return { out: out.replace(/\r\n/g, "\n"), shot: fs.existsSync(shot) ? fs.readFileSync(shot) : null };
  };
  const bmpPixel = (bmp, x, y) => {      // "#rrggbb" of a 32-bit .bmp from SDL (rows from the bottom)
    const off = bmp.readUInt32LE(10), w = bmp.readInt32LE(18), h = bmp.readInt32LE(22);
    const row = h > 0 ? h - 1 - y : y, i = off + (row * w + x) * 4;
    return "#" + [bmp[i + 2], bmp[i + 1], bmp[i]].map((v) => v.toString(16).padStart(2, "0")).join("");
  };
  const [download] = await exportZip();
  const zip = fs.readFileSync(await download.path());
  const base = download.suggestedFilename().replace(/\.zip$/, "");    // the program's name (web_test, saved above)
  const inZip = unzip(zip);
  const exe = inZip[`${base}/${base}.exe`];
  check(base === "web_test" && !!exe && `${base}/SDL2.dll` in inZip && `${base}/README.txt` in inZip &&
        !Object.keys(inZip).some((n) => /\.(html|bat)$/.test(n)),
        `Export downloads ${download.suggestedFilename()}: ${Object.keys(inZip).join(", ")}`);
  check(!!exe && exe.slice(0, 2).toString() === "MZ" && subsystem(exe) === 2,
        `${base}.exe is a real Windows program, a window only (${exe ? exe.length : 0} bytes, subsystem ${exe ? subsystem(exe) : "?"})`);
  if (exe && hasWine) {
    const ran = runExported(inZip, base, {});
    const colours = ran.shot ? [bmpPixel(ran.shot, 5, 5), bmpPixel(ran.shot, 120, 90)] : [];
    check(colours[0] === "#000000" && colours[1] === "#ffffff",
          `the exported game runs and draws (Wine): ${ran.shot ? colours.join(" ") : "no picture. " + ran.out.slice(0, 300)}`);
  } else if (exe) console.log("(the exported game isn't run here: no Wine)");

  // a program that says things: a console program
  await setCode('say.same "hi from"\nsay " the exe"\nsay 6 * 7\n');
  const [textZip] = await exportZip();
  const textFiles = unzip(fs.readFileSync(await textZip.path()));
  const textBase = textZip.suggestedFilename().replace(/\.zip$/, ""), textExe = textFiles[`${textBase}/${textBase}.exe`];
  check(!!textExe && subsystem(textExe) === 3 && !(`${textBase}/SDL2.dll` in textFiles),
        `a program that says things exports as a console program (${Object.keys(textFiles).join(", ")})`);
  if (textExe && hasWine) {
    const ran = runExported(textFiles, textBase, {});
    check(ran.out.startsWith("hi from the exe\n42\n"), `...and says the right things (Wine): ${JSON.stringify(ran.out.slice(0, 120))}`);
  }

  // mouse look. Headless Chromium locks the pointer (clicking Run counts as the click
  // browsers want), but its pretend mouse doesn't move a locked pointer. So: lock_mouse()
  // asks for the lock; letting go (what Esc does) leaves the program wanting it; the
  // mouse moves unlocked; and a click on the game asks for the lock again. Movement is
  // in screen (CSS) pixels: the 800x600 game is shown smaller in the panel and bigger
  // fullscreen, and the same movement must give the same numbers everywhere.
  const spyLocks = () => {
    window.__locks = 0;
    const ask = HTMLCanvasElement.prototype.requestPointerLock;
    HTMLCanvasElement.prototype.requestPointerLock = function (...a) { window.__locks++; return ask.apply(this, a); };
  };
  const LOOK = 'window(800, 600)\nlock_mouse()\nset tx to 0\nset ty to 0\nrepeat forever\n' +
    '    if mouse_moved_x() != 0 or mouse_moved_y() != 0\n        set tx to tx + mouse_moved_x()\n' +
    '        set ty to ty + mouse_moved_y()\n        say "moved {tx} {ty}"\n    end\n' +
    '    if mouse_clicked()\n        say "click locked={mouse_locked()}"\n    end\n    show()\nend\n';
  const lastMove = async (pg, out) => {
    const m = (await pg.textContent(out)).match(/moved (-?\d+) (-?\d+)\n(?![\s\S]*moved)/);
    return m ? [+m[1], +m[2]] : [0, 0];
  };
  // move the (unlocked) mouse dx, dy screen pixels over the canvas: mouse_moved_x/y() must add up to that
  const moveCheck = async (pg, canvasSel, out, dx, dy, where) => {
    const r = await pg.evaluate((sel) => { const b = document.querySelector(sel).getBoundingClientRect();
      return { x: b.left, y: b.top, w: b.width, h: b.height }; }, canvasSel);
    await pg.mouse.move(r.x + r.w / 2, r.y + r.h / 2);
    await pg.waitForTimeout(200);
    const [sx, sy] = await lastMove(pg, out);
    await pg.mouse.move(r.x + r.w / 2 + dx, r.y + r.h / 2 + dy, { steps: 5 });
    await pg.waitForTimeout(300);
    const [ex, ey] = await lastMove(pg, out);
    check(ex - sx === dx && ey - sy === dy, `${where}: mouse_moved_x/y() give screen pixels (${ex - sx}, ${ey - sy}` +
          ` for ${dx}, ${dy}; the 800x600 game is shown at ${(r.w / 800).toFixed(2)}x)`);
  };
  const lookTest = async (pg, canvasSel, out, where, hintSel) => {
    await pg.waitForTimeout(300);
    check(await pg.evaluate(() => window.__locks) >= 1, `${where}: lock_mouse() asks the browser to lock the pointer` +
          ` (locked: ${await pg.evaluate(() => !!document.pointerLockElement)})`);
    await pg.evaluate(() => document.exitPointerLock());           // what Esc does
    await pg.waitForFunction(() => !document.pointerLockElement);
    await pg.waitForTimeout(100);
    if (hintSel) {
      const hint = await pg.textContent(hintSel);
      check(hint.includes("click the game to look around"), `${where}: after Esc it says to click to look around ("${hint}")`);
    }
    await moveCheck(pg, canvasSel, out, 30, 20, where);
    const before = await pg.evaluate(() => window.__locks);
    await pg.mouse.down(); await pg.mouse.up();
    await pg.waitForFunction((o) => document.querySelector(o).textContent.includes("click locked="), out, { timeout: 5000 });
    await pg.waitForTimeout(200);
    const after = await pg.evaluate(() => [window.__locks, !!document.pointerLockElement]);
    check(after[0] > before, `${where}: a click on the game locks it again (locked: ${after[1]}), and counts as a click`);
  };
  await page.evaluate(spyLocks);
  await setCode(LOOK);
  await page.click("#btn-run");
  await page.waitForSelector("#game:not([hidden])", { timeout: 60000 });
  await lookTest(page, "#game-canvas", "#console", "Studio", "#game-title");
  // and fullscreen: a click on the game locks the mouse there too, still fullscreen
  await page.evaluate(() => document.exitPointerLock());
  await page.waitForFunction(() => !document.pointerLockElement);
  await page.click("#btn-fullscreen");
  await page.waitForFunction(() => document.fullscreenElement && document.fullscreenElement.id === "game-screen", null, { timeout: 5000 });
  await page.waitForTimeout(1200);       // (browsers refuse a lock straight after one was let go)
  const fb = await page.evaluate(() => { const b = document.getElementById("game-canvas").getBoundingClientRect();
    return { x: b.left + b.width / 2, y: b.top + b.height / 2 }; });
  await page.mouse.click(fb.x, fb.y);
  await page.waitForTimeout(300);
  check(await page.evaluate(() => document.pointerLockElement === document.getElementById("game-canvas") &&
        document.fullscreenElement && document.fullscreenElement.id === "game-screen"),
        "fullscreen: a click on the game locks the mouse, and it stays fullscreen");
  await page.evaluate(() => document.exitPointerLock());
  await page.waitForFunction(() => !document.pointerLockElement);
  await moveCheck(page, "#game-canvas", "#console", 30, 20, "fullscreen");
  await page.evaluate(() => document.getElementById("btn-stop").click());   // (a locked pointer can't click it)
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Stopped"), null, { timeout: 15000 });
  check(await page.evaluate(() => !document.pointerLockElement && !FlauxWeb.mouseLook().wanted),
        "Stop lets go of the mouse");

  // on a sharp screen (two screen pixels for each CSS pixel) the game gets all of them
  const sharpScreen = await browser.newContext({ viewport: { width: 1300, height: 820 }, deviceScaleFactor: 2 });
  const sharpPage = await sharpScreen.newPage();
  sharpPage.on("pageerror", (e) => errors.push("sharp screen: " + e.message));
  await sharpPage.goto(url);
  await sharpPage.waitForFunction(() => /Ready|No problems/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
  await sharpPage.evaluate((t) => { code.value = t; state.lastChecked = null; render(); },
                           'window(300, 200)\nrepeat forever\n    clear("navy")\n    show()\nend\n');
  await sharpPage.click("#btn-run");
  await sharpPage.waitForSelector("#game:not([hidden])", { timeout: 60000 });
  await sharpPage.waitForFunction(() => document.getElementById("game-canvas").width > 300, null, { timeout: 5000 }).catch(() => {});
  const dense = await sharpPage.evaluate(() => { const c = document.getElementById("game-canvas"), r = c.getBoundingClientRect();
    return { w: c.width, h: c.height, cssW: Math.round(r.width), cssH: Math.round(r.height) }; });
  check(Math.abs(dense.w - 2 * dense.cssW) <= 2 && Math.abs(dense.h - 2 * dense.cssH) <= 2,
        `on a sharp screen the game gets every screen pixel (${dense.w}x${dense.h} pixels, shown ${dense.cssW}x${dense.cssH})`);
  await sharpScreen.close();

  // 3D in fullscreen: drawn at the screen's resolution too, and screen effects still see the
  // program's own pixels (this one splits the picture down the program's middle)
  await setCode('shader halves\n    if x < width / 2\n        return rgb(red, 0, 0)\n    end\n' +
                '    return rgb(0, green, 0)\nend\n\nwindow(640, 400)\nrepeat forever\n    sky("white", "white")\n' +
                '    camera(0, 2, 6, 0, 0, 0)\n    cube(0, 0, 0, 2, "white")\n    effect(halves)\n    show()\nend\n');
  await page.click("#btn-run");
  await page.waitForSelector("#game:not([hidden])", { timeout: 60000 });
  await page.waitForTimeout(300);
  await page.click("#btn-fullscreen");
  await page.waitForFunction(() => document.fullscreenElement && document.getElementById("game-canvas").width > 1000, null, { timeout: 5000 })
    .catch(() => {});
  await page.waitForTimeout(400);
  const full3d = await page.evaluate(() => {
    const c = document.getElementById("game-canvas"), r = c.getBoundingClientRect();
    const t = document.createElement("canvas"); t.width = c.width; t.height = c.height;
    const g = t.getContext("2d"); g.drawImage(c, 0, 0);
    const at = (x, y) => Array.from(g.getImageData(x, y, 1, 1).data.slice(0, 3));
    const mid = Math.floor(c.width / 2);
    return { w: c.width, h: c.height, cssW: Math.round(r.width), left: at(mid - 3, 10), right: at(mid + 3, 10) };
  });
  check(full3d.w > 1000 && Math.abs(full3d.w - full3d.cssW) <= 2 && full3d.left[0] > 200 && full3d.left[1] < 40 &&
        full3d.right[1] > 200 && full3d.right[0] < 40,
        `3D fullscreen draws at the screen's resolution, effects in the program's pixels (${full3d.w}x${full3d.h}, ` +
        `shown ${full3d.cssW} wide; left of the middle ${full3d.left}, right ${full3d.right})`);
  await page.evaluate(() => document.exitFullscreen());
  await page.waitForFunction(() => !document.fullscreenElement);
  await page.click("#btn-stop");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Stopped"), null, { timeout: 15000 });

  // touch: dragging a finger is mouse movement (mouse look on phones and tablets)
  await setCode('window(400, 300)\nset tx to 0\nrepeat forever\n    if mouse_moved_x() != 0\n' +
                '        set tx to tx + mouse_moved_x()\n        say "dragged {tx}"\n    end\n    show()\nend\n');
  await page.click("#btn-run");
  await page.waitForSelector("#game:not([hidden])", { timeout: 60000 });
  await page.evaluate(() => {
    const c = document.getElementById("game-canvas"), r = c.getBoundingClientRect();
    const at = (x) => new Touch({ identifier: 1, target: c, clientX: r.left + x, clientY: r.top + 50 });
    const send = (type, x) => c.dispatchEvent(new TouchEvent(type, { touches: x === null ? [] : [at(x)],
      changedTouches: [at(x === null ? 70 : x)], bubbles: true, cancelable: true }));
    send("touchstart", 10); send("touchmove", 30); send("touchmove", 70); send("touchend", null);
  });
  await page.waitForFunction(() => /dragged 60\n/.test(document.getElementById("console").textContent), null, { timeout: 5000 })
    .catch(() => {});
  check(/dragged 60\n/.test(await page.textContent("#console")), "dragging a finger 60 pixels moves the mouse 60 (mouse_moved_x)");
  await page.click("#btn-stop");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Stopped"), null, { timeout: 15000 });

  // an exported 3D game: the 3D engine (gl.h) compiles too, and its model goes in the zip
  await setCode('window(300, 200)\nsky("black")\ncamera(0, 1, 4, 0, 0, 0)\nmodel("house.obj", 0, 0, 0, 2)\nshow()\n');
  const [zip3d] = await exportZip();
  const files3d = unzip(fs.readFileSync(await zip3d.path()));
  const base3d = zip3d.suggestedFilename().replace(/\.zip$/, "");
  check([`${base3d}/${base3d}.exe`, `${base3d}/SDL2.dll`, `${base3d}/house.obj`, `${base3d}/house.mtl`].every((n) => n in files3d),
        `an exported 3D game is a real program, with its model (${Object.keys(files3d).join(", ")})`);


  check(!errors.length, "no errors in the page" + (errors.length ? ": " + errors.join(" | ") : ""));
  await browser.close();
  console.log(failures ? `${failures} problem(s)` : "Flaux Studio on the web works.");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error("The test couldn't finish: " + e.message); process.exit(1); });
