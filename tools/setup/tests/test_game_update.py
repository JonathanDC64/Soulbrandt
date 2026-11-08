"""Tests for a disc's update (des_setup/game_update.py) and its place in the check, on made-up files only.

  cd tools/setup && python -m unittest discover -s tests -p "test_game_update.py"
"""
import hashlib
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from des_setup import game_update, gamefiles, sfo  # noqa: E402

TITLE = "BCJS30022"
EBOOT_U = b"SCE\0" + b"u" * 60
EBOOT_ELF = b"\x7fELF" + b"v" * 60
SDAT_D = b"NPD\0" + b"d" * 28   # a file the update does not change
SDAT_DEC = b"DCX\0" + b"w" * 20
BONUS = b"only in the update"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def make_pkg(files, iv=b"\x11" * 16):
    """A retail package of {name: bytes, or None for a folder}, as Sony's servers make them."""
    names = list(files)
    table_size = 32 * len(names)
    blob = bytearray(table_size)
    entries = []
    for name in names:
        n = name.encode("utf-8")
        no = len(blob)
        blob += n + bytes(-len(n) % 16)
        body = files[name]
        fo = len(blob)
        if body is not None:
            blob += body + bytes(-len(body) % 16)
        entries.append((no, len(n), fo, len(body or b""), 4 if body is None else 3))
    for i, (no, ns, fo, fs, fl) in enumerate(entries):
        blob[i * 32:(i + 1) * 32] = struct.pack(">IIQQII", no, ns, fo, fs, fl, 0)
    data = game_update.aes128_ctr(game_update.PKG_KEY, iv, bytes(blob))
    off = 0xC0
    head = bytearray(off)
    struct.pack_into(">IHHIIII", head, 0, game_update.PKG_MAGIC, game_update.RETAIL, 1, 0xC0, 0, 0, len(names))
    struct.pack_into(">QQQ", head, 0x18, off + len(data), off, len(data))
    head[0x70:0x80] = iv
    return bytes(head) + data


class AesTest(unittest.TestCase):
    def test_fips_block(self):
        got = game_update.aes128_ctr(bytes(range(16)), bytes.fromhex("00112233445566778899aabbccddeeff"), bytes(16))
        self.assertEqual(got.hex(), "69c4e0d86a7b0430d8cdb78070b4c55a")

    def test_sp800_38a_ctr(self):
        k = bytes.fromhex("2b7e151628aed2a6abf7158809cf4f3c")
        iv = bytes.fromhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff")
        pt = bytes.fromhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51")
        ct = "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
        self.assertEqual(game_update.aes128_ctr(k, iv, pt).hex(), ct)

    def test_counter_carries_across_blocks_and_chunks(self):
        k, iv = bytes(16), b"\xff" * 15 + b"\xfe"   # (the counter wraps inside the data)
        data = b"x" * (game_update.CHUNK + 40)
        seen = []
        out = game_update.aes128_ctr(k, iv, data, seen.append)
        self.assertEqual(sum(seen), len(data))
        self.assertEqual(game_update.aes128_ctr(k, iv, out), data)
        self.assertNotEqual(out[:16], out[16:32])


class Update(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.sfo_u = sfo.build({"TITLE_ID": TITLE, "APP_VER": "01.04", "VERSION": "01.03", "CATEGORY": "GD"})
        self.files = {"USRDIR": None, "PARAM.SFO": self.sfo_u, "USRDIR/EBOOT.BIN": EBOOT_U, "USRDIR/msg": None,
                      "USRDIR/msg/bonus.bin": BONUS}
        self.pkg = make_pkg(self.files)
        self.want = {"app_ver": "01.04", "pkg": {"size": len(self.pkg), "sha256": sha(self.pkg)},
                     "files": [{"path": k, "size": len(v), "sha256": sha(v)} for k, v in self.files.items() if v is not None]}

    def put(self, name, body):
        p = self.dir / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(body)
        return p


class PackageTest(Update):
    def test_unpack(self):
        pkg = self.put("u.pkg", self.pkg)
        out = self.dir / "out"
        self.assertEqual(game_update.unpack(pkg, out), 3)
        self.assertEqual((out / "USRDIR" / "EBOOT.BIN").read_bytes(), EBOOT_U)
        self.assertEqual((out / "PARAM.SFO").read_bytes(), self.sfo_u)
        self.assertEqual((out / "USRDIR" / "msg" / "bonus.bin").read_bytes(), BONUS)

    def test_a_name_that_leaves_the_folder_is_refused(self):
        pkg = self.put("u.pkg", make_pkg({"../evil.bin": b"x"}))
        with self.assertRaises(game_update.UpdateError):
            game_update.unpack(pkg, self.dir / "out")
        self.assertFalse((self.dir / "evil.bin").exists())

    def test_not_a_package(self):
        with self.assertRaises(game_update.UpdateError):
            game_update.pkg_items(b"not a package" * 40)

    def test_install_from_the_package(self):
        pkg = self.put("u.pkg", self.pkg)
        dest = self.dir / "data" / "update" / TITLE
        game_update.install(("pkg", pkg), dest, self.want)
        self.assertEqual(game_update.bad_files(dest, self.want), [])
        self.assertFalse(dest.with_name(dest.name + ".tmp").exists())

    def test_install_refuses_files_that_differ(self):
        pkg = self.put("u.pkg", self.pkg)
        want = dict(self.want, files=[dict(self.want["files"][0], sha256="0" * 64)] + self.want["files"][1:])
        dest = self.dir / "dest"
        with self.assertRaises(game_update.UpdateError):
            game_update.install(("pkg", pkg), dest, want)
        self.assertFalse(dest.exists())
        self.assertFalse(dest.with_name(dest.name + ".tmp").exists())


class FindTest(Update):
    def installed(self, where):
        for name, body in self.files.items():
            if body is not None:
                self.put(f"{where}/{name}", body)
        return self.dir / where

    def test_the_package_itself(self):
        pkg = self.put("anywhere/name-does-not-matter.bin", self.pkg)
        self.assertEqual(game_update.find_in(pkg, TITLE, self.want), ("pkg", pkg))

    def test_a_package_in_a_folder(self):
        self.put("dl/other.pkg", b"not this one")
        pkg = self.put("dl/zz.pkg", self.pkg)
        self.assertEqual(game_update.find_in(self.dir / "dl", TITLE, self.want), ("pkg", pkg))

    def test_a_wrong_package_is_not_it(self):
        pkg = self.put("dl/u.pkg", self.pkg[:-1] + b"Z")
        self.assertIsNone(game_update.find_in(pkg, TITLE, self.want))
        self.assertIsNone(game_update.find_in(self.dir / "dl", TITLE, self.want))

    def test_the_installed_folder(self):
        f = self.installed("hdd/game/" + TITLE)
        self.assertEqual(game_update.find_in(f, TITLE, self.want), ("folder", f))

    def test_the_folder_above_it(self):
        f = self.installed("hdd/game/" + TITLE)
        self.assertEqual(game_update.find_in(self.dir / "hdd" / "game", TITLE, self.want), ("folder", f))

    def test_another_title_or_version_is_not_it(self):
        self.files["PARAM.SFO"] = sfo.build({"TITLE_ID": TITLE, "APP_VER": "01.03", "VERSION": "01.03", "CATEGORY": "GD"})
        f = self.installed("hdd/game/" + TITLE)
        self.assertIsNone(game_update.find_in(f, TITLE, self.want))

    def test_install_from_the_folder(self):
        f = self.installed("hdd/game/" + TITLE)
        dest = self.dir / "dest"
        game_update.install(("folder", f), dest, self.want)
        self.assertEqual(game_update.bad_files(dest, self.want), [])

    def test_places(self):
        got = game_update.places(Path("D:/roms/ps3/game"), "D:/u", "D:/hdd0/")
        self.assertEqual(got[:2], [Path("D:/u"), Path("D:/hdd0/game")])
        self.assertEqual(got[2:], [Path("D:/roms/ps3/game"), Path("D:/roms/ps3"), Path("D:/roms")])


class CheckTest(Update):
    def table(self):
        sfo_d = self.sfo_d = sfo.build({"TITLE_ID": TITLE, "APP_VER": "01.00", "VERSION": "01.00", "CATEGORY": "DG"})
        return {"title_id": "BLUS30443", "app_ver": "01.00", "version": "01.00", "disc": "the US disc",
                "other_titles": {}, "firmware": [],
                "originals": [{"path": "PS3_GAME/PARAM.SFO", "size": 1, "sha256": "0" * 64},
                              {"path": "PS3_GAME/USRDIR/EBOOT.BIN", "size": 1, "sha256": "0" * 64},
                              {"path": "PS3_GAME/USRDIR/d.sdat", "size": len(SDAT_D), "sha256": sha(SDAT_D)}],
                "decrypted": [{"path": "PS3_GAME/USRDIR/EBOOT.elf", "of": "PS3_GAME/USRDIR/EBOOT.BIN", "size": 1,
                               "sha256": "0" * 64, "magic": "7f454c46"},
                              {"path": "PS3_GAME/USRDIR/d.sdat.unedat", "of": "PS3_GAME/USRDIR/d.sdat",
                               "size": len(SDAT_DEC), "sha256": sha(SDAT_DEC), "magic": "44435800"}],
                "titles": {TITLE: {
                    "disc": "the Japanese disc", "app_ver": "01.00", "version": "01.00", "update": self.want,
                    "files": {"PS3_GAME/PARAM.SFO": {"size": len(sfo_d), "sha256": sha(sfo_d)},
                              "PS3_GAME/USRDIR/EBOOT.BIN": {"size": len(EBOOT_U), "sha256": sha(EBOOT_U), "from": "update"},
                              "PS3_GAME/USRDIR/EBOOT.elf": {"size": len(EBOOT_ELF), "sha256": sha(EBOOT_ELF)}}}}}

    def game(self):
        t = self.table()
        self.put("dump/PS3_GAME/PARAM.SFO", self.sfo_d)
        self.put("dump/PS3_GAME/USRDIR/d.sdat", SDAT_D)
        self.put("dump/PS3_GAME/USRDIR/d.sdat.unedat", SDAT_DEC)
        self.put("dump/PS3_GAME/USRDIR/EBOOT.elf", EBOOT_ELF)
        return t

    def unpacked(self):
        f = self.dir / "update"
        for name, body in self.files.items():
            if body is not None:
                self.put(f"update/{name}", body)
        return f

    def test_needs_the_update(self):
        t = self.game()
        rep = gamefiles.check(self.dir / "dump", table=t)
        self.assertFalse(rep.ok)
        item = next(i for i in rep.items if i.kind == "update")
        self.assertEqual(item.state, "missing")
        self.assertIn("the Japanese disc needs its 01.04 update", item.message)

    def test_with_the_update(self):
        t = self.game()
        rep = gamefiles.check(self.dir / "dump", table=t, update=self.unpacked())
        self.assertTrue(rep.ok, [i.message for i in rep.problems()])
        self.assertEqual(rep.title_id, TITLE)
        eboot = next(i for i in rep.items if i.name == "PS3_GAME/USRDIR/EBOOT.BIN")
        self.assertIn("update", eboot.found)

    def test_decrypted_eboot_beside_the_updates_original(self):
        t = self.game()
        (self.dir / "dump/PS3_GAME/USRDIR/EBOOT.elf").unlink()
        f = self.unpacked()
        self.put("update/USRDIR/EBOOT.elf", EBOOT_ELF)
        rep = gamefiles.check(self.dir / "dump", table=t, update=f)
        self.assertTrue(rep.ok, [i.message for i in rep.problems()])

    def test_a_changed_update_file(self):
        t = self.game()
        f = self.unpacked()
        (f / "USRDIR" / "msg" / "bonus.bin").write_bytes(b"changed")
        rep = gamefiles.check(self.dir / "dump", table=t, update=f)
        self.assertFalse(rep.ok)
        self.assertEqual(next(i for i in rep.items if i.kind == "update").state, "mismatch")


if __name__ == "__main__":
    unittest.main()
