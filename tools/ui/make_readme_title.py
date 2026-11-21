#!/usr/bin/env python3
"""The README's title: assets/brand/soulbrandt-title.webp, the logo with its soul flame over rising smoke, as on the
start screen. It runs the start screen's own maths (port/ui/src/fog_shader.hpp's "start" smoke and
port/ui/src/flame_shader.hpp's "logo" flame, the flame from port/ui/res/img/soulbrandt-logo-map.png) with numpy, puts
the letters (port/ui/res/img/soulbrandt-logo.png) over them, and makes the frames loop: the last LOOP_BLEND seconds
fade into the frames before the first. Run it again when the logo, the smoke or the flame changes:

    python tools/ui/make_readme_title.py [--seconds 6] [--fps 15] [--scale 1.0] [--quality 82] [--out <file>]

An animated WebP, not a GIF: the smoke's soft greys need more than a GIF's 256 colours (they band), and the WebP is
about a tenth of the size. --out <name>.gif still writes a GIF. It needs numpy and Pillow, so CI doesn't run it; the
picture is committed as it comes out. Nothing in it comes from the game: the smoke and the flame are this project's
shaders, the letters its logo.
"""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
LOGO = ROOT / "port" / "ui" / "res" / "img" / "soulbrandt-logo.png"
MAP = ROOT / "port" / "ui" / "res" / "img" / "soulbrandt-logo-map.png"
OUT = ROOT / "assets" / "brand" / "soulbrandt-title.webp"

# The picture, in dp (the start screen's units: 720 dp is a 16:9 window's height). The logo is 440 x 156 dp as on the
# start screen, its flame's box (the map's 560 x 276 dp and 100 dp above for the flame) as there too.
W_DP, H_DP = 1000.0, 400.0
LOGO_DP = (440.0, 156.0)
ELEM_DP = (560.0, 376.0)              # flame_shader.hpp: the logo with 60 dp round it and 160 above
ELEM_AT = ((W_DP - ELEM_DP[0]) / 2, 12.0)
LETTERS_AT = (ELEM_AT[0] + 60.0, ELEM_AT[1] + 160.0)
TINT = np.array([0xF7, 0xFB, 0xFF], np.float32) / 255.0   # des.rcss: image-color #f7fbff
LOOP_BLEND = 1.5                      # seconds at the end that fade into the start

# ---- the noise (fog_shader.hpp's DES_UI_HLSL_NOISE) ----

U32 = np.uint32


def pcg3d(x, y, z):
    x = x * U32(1664525) + U32(1013904223)
    y = y * U32(1664525) + U32(1013904223)
    z = z * U32(1664525) + U32(1013904223)
    x = x + y * z
    y = y + z * x
    z = z + x * y
    x ^= x >> U32(16)
    y ^= y >> U32(16)
    z ^= z >> U32(16)
    x = x + y * z
    y = y + z * x
    z = z + x * y
    return x, y, z


def hash33(ix, iy, iz):
    h = pcg3d(ix.astype(np.int32).view(U32), iy.astype(np.int32).view(U32), iz.astype(np.int32).view(U32))
    k = np.float32(2.0 / 4294967295.0)
    return [c.astype(np.float32) * k - np.float32(1.0) for c in h]


def gnoise(px, py, pz):
    ix, iy, iz = np.floor(px), np.floor(py), np.floor(pz)
    fx, fy, fz = px - ix, py - iy, pz - iz

    def fade(f):
        return f * f * f * (f * (f * 6.0 - 15.0) + 10.0)

    ux, uy, uz = fade(fx), fade(fy), fade(fz)

    def corner(dx, dy, dz):
        g = hash33(ix + dx, iy + dy, iz + dz)
        return g[0] * (fx - dx) + g[1] * (fy - dy) + g[2] * (fz - dz)

    def lerp(a, b, t):
        return a + (b - a) * t

    x00 = lerp(corner(0, 0, 0), corner(1, 0, 0), ux)
    x10 = lerp(corner(0, 1, 0), corner(1, 1, 0), ux)
    x01 = lerp(corner(0, 0, 1), corner(1, 0, 1), ux)
    x11 = lerp(corner(0, 1, 1), corner(1, 1, 1), ux)
    return lerp(lerp(x00, x10, uy), lerp(x01, x11, uy), uz)


def fbm(px, py, pz, octaves, gain):
    s, a, n = 0.0, 1.0, 0.0
    for i in range(6):
        if i >= octaves:
            break
        s = s + a * gnoise(px, py, pz)
        n += a
        px, py, pz = px * 2.02 + 17.3, py * 2.02 + 9.1, pz * 2.02 + 4.7
        a *= gain
    return s / n


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---- the smoke: fog_shader.hpp's "start" (one layer) ----

FOG_SPEED = 3.5
FOG = dict(size=3.0, detail=4.0, rough=0.5, swirl=1.0, churn=0.035, curl=0.1, drift_up=15.0, cover=0.79, soft=0.9,
           bright=0.63, seed=7.0)


def fog(uvx, uvy, seconds):
    """The smoke's alpha at uv (x over the height, as the shader's uv.x times its aspect; y down, 1 = 720 dp)."""
    L = FOG
    t = seconds * FOG_SPEED
    qx = uvx * L["size"]
    qy = (uvy + L["drift_up"] / 720.0 * t) * L["size"]
    e_z = L["seed"] * 13.37
    zw = t * L["curl"] + e_z * 1.7
    wx = fbm(qx + 1.7, qy + 9.2, np.full_like(qx, zw), 3, 0.5)
    wy = fbm(qx + 8.3, qy + 2.8, np.full_like(qx, zw + 3.1), 3, 0.5)
    n = fbm(qx + L["swirl"] * wx, qy + L["swirl"] * wy, np.full_like(qx, t * L["churn"] + e_z), L["detail"],
            L["rough"])
    lo = 0.35 + (-0.35 - 0.35) * L["cover"]
    a = smoothstep(lo, lo + L["soft"] * 0.6 + 0.01, n)
    return a * L["bright"]


# ---- the flame: flame_shader.hpp's "logo" ----

FLAME = dict(color=np.array([0.70, 0.84, 1.00], np.float32), strength=0.59, reach=2.0, width=1.0, speed=0.4,
             flicker=0.3)
MAP_DP = (560.0, 276.0)
K_TOP = 100.0 / 376.0


def sampler(img):
    """Bilinear, clamped, zero outside [0, 1] (flame_map): a function of q (x, y in the map's box, 0 to 1)."""
    a = np.asarray(img, np.float32) / 255.0
    h, w = a.shape[:2]

    def sample(qx, qy):
        inside = (qx >= 0) & (qx <= 1) & (qy >= 0) & (qy <= 1)
        x = np.clip(qx * w - 0.5, 0, w - 1)
        y = np.clip(qy * h - 0.5, 0, h - 1)
        x0, y0 = np.floor(x).astype(np.int32), np.floor(y).astype(np.int32)
        x1, y1 = np.minimum(x0 + 1, w - 1), np.minimum(y0 + 1, h - 1)
        fx, fy = (x - x0)[..., None], (y - y0)[..., None]
        top = a[y0, x0] * (1 - fx) + a[y0, x1] * fx
        bot = a[y1, x0] * (1 - fx) + a[y1, x1] * fx
        return (top * (1 - fy) + bot * fy) * inside[..., None]

    return sample


def flame(uvx, uvy, seconds, flame_map):
    """The flame's light (rgb) at uv over its element (0 to 1)."""
    L = FLAME
    t, clock = seconds * L["speed"], seconds
    S, R, Wd = L["strength"], L["reach"], L["width"]
    qx, qy = uvx, (uvy - K_TOP) / (1.0 - K_TOP)
    n = fbm(qx * 9.0, qy * 5.0 + t * 1.1, np.full_like(qx, t * 0.25), 3, 0.5)
    offx, offy = n * 0.05 * (0.4 + Wd), 0.10 + 0.16 * R
    f = np.zeros_like(qx)
    for k in range(8):
        fk = (k + 0.5) / 8.0
        f = f + flame_map(qx + offx * fk, qy + offy * fk)[..., 2] * (1.0 - fk) * (1.0 - fk)
    f = f / 8.0
    tongues = smoothstep(-0.1, 0.45,
                         fbm(qx * 18.0 + n * 3.0, qy * 6.0 + t * 2.0, np.full_like(qx, t * 0.4), 3, 0.5) + 0.25)
    m = flame_map(qx, qy)
    above = smoothstep(-0.05, 0.15, f - m[..., 2] * 0.35)
    g = gnoise(qx * MAP_DP[0] * 0.035, qy * MAP_DP[1] * 0.035, np.full_like(qx, clock * 2.3))
    flick = 1.0 + (0.55 + 0.9 * (0.5 + 0.5 * g) - 1.0) * L["flicker"]
    amount = S * flick * (f * tongues * above * 9.0 + m[..., 0] * 0.45 + m[..., 2] * 0.2 * R)
    return np.maximum(L["color"] * amount[..., None], 0.0)


# ---- one frame ----

class Title:
    def __init__(self, scale):
        self.s = scale
        self.w, self.h = round(W_DP * scale), round(H_DP * scale)
        # The smoke at half the size, scaled up (soft enough; the start screen does the same on an integrated GPU).
        self.fw, self.fh = (self.w + 1) // 2, (self.h + 1) // 2
        ys, xs = np.mgrid[0:self.fh, 0:self.fw].astype(np.float32)
        self.fog_uv = ((xs + 0.5) / self.fh * H_DP / 720.0, (ys + 0.5) / self.fh * H_DP / 720.0)
        # The flame's element.
        self.ex, self.ey = round(ELEM_AT[0] * scale), round(ELEM_AT[1] * scale)
        self.ew, self.eh = round(ELEM_DP[0] * scale), round(ELEM_DP[1] * scale)
        ys, xs = np.mgrid[0:self.eh, 0:self.ew].astype(np.float32)
        self.el_uv = ((xs + 0.5) / self.ew, (ys + 0.5) / self.eh)
        self.flame_map = sampler(Image.open(MAP).convert("RGB"))
        # The letters, premultiplied, at their size here.
        lw, lh = round(LOGO_DP[0] * scale), round(LOGO_DP[1] * scale)
        logo = np.asarray(Image.open(LOGO).convert("RGBA"), np.float32) / 255.0
        pre = np.concatenate([logo[..., :3] * logo[..., 3:], logo[..., 3:]], axis=2)
        planes = [Image.fromarray(pre[..., c]).resize((lw, lh), Image.LANCZOS) for c in range(4)]
        pre = np.clip(np.stack([np.asarray(p, np.float32) for p in planes], axis=2), 0.0, 1.0)
        self.letters = pre[..., :3] * TINT
        self.letters_a = pre[..., 3:]
        self.lx, self.ly = round(LETTERS_AT[0] * scale), round(LETTERS_AT[1] * scale)
        # The dark round the logo and its flame, at the top and in the corners (des.rcss's fades and vignette, made for
        # a banner): the logo stands on black as on the start screen, the smoke round it.
        ys, xs = np.mgrid[0:self.h, 0:self.w].astype(np.float32)
        cx, cy = (self.lx + lw / 2), (self.ly + lh / 2) - 40.0 * scale
        d = np.sqrt(((xs - cx) / (430.0 * scale)) ** 2 + ((ys - cy) / (200.0 * scale)) ** 2)
        behind = smoothstep(1.0, 0.25, d) * 0.97
        top = np.clip(1.0 - ys / (self.h * 0.45), 0.0, 1.0) * 0.6
        vx, vy = (xs - self.w / 2) / (self.w / 2), (ys - self.h * 0.45) / (self.h * 0.55)
        v = np.sqrt(vx ** 2 + vy ** 2) / np.sqrt(2)
        vignette = np.clip((v - 0.3) / 0.7, 0.0, 1.0) * 0.55
        self.fog_keep = ((1.0 - behind) * (1.0 - top) * (1.0 - vignette))[..., None]

    def frame(self, seconds):
        a = fog(self.fog_uv[0], self.fog_uv[1], seconds)
        a = np.asarray(Image.fromarray(a.astype(np.float32)).resize((self.w, self.h), Image.BILINEAR), np.float32)
        rgb = (np.float32(0.894) * a)[..., None] * np.array([1.0, 1.0, 0.886 / 0.894], np.float32)
        rgb = rgb * self.fog_keep
        light = flame(self.el_uv[0], self.el_uv[1], seconds, self.flame_map)
        rgb[self.ey:self.ey + self.eh, self.ex:self.ex + self.ew] += light
        lh, lw = self.letters.shape[:2]
        box = rgb[self.ly:self.ly + lh, self.lx:self.lx + lw]
        rgb[self.ly:self.ly + lh, self.lx:self.lx + lw] = self.letters + box * (1.0 - self.letters_a)
        return np.clip(rgb, 0.0, 1.0)


# ---- a GIF's colours (--out <name>.gif) ----

GREYS = 128     # the smoke's ramp; the rest of the 255 colours are the flame's and the letters' (256th: "as before")
FOG_TINT = np.array([1.0, 1.0, 0.886 / 0.894], np.float32)
# An 8 x 8 ordered dither, the same at every frame: smooth smoke shows no bands, and a pixel that doesn't change keeps
# its colour from frame to frame (Floyd-Steinberg's would crawl, and pack far worse).
_B2 = np.array([[0, 2], [3, 1]])
_B4 = np.block([[4 * _B2, 4 * _B2 + 2], [4 * _B2 + 3, 4 * _B2 + 1]])
BAYER8 = (np.block([[4 * _B4, 4 * _B4 + 2], [4 * _B4 + 3, 4 * _B4 + 1]]).astype(np.float32) + 0.5) / 64.0 - 0.5


def palette_for(frames):
    """255 colours and the reserved 256th: a fine ramp of the smoke's grey, then a median cut of the rest (the flame's
    pale blue, the letters) from a few of the frames."""
    top = max(float(np.asarray(f, np.float32)[..., 0].max()) for f in frames[:: max(1, len(frames) // 8)]) / 255.0
    ramp = np.linspace(0.0, min(top, 0.6), GREYS, dtype=np.float32)[:, None] * FOG_TINT
    rest = []
    for f in frames[:: max(1, len(frames) // 6)]:
        a = np.asarray(f, np.float32) / 255.0
        grey = (np.abs(a[..., 0] - a[..., 1]) < 2.5 / 255.0) & (a[..., 0] <= ramp[-1, 0] + 1.0 / 255.0)
        rest.append(a[~grey])
    rest = np.concatenate(rest)
    n = 255 - GREYS
    if len(rest) < n:
        cut = np.zeros((n, 3), np.float32)
    else:
        side = int(np.ceil(np.sqrt(len(rest))))
        pad = np.zeros((side * side, 3), np.float32)
        pad[: len(rest)] = rest
        pad[len(rest):] = rest[0]
        im = Image.fromarray((pad.reshape(side, side, 3) * 255.0 + 0.5).astype(np.uint8), "RGB")
        q = im.quantize(colors=n, method=Image.Quantize.MEDIANCUT)
        cut = np.array(q.getpalette()[: n * 3], np.float32).reshape(-1, 3) / 255.0
        if len(cut) < n:
            cut = np.concatenate([cut, np.zeros((n - len(cut), 3), np.float32)])
    return np.concatenate([ramp, cut, np.zeros((1, 3), np.float32)])


def to_palette(frame, colours):
    """Each pixel's nearest of the first 255 colours, after the ordered dither (one grey step's worth)."""
    a = np.asarray(frame, np.float32) / 255.0
    h, w = a.shape[:2]
    step = colours[1, 0] - colours[0, 0]
    d = np.tile(BAYER8, (h // 8 + 1, w // 8 + 1))[:h, :w, None] * step
    flat = (a + d).reshape(-1, 3)
    pal = colours[:255]
    idx = np.empty(len(flat), np.uint8)
    p2 = (pal * pal).sum(axis=1)
    for s in range(0, len(flat), 65536):
        c = flat[s:s + 65536]
        dist = p2[None, :] - 2.0 * (c @ pal.T)
        idx[s:s + 65536] = np.argmin(dist, axis=1)
    return idx.reshape(h, w)


def write_gif(frames, out, fps, keep_near):
    colours = palette_for(frames)
    pal = [int(round(v * 255)) for v in colours.reshape(-1)]
    # After the first frame, a pixel whose colour is within keep_near of what is shown there already is left clear
    # (KEEP): the black, the letters and the slow smoke stay as they were, and runs of one index pack small. What is
    # shown never strays further than that from the frame's own colour there.
    KEEP, KEEP_NEAR = 255, keep_near / 255.0
    pics, shown = [], None
    for k, f in enumerate(frames):
        idx = to_palette(f, colours)
        if shown is None:
            shown = colours[idx]
        else:
            keep = np.abs(colours[idx] - shown).max(axis=2) <= KEEP_NEAR
            shown = np.where(keep[..., None], shown, colours[idx])
            idx = np.where(keep, KEEP, idx).astype(np.uint8)
        im = Image.fromarray(idx, "P")
        im.putpalette(pal)
        if k:
            im.info["transparency"] = KEEP
        pics.append(im)
    pics[0].save(out, save_all=True, append_images=pics[1:], duration=round(1000 / fps), loop=0, optimize=False,
                 disposal=1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--fps", type=float, default=15.0)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--quality", type=int, default=82, help="the WebP's quality, 0 to 100")
    ap.add_argument("--keep", type=float, default=4.0,
                    help="GIF only: a pixel within this much (of 255) stays as it was")
    ap.add_argument("--out", type=pathlib.Path, default=OUT)
    args = ap.parse_args()

    title = Title(args.scale)
    count = round(args.seconds * args.fps)
    period = count / args.fps
    frames = []
    for i in range(count):
        t = i / args.fps
        rgb = title.frame(t)
        # The loop: the last LOOP_BLEND seconds fade into the ones just before the first, so the last frame leads into
        # the first.
        into = (t - (period - LOOP_BLEND)) / LOOP_BLEND
        if into > 0.0:
            w = into * into * (3.0 - 2.0 * into)
            rgb = rgb * (1.0 - w) + title.frame(t - period) * w
        frames.append(Image.fromarray((rgb * 255.0 + 0.5).astype(np.uint8), "RGB"))
        print(f"\rframe {i + 1} of {count}", end="", file=sys.stderr, flush=True)
    print(file=sys.stderr)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.out.suffix.lower() == ".gif":
        write_gif(frames, args.out, args.fps, args.keep)
    else:
        frames[0].save(args.out, "WEBP", save_all=True, append_images=frames[1:], duration=round(1000 / args.fps),
                       loop=0, quality=args.quality, method=6)
    print(f"{args.out}: {title.w} x {title.h}, {count} frames, {args.out.stat().st_size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
