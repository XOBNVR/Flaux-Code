// Tests Flaux Studio on the web in a real browser (Playwright).
//
//   python3 -m flauxc.web.build site --pyodide <pyodide folder>
//   python3 -m http.server --directory site 8790 &
//   node tests/web_studio_test.js http://127.0.0.1:8790/
//
// Checks: Flaux starts in the page, a program runs and asks a question, a
// problem while running is explained, Apply fix, Blocks mode, saving in the
// browser, the graphics pixel tests from tests/graphics, pictures, keys,
// fullscreen (size, keys and mouse positions), Export, the Stop button, and
// mouse look (lock_mouse() asks for pointer lock, again on a click; mouse
// movement; the pointer is let go at Stop), in the Studio and an exported game.
"use strict";
const fs = require("fs");
const path = require("path");
const { chromium } = require("playwright");

const url = process.argv[2] || "http://127.0.0.1:8790/";
const graphics = path.join(__dirname, "graphics");
const read = (name) => fs.readFileSync(path.join(graphics, name), "utf8");
const pixels = (src) => src.split("\n").filter((l) => l.startsWith("# pixel:")).map((l) => l.slice(8).trim().split(/\s+/));

let failures = 0;
function check(ok, what) { console.log((ok ? "OK    " : "FAIL  ") + what); if (!ok) failures++; }

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1300, height: 820 } });
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
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
  await page.evaluate(() => document.exitFullscreen());
  await page.waitForFunction(() => !document.fullscreenElement && document.getElementById("game-canvas").style.width === "");
  check(true, "leaving fullscreen puts the game back in the panel");
  await page.click("#btn-stop");
  await page.waitForFunction(() => document.getElementById("console").textContent.includes("Stopped"), null, { timeout: 15000 });
  check(true, "the Stop button stops a game");

  // Export: a zip with the program as a web page and a .bat that opens it
  const [download] = await Promise.all([page.waitForEvent("download"), page.click("#btn-export")]);
  const zip = fs.readFileSync(await download.path());
  const base = download.suggestedFilename().replace(/\.zip$/, "");    // the program's name (web_test, saved above)
  const names = [`${base}/game.html`, `${base}/Play ${base}.bat`, `${base}/README.txt`];
  check(base === "web_test" && zip.readUInt32LE(0) === 0x04034b50 &&
        names.every((n) => zip.includes(Buffer.from(n))) && zip.includes(Buffer.from('start "" "%~dp0game.html"')),
        `Export downloads ${download.suggestedFilename()} (${zip.length} bytes)`);

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
      return { x: b.left, y: b.top, w: b.width, h: b.height, cw: document.querySelector(sel).width }; }, canvasSel);
    await pg.mouse.move(r.x + r.w / 2, r.y + r.h / 2);
    await pg.waitForTimeout(200);
    const [sx, sy] = await lastMove(pg, out);
    await pg.mouse.move(r.x + r.w / 2 + dx, r.y + r.h / 2 + dy, { steps: 5 });
    await pg.waitForTimeout(300);
    const [ex, ey] = await lastMove(pg, out);
    check(ex - sx === dx && ey - sy === dy, `${where}: mouse_moved_x/y() give screen pixels (${ex - sx}, ${ey - sy}` +
          ` for ${dx}, ${dy}; the game is shown at ${(r.w / r.cw).toFixed(2)}x)`);
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

  // the same in an exported game.html
  const [lookZip] = await Promise.all([page.waitForEvent("download"), page.click("#btn-export")]);
  const zbytes = fs.readFileSync(await lookZip.path());
  let html = null;
  for (let at = 0; zbytes.readUInt32LE(at) === 0x04034b50;) {      // stored files, one after another
    const size = zbytes.readUInt32LE(at + 18), nlen = zbytes.readUInt16LE(at + 26), xlen = zbytes.readUInt16LE(at + 28);
    const name = zbytes.slice(at + 30, at + 30 + nlen).toString();
    const data = zbytes.slice(at + 30 + nlen + xlen, at + 30 + nlen + xlen + size);
    if (name.endsWith("/game.html")) html = data.toString();
    at += 30 + nlen + xlen + size;
  }
  const player = await browser.newPage({ viewport: { width: 1000, height: 600 } });   // (the game shown smaller)
  player.on("pageerror", (e) => errors.push("game.html: " + e.message));
  // (the spy goes in first, so it's there when the program calls lock_mouse())
  await player.setContent((html || "<p>no game.html</p>").replace("<head>", `<head><script>(${spyLocks})()</script>`));
  await lookTest(player, "#canvas", "#out", "exported game", "#title");
  await player.close();

  check(!errors.length, "no errors in the page" + (errors.length ? ": " + errors.join(" | ") : ""));
  await browser.close();
  console.log(failures ? `${failures} problem(s)` : "Flaux Studio on the web works.");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error("The test couldn't finish: " + e.message); process.exit(1); });
