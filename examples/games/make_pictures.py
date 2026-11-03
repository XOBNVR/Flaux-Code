"""Draws the pixel-art pictures used by the example games.

Each picture is a grid of letters; each letter is a colour and "." is
see-through. Run it (it needs the Pillow library) to make the .png files:

    python3 make_pictures.py
"""
import os

from PIL import Image

COLORS = {
    "W": (232, 238, 245), "B": (47, 111, 237), "R": (232, 48, 58), "O": (255, 140, 26),
    "Y": (255, 225, 53), "G": (138, 127, 115), "D": (92, 84, 75), "L": (176, 166, 152),
}

PICTURES = {
    "ship.png": """
.......WW.......
......WWWW......
......WBBW......
.....WWBBWW.....
.....WWWWWW.....
....RWWWWWWR....
...RRWWWWWWRR...
..RRRWWWWWWRRR..
.RRRRWWWWWWRRRR.
RRRRWWWWWWWWRRRR
RRR.WWWWWWWW.RRR
RR..WWW..WWW..RR
.....OO..OO.....
.....YY..YY.....
......Y..Y......
................
""",
    "rock.png": """
.....GGGGG......
...GGGGGGGGG....
..GGGDGGGGGGG...
.GGGDDGGGGLGGG..
.GGGGGGGGLLGGGG.
GGGGGGGGGGGGGGG.
GGLGGGGDDGGGGGGG
GGLLGGGDDGGGGGGG
GGGGGGGGGGGGLGGG
.GGGGGGGGGGGLLGG
.GGGDGGGGGGGGGG.
..GGDDGGGGGDGGG.
...GGGGGGGGDGG..
....GGGGGGGGG...
......GGGGG.....
................
""",
}

here = os.path.dirname(os.path.abspath(__file__))
for name, art in PICTURES.items():
    rows = art.strip().split("\n")
    img = Image.new("RGBA", (len(rows[0]), len(rows)), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                img.putpixel((x, y), COLORS[ch] + (255,))
    img.save(os.path.join(here, name))
    print("made", name)
