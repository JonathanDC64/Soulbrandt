#!/usr/bin/env python3
"""The start screen logo's map for its flame: port/ui/res/img/soulbrandt-logo-map.png, made from the logo
(port/ui/res/img/soulbrandt-logo.png). Run it again whenever the logo changes:

    python tools/ui/make_logo_map.py

The map covers the logo and 180 px round it at the logo's 1320 px (60 dp round a 440 dp logo), at half that size:
- red: the letters, softened a little (the glow right at their edges);
- green: nothing;
- blue: a wide soft halo round the letters, their counters and cracks filled (what the flame rises from).
flame_shader.hpp reads it. It needs Pillow, so CI doesn't run it; the map is committed as it comes out.
"""
import pathlib
import sys

from PIL import Image, ImageChops, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parents[2]
LOGO = ROOT / "port" / "ui" / "res" / "img" / "soulbrandt-logo.png"
MAP = ROOT / "port" / "ui" / "res" / "img" / "soulbrandt-logo-map.png"
PAD = 180     # round the logo, at its own size
CLOSE = 7     # closes the cracks drawn into the letters, so the halo has no gaps along them
HALO = 40     # the halo's blur radius, at the logo's own size
EDGE = 1.0    # the letters' softening, at the map's size


def main() -> int:
    alpha = Image.open(LOGO).getchannel("A")
    w, h = alpha.width + 2 * PAD, alpha.height + 2 * PAD

    def padded(img: Image.Image) -> Image.Image:
        out = Image.new("L", (w, h), 0)
        out.paste(img, (PAD, PAD))
        return out

    letters = padded(alpha)
    closed = padded(alpha.filter(ImageFilter.MaxFilter(CLOSE)).filter(ImageFilter.MinFilter(CLOSE)))
    halo = ImageChops.lighter(letters, closed).filter(ImageFilter.GaussianBlur(HALO))
    peak = max(halo.getextrema()[1], 1)
    halo = halo.point(lambda v: min(255, round(v * 255 / peak * 1.6)))

    size = (w // 2, h // 2)
    red = letters.resize(size, Image.LANCZOS).filter(ImageFilter.GaussianBlur(EDGE))
    blue = halo.resize(size, Image.LANCZOS)
    Image.merge("RGB", (red, Image.new("L", size, 0), blue)).save(MAP, optimize=True)
    print(f"{MAP.relative_to(ROOT)}: {size[0]} x {size[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
