"""Makes every Flaux icon from logo-shape.png (the logo's shape, as see-through
pixels). Needs the Pillow library:  python3 assets/make_icon.py

Colours: black background, dark purple logo, light purple outline.
"""
import base64
import os

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BACKGROUND = (0, 0, 0)
LOGO = (76, 29, 149)        # dark purple  #4C1D95
OUTLINE = (196, 181, 253)   # light purple #C4B5FD


def icon(size, fill=0.80):
    """One square icon. The outline is thicker (compared to the size) on small icons."""
    ss = 4 if size <= 256 else 2                      # draw big, then shrink: smooth edges
    big = size * ss
    shape = Image.open(os.path.join(HERE, "logo-shape.png")).getchannel("A")
    width = int(big * fill)
    height = int(shape.height * width / shape.width)
    shape = shape.resize((width, height), Image.LANCZOS).point(lambda v: 255 if v >= 128 else 0)
    mask = Image.new("L", (big, big), 0)
    mask.paste(shape, ((big - width) // 2, (big - height) // 2))

    # The outline: grow the shape evenly in every direction (blur, then keep
    # everything that got a little colour). Thin on big icons, at least 1 pixel.
    thickness = max(1.0, size * 0.008) * ss            # outline width, in big pixels
    sigma = thickness / 1.87                           # a blur this wide grows it by `thickness`
    ring = mask.filter(ImageFilter.GaussianBlur(sigma)).point(lambda v: 255 if v > 8 else 0)

    img = Image.new("RGB", (big, big), BACKGROUND)
    img.paste(Image.new("RGB", (big, big), OUTLINE), mask=ring)
    img.paste(Image.new("RGB", (big, big), LOGO), mask=mask)
    return img.resize((size, size), Image.LANCZOS)


def main():
    out = os.path.join(HERE, "flaux-icon.png")
    icon(1024).save(out, optimize=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [icon(s) for s in sizes]
    images[-1].save(os.path.join(HERE, "flaux.ico"), format="ICO", sizes=[(s, s) for s in sizes],
                    append_images=images[:-1])
    icon(256).save(os.path.join(ROOT, "flauxc", "studio", "icon.png"), optimize=True)
    # the little icon in the corner of game windows, as raw pixels for the C runtime
    small = icon(32).convert("RGBA")
    data = base64.b64encode(small.tobytes()).decode()
    with open(os.path.join(ROOT, "flauxc", "window_icon.py"), "w") as fh:
        fh.write('"""The 32x32 Flaux icon for game windows, as RGBA pixels (made by assets/make_icon.py)."""\n')
        fh.write(f"SIZE = 32\nRGBA = {data!r}\n")
    print("made", out, "flaux.ico, studio/icon.png and window_icon.py")


if __name__ == "__main__":
    main()
