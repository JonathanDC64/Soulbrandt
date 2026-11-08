"""The update a disc needs before the port can use it (the Japanese and Asian discs' 1.04, docs/139).

Those two discs are an earlier build of the game than the US and European discs. Their 1.04 update brings the program
and its data up to the US disc's, so setup asks for it. It takes the update as either of the forms a player has it in:

  - the update's .pkg file, as Sony's servers give it;
  - the folder a PS3 or RPCS3 installed it to (dev_hdd0/game/<TITLE_ID>, with PARAM.SFO and USRDIR/).

Either way its files end up in <data>/update/<TITLE_ID>, the layout of the installed folder. A later rebuild needs
nothing from the player. The game reads them there in place of the disc's copies (PS3_VFS_UPDATE, ps3recomp's
vfs_path.h). The game table has every file's size and SHA-256 (the "update" of the disc's entry), so what is there is
exactly the update the port was made for.

A package is a retail PS3 package: a header, then a data area that is AES-128-CTR under the public PS3 package key,
with the header's IV as the counter. In the data area, a table of 32-byte entries (name offset, name size, data offset,
data size, flags) and the names and files. Setup needs only the Python standard library (docs/88), so the cipher is
written here; about a minute for the 48 MB, once.
"""
import hashlib
import os
import shutil
import struct
from pathlib import Path

from . import sfo

PKG_KEY = bytes.fromhex("2E7B71D7C9C9A14EA3221F188828B8F8")
PKG_MAGIC = 0x7F504B47
RETAIL = 0x8000
FOLDER = "update"        # <data>/update/<TITLE_ID>
DIRECTORY = 4            # an item's flags (low byte): a folder
CHUNK = 1 << 20          # the keystream's unit, between progress calls


class UpdateError(ValueError):
    """Not the update it should be. The message is the sentence for the player."""


# ---- AES-128, counter mode ----

_SBOX = []


def _tables():
    """The S-box and the four encryption tables, built once."""
    if _SBOX:
        return _SBOX[0]
    p, q, sbox = 1, 1, [0] * 256
    while True:                                 # (p runs over the field; q is its inverse)
        p = p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)
        q ^= q << 1
        q ^= q << 2
        q ^= q << 4
        q &= 0xFF
        if q & 0x80:
            q ^= 0x09
        x = q ^ (q << 1 | q >> 7) & 0xFF ^ (q << 2 | q >> 6) & 0xFF ^ (q << 3 | q >> 5) & 0xFF ^ (q << 4 | q >> 4) & 0xFF
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break
    sbox[0] = 0x63
    t0, t1, t2, t3 = [], [], [], []
    for x in sbox:
        x2 = ((x << 1) ^ (0x1B if x & 0x80 else 0)) & 0xFF
        x3 = x2 ^ x
        w = x2 << 24 | x << 16 | x << 8 | x3
        t0.append(w)
        t1.append((w >> 8 | w << 24) & 0xFFFFFFFF)
        t2.append((w >> 16 | w << 16) & 0xFFFFFFFF)
        t3.append((w >> 24 | w << 8) & 0xFFFFFFFF)
    _SBOX.append((sbox, t0, t1, t2, t3))
    return _SBOX[0]


def _expand(key: bytes) -> list:
    """The 44 round-key words of an AES-128 key."""
    sbox = _tables()[0]
    w = list(struct.unpack(">4I", key))
    rcon = 1
    for i in range(4, 44):
        t = w[i - 1]
        if i % 4 == 0:
            t = (t << 8 | t >> 24) & 0xFFFFFFFF
            t = sbox[t >> 24] << 24 | sbox[t >> 16 & 255] << 16 | sbox[t >> 8 & 255] << 8 | sbox[t & 255]
            t ^= rcon << 24
            rcon = ((rcon << 1) ^ (0x11B if rcon & 0x80 else 0)) & 0xFF
        w.append(w[i - 4] ^ t)
    return w


def aes128_ctr(key: bytes, iv: bytes, data: bytes, progress=None) -> bytes:
    """`data` en- or decrypted, AES-128-CTR. progress(bytes): called between chunks."""
    sbox, t0, t1, t2, t3 = _tables()
    rk = _expand(key)
    ctr = int.from_bytes(iv, "big")
    r0, r1, r2, r3 = rk[0:4]
    middle = [tuple(rk[i:i + 4]) for i in range(4, 40, 4)]
    last = rk[40:44]
    out = bytearray(len(data))
    pack = struct.Struct(">4I").pack_into
    mask = (1 << 128) - 1
    size = len(data)
    pos = 0
    while pos < size:
        n = min(CHUNK, size - pos)
        stream = bytearray((n + 15) // 16 * 16)
        for off in range(0, len(stream), 16):
            s0 = (ctr >> 96) ^ r0
            s1 = (ctr >> 64 & 0xFFFFFFFF) ^ r1
            s2 = (ctr >> 32 & 0xFFFFFFFF) ^ r2
            s3 = (ctr & 0xFFFFFFFF) ^ r3
            for k0, k1, k2, k3 in middle:
                s0, s1, s2, s3 = (
                    t0[s0 >> 24] ^ t1[s1 >> 16 & 255] ^ t2[s2 >> 8 & 255] ^ t3[s3 & 255] ^ k0,
                    t0[s1 >> 24] ^ t1[s2 >> 16 & 255] ^ t2[s3 >> 8 & 255] ^ t3[s0 & 255] ^ k1,
                    t0[s2 >> 24] ^ t1[s3 >> 16 & 255] ^ t2[s0 >> 8 & 255] ^ t3[s1 & 255] ^ k2,
                    t0[s3 >> 24] ^ t1[s0 >> 16 & 255] ^ t2[s1 >> 8 & 255] ^ t3[s2 & 255] ^ k3)
            pack(stream, off,
                 (sbox[s0 >> 24] << 24 | sbox[s1 >> 16 & 255] << 16 | sbox[s2 >> 8 & 255] << 8 | sbox[s3 & 255]) ^ last[0],
                 (sbox[s1 >> 24] << 24 | sbox[s2 >> 16 & 255] << 16 | sbox[s3 >> 8 & 255] << 8 | sbox[s0 & 255]) ^ last[1],
                 (sbox[s2 >> 24] << 24 | sbox[s3 >> 16 & 255] << 16 | sbox[s0 >> 8 & 255] << 8 | sbox[s1 & 255]) ^ last[2],
                 (sbox[s3 >> 24] << 24 | sbox[s0 >> 16 & 255] << 16 | sbox[s1 >> 8 & 255] << 8 | sbox[s2 & 255]) ^ last[3])
            ctr = (ctr + 1) & mask
        x = int.from_bytes(data[pos:pos + n], "big") ^ int.from_bytes(stream[:n], "big")
        out[pos:pos + n] = x.to_bytes(n, "big")
        pos += n
        if progress:
            progress(n)
    return bytes(out)


# ---- a package ----

def pkg_items(pkg_bytes: bytes, progress=None) -> list:
    """[(name, data or None for a folder)] of a retail package. Raises UpdateError."""
    if len(pkg_bytes) < 0xC0:
        raise UpdateError("This file is too short to be a PS3 update package.")
    magic, rev, _typ, _mo, _mc, _ms, count = struct.unpack(">IHHIIII", pkg_bytes[:0x18])
    if magic != PKG_MAGIC or rev != RETAIL:
        raise UpdateError("This file is not a PS3 update package.")
    _total, off, size = struct.unpack(">QQQ", pkg_bytes[0x18:0x30])
    if off + size > len(pkg_bytes) or count > 1 << 20:
        raise UpdateError("This PS3 update package is cut short.")
    data = aes128_ctr(PKG_KEY, pkg_bytes[0x70:0x80], pkg_bytes[off:off + size], progress)
    items = []
    for i in range(count):
        no, ns, fo, fs, flags, _ = struct.unpack(">IIQQII", data[i * 32:i * 32 + 32])
        name = data[no:no + ns].decode("utf-8", "replace")
        items.append((name, None if flags & 0xFF == DIRECTORY else data[fo:fo + fs]))
    return items


def _inside(root: Path, name: str) -> Path:
    """`name` under root; a name that would leave it is refused."""
    parts = name.replace("\\", "/").split("/")
    if not name or any(p in ("", ".", "..") or ":" in p for p in parts):
        raise UpdateError("This PS3 update package has a file name setup will not write.")
    return root.joinpath(*parts)


def unpack(pkg: Path, dest: Path, progress=None) -> int:
    """A package's files into dest (a new folder). Returns how many. Raises UpdateError."""
    items = pkg_items(Path(pkg).read_bytes(), progress)
    n = 0
    for name, body in items:
        p = _inside(dest, name)
        if body is None:
            p.mkdir(parents=True, exist_ok=True)
            continue
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(body)
        n += 1
    return n


# ---- finding it ----

def sha256(path: Path, block=1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(block)
            if not b:
                return h.hexdigest()
            h.update(b)


def _is_pkg(path: Path, want: dict) -> bool:
    """The package with the table's size and sum."""
    try:
        return path.stat().st_size == want["size"] and sha256(path) == want["sha256"]
    except OSError:
        return False


def _installed(folder: Path, title: str, want: dict) -> bool:
    """A folder holding the installed update: its PARAM.SFO has this title at the update's version."""
    try:
        vals = sfo.parse((folder / "PARAM.SFO").read_bytes())
    except (OSError, sfo.SfoError):
        return False
    return vals.get("TITLE_ID") == title and vals.get("APP_VER") == want["app_ver"] and (folder / "USRDIR").is_dir()


def find_in(place: Path, title: str, want: dict):
    """The update in `place`: a .pkg file, the installed folder itself, a folder of it named for the title, or a .pkg
    among the folder's files. ("pkg" or "folder", its path), or None."""
    place = Path(place)
    if place.is_file():
        return ("pkg", place) if _is_pkg(place, want["pkg"]) else None
    if not place.is_dir():
        return None
    for folder in (place, place / title):
        if _installed(folder, title, want):
            return "folder", folder
    try:
        names = sorted(n for n in os.listdir(place) if n.lower().endswith(".pkg"))
    except OSError:
        return None
    for n in names:
        if _is_pkg(place / n, want["pkg"]):
            return "pkg", place / n
    return None


def places(game_root: Path, given, hdd0=None) -> list:
    """Where to look, in order: what the player gave; RPCS3's dev_hdd0/game (the RPCS3 route); the game folder and the
    two folders above it, where a package is often kept beside the dump."""
    out = [Path(given)] if given else []
    if hdd0:
        out.append(Path(hdd0) / "game")
    if game_root:
        root = Path(game_root)
        out += [root, root.parent, root.parent.parent]
    return out


def bad_files(folder: Path, want: dict, progress=None) -> list:
    """The update's files in `folder` that are missing or not the table's: their paths."""
    bad = []
    for e in want["files"]:
        p = folder.joinpath(*e["path"].split("/"))
        try:
            if p.stat().st_size != e["size"] or sha256(p) != e["sha256"]:
                bad.append(e["path"])
        except OSError:
            bad.append(e["path"])
        if progress:
            progress(e["size"])
    return bad


def install(found, dest: Path, want: dict, progress=None):
    """The update `found` (find_in's) into dest, checked file by file. A folder replaced only when it is whole, so a
    run stopped here starts again. Raises UpdateError."""
    kind, src = found
    tmp = dest.with_name(dest.name + ".tmp")
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    try:
        if kind == "pkg":
            unpack(src, tmp, progress)
        else:
            for e in want["files"]:
                s = Path(src).joinpath(*e["path"].split("/"))
                d = tmp.joinpath(*e["path"].split("/"))
                d.parent.mkdir(parents=True, exist_ok=True)
                try:
                    shutil.copyfile(s, d)
                except OSError as err:
                    raise UpdateError(f"{e['path']} could not be copied from the update's folder ({err}).")
        wrong = bad_files(tmp, want)
        if wrong:
            raise UpdateError(f"{len(wrong)} of the update's files do not match, the first {wrong[0]}. "
                              "Get the update again.")
        if dest.exists():
            shutil.rmtree(dest)
        os.replace(tmp, dest)
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
