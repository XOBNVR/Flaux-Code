#!/usr/bin/env python3
"""Checks the block editor: every example and test program is opened in
Flaux Studio's Blocks mode (in a real browser), turned back into code, and
must mean exactly the same as before.

    python3 tests/blocks_roundtrip.py

Needs Node.js and Playwright (npm install playwright; npx playwright install chromium).
Skipped, with a message, if they aren't there.
"""

import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from flauxc import nodes as N  # noqa: E402
from flauxc.compiler import compile_source  # noqa: E402
from flauxc.errors import FlauxError  # noqa: E402
from flauxc.lexer import tokenize  # noqa: E402
from flauxc.parser import parse  # noqa: E402

BROWSER_SCRIPT = r"""
const { chromium } = require('playwright');
(async () => {
  const [url, filesJson] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 1300, height: 820 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById('code').value.length > 0);
  await page.evaluate(() => setMode('blocks'));
  await page.waitForFunction(() => state.mode === 'blocks' && window.FlauxBlocks.workspace);
  const out = {};
  for (const f of JSON.parse(filesJson)) {
    out[f] = await page.evaluate(async (f) => {
      state.dirty = false;
      await openFile(f);
      return state.mode === 'blocks' ? code.value : null;
    }, f);
  }
  await browser.close();
  console.log(JSON.stringify({ out, errors }));
})().catch((e) => { console.error(e.message); process.exit(1); });
"""


def meaning(node):
    """A program's meaning, without positions, notes or formatting."""
    if isinstance(node, (list, tuple)):
        return [meaning(x) for x in node]
    if isinstance(node, N.Call) and node.name == "length" and len(node.args) == 1:
        return ["Length", meaning(node.args[0])]
    if isinstance(node, N.Num):
        return ["Num", float(node.value)]
    if isinstance(node, N.Node):
        return [type(node).__name__] + [meaning(getattr(node, f)) for f in node.fields]
    return node


def main():
    node = shutil.which("node")
    check = subprocess.run([node, "-e", "require('playwright')"], capture_output=True) if node else None
    if not node or check.returncode != 0:
        print("(skipping the block editor test: Node.js with Playwright isn't installed)")
        return 0
    files = sorted(glob.glob(os.path.join(ROOT, "examples", "**", "*.flx"), recursive=True)
                   + glob.glob(os.path.join(HERE, "programs", "*.flx")))
    with tempfile.TemporaryDirectory() as home:
        env = dict(os.environ, HOME=home, USERPROFILE=home)
        server = subprocess.Popen([sys.executable, os.path.join(ROOT, "flaux"), "studio", "--no-window",
                                   "--keep-running"], stdout=subprocess.PIPE, text=True, env=env)
        try:
            url = server.stdout.readline().split(" at ")[-1].strip()
            script = os.path.join(home, "roundtrip.js")
            with open(script, "w") as fh:
                fh.write(BROWSER_SCRIPT)
            # (the browser keeps the normal home folder: Playwright finds its browsers there)
            run = subprocess.run([node, script, url, json.dumps(files)], capture_output=True, text=True,
                                 cwd=home)
        finally:
            server.kill()
    if run.returncode != 0:
        print("The browser test didn't run:\n" + run.stderr)
        return 1
    result = json.loads(run.stdout.strip().splitlines()[-1])
    failures = list(result["errors"])
    for f in files:
        made = result["out"].get(f)
        name = os.path.relpath(f, ROOT)
        if made is None:
            failures.append(f"{name}: didn't open as blocks")
            continue
        original = open(f, encoding="utf-8").read()
        try:
            compile_source(made, os.path.basename(f), os.path.dirname(f))
            if meaning(parse(tokenize(original), original)) != meaning(parse(tokenize(made), made)):
                failures.append(f"{name}: the blocks' code means something different:\n{made}")
        except FlauxError as e:
            failures.append(f"{name}: the blocks' code has a problem on line {e.line}: {e.message}\n{made}")
    for failure in failures:
        print("FAIL", failure)
    print(f"{len(files) - len(failures)} of {len(files)} programs went through blocks and back unchanged")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
