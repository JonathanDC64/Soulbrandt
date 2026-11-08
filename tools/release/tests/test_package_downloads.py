"""Tests for the check that no download holds a key (tools/release/package.py), on made-up keys.

  python -m unittest discover -s tools/release/tests
"""
import io
import re
import sys
import tarfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package as p  # noqa: E402

C_KEY = bytes(range(0x10, 0x20))     # made up
NAMED = bytes(range(0x30, 0x40))     # made up
HEX_KEY = bytes(range(0x50, 0x60))   # made up
LIST_KEY = bytes(range(0x70, 0x80))  # made up


def c_bytes(b):
    return ", ".join(f"0x{x:02X}" for x in b)


def fork(skip=()):
    """A `git archive` tar with the three key sources, written the way the real ones are."""
    texts = {
        "libs/filesystem/edat_decrypt.c": (
            f"static const uint8_t AES_SBOX[256] = {{ {c_bytes(bytes(range(16)))},\n 0x10 }};\n"
            f"static const uint8_t SDAT_KEY[16] = {{\n    {c_bytes(C_KEY)}}};\n"
            f'{{ "NP_FREE", {{ {c_bytes(NAMED[:8])},\n   {c_bytes(NAMED[8:])}}} }},\n').encode(),
        "tools/pkg_extract.py": f'PS3_GPKG_KEY = bytes.fromhex("{HEX_KEY.hex()}")\n'.encode(),
        "tools/extract_jobmods.py": f"EDAT_KEY_0 = bytes([{c_bytes(LIST_KEY)}])\ncrypt_key = bytes(a ^ b for a, b in z)\n".encode(),
    }
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w") as tar:
        for name, data in texts.items():
            if name in skip:
                continue
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return out.getvalue()


class Keys(unittest.TestCase):
    def test_found_by_name_and_only_keys(self):
        self.assertEqual(sorted(p.fork_keys(fork())),
                         sorted([("SDAT_KEY", C_KEY), ("NP_FREE", NAMED), ("PS3_GPKG_KEY", HEX_KEY), ("EDAT_KEY_0", LIST_KEY)]))

    def test_a_moved_source_stops_the_packaging(self):
        with self.assertRaisesRegex(p.Error, "pkg_extract"):
            p.fork_keys(fork(skip=("tools/pkg_extract.py",)))


class Check(unittest.TestCase):
    keys = [("SDAT_KEY", C_KEY)]

    def bad(self, data):
        with self.assertRaises(p.Error) as e:
            p.check_no_keys("a.zip", [("x/y", 0o644, data)], self.keys)
        self.assertIn("x/y holds SDAT_KEY", str(e.exception))
        self.assertNotIn(C_KEY.hex(), str(e.exception).lower())

    def test_raw(self):
        self.bad(b"exe" + C_KEY + b"more")

    def test_c_table_over_lines(self):
        self.bad(f"{{ {c_bytes(C_KEY[:8])},\r\n  {c_bytes(C_KEY[8:])} }}".encode())

    def test_hex_text(self):
        self.bad(C_KEY.hex().upper().encode())

    def test_clean(self):
        p.check_no_keys("a.zip", [("x/y", 0o644, b"nothing here " + C_KEY[:15])], self.keys)

    def test_the_sources_are_left_out(self):
        for source in p.KEY_SOURCES:
            self.assertTrue(any(rx.search(source) for rx in p.PS3RECOMP_SKIP), source)
        self.assertFalse(any(rx.search("libs/filesystem/edat.c") for rx in p.PS3RECOMP_SKIP))


if __name__ == "__main__":
    unittest.main()
