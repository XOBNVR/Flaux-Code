/* Flaux 3D for the web: the same 3D engine as the desktop app (flauxc/gl.h),
 * drawn with WebGL 2, plus VR and AR headsets with WebXR.
 *
 * A compiled program that uses 3D or shaders starts with
 *     fx.setup3d(glslSections, shaders)
 * which calls install() below: it replaces the 2D drawing functions of the
 * runtime (fx.js) with ones that draw with the graphics card, and adds the 3D
 * ones. The GPU programs are engine.glsl, exactly as on the desktop; only the
 * #version line differs.
 */
(function (root) {
  "use strict";

  const HEADER = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2DShadow;\n";
  const SHADOW_SIZE = 2048;

  // ------------------------------------------------------------- maths
  // 4x4 matrices are Float32Array(16), column by column (like OpenGL).
  function mul(a, b) {
    const r = new Float32Array(16);
    for (let c = 0; c < 4; c++)
      for (let w = 0; w < 4; w++) {
        let s = 0;
        for (let k = 0; k < 4; k++) s += a[k * 4 + w] * b[c * 4 + k];
        r[c * 4 + w] = s;
      }
    return r;
  }
  function perspective(fovy, aspect, n, f) {
    const r = new Float32Array(16), t = 1 / Math.tan(fovy / 2);
    r[0] = t / aspect; r[5] = t; r[10] = (f + n) / (n - f); r[11] = -1; r[14] = 2 * f * n / (n - f);
    return r;
  }
  function ortho(l, rr, b, t, n, f) {
    const r = new Float32Array(16);
    r[0] = 2 / (rr - l); r[5] = 2 / (t - b); r[10] = -2 / (f - n);
    r[12] = -(rr + l) / (rr - l); r[13] = -(t + b) / (t - b); r[14] = -(f + n) / (f - n); r[15] = 1;
    return r;
  }
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  function norm(a) { const l = Math.hypot(a[0], a[1], a[2]); return l > 1e-12 ? [a[0] / l, a[1] / l, a[2] / l] : [0, 1, 0]; }
  function lookAt(eye, target) {
    const f = norm(sub(target, eye));
    const up = Math.abs(f[1]) > 0.999 ? [0, 0, f[1] > 0 ? 1 : -1] : [0, 1, 0];
    const s = norm(cross(f, up)), u = cross(s, f);
    return new Float32Array([s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0,
      -dot(s, eye), -dot(u, eye), dot(f, eye), 1]);
  }
  function invert(m) {
    const inv = new Float32Array(16);
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    const d = det !== 0 ? 1 / det : 0;
    for (let i = 0; i < 16; i++) inv[i] *= d;
    return inv;
  }
  const point = (m, p) => [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
    m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]];
  const LIN = new Float32Array(256).map((_, i) => Math.pow(i / 255, 2.2));

  // ------------------------------------------------------------- meshes
  // vertex: position (3), normal (3), uv (2) as floats, then colour (4 bytes) = 36 bytes
  function builder() { return { v: [], c: [], i: [] }; }
  function vert(b, p, n, u, v, rgb) {
    b.v.push(p[0], p[1], p[2], n[0], n[1], n[2], u, v);
    b.c.push(rgb ? rgb[0] : 255, rgb ? rgb[1] : 255, rgb ? rgb[2] : 255, 255);
    return b.c.length / 4 - 1;
  }
  function packed(b) {
    const n = b.c.length / 4, buf = new ArrayBuffer(n * 36), f = new Float32Array(buf), u8 = new Uint8Array(buf);
    for (let k = 0; k < n; k++) {
      for (let j = 0; j < 8; j++) f[k * 9 + j] = b.v[k * 8 + j];
      for (let j = 0; j < 4; j++) u8[k * 36 + 32 + j] = b.c[k * 4 + j];
    }
    return buf;
  }
  function cubeFace(b, n, u, v, half) {
    const c = n.map((x) => x * half);
    const at = (su, sv) => [c[0] + u[0] * su * half + v[0] * sv * half, c[1] + u[1] * su * half + v[1] * sv * half,
      c[2] + u[2] * su * half + v[2] * sv * half];
    const a = vert(b, at(-1, -1), n, 0, 1), b1 = vert(b, at(1, -1), n, 1, 1);
    const c1 = vert(b, at(1, 1), n, 1, 0), d = vert(b, at(-1, 1), n, 0, 0);
    b.i.push(a, b1, c1, a, c1, d);
  }
  function shapes() {
    const out = [];
    let b = builder();
    cubeFace(b, [1, 0, 0], [0, 0, -1], [0, 1, 0], 0.5); cubeFace(b, [-1, 0, 0], [0, 0, 1], [0, 1, 0], 0.5);
    cubeFace(b, [0, 1, 0], [1, 0, 0], [0, 0, -1], 0.5); cubeFace(b, [0, -1, 0], [1, 0, 0], [0, 0, 1], 0.5);
    cubeFace(b, [0, 0, 1], [1, 0, 0], [0, 1, 0], 0.5); cubeFace(b, [0, 0, -1], [-1, 0, 0], [0, 1, 0], 0.5);
    out.push([b, false]);
    b = builder();
    const rings = 24, segs = 48;
    for (let i = 0; i <= rings; i++) {
      const th = Math.PI * i / rings;
      for (let j = 0; j <= segs; j++) {
        const ph = 2 * Math.PI * j / segs, p = [Math.sin(th) * Math.cos(ph), Math.cos(th), Math.sin(th) * Math.sin(ph)];
        vert(b, p, p, j / segs, i / rings);
      }
    }
    for (let i = 0; i < rings; i++)
      for (let j = 0; j < segs; j++) {
        const a = i * (segs + 1) + j, lo = a + segs + 1;
        b.i.push(a, a + 1, lo, a + 1, lo + 1, lo);
      }
    out.push([b, false]);
    for (const cone of [false, true]) {
      b = builder();
      for (let j = 0; j <= segs; j++) {
        const ph = 2 * Math.PI * j / segs, phm = 2 * Math.PI * (j + 0.5) / segs;
        const n = cone ? norm([Math.cos(ph), 1, Math.sin(ph)]) : [Math.cos(ph), 0, Math.sin(ph)];
        vert(b, cone ? [0, 0.5, 0] : [Math.cos(ph), 0.5, Math.sin(ph)], cone ? norm([Math.cos(phm), 1, Math.sin(phm)]) : n, j / segs, 0);
        vert(b, [Math.cos(ph), -0.5, Math.sin(ph)], n, j / segs, 1);
      }
      for (let j = 0; j < segs; j++) {
        const t = 2 * j, lo = t + 1, t2 = t + 2, lo2 = t + 3;
        if (!cone) b.i.push(t, t2, lo);
        b.i.push(cone ? t : t2, lo2, lo);
      }
      for (let cap = cone ? 1 : 0; cap < 2; cap++) {
        const y = cap ? -0.5 : 0.5, n = [0, cap ? -1 : 1, 0];
        const c = vert(b, [0, y, 0], n, 0.5, 0.5);
        for (let j = 0; j <= segs; j++) {
          const ph = 2 * Math.PI * j / segs;
          vert(b, [Math.cos(ph), y, Math.sin(ph)], n, 0.5 + Math.cos(ph) / 2, 0.5 + Math.sin(ph) / 2);
        }
        for (let j = 0; j < segs; j++) {
          if (cap) b.i.push(c, c + 1 + j, c + 2 + j);
          else b.i.push(c, c + 2 + j, c + 1 + j);
        }
      }
      out.push([b, false]);
    }
    b = builder();
    cubeFace(b, [0, 1, 0], [1, 0, 0], [0, 0, -1], 0.5);
    for (let k = 0; k < 4; k++) b.v[k * 8 + 1] = 0;
    out.push([b, true]);
    return out;
  }

  // A model (.obj, with its .mtl colours), fitted into a 1 by 1 by 1 box.
  function parseObj(text, mtl) {
    const mats = {};
    let current = null;
    for (const raw of (mtl || "").split("\n")) {
      const line = raw.trim();
      if (line.startsWith("newmtl ")) { current = line.slice(7).trim(); mats[current] = [255, 255, 255]; }
      else if (line.startsWith("Kd ") && current) {
        mats[current] = line.slice(3).trim().split(/\s+/).slice(0, 3)
          .map((v) => Math.round(Math.pow(Math.max(0, Math.min(1, parseFloat(v) || 0)), 1 / 2.2) * 255));
      }
    }
    const pos = [], nor = [], tex = [], b = builder();
    let rgb = [255, 255, 255];
    for (const raw of text.split("\n")) {
      const line = raw.trim(), parts = line.split(/\s+/);
      if (parts[0] === "v") pos.push(parts.slice(1, 4).map(Number));
      else if (parts[0] === "vn") nor.push(parts.slice(1, 4).map(Number));
      else if (parts[0] === "vt") tex.push([Number(parts[1]) || 0, 1 - (Number(parts[2]) || 0)]);
      else if (parts[0] === "usemtl") rgb = mats[line.slice(7).trim()] || [255, 255, 255];
      else if (parts[0] === "f") {
        const idx = parts.slice(1).map((p) => {
          const [vi, ti, ni] = p.split("/").map((x) => (x ? parseInt(x, 10) : 0));
          const fix = (i, n) => (i < 0 ? n + i + 1 : i);
          return [fix(vi, pos.length), fix(ti || 0, tex.length), fix(ni || 0, nor.length)];
        });
        if (idx.length < 3) continue;
        const P = (i) => pos[i - 1] || [0, 0, 0];
        const flat = norm(cross(sub(P(idx[1][0]), P(idx[0][0])), sub(P(idx[2][0]), P(idx[0][0]))));
        const first = b.c.length / 4;
        for (const [vi, ti, ni] of idx)
          vert(b, P(vi), nor[ni - 1] || flat, (tex[ti - 1] || [0, 0])[0], (tex[ti - 1] || [0, 0])[1], rgb);
        for (let k = 1; k + 1 < idx.length; k++) b.i.push(first, first + k, first + k + 1);
      }
    }
    if (!b.i.length) return null;
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (let k = 0; k < b.v.length; k += 8)
      for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], b.v[k + a]); hi[a] = Math.max(hi[a], b.v[k + a]); }
    const big = Math.max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]), s = big > 0 ? 1 / big : 1;
    for (let k = 0; k < b.v.length; k += 8)
      for (let a = 0; a < 3; a++) b.v[k + a] = (b.v[k + a] - (lo[a] + hi[a]) / 2) * s;
    return b;
  }

  // ------------------------------------------------------------- install
  function install(fx, b, io, h, glsl, shaders) {
    const fail = fx.fail;
    let gl = null, win = null, W = 0, H = 0, dirty = false, frames = 0, startTime = 0;
    let samples = 0, msaaFbo, sceneFbo, sceneTex, shadowFbo, shadowTex, instVbo, flatVao, flatVbo, emptyVao;
    let whiteTex, fontTex, sampPixel, sampSmooth, sampBlocky, sampShadow, sampScene;
    let sceneProg, shadowProg, skyProg, flatProg;
    const surfaceProgs = [], effectProgs = [], meshes = [];
    let camEye = [0, 3, 10], camTarget = [0, 0, 0], camFov = 60;
    let skyTop = [0, 0, 0], skyHorizon = [0, 0, 0], skySet = false, drew3d = false;
    let sunDir = norm([0.4, 1, 0.3]), sunRgb = [255, 255, 255], sunOn = true;
    let fogFar = 0, fogOwn = false, fogRgb = [0, 0, 0];
    const lights = new Float32Array(64);
    let lightCount = 0, turnM = [1, 0, 0, 0, 1, 0, 0, 0, 1], effectId = -1, effectVals = new Float32Array(8);
    let batches = [], lastBatch = null, flat = [], runs = [], shadowMat = new Float32Array(16), shadowsNow = false;
    const textures = new Map();      // picture name -> {tex, w, h} once loaded (or null if missing)
    const models = new Map();        // model name -> mesh index once loaded (or null if missing)
    const pending = [];              // pictures and models still loading
    let fpsValue = 0, fpsFrames = 0, fpsSince = 0;

    function rgbOf(c, line) {
      const s = h.colour(c, line);   // "#rrggbb"
      return [parseInt(s.slice(1, 3), 16), parseInt(s.slice(3, 5), 16), parseInt(s.slice(5, 7), 16)];
    }
    function needWindow(line) {
      if (!gl) fail(line, "Open a window first, at the top of your program, like this:\n\n      window(800, 600)",
        "There's no window to draw in yet.");
    }
    const seconds = () => (io.fast ? frames / 60 : (io.now() - startTime) / 1000);

    // --------------------------------------------------------- GPU programs
    function compile(type, src, what) {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
        fail(0, "This is a bug in Flaux, not in your program. Please report it, with your program.",
          `The graphics card didn't accept the ${what} shader:\n${gl.getShaderInfoLog(s)}`);
      return s;
    }
    function program(vs, fs, user, what) {
      const mark = fs.indexOf("//@@USER@@");
      const fsrc = mark >= 0 ? fs.slice(0, mark) + (user || "") + "\n" + fs.slice(mark) : fs;
      const p = gl.createProgram();
      gl.attachShader(p, compile(gl.VERTEX_SHADER, HEADER + glsl.common + vs, what));
      gl.attachShader(p, compile(gl.FRAGMENT_SHADER, HEADER + glsl.common + fsrc, what));
      gl.linkProgram(p);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS))
        fail(0, "This is a bug in Flaux, not in your program. Please report it, with your program.",
          `The graphics card couldn't link the ${what} shader:\n${gl.getProgramInfoLog(p)}`);
      return p;
    }
    function sceneProgram(user, what) {
      const id = program(glsl.scene_vs, glsl.scene_fs, user, what);
      const u = {};
      for (const name of ["viewproj", "tex", "textured", "shadow_map", "shadows", "shadow_mat", "sun_dir", "sun_color",
        "amb_top", "amb_bottom", "camera", "lights", "light_count", "fog_color", "fog", "time"])
        u[name] = gl.getUniformLocation(id, "u_" + name);
      gl.useProgram(id);
      gl.uniform1i(u.tex, 0);
      gl.uniform1i(u.shadow_map, 1);
      return { id, u };
    }
    function surfaceProgram(shader) {
      if (shader < 0) return sceneProg;
      if (!surfaceProgs[shader]) surfaceProgs[shader] = sceneProgram(shaders[shader].surface, `"${shaders[shader].name}"`);
      return surfaceProgs[shader];
    }
    function effectProgram(shader) {
      if (!effectProgs[shader]) {
        const p = program(glsl.fullscreen_vs, glsl.effect_fs, shaders[shader].screen, `"${shaders[shader].name}" screen effect`);
        gl.useProgram(p);
        gl.uniform1i(gl.getUniformLocation(p, "u_scene"), 0);
        effectProgs[shader] = p;
      }
      return effectProgs[shader];
    }

    // --------------------------------------------------------------- meshes
    function finishMesh(bld, twoSided) {
      const m = { vao: gl.createVertexArray(), count: bld.i.length, twoSided };
      gl.bindVertexArray(m.vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
      gl.bufferData(gl.ARRAY_BUFFER, packed(bld), gl.STATIC_DRAW);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, gl.createBuffer());
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(bld.i), gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 36, 0);
      gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 36, 12);
      gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 36, 24);
      gl.enableVertexAttribArray(8); gl.vertexAttribPointer(8, 4, gl.UNSIGNED_BYTE, true, 36, 32);
      for (let a = 3; a <= 7; a++) { gl.enableVertexAttribArray(a); gl.vertexAttribDivisor(a, 1); }
      gl.bindVertexArray(null);
      meshes.push(m);
      return meshes.length - 1;
    }

    // ------------------------------------------------------------- pictures
    function upload(source, w, hh, mipmaps) {
      const t = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
      if (source instanceof Uint8Array) gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, hh, 0, gl.RGBA, gl.UNSIGNED_BYTE, source);
      else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, source);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, mipmaps ? gl.LINEAR_MIPMAP_LINEAR : gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      if (mipmaps) gl.generateMipmap(gl.TEXTURE_2D);
      return t;
    }
    // Pictures load while the program keeps going; show() waits for them.
    function texture(name, line) {
      if (textures.has(name)) {
        const t = textures.get(name);
        if (t === null) fail(line, "Add the picture to your program with the Pictures button, and check the name is exactly\n" +
          "  right (including .png at the end).", `I can't find the picture "${name}".`);
        return t;
      }
      textures.set(name, undefined);
      pending.push(io.loadPicture(name).then((img) => {
        textures.set(name, img ? { tex: upload(img, img.width, img.height, true), w: img.width, h: img.height, img } : null);
      }));
      return undefined;
    }
    function modelMesh(name, line) {
      if (models.has(name)) {
        const m = models.get(name);
        if (m === null) fail(line, "Add the model (.obj, and its .mtl if it has one) with the Pictures button, and check the\n" +
          "  name is exactly right (including .obj at the end).", `I can't find the model "${name}".`);
        return m;
      }
      models.set(name, undefined);
      pending.push((async () => {
        const text = io.loadFile ? await io.loadFile(name) : null;
        if (text === null || text === undefined) { models.set(name, null); return; }
        const lib = /^\s*mtllib\s+(.+)$/m.exec(text);
        const mtl = lib && io.loadFile ? await io.loadFile(lib[1].trim()) : "";
        const bld = parseObj(text, mtl || "");
        models.set(name, bld ? finishMesh(bld, true) : null);
      })());
      return undefined;
    }
    async function waitForLoading() {
      while (pending.length) await pending.shift();
    }

    // -------------------------------------------------------------- shapes
    function batch(mesh, pic, shader, line) {
      if (lastBatch && lastBatch.mesh === mesh && lastBatch.pic === pic && lastBatch.shader === shader) return lastBatch;
      let found = batches.find((q) => q.mesh === mesh && q.pic === pic && q.shader === shader);
      if (!found) {
        found = { mesh, pic, shader, line, n: 0, data: new Float32Array(20 * 64), offset: 0 };
        batches.push(found);
      }
      lastBatch = found;
      return found;
    }
    function isPicture(t) { return typeof t === "string" && /\.(png|jpe?g|bmp|gif)$/i.test(t); }
    function shape(mesh, x, y, z, sx, sy, sz, paint, tile, line) {
      needWindow(line);
      let r = 1, g = 1, bl = 1, pic = null, shader = -1, extra = null;
      if (paint && typeof paint === "object") { shader = paint.shader; extra = paint.values; }
      else {
        const p = paint === undefined ? "white" : paint;
        if (isPicture(p)) { pic = p; texture(p, line); }
        else { const c = rgbOf(p, line); r = LIN[c[0]]; g = LIN[c[1]]; bl = LIN[c[2]]; }
      }
      const q = batch(mesh, pic, shader, line);
      if ((q.n + 1) * 20 > q.data.length) { const d = new Float32Array(q.data.length * 2); d.set(q.data); q.data = d; }
      const d = q.data, o = q.n * 20, m = turnM, s = [sx, sy, sz], pos = [x, y, z];
      for (let i = 0; i < 3; i++) {
        d[o + i * 4] = m[i * 3] * s[0]; d[o + i * 4 + 1] = m[i * 3 + 1] * s[1];
        d[o + i * 4 + 2] = m[i * 3 + 2] * s[2]; d[o + i * 4 + 3] = pos[i];
      }
      d[o + 12] = r; d[o + 13] = g; d[o + 14] = bl; d[o + 15] = tile ? 1 : 0;
      for (let k = 0; k < 4; k++) d[o + 16 + k] = extra && k < extra.length ? extra[k] : 0;
      q.n++;
      drew3d = true;
      dirty = true;
    }

    function setTurn(ax, ay, az) {
      const rx = ax * Math.PI / 180, ry = ay * Math.PI / 180, rz = az * Math.PI / 180;
      const cx = Math.cos(rx), sx = Math.sin(rx), cy = Math.cos(ry), sy = Math.sin(ry), cz = Math.cos(rz), sz = Math.sin(rz);
      const Rx = [1, 0, 0, 0, cx, -sx, 0, sx, cx], Ry = [cy, 0, sy, 0, 1, 0, -sy, 0, cy], Rz = [cz, -sz, 0, sz, cz, 0, 0, 0, 1];
      const m3 = (A, B) => { const r = []; for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) { let s = 0; for (let k = 0; k < 3; k++) s += A[i * 3 + k] * B[k * 3 + j]; r.push(s); } return r; };
      turnM = m3(Ry, m3(Rx, Rz));
    }

    // ----------------------------------------------------------- 2D on top
    function flatUse(tex) {
      if (runs.length && runs[runs.length - 1].tex === tex) return;
      runs.push({ tex, first: flat.length / 5, count: 0 });
    }
    function flatVertex(x, y, u, v, c) {
      flat.push(x, y, u, v, (c[0] | (c[1] << 8) | (c[2] << 16) | (255 << 24)) >>> 0);
      runs[runs.length - 1].count++;
      dirty = true;
    }
    function flatTri(x1, y1, x2, y2, x3, y3, c) {
      flatUse(whiteTex);
      flatVertex(x1, y1, 0.5, 0.5, c); flatVertex(x2, y2, 0.5, 0.5, c); flatVertex(x3, y3, 0.5, 0.5, c);
    }
    function flatQuad(tex, xy, uv, c) {
      flatUse(tex);
      for (const k of [0, 1, 2, 0, 2, 3]) flatVertex(xy[k * 2], xy[k * 2 + 1], uv[k * 2], uv[k * 2 + 1], c);
    }
    function flatRect(x, y, w, hh, c) {
      flatQuad(whiteTex, [x, y, x + w, y, x + w, y + hh, x, y + hh], [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5], c);
    }
    function circle(cx, cy, radius, c) {
      if (radius <= 0) return;
      radius = Math.min(radius, 20000);
      const segs = Math.max(16, Math.min(256, Math.floor(radius * 1.5)));
      for (let i = 0; i < segs; i++) {
        const a0 = 2 * Math.PI * i / segs, a1 = 2 * Math.PI * (i + 1) / segs;
        flatTri(cx, cy, cx + Math.cos(a0) * radius, cy + Math.sin(a0) * radius, cx + Math.cos(a1) * radius, cy + Math.sin(a1) * radius, c);
      }
    }

    // ------------------------------------------------------------- drawing
    function skyColors() {
      if (skySet) return [skyTop, skyHorizon];
      if (drew3d) return [[0x5a, 0xa8, 0xe8], [0xcf, 0xe8, 0xf7]];
      return [[0, 0, 0], [0, 0, 0]];
    }
    const total = () => batches.reduce((s, q) => s + q.n, 0);
    function uploadInstances() {
      const n = total();
      if (!n) return;
      gl.bindBuffer(gl.ARRAY_BUFFER, instVbo);
      gl.bufferData(gl.ARRAY_BUFFER, n * 80, gl.STREAM_DRAW);
      let at = 0;
      for (const q of batches) {
        q.offset = at;
        if (!q.n) continue;
        gl.bufferSubData(gl.ARRAY_BUFFER, at, q.data, 0, q.n * 20);
        at += q.n * 80;
      }
    }
    function drawBatch(q) {
      const m = meshes[q.meshIndex];
      gl.bindVertexArray(m.vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, instVbo);
      for (let r = 0; r < 3; r++) gl.vertexAttribPointer(3 + r, 4, gl.FLOAT, false, 80, q.offset + 16 * r);
      gl.vertexAttribPointer(6, 4, gl.FLOAT, false, 80, q.offset + 48);
      gl.vertexAttribPointer(7, 4, gl.FLOAT, false, 80, q.offset + 64);
      if (m.twoSided) gl.disable(gl.CULL_FACE); else gl.enable(gl.CULL_FACE);
      gl.drawElementsInstanced(gl.TRIANGLES, m.count, gl.UNSIGNED_INT, 0, q.n);
    }
    // which mesh each batch draws, now that models have loaded (a model that
    // hasn't loaded yet is skipped)
    function resolveBatches() {
      for (const q of batches) {
        q.meshIndex = typeof q.mesh === "number" ? q.mesh : models.get(q.mesh);
        const t = q.pic ? textures.get(q.pic) : null;
        if (q.pic && t === null) texture(q.pic, q.line);        // missing: explain
        if (typeof q.mesh === "string" && q.meshIndex === null) modelMesh(q.mesh, q.line);
        q.tex = t ? t.tex : null;
        q.blocky = !!(t && t.w <= 64 && t.h <= 64);
        q.skip = q.meshIndex === undefined || (q.pic && !t);
      }
    }
    function shadowPass(eye, target) {
      shadowsNow = sunOn && total() > 0 && (sunRgb[0] || sunRgb[1] || sunRgb[2]);
      if (!shadowsNow) return;
      const to = sub(target, eye), dist = Math.hypot(to[0], to[1], to[2]);
      const radius = Math.max(12, Math.min(60, dist * 1.2 + 8));
      const center = dist > radius ? [eye[0] + to[0] * radius / dist, eye[1] + to[1] * radius / dist, eye[2] + to[2] * radius / dist] : target;
      const rot = lookAt([0, 0, 0], [-sunDir[0], -sunDir[1], -sunDir[2]]);
      const c = point(rot, center), texel = 2 * radius / SHADOW_SIZE;
      c[0] = Math.floor(c[0] / texel) * texel; c[1] = Math.floor(c[1] / texel) * texel;
      shadowMat = mul(ortho(c[0] - radius, c[0] + radius, c[1] - radius, c[1] + radius, -c[2] - 3 * radius, -c[2] + 3 * radius), rot);
      gl.bindFramebuffer(gl.FRAMEBUFFER, shadowFbo);
      gl.viewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);
      gl.depthMask(true);
      gl.clear(gl.DEPTH_BUFFER_BIT);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LESS);
      gl.enable(gl.POLYGON_OFFSET_FILL);
      gl.polygonOffset(2, 4);
      gl.useProgram(shadowProg);
      gl.uniformMatrix4fv(gl.getUniformLocation(shadowProg, "u_viewproj"), false, shadowMat);
      for (const q of batches) if (q.n && !q.skip) drawBatch(q);
      gl.disable(gl.POLYGON_OFFSET_FILL);
    }
    // the sky and every shape, seen through view and proj, into what's bound now
    function drawWorld(view, proj, eye, opts = {}) {
      const [top, horizon] = skyColors();
      const viewproj = mul(proj, view);
      gl.disable(gl.BLEND);
      gl.depthMask(true);
      if (!opts.noClear) {
        gl.clearColor(top[0] / 255, top[1] / 255, top[2] / 255, 1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      }
      if (!total()) return;
      if (!opts.seeThrough && (top[0] !== horizon[0] || top[1] !== horizon[1] || top[2] !== horizon[2] || opts.noClear)) {
        gl.disable(gl.DEPTH_TEST);
        gl.useProgram(skyProg);
        gl.uniformMatrix4fv(gl.getUniformLocation(skyProg, "u_inv_viewproj"), false, invert(viewproj));
        gl.uniform3f(gl.getUniformLocation(skyProg, "u_top"), top[0] / 255, top[1] / 255, top[2] / 255);
        gl.uniform3f(gl.getUniformLocation(skyProg, "u_horizon"), horizon[0] / 255, horizon[1] / 255, horizon[2] / 255);
        gl.bindVertexArray(emptyVao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LESS);
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, shadowTex);
      gl.bindSampler(1, sampShadow);
      gl.activeTexture(gl.TEXTURE0);
      const ambTop = top.map((v) => LIN[v] * 0.45 + 0.10), ambBottom = horizon.map((v) => LIN[v] * 0.25 + 0.05);
      const fog = fogOwn ? fogRgb : horizon;
      let lastProg = -2;
      for (const q of batches) {
        if (!q.n || q.skip) continue;
        const p = surfaceProgram(q.shader);
        if (q.shader !== lastProg) {
          lastProg = q.shader;
          const u = p.u;
          gl.useProgram(p.id);
          gl.uniformMatrix4fv(u.viewproj, false, viewproj);
          gl.uniformMatrix4fv(u.shadow_mat, false, shadowMat);
          gl.uniform1i(u.shadows, shadowsNow ? 1 : 0);
          gl.uniform3f(u.sun_dir, sunDir[0], sunDir[1], sunDir[2]);
          if (sunOn) gl.uniform3f(u.sun_color, LIN[sunRgb[0]], LIN[sunRgb[1]], LIN[sunRgb[2]]);
          else gl.uniform3f(u.sun_color, 0, 0, 0);
          gl.uniform3f(u.amb_top, ambTop[0], ambTop[1], ambTop[2]);
          gl.uniform3f(u.amb_bottom, ambBottom[0], ambBottom[1], ambBottom[2]);
          gl.uniform3f(u.camera, eye[0], eye[1], eye[2]);
          gl.uniform4fv(u.lights, lights);
          gl.uniform1i(u.light_count, lightCount);
          gl.uniform3f(u.fog_color, LIN[fog[0]], LIN[fog[1]], LIN[fog[2]]);
          gl.uniform2f(u.fog, fogFar * 0.35, fogFar);
          gl.uniform1f(u.time, seconds());
        }
        gl.uniform1i(p.u.textured, q.tex ? 1 : 0);
        gl.bindTexture(gl.TEXTURE_2D, q.tex || whiteTex);
        gl.bindSampler(0, q.blocky ? sampBlocky : sampSmooth);
        drawBatch(q);
      }
      gl.disable(gl.CULL_FACE);
    }
    function drawFlat() {
      if (!flat.length) return;
      gl.disable(gl.DEPTH_TEST);
      gl.disable(gl.CULL_FACE);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.useProgram(flatProg);
      gl.uniform2f(gl.getUniformLocation(flatProg, "u_screen"), W, H);
      gl.bindVertexArray(flatVao);
      gl.bindBuffer(gl.ARRAY_BUFFER, flatVbo);
      const buf = new ArrayBuffer(flat.length * 4), f = new Float32Array(buf), u32 = new Uint32Array(buf);
      for (let k = 0; k < flat.length; k += 5) { f[k] = flat[k]; f[k + 1] = flat[k + 1]; f[k + 2] = flat[k + 2]; f[k + 3] = flat[k + 3]; u32[k + 4] = flat[k + 4]; }
      gl.bufferData(gl.ARRAY_BUFFER, buf, gl.STREAM_DRAW);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindSampler(0, sampPixel);
      for (const r of runs) {
        if (!r.count) continue;
        gl.bindTexture(gl.TEXTURE_2D, r.tex);
        gl.drawArrays(gl.TRIANGLES, r.first, r.count);
      }
      gl.disable(gl.BLEND);
    }
    let shownEffect = -1, shownVals = new Float32Array(8), shownTime = 0;
    function present() {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, W, H);
      if (shownEffect >= 0) {
        const p = effectProgram(shownEffect);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.BLEND);
        gl.useProgram(p);
        gl.uniform2f(gl.getUniformLocation(p, "u_size"), W, H);
        gl.uniform1f(gl.getUniformLocation(p, "u_time"), shownTime);
        gl.uniform1fv(gl.getUniformLocation(p, "u_vars"), shownVals);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, sceneTex);
        gl.bindSampler(0, sampScene);
        gl.bindVertexArray(emptyVao);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      } else {
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, sceneFbo);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, W, H, 0, 0, W, H, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      }
    }
    function renderFrame() {
      resolveBatches();
      const view = lookAt(camEye, camTarget), proj = perspective(camFov * Math.PI / 180, W / H, 0.1, 1000);
      uploadInstances();
      shadowPass(camEye, camTarget);
      gl.bindFramebuffer(gl.FRAMEBUFFER, msaaFbo);
      gl.viewport(0, 0, W, H);
      drawWorld(view, proj, camEye);
      drawFlat();
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, msaaFbo);
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, sceneFbo);
      gl.blitFramebuffer(0, 0, W, H, 0, 0, W, H, gl.COLOR_BUFFER_BIT, gl.NEAREST);
      shownEffect = effectId;
      shownVals = effectVals.slice();
      shownTime = seconds();
      present();
    }
    function newFrame() {
      for (const q of batches) q.n = 0;
      flat = []; runs = [];
      lightCount = 0;
      setTurn(0, 0, 0);
      effectId = -1;
      dirty = false;
    }
    function countFrame() {
      const now = io.now();
      if (!fpsSince) fpsSince = now;
      fpsFrames++;
      if (now - fpsSince >= 500) { fpsValue = fpsFrames * 1000 / (now - fpsSince); fpsFrames = 0; fpsSince = now; }
    }

    // ------------------------------------------------------------ VR and AR
    // In a headset, camera(...) says where you stand (your eyes, at the start)
    // and which way you face; your head and hands move from there.
    let xr = null;
    const hands = { left: null, right: null };
    function playToWorld() {
      const d = sub(camTarget, camEye), yaw = Math.atan2(-d[0], -d[2]);
      const c = Math.cos(yaw), s = Math.sin(yaw);
      const floorY = camEye[1] - (xr && xr.floor ? 1.6 : 0);
      return new Float32Array([c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, camEye[0], floorY, camEye[2], 1]);
    }
    async function enterXR(mode) {
      if (xr) return;
      try {
        const session = await navigator.xr.requestSession(mode, { optionalFeatures: ["local-floor"] });
        if (gl.makeXRCompatible) await gl.makeXRCompatible();
        const layer = new XRWebGLLayer(session, gl, { antialias: true, alpha: mode === "immersive-ar" });
        session.updateRenderState({ baseLayer: layer, depthNear: 0.05, depthFar: 1000 });
        let space = null, floor = true;
        try { space = await session.requestReferenceSpace("local-floor"); }
        catch (e) { space = await session.requestReferenceSpace("local"); floor = false; }
        xr = { session, layer, space, mode, floor };
        session.addEventListener("end", () => { xr = null; fx.extraPads = null; });
      } catch (e) {
        io.write("err", `\nFlaux couldn't start ${mode === "immersive-ar" ? "AR" : "VR"}: ${e && e.message}\n`);
      }
    }
    function readXRInput(frame, base) {
      const pad = { buttons: new Set(), stick: [[0, 0], [0, 0]] };
      for (const src of xr.session.inputSources) {
        const side = src.handedness === "left" ? "left" : "right";
        if (src.gripSpace) {
          const pose = frame.getPose(src.gripSpace, xr.space);
          if (pose) { const m = mul(base, pose.transform.matrix); hands[side] = [m[12], m[13], m[14]]; }
        }
        const gp = src.gamepad;
        if (!gp) continue;
        const pressed = (i) => gp.buttons[i] && (gp.buttons[i].pressed || gp.buttons[i].value > 0.5);
        if (pressed(0)) pad.buttons.add(side === "left" ? "lt" : "rt");
        if (pressed(1)) pad.buttons.add(side === "left" ? "lb" : "rb");
        if (pressed(3)) pad.buttons.add(side === "left" ? "l3" : "r3");
        if (pressed(4)) pad.buttons.add(side === "left" ? "x" : "a");
        if (pressed(5)) pad.buttons.add(side === "left" ? "y" : "b");
        const s = side === "left" ? 0 : 1;
        if (gp.axes.length >= 4) { pad.stick[s][0] = gp.axes[2]; pad.stick[s][1] = -gp.axes[3]; }
      }
      fx.extraPads = pad;
    }
    // One frame in the headset: both eyes, straight into the headset's picture.
    function renderXR(frame) {
      const pose = frame.getViewerPose(xr.space);
      const base = playToWorld();
      readXRInput(frame, base);
      for (const side of ["left", "right"])       // your hands, as small balls
        if (hands[side]) shape(1, hands[side][0], hands[side][1], hands[side][2], 0.04, 0.04, 0.04, "light gray", false, 0);
      resolveBatches();
      uploadInstances();
      shadowPass(camEye, camTarget);
      if (!pose) return;
      const ar = xr.mode === "immersive-ar";
      gl.bindFramebuffer(gl.FRAMEBUFFER, xr.layer.framebuffer);
      const [top] = skyColors();
      gl.clearColor(ar ? 0 : top[0] / 255, ar ? 0 : top[1] / 255, ar ? 0 : top[2] / 255, ar ? 0 : 1);
      gl.depthMask(true);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      for (const view of pose.views) {
        const vp = xr.layer.getViewport(view);
        gl.viewport(vp.x, vp.y, vp.width, vp.height);
        const eyeWorld = mul(base, view.transform.matrix);
        drawWorld(invert(eyeWorld), view.projectionMatrix, [eyeWorld[12], eyeWorld[13], eyeWorld[14]],
          { noClear: true, seeThrough: ar });
      }
    }

    // ---------------------------------------------------- built-in functions
    const fontGlyphs = () => h.font();
    Object.assign(b, {
      window: ([w, hh, title], line) => {
        if (win) fail(line, "Call window(...) just once, at the start of your program.", "There's already a window open.");
        w = Math.round(w); hh = Math.round(hh);
        if (w < 50 || hh < 50 || w > 8000 || hh > 8000)
          fail(line, "Pick a size between 50 and 8000, like window(800, 600).", `A window can't be ${w} by ${hh} pixels.`);
        win = io.openWindow(w, hh, title === undefined ? "Flaux" : fx.str(title), "webgl2");
        gl = win.screen.getContext("webgl2", { antialias: false, alpha: false, depth: false, stencil: false,
          preserveDrawingBuffer: true, xrCompatible: true });
        if (!gl) fail(line, "Try another browser (Chrome, Edge or Firefox), or turn on hardware acceleration in its settings.",
          "This browser can't draw in 3D (it doesn't have WebGL 2).");
        W = w; H = hh; startTime = io.now();
        instVbo = gl.createBuffer();
        emptyVao = gl.createVertexArray();
        for (const [bld, two] of shapes()) finishMesh(bld, two);
        flatVao = gl.createVertexArray();
        gl.bindVertexArray(flatVao);
        flatVbo = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, flatVbo);
        gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 20, 0);
        gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 20, 8);
        gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.UNSIGNED_BYTE, true, 20, 16);
        gl.bindVertexArray(null);
        whiteTex = upload(new Uint8Array([255, 255, 255, 255]), 1, 1, false);
        const fontPx = new Uint8Array(570 * 8 * 4), font = fontGlyphs();
        for (let c = 0; c < 95; c++)
          for (let x = 0; x < 5; x++)
            for (let y = 0; y < 7; y++)
              if ((font[c][x] >> y) & 1) fontPx.fill(255, (y * 570 + c * 6 + x) * 4, (y * 570 + c * 6 + x) * 4 + 4);
        fontTex = upload(fontPx, 570, 8, false);
        const sampler = (min, mag, wrap) => {
          const s = gl.createSampler();
          gl.samplerParameteri(s, gl.TEXTURE_MIN_FILTER, min); gl.samplerParameteri(s, gl.TEXTURE_MAG_FILTER, mag);
          gl.samplerParameteri(s, gl.TEXTURE_WRAP_S, wrap); gl.samplerParameteri(s, gl.TEXTURE_WRAP_T, wrap);
          return s;
        };
        sampPixel = sampler(gl.NEAREST, gl.NEAREST, gl.CLAMP_TO_EDGE);
        sampSmooth = sampler(gl.LINEAR_MIPMAP_LINEAR, gl.LINEAR, gl.REPEAT);
        sampBlocky = sampler(gl.LINEAR_MIPMAP_LINEAR, gl.NEAREST, gl.REPEAT);
        const aniso = gl.getExtension("EXT_texture_filter_anisotropic");
        if (aniso) gl.samplerParameterf(sampSmooth, aniso.TEXTURE_MAX_ANISOTROPY_EXT,
          Math.min(8, gl.getParameter(aniso.MAX_TEXTURE_MAX_ANISOTROPY_EXT)));
        sampShadow = sampler(gl.LINEAR, gl.LINEAR, gl.CLAMP_TO_EDGE);
        gl.samplerParameteri(sampShadow, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE);
        gl.samplerParameteri(sampShadow, gl.TEXTURE_COMPARE_FUNC, gl.LEQUAL);
        sampScene = sampler(gl.LINEAR, gl.LINEAR, gl.CLAMP_TO_EDGE);
        const fbo = (colorTex, colorRb, depthRb, depthTex) => {
          const f = gl.createFramebuffer();
          gl.bindFramebuffer(gl.FRAMEBUFFER, f);
          if (colorTex) gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, colorTex, 0);
          if (colorRb) gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, colorRb);
          if (depthRb) gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depthRb);
          if (depthTex) gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, depthTex, 0);
          return f;
        };
        const makeMsaa = (n) => {
          const c = gl.createRenderbuffer();
          gl.bindRenderbuffer(gl.RENDERBUFFER, c);
          gl.renderbufferStorageMultisample(gl.RENDERBUFFER, n, gl.RGBA8, W, H);
          const d = gl.createRenderbuffer();
          gl.bindRenderbuffer(gl.RENDERBUFFER, d);
          gl.renderbufferStorageMultisample(gl.RENDERBUFFER, n, gl.DEPTH_COMPONENT24, W, H);
          return fbo(null, c, d, null);
        };
        samples = Math.min(4, gl.getParameter(gl.MAX_SAMPLES) || 0);
        msaaFbo = makeMsaa(samples);
        if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) { samples = 0; msaaFbo = makeMsaa(0); }
        sceneTex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, sceneTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, W, H, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        sceneFbo = fbo(sceneTex, null, null, null);
        shadowTex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, shadowTex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, SHADOW_SIZE, SHADOW_SIZE, 0, gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        shadowFbo = fbo(null, null, null, shadowTex);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        sceneProg = sceneProgram(null, "3D");
        shadowProg = program(glsl.shadow_vs, glsl.shadow_fs, null, "shadow");
        skyProg = program(glsl.fullscreen_vs, glsl.sky_fs, null, "sky");
        flatProg = program(glsl.flat_vs, glsl.flat_fs, null, "2D");
        gl.useProgram(flatProg);
        gl.uniform1i(gl.getUniformLocation(flatProg, "u_tex"), 0);
        renderFrame();
        h.readInput();
        // VR and AR, if this browser and computer have them
        if (navigator.xr && io.addButton) {
          for (const [mode, label] of [["immersive-vr", "Enter VR"], ["immersive-ar", "Enter AR"]])
            navigator.xr.isSessionSupported(mode).then((ok) => { if (ok) io.addButton(label, () => enterXR(mode)); }).catch(() => {});
        }
      },
      show: async (_, line) => {
        needWindow(line);
        await waitForLoading();
        frames++;
        countFrame();
        if (xr) {
          await new Promise((resolve) => xr.session.requestAnimationFrame((t, frame) => {
            try { renderXR(frame); } finally { resolve(); }
          }));
        } else {
          renderFrame();
          await io.nextFrame();
        }
        newFrame();
        if (io.cancelled()) throw new h.Cancelled();
        h.readInput();
      },
      clear: ([c], line) => {
        needWindow(line);
        skyTop = skyHorizon = rgbOf(c === undefined ? "black" : c, line);
        skySet = true;
        for (const q of batches) q.n = 0;
        flat = []; runs = [];
        dirty = true;
      },
      rect: ([x, y, w, hh, c], line) => {
        needWindow(line);
        const col = rgbOf(c === undefined ? "white" : c, line);
        if (w < 0) { x += w; w = -w; }
        if (hh < 0) { y += hh; hh = -hh; }
        const x0 = h.px(x), y0 = h.px(y);
        flatRect(x0, y0, h.px(x + w) - x0, h.px(y + hh) - y0, col);
      },
      circle: ([x, y, r, c], line) => { needWindow(line); circle(x, y, r, rgbOf(c === undefined ? "white" : c, line)); },
      triangle: ([x1, y1, x2, y2, x3, y3, c], line) => {
        needWindow(line);
        flatTri(x1, y1, x2, y2, x3, y3, rgbOf(c === undefined ? "white" : c, line));
      },
      dot: ([x, y, c], line) => { needWindow(line); flatRect(h.px(x), h.px(y), 1, 1, rgbOf(c === undefined ? "white" : c, line)); },
      line: ([x1, y1, x2, y2, c, thickness], line) => {
        needWindow(line);
        const col = rgbOf(c === undefined ? "white" : c, line);
        thickness = Math.max(1, thickness === undefined ? 1 : thickness);
        const thin = thickness <= 1.5;
        if (thin) { x1 += 0.5; y1 += 0.5; x2 += 0.5; y2 += 0.5; }
        const dx = x2 - x1, dy = y2 - y1, len = Math.hypot(dx, dy), r = thickness / 2;
        if (len > 0) {
          const nx = -dy / len * r, ny = dx / len * r, ex = thin ? dx / len * 0.5 : 0, ey = thin ? dy / len * 0.5 : 0;
          flatQuad(whiteTex, [x1 - ex + nx, y1 - ey + ny, x2 + ex + nx, y2 + ey + ny, x2 + ex - nx, y2 + ey - ny, x1 - ex - nx, y1 - ey - ny],
            [0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5], col);
        }
        if (!thin) { circle(x1, y1, r, col); circle(x2, y2, r, col); }
      },
      write: ([value, x, y, c, size], line) => {
        needWindow(line);
        const col = rgbOf(c === undefined ? "white" : c, line);
        const s = Math.max(1, Math.min(50, size === undefined ? 2 : Math.round(size)));
        const px0 = h.px(x), py0 = h.px(y);
        let colN = 0, row = 0;
        for (const ch of fx.str(value)) {
          if (ch === "\n") { row++; colN = 0; continue; }
          let code = ch.codePointAt(0);
          if (code < 32 || code > 126) code = 63;
          const x0 = px0 + colN * 6 * s, y0 = py0 + row * 9 * s;
          const u0 = (code - 32) * 6 / 570, u1 = ((code - 32) * 6 + 5) / 570, v1 = 7 / 8;
          flatQuad(fontTex, [x0, y0, x0 + 5 * s, y0, x0 + 5 * s, y0 + 7 * s, x0, y0 + 7 * s], [u0, 0, u1, 0, u1, v1, u0, v1], col);
          colN++;
        }
      },
      image: async ([name, x, y, w, hh, angle], line) => {
        needWindow(line);
        if (texture(name, line) === undefined) await waitForLoading();
        const t = texture(name, line);
        let dw = w === undefined ? t.w : w, dh = hh === undefined ? t.h : hh;
        let u0 = 0, u1 = 1, v0 = 0, v1 = 1;
        if (dw < 0) { dw = -dw; u0 = 1; u1 = 0; }
        if (dh < 0) { dh = -dh; v0 = 1; v1 = 0; }
        const x0 = h.px(x), y0 = h.px(y), x1 = h.px(x + dw), y1 = h.px(y + dh);
        const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, a = (angle || 0) * Math.PI / 180, ca = Math.cos(a), sa = Math.sin(a);
        const xy = [];
        for (const [px, py] of [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]) {
          const ddx = px - cx, ddy = py - cy;
          xy.push(cx + ddx * ca - ddy * sa, cy + ddx * sa + ddy * ca);
        }
        flatQuad(t.tex, xy, [u0, v0, u1, v0, u1, v1, u0, v1], [255, 255, 255]);
      },
      image_width: async ([name], line) => { needWindow(line); if (texture(name, line) === undefined) await waitForLoading(); return texture(name, line).w; },
      image_height: async ([name], line) => { needWindow(line); if (texture(name, line) === undefined) await waitForLoading(); return texture(name, line).h; },
      screenshot: ([name], line) => { needWindow(line); if (io.screenshot) io.screenshot(fx.str(name), win.screen); },
      fps: () => Math.round(fpsValue),
      seconds: () => seconds(),
      // 3D
      camera: ([x, y, z, lx, ly, lz, fov]) => {
        camEye = [x, y, z]; camTarget = [lx, ly, lz];
        if (lx === x && ly === y && lz === z) camTarget[2] -= 1;
        camFov = Math.max(5, Math.min(170, fov === undefined ? 60 : fov));
      },
      sky: ([top, horizon], line) => {
        skyTop = rgbOf(top, line);
        skyHorizon = horizon === undefined ? skyTop : rgbOf(horizon, line);
        skySet = true;
      },
      sun: ([x, y, z, c], line) => {
        sunOn = !(x === 0 && y === 0 && z === 0);
        if (sunOn) sunDir = norm([x, y, z]);
        sunRgb = rgbOf(c === undefined ? "white" : c, line);
      },
      light: ([x, y, z, c, range], line) => {
        const col = rgbOf(c === undefined ? "white" : c, line);
        range = range === undefined ? 10 : range;
        if (lightCount >= 8 || range <= 0) return;
        lights.set([x, y, z, range, LIN[col[0]] * 1.5, LIN[col[1]] * 1.5, LIN[col[2]] * 1.5, 0], lightCount * 8);
        lightCount++;
      },
      fog: ([distance, c], line) => {
        fogFar = distance > 0 ? distance : 0;
        fogOwn = c !== undefined;
        if (fogOwn) fogRgb = rgbOf(c, line);
      },
      turn: ([ax, ay, az]) => setTurn(ax, ay, az),
      cube: ([x, y, z, size, p], line) => shape(0, x, y, z, size, size, size, p, false, line),
      box: ([x, y, z, w, hh, d, p], line) => shape(0, x, y, z, w, hh, d, p, false, line),
      ball: ([x, y, z, r, p], line) => shape(1, x, y, z, r, r, r, p, false, line),
      cylinder: ([x, y, z, r, hh, p], line) => shape(2, x, y, z, r, hh, r, p, false, line),
      cone: ([x, y, z, r, hh, p], line) => shape(3, x, y, z, r, hh, r, p, false, line),
      plane: ([x, y, z, w, d, p], line) => shape(4, x, y, z, w, 1, d, p, true, line),
      model: ([name, x, y, z, size, p], line) => {
        needWindow(line);
        const m = modelMesh(name, line);
        shape(m === undefined ? name : m, x, y, z, size, size, size, p, false, line);
      },
      effect: ([paint]) => { effectId = paint.shader; effectVals = new Float32Array(8); effectVals.set(paint.values.slice(0, 8)); dirty = true; },
      // VR
      in_vr: () => !!xr,
      hand_x: ([side]) => handPart(side, 0),
      hand_y: ([side]) => handPart(side, 1),
      hand_z: ([side]) => handPart(side, 2),
    });
    function handPart(side, k) {
      const s = String(side === undefined ? "right" : side).toLowerCase() === "left" ? "left" : "right";
      return hands[s] ? hands[s][k] : camEye[k];
    }

    fx.finish = async () => {
      if (!gl) return;
      await waitForLoading();
      if (dirty) renderFrame();
      else present();
    };
    fx.gl = () => gl;   // for tests
  }

  const api = { install, parseObj };
  if (typeof module === "object" && module.exports) module.exports = api;
  root.FlauxRuntime3D = api;
})(typeof window !== "undefined" ? window : globalThis);
