#!/usr/bin/env python3
"""Turns the controller models (models/left.glb, models/right.glb) into the simple files the app
reads (assets/controllers/*.fxm): every part of the model moved into place, as one list of
triangles, with its picture.

    FXM1, vertex count, index count, picture width, picture height   (five 32-bit numbers)
    vertices: x y z, normal x y z, u v   (floats)
    indices   (32-bit)
    picture   (RGBA bytes)

Run:  python3 tools/make_controllers.py   (needs Pillow and numpy)"""
import io, json, os, struct
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX = 512

def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])

def node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], dtype=np.float64).reshape(4, 4).T
    m = np.eye(4)
    m[:3, :3] = quat_matrix(n.get("rotation", [0, 0, 0, 1])) * np.array(n.get("scale", [1, 1, 1]))
    m[:3, 3] = n.get("translation", [0, 0, 0])
    return m

def convert(name):
    data = open(os.path.join(ROOT, "models", name + ".glb"), "rb").read()
    json_len = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + json_len])
    blob = data[20 + json_len + 8:]

    def view(i):
        v = gltf["bufferViews"][i]
        return blob[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]], v.get("byteStride", 0)

    def accessor(i):
        a = gltf["accessors"][i]
        raw, stride = view(a["bufferView"])
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        kind = {5126: "<f4", 5125: "<u4", 5123: "<u2", 5121: "u1"}[a["componentType"]]
        size = np.dtype(kind).itemsize * width
        off = a.get("byteOffset", 0)
        if stride and stride != size:
            rows = [np.frombuffer(raw, kind, width, off + k * stride) for k in range(a["count"])]
            return np.array(rows)
        return np.frombuffer(raw, kind, a["count"] * width, off).reshape(a["count"], width)

    verts, indices = [], []
    def walk(i, parent):
        n = gltf["nodes"][i]
        m = parent @ node_matrix(n)
        if "mesh" in n:
            normal_m = np.linalg.inv(m[:3, :3]).T
            for prim in gltf["meshes"][n["mesh"]]["primitives"]:
                pos = accessor(prim["attributes"]["POSITION"]).astype(np.float64)
                nrm = accessor(prim["attributes"]["NORMAL"]).astype(np.float64)
                uv = accessor(prim["attributes"]["TEXCOORD_0"]).astype(np.float64)
                pos = pos @ m[:3, :3].T + m[:3, 3]
                nrm = nrm @ normal_m.T
                nrm /= np.maximum(np.linalg.norm(nrm, axis=1, keepdims=True), 1e-9)
                base = sum(len(v) for v in verts)
                verts.append(np.hstack([pos, nrm, uv]))
                indices.append(accessor(prim["indices"]).reshape(-1).astype(np.uint32) + base)
        for c in n.get("children", []):
            walk(c, m)
    for root in gltf["scenes"][gltf.get("scene", 0)]["nodes"]:
        walk(root, np.eye(4))
    v = np.vstack(verts).astype("<f4"); idx = np.concatenate(indices).astype("<u4")

    raw, _ = view(gltf["images"][0]["bufferView"])
    tex = Image.open(io.BytesIO(raw)).convert("RGBA").resize((TEX, TEX), Image.LANCZOS)
    out = os.path.join(ROOT, "assets", "controllers", name + ".fxm")
    with open(out, "wb") as f:
        f.write(b"FXM1" + struct.pack("<4I", len(v), len(idx), TEX, TEX))
        f.write(v.tobytes()); f.write(idx.tobytes()); f.write(tex.tobytes())
    lo, hi = v[:, :3].min(0), v[:, :3].max(0)
    print(f"{name}: {len(v)} vertices, {len(idx) // 3} triangles, from {lo.round(3)} to {hi.round(3)} m, {os.path.getsize(out) // 1024} KB")
    return tex

if __name__ == "__main__":
    for n in ("left", "right"):
        convert(n)
