#!/usr/bin/env python3
"""Regenerate the Linux hicolor icons from the npad artwork in assets/.

Writes packaging/linux/icons/hicolor/<N>x<N>/apps/<name>.png for the standard
size (icon_v2_light.png) and the dark variant (icon_v2_dark.png, installed as
<name>-dark and picked by the window when the colour scheme is dark).

Both variants share one crop and one scale per size, so they line up exactly
and the artwork is never stretched: it is cropped to its ink, scaled by a
single factor to fit the canvas minus a margin, and centred. The margin
matches what the Adwaita/hicolor application icons leave, so npad is no
bigger than its neighbours in a panel. Rendering at 8x and reducing lets the
art sit on half-pixel centres without distorting its proportions.

Development tool only (needs Pillow); the PNGs are committed. Run from the
repository root:  python3 scripts/make-linux-icons.py
"""

import os
import sys

from PIL import Image

APP_ID = "io.github.platima.npad"
VARIANTS = (("icon_v2_light.png", APP_ID), ("icon_v2_dark.png", APP_ID + "-dark"))
# size -> transparent margin on each side, in pixels
MARGINS = {16: 1, 24: 2, 32: 2, 48: 3, 64: 4, 128: 8, 256: 16}
SUPERSAMPLE = 8
ALPHA_FLOOR = 8  # ignore the faintest shadow fringe when finding the ink box

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ink_box(images):
    box = None
    for im in images:
        alpha = im.getchannel("A").point(lambda v: 255 if v > ALPHA_FLOOR else 0)
        b = alpha.getbbox()
        box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    return box


def render(art, size):
    # Uniform scale: the art's longer side fills the canvas minus the margins.
    avail = size - 2 * MARGINS[size]
    hi = size * SUPERSAMPLE
    factor = avail * SUPERSAMPLE / max(art.size)
    w = max(1, round(art.width * factor))
    h = max(1, round(art.height * factor))
    scaled = art.resize((w, h), Image.LANCZOS)
    canvas = Image.new("RGBA", (hi, hi), (0, 0, 0, 0))
    canvas.paste(scaled, ((hi - w) // 2, (hi - h) // 2), scaled)
    return canvas.resize((size, size), Image.LANCZOS)


def main():
    sources = [
        (Image.open(os.path.join(ROOT, "assets", f)).convert("RGBA"), name) for f, name in VARIANTS
    ]
    box = ink_box([im for im, _ in sources])
    for im, name in sources:
        art = im.crop(box)
        for size in MARGINS:
            out = os.path.join(
                ROOT, "packaging", "linux", "icons", "hicolor", f"{size}x{size}", "apps", name + ".png"
            )
            os.makedirs(os.path.dirname(out), exist_ok=True)
            render(art, size).save(out, optimize=True)
            print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
