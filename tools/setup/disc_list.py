"""The list of a disc's files that setup checks a copy against (des_setup/disc.py), made from the disc's IRD.

  python -I tools/setup/disc_list.py <disc.ird> <out.txt.gz>

An IRD is the record of a PS3 disc that dump tools check a dump with: its ISO header (every folder and file, with
sizes) and an MD5 per file. The list keeps every file under PS3_GAME with its size, grouped by folder, in a gzip
written the same way each time (no time or name in its header), so a list made again from the same IRD has the same
bytes. It leaves out:
  - PS3_DISC.SFB and PS3_UPDATE/ (the disc's system update): the game never reads them, and many dumps lack them;
  - a name that ends in a dot (map/m99_99_98_06/m99_99_98_06mcp.): Windows cannot write one, so unpackers rename it,
    each its own way. It is left out in each spelling a copy may have (DOTTED), so a list made from a copy's folder
    rather than an IRD leaves it out too (issue #499).

The IRD format: a gzip stream holding "3IRD", a version byte, the title ID (9 bytes), the title (a .NET 7-bit length
and UTF-8), the system version (4), disc version (5) and app version (5), on version 7 an ID (4), then the ISO header
(int32 length and a gzip of the disc's first sectors: volume descriptors, path tables and every folder), the footer
(int32 and a gzip), the region hashes (a byte count and 16 bytes each) and the file hashes (int32 count; per file an
int64 start sector and a 16-byte MD5). The names come from the header's Joliet tree (full-length UCS-2 names) when it
has one, else from its primary tree.
"""
import gzip
import io
import struct
import sys

SECTOR = 2048
KEEP = "PS3_GAME/"
# The names on the supported discs that end in a dot (the US and European IRDs; the Japanese and Asian discs have the
# same file). It is a stray 56-byte copy of m99_99_98_06.mcp, which is the file the game opens. A copy names it as its
# unpacker wrote it: with the dot (Linux), with "_" for the dot (7-Zip), or without it (other Windows tools).
DOTTED = ("PS3_GAME/USRDIR/map/m99_99_98_06/m99_99_98_06mcp.",)


def spellings(path: str) -> tuple:
    """The names a copy may give a disc file whose name ends in a dot."""
    return path, path[:-1] + "_", path[:-1]


def left_out(path: str) -> bool:
    """Whether the list leaves out `path`, a file as the IRD or a copy's folder names it (the module's docstring)."""
    return not path.startswith(KEEP) or path.endswith(".") or any(path in spellings(d) for d in DOTTED)


def _net_string(f) -> str:
    n, shift = 0, 0
    while True:
        b = f.read(1)[0]
        n |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            break
    return f.read(n).decode("utf-8", "replace")


def parse_ird(data: bytes):
    """(title ID, title, app version, ISO header) of an IRD's bytes."""
    f = io.BytesIO(gzip.decompress(data))
    if f.read(4) != b"3IRD":
        raise ValueError("not an IRD file")
    ver = f.read(1)[0]
    title_id = f.read(9).decode("ascii")
    title = _net_string(f)
    f.read(4 + 5)                      # the system and disc versions
    app = f.read(5).decode("ascii", "replace")
    if ver == 7:
        f.read(4)
    (n,) = struct.unpack("<i", f.read(4))
    return title_id, title, app, gzip.decompress(f.read(n))


def _record(r):
    """(start sector, size, flags, name bytes) of an ISO 9660 directory record."""
    return struct.unpack("<I", r[2:6])[0], struct.unpack("<I", r[10:14])[0], r[25], r[33:33 + r[32]]


def files_of(header: bytes, joliet=True):
    """[(path, size)] of every file in the ISO header's tree (Joliet's, or the primary one), or None without that
    tree."""
    root = None
    for s in range(16, 64):
        vd = header[s * SECTOR:(s + 1) * SECTOR]
        if len(vd) < SECTOR or vd[1:6] != b"CD001" or vd[0] == 255:
            break
        is_joliet = vd[0] == 2 and vd[88:91] in (b"%/@", b"%/C", b"%/E")
        if (joliet and is_joliet) or (not joliet and vd[0] == 1):
            root = vd[156:156 + 34]
            break
    if root is None:
        return None
    enc = "utf-16-be" if joliet else "latin-1"
    out = []

    def visit(lba, size, prefix):
        data = header[lba * SECTOR:lba * SECTOR + size]
        if len(data) < size:
            raise ValueError(f"the folder at sector {lba} is past the end of the IRD's header")
        pos = 0
        while pos < len(data):
            ln = data[pos]
            if ln == 0:                # a record never crosses a sector: the rest of this one is empty
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            clba, csize, flags, name = _record(data[pos:pos + ln])
            pos += ln
            if name in (b"\x00", b"\x01"):     # "." and ".."
                continue
            nm = name.decode(enc, "replace")
            if not joliet or nm.endswith(";1"):
                nm = nm.split(";")[0]
            if flags & 0x02:
                visit(clba, csize, prefix + nm + "/")
            elif flags & 0x80:
                raise ValueError(f"{prefix + nm} has more than one extent, which this tool does not read")
            else:
                out.append((prefix + nm, csize))

    lba, size, _, _ = _record(root)
    visit(lba, size, "")
    return out


def disc_list(title_id, title, app, files) -> bytes:
    """The list's text: a comment, then each folder (a line ending in /) and its files (name, tab, size)."""
    kept = sorted((p, n) for p, n in files if not left_out(p))
    lines = [f"# {title_id} {app}: {title}. {len(kept)} files under PS3_GAME, with their sizes, from the disc's IRD",
             "# (tools/setup/disc_list.py). A line that ends in / is a folder; the lines after it are its files."]
    folder = None
    for p, n in kept:
        d, name = p.rsplit("/", 1)
        if d != folder:
            folder = d
            lines.append(d + "/")
        lines.append(f"{name}\t{n}")
    return ("\n".join(lines) + "\n").encode("utf-8")


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__.split("\n\n")[1].strip())
        return 2
    with open(argv[1], "rb") as f:
        title_id, title, app, header = parse_ird(f.read())
    files = files_of(header, True)
    if files is None:
        files = files_of(header, False)
    text = disc_list(title_id, title, app, files)
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0, filename="") as z:
        z.write(text)
    with open(argv[2], "wb") as f:
        f.write(buf.getvalue())
    print(f"{title_id} {app}: {text.count(chr(9).encode())} files, {len(buf.getvalue())} bytes in {argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
