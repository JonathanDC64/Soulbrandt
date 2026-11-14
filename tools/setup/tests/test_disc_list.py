"""Tests for the tool that makes the disc lists setup checks a copy against (tools/setup/disc_list.py, from a disc's
IRD; des_setup/disc.py reads them). On a made-up ISO header: no game data.

  cd tools/setup && python -m unittest discover -s tests
"""
import gzip
import io
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import disc_list  # noqa: E402

SECTOR = 2048


# ---- a made-up ISO header, as an IRD holds one ----

def _record(name: bytes, lba, size, is_dir) -> bytes:
    n = len(name)
    r = bytearray(33 + n + (0 if n % 2 else 1))     # (padded to an even length)
    r[0] = len(r)
    r[2:10] = struct.pack("<I", lba) + struct.pack(">I", lba)
    r[10:18] = struct.pack("<I", size) + struct.pack(">I", size)
    r[25] = 2 if is_dir else 0
    r[32] = n
    r[33:33 + n] = name
    return bytes(r)


def iso_header(tree) -> bytes:
    """The first sectors of a disc with `tree` ({name: size, or name: {...} for a folder}) as its Joliet tree: the
    volume descriptors at sector 16 on (primary, Joliet, end) and one or more sectors for each folder."""
    sectors = {}
    next_free = [20]

    def place(folder) -> tuple:
        """(start sector, size) of a folder, its sub-folders placed first."""
        kids = []
        for name, v in sorted(folder.items()):
            if isinstance(v, dict):
                kids.append((name, *place(v), True))
            else:
                kids.append((name + ";1", 900, v, False))
        recs = [_record(b"\x00", 0, 0, True), _record(b"\x01", 0, 0, True)]
        recs += [_record(n.encode("utf-16-be"), lba, size, d) for n, lba, size, d in kids]
        data = bytearray()
        for r in recs:                              # a record never crosses a sector
            if len(data) // SECTOR != (len(data) + len(r) - 1) // SECTOR:
                data += bytes(SECTOR - len(data) % SECTOR)
            data += r
        data += bytes(-len(data) % SECTOR)
        start = next_free[0]
        next_free[0] += len(data) // SECTOR
        sectors[start] = bytes(data)
        return start, len(data)

    root_lba, root_size = place(tree)
    out = bytearray(next_free[0] * SECTOR)
    root = _record(b"\x00", root_lba, root_size, True)
    for s, kind, esc in ((16, 1, b""), (17, 2, b"%/E")):
        vd = bytearray(SECTOR)
        vd[0], vd[1:6] = kind, b"CD001"
        vd[88:88 + len(esc)] = esc
        vd[156:156 + 34] = root
        out[s * SECTOR:(s + 1) * SECTOR] = vd
    out[18 * SECTOR:18 * SECTOR + 6] = b"\xffCD001"
    for start, data in sectors.items():
        out[start * SECTOR:start * SECTOR + len(data)] = data
    return bytes(out)


def ird(title_id, title, app, header) -> bytes:
    """An IRD (version 9) holding `header`: the parts disc_list reads, then an empty footer and no hashes."""
    t = title.encode("utf-8")
    body = b"3IRD" + bytes([9]) + title_id.encode("ascii") + bytes([len(t)]) + t + b"4.21" + b"01.00" + \
        app.encode("ascii")
    z = gzip.compress(header)
    body += struct.pack("<i", len(z)) + z + struct.pack("<i", 0) + bytes([0]) + struct.pack("<i", 0)
    return gzip.compress(body)


class DiscListTest(unittest.TestCase):
    TREE = {"PS3_DISC.SFB": 5, "PS3_UPDATE": {"PS3UPDAT.PUP": 99},
            "PS3_GAME": {"PARAM.SFO": 1008, "ICON0.PNG": 12,
                         "USRDIR": {"EBOOT.BIN": 4096, "s[magic circle].mtd": 7, "odd.": 3,
                                    "many": {f"file_with_a_long_name_{i:03}.bin": i for i in range(60)}}}}

    def test_files_of_reads_the_joliet_tree(self):
        files = dict(disc_list.files_of(iso_header(self.TREE)))
        self.assertEqual(files["PS3_GAME/USRDIR/EBOOT.BIN"], 4096)
        self.assertEqual(files["PS3_GAME/USRDIR/s[magic circle].mtd"], 7)
        self.assertEqual(files["PS3_UPDATE/PS3UPDAT.PUP"], 99)
        # 60 records do not fit one sector: the second sector's are read too
        self.assertEqual(files["PS3_GAME/USRDIR/many/file_with_a_long_name_059.bin"], 59)
        self.assertEqual(len(files), 5 + 60 + 2)

    def test_the_list_keeps_ps3_game_only(self):
        files = disc_list.files_of(iso_header(self.TREE))
        text = disc_list.disc_list("TEST00000", "A test disc", "01.00", files).decode("utf-8")
        self.assertNotIn("PS3UPDAT", text)
        self.assertNotIn("PS3_DISC.SFB", text)
        self.assertNotIn("odd.", text)               # (a name ending in a dot: Windows cannot write it)
        lines = text.splitlines()
        self.assertEqual(lines[2:5], ["PS3_GAME/", "ICON0.PNG\t12", "PARAM.SFO\t1008"])
        self.assertIn("s[magic circle].mtd\t7", lines)
        self.assertEqual(sum(1 for line in lines if "\t" in line), 64)

    def test_the_dotted_name_is_left_out_in_each_spelling(self):
        """Issue #499: a list made from a copy's folder had the name as the copy's unpacker wrote it
        (m99_99_98_06mcp_), so a copy whose unpacker wrote m99_99_98_06mcp was one file short. No spelling is listed;
        other names are, a final "_" and the file the game opens included."""
        folder = "PS3_GAME/USRDIR/map/m99_99_98_06/"
        files = [(folder + n, 56) for n in ("m99_99_98_06mcp.", "m99_99_98_06mcp_", "m99_99_98_06mcp",
                                             "m99_99_98_06.mcp")] + \
            [("PS3_GAME/USRDIR/other/m99_99_98_06mcp_", 1), ("PS3_GAME/USRDIR/name_", 2)]
        text = disc_list.disc_list("TEST00000", "A test disc", "01.00", files).decode("utf-8")
        self.assertEqual([line for line in text.splitlines() if not line.startswith("#")],
                         ["PS3_GAME/USRDIR/map/m99_99_98_06/", "m99_99_98_06.mcp\t56", "PS3_GAME/USRDIR/", "name_\t2",
                          "PS3_GAME/USRDIR/other/", "m99_99_98_06mcp_\t1"])

    def test_main_writes_the_same_bytes_each_time(self):
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "in.ird"
            src.write_bytes(ird("TEST00000", "A test disc", "01.00", iso_header(self.TREE)))
            outs = []
            for n in ("a.txt.gz", "b.txt.gz"):
                out = io.StringIO()
                stdout, sys.stdout = sys.stdout, out
                try:
                    self.assertEqual(disc_list.main(["disc_list.py", str(src), str(Path(d) / n)]), 0)
                finally:
                    sys.stdout = stdout
                outs.append((Path(d) / n).read_bytes())
            self.assertEqual(outs[0], outs[1])
            self.assertTrue(gzip.decompress(outs[0]).startswith(b"# TEST00000 01.00: A test disc. 64 files"))


if __name__ == "__main__":
    unittest.main()
