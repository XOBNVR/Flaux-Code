"""Makes rocket.obj and rocket.mtl, the model in model_viewer.flx.

    python3 make_models.py

A model is a list of corners (v x y z) and faces that join them (f a b c),
grouped by material (usemtl), whose colours are in the .mtl file (Kd r g b,
each 0 to 1). Blender and most 3D programs can save models like this
(File, Export, Wavefront (.obj)).
"""
import math

SEGMENTS = 24
v, faces = [], []          # faces: (material, [corner numbers])


def ring(y, r):
    start = len(v) + 1
    for i in range(SEGMENTS):
        a = 2 * math.pi * i / SEGMENTS
        v.append((r * math.cos(a), y, r * math.sin(a)))
    return start


def join(lower, upper, material):
    for i in range(SEGMENTS):
        j = (i + 1) % SEGMENTS
        faces.append((material, [lower + i, upper + i, upper + j, lower + j]))


def tip(top_ring, y, material):
    v.append((0, y, 0))
    t = len(v)
    for i in range(SEGMENTS):
        faces.append((material, [top_ring + i, t, top_ring + (i + 1) % SEGMENTS]))


profile = [(0.0, 0.45), (0.3, 0.5), (2.2, 0.5), (2.6, 0.42)]
rings = [ring(y, r) for y, r in profile]
join(rings[0], rings[1], "flame_red")
join(rings[1], rings[2], "body")
join(rings[2], rings[3], "stripe")
tip(rings[3], 3.4, "nose")
v.append((0, 0, 0))
bottom = len(v)
for i in range(SEGMENTS):
    faces.append(("dark", [rings[0] + (i + 1) % SEGMENTS, bottom, rings[0] + i]))
for k in range(3):                      # three fins
    a = 2 * math.pi * k / 3
    c, s = math.cos(a), math.sin(a)
    base = len(v) + 1
    for x, y in ((0.45, 0.0), (1.1, -0.2), (0.5, 1.0)):
        v.append((c * x, y, s * x))
    for x, y in ((0.45, 0.0), (1.1, -0.2), (0.5, 1.0)):
        v.append((c * x - s * 0.06, y, s * x + c * 0.06))
    faces.append(("fin", [base, base + 1, base + 2]))
    faces.append(("fin", [base + 3, base + 5, base + 4]))

with open("rocket.mtl", "w") as fh:
    for name, rgb in [("body", (0.85, 0.85, 0.9)), ("stripe", (0.05, 0.25, 0.85)), ("nose", (0.85, 0.08, 0.05)),
                      ("fin", (0.85, 0.08, 0.05)), ("flame_red", (0.9, 0.35, 0.02)), ("dark", (0.1, 0.1, 0.1))]:
        fh.write(f"newmtl {name}\nKd {rgb[0]} {rgb[1]} {rgb[2]}\n\n")
with open("rocket.obj", "w") as fh:
    fh.write("# a rocket for Flaux's model example (made by make_models.py)\nmtllib rocket.mtl\n")
    for x, y, z in v:
        fh.write(f"v {x:.4f} {y:.4f} {z:.4f}\n")
    current = None
    for material, corners in faces:
        if material != current:
            fh.write(f"usemtl {material}\n")
            current = material
        fh.write("f " + " ".join(str(c) for c in corners) + "\n")
print(f"rocket.obj: {len(v)} corners, {len(faces)} faces")
