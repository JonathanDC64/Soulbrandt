"""Make the menus' own images (port/ui/res/img): drawn here from numbers, with no game pixels.

  grain.png     192x192, tiles: a worn-stone noise laid over the menus' bars and boxes. Four octaves of value noise
                from a fixed seed (the looks mock-up's, so the same image), dark specks and light ones, mostly clear.
  valuebox.png  24x24 at 2x: DeS UI's value box, ink inside a one-pixel warm edge, its corners cut at 45
                degrees. A nine-patch: its 10-pixel corners stay, the middle stretches (the sheet's spritesheet says
                where).

  python tools/ui/make_ui_art.py           write them
  python tools/ui/make_ui_art.py --check   exit 1 if the files in the tree do not hold the pixels this makes

Standard library only, so the same pixels on every machine.
"""
import argparse
import math
import os
import struct
import sys
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "port", "ui", "res", "img")


def png(width, height, rgba):
    """An 8-bit RGBA PNG of rows top to bottom; rgba is width*height*4 bytes."""
    raw = b"".join(b"\0" + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))  # (filter 0 each)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def decoded(data):
    """A PNG's header and uncompressed rows, or None if it is not one. --check compares these, not the bytes: another
    zlib may compress the same rows to other bytes."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    header, idat, i = None, b"", 8
    while i + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[i:i + 8])
        if kind == b"IHDR":
            header = data[i + 8:i + 8 + n]
        elif kind == b"IDAT":
            idat += data[i + 8:i + 8 + n]
        i += 12 + n
    try:
        return header, zlib.decompress(idat)
    except zlib.error:
        return None


def grain(n=192):
    seed = [7]

    def rnd():  # Park and Miller's minimal standard generator
        seed[0] = (seed[0] * 16807) % 2147483647
        return (seed[0] - 1) / 2147483646

    def layer(cell):
        m = math.ceil(n / cell) + 1
        return cell, m, [rnd() for _ in range(m * m)]

    layers = [layer(48), layer(16), layer(6), layer(2)]
    weights = [0.42, 0.28, 0.18, 0.12]

    def sample(lay, px, py):
        cell, m, g = lay
        fx, fy = px / cell, py / cell
        x0, y0 = math.floor(fx), math.floor(fy)
        tx, ty = fx - x0, fy - y0

        def at(i, j):  # (wraps, so the image tiles)
            return g[((j % (m - 1)) * m) + (i % (m - 1))]

        a = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx
        b = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx
        return a * (1 - ty) + b * ty

    out = bytearray(n * n * 4)
    for py in range(n):
        for px in range(n):
            v = 0.0
            for lay, w in zip(layers, weights):
                v += w * sample(lay, px, py)
            dark = v < 0.5
            o = (py * n + px) * 4
            out[o] = out[o + 1] = out[o + 2] = 0 if dark else 255
            out[o + 3] = math.floor(min(1.0, abs(v - 0.5) * 2.2) * (46 if dark else 22) + 0.5)
    return n, n, out


def valuebox(size=24, scale=2, cut=4, edge=(0x4D, 0x4A, 0x42), ink=(0x03, 0x03, 0x03), samples=8):
    """The box's outer octagon in edge's colour, the octagon one pixel inside it in ink, at scale times."""
    c, b = cut * scale, 1 * scale

    def inside(x, y, x0, y0, x1, y1):
        return (x0 <= x <= x1 and y0 <= y <= y1 and (x - x0) + (y - y0) >= c and (x1 - x) + (y - y0) >= c and
                (x - x0) + (y1 - y) >= c and (x1 - x) + (y1 - y) >= c)

    out = bytearray(size * size * 4)
    for py in range(size):
        for px in range(size):
            outer = inner = 0
            for sy in range(samples):
                for sx in range(samples):
                    x, y = px + (sx + 0.5) / samples, py + (sy + 0.5) / samples
                    outer += inside(x, y, 0, 0, size, size)
                    inner += inside(x, y, b, b, size - b, size - b)
            total = samples * samples
            a = outer / total
            o = (py * size + px) * 4
            if a > 0:
                ki, ke = inner / total, (outer - inner) / total
                for k in range(3):
                    out[o + k] = math.floor((ink[k] * ki + edge[k] * ke) / a + 0.5)
            out[o + 3] = math.floor(a * 255 + 0.5)
    return size, size, out


IMAGES = {"grain.png": grain, "valuebox.png": valuebox}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="compare with the files in the tree; write nothing")
    args = ap.parse_args()
    bad = 0
    for name, make in IMAGES.items():
        data = png(*make())
        path = os.path.normpath(os.path.join(OUT, name))
        if args.check:
            try:
                same = decoded(open(path, "rb").read()) == decoded(data)
            except OSError:
                same = False
            if not same:
                print(f"make_ui_art: {path} is not what this script makes; run it and commit the file")
                bad += 1
        else:
            os.makedirs(OUT, exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            print(f"make_ui_art: wrote {path} ({len(data)} bytes)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
