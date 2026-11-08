"""Tests for a disc's update (des_setup/game_update.py) and its place in the check, on made-up files only.

  cd tools/setup && python -m unittest discover -s tests -p "test_game_update.py"
"""
import hashlib
import sys
import tempfile
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from des_setup import game_update, gamefiles, journal, pipeline, rpcs3, sfo  # noqa: E402

TITLE = "BCJS30022"
EBOOT_U = b"SCE\0" + b"u" * 60
EBOOT_ELF = b"\x7fELF" + b"v" * 60
SDAT_D = b"NPD\0" + b"d" * 28   # a file the update does not change
SDAT_DEC = b"DCX\0" + b"w" * 20
SDAT_U = b"NPD\0" + b"e" * 28   # a .sdat of the update, and what RPCS3 leaves when it installs it
SDAT_U_PLAIN = b"DCX\0" + b"p" * 20
BONUS = b"only in the update"

# A stand-in for `rpcs3 --headless --installpkg <file>`: its folder as RPCS3 finds it, the option's path checked, then
# what the mode beside it says. It ends with an error status when it did install, as RPCS3 does.
FAKE = r"""
import os, sys
mode = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "mode.txt")).read().strip()
if mode == "old":
    sys.stderr.write("Unknown option 'installpkg'.\n")
    sys.exit(1)
assert sys.argv[1:3] == ["--headless", "--installpkg"] and os.path.isabs(sys.argv[3]) and os.path.isfile(sys.argv[3])
home = os.environ.get("RPCS3_CONFIG_DIR") or os.path.join(os.environ["XDG_CONFIG_HOME"], "rpcs3")
if mode == "ok":
    game = os.path.join(home, "dev_hdd0", "game", "BCJS30022")
    os.makedirs(game)
    open(os.path.join(game, "PARAM.SFO"), "wb").close()
sys.exit(9)
"""


def sha(b):
    return hashlib.sha256(b).hexdigest()


class Update(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.sfo_u = sfo.build({"TITLE_ID": TITLE, "APP_VER": "01.04", "VERSION": "01.03", "CATEGORY": "GD"})
        self.files = {"USRDIR": None, "PARAM.SFO": self.sfo_u, "USRDIR/EBOOT.BIN": EBOOT_U, "USRDIR/msg": None,
                      "USRDIR/msg/bonus.bin": BONUS}
        self.pkg = b"as far as setup can tell, a package is its size and its sum"
        self.want = {"app_ver": "01.04", "pkg": {"size": len(self.pkg), "sha256": sha(self.pkg)},
                     "files": [{"path": k, "size": len(v), "sha256": sha(v)} for k, v in self.files.items() if v is not None]}

    def put(self, name, body):
        p = self.dir / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(body)
        return p


class NoKeyTest(unittest.TestCase):
    def test_setup_has_no_package_key_or_cipher(self):
        for name in ("PKG_KEY", "aes128_ctr", "pkg_items", "unpack"):
            self.assertFalse(hasattr(game_update, name), name)


class InstallPackageTest(Update):
    """rpcs3.install_package, with a stand-in for RPCS3."""

    def run_it(self, mode):
        self.put("fake/mode.txt", mode.encode())
        fake = self.put("fake/fake.py", FAKE.encode())
        pkg = self.put("u.pkg", self.pkg)
        return rpcs3.install_package("unused", pkg, TITLE, self.dir / "work", self.dir / "logs" / "update.log",
                                     launch=[sys.executable, "-I", str(fake)], stall=10.0, poll=0.02)

    def test_the_installed_folder_counts_whatever_the_exit_status(self):
        folder = self.run_it("ok")
        self.assertTrue((folder / "PARAM.SFO").is_file())
        self.assertEqual(folder.parts[-3:], ("dev_hdd0", "game", TITLE))
        self.assertTrue(str(folder).startswith(str((self.dir / "work" / "rpcs3").absolute())))
        self.assertIn("exit status 9", (self.dir / "logs" / "update.log").read_text(encoding="utf-8"))

    def test_nothing_installed(self):
        with self.assertRaises(game_update.UpdateError) as c:
            self.run_it("nothing")
        self.assertIn("RPCS3 could not install the update", str(c.exception))
        self.assertIn("dev_hdd0/game/" + TITLE, str(c.exception))

    def test_an_rpcs3_without_the_option(self):
        with self.assertRaises(game_update.UpdateError) as c:
            self.run_it("old")
        self.assertIn("too old", str(c.exception))


class InstallTest(Update):
    def installed(self, where):
        for name, body in self.files.items():
            if body is not None:
                self.put(f"{where}/{name}", body)
        return self.dir / where

    def test_install_from_the_folder(self):
        f = self.installed("hdd/game/" + TITLE)
        dest = self.dir / "data" / "update" / TITLE
        game_update.install(f, dest, self.want)
        self.assertEqual(game_update.bad_files(dest, self.want), [])
        self.assertFalse(dest.with_name(dest.name + ".tmp").exists())

    def test_install_refuses_files_that_differ(self):
        f = self.installed("hdd/game/" + TITLE)
        want = dict(self.want, files=[dict(self.want["files"][0], sha256="0" * 64)] + self.want["files"][1:])
        dest = self.dir / "dest"
        with self.assertRaises(game_update.UpdateError):
            game_update.install(f, dest, want)
        self.assertFalse(dest.exists())
        self.assertFalse(dest.with_name(dest.name + ".tmp").exists())

    def test_a_sdat_rpcs3_decrypted_is_good_with_its_plain_form_only(self):
        self.want["files"].append({"path": "USRDIR/u.sdat", "size": len(SDAT_U), "sha256": sha(SDAT_U)})
        plain = {"USRDIR/u.sdat": {"size": len(SDAT_U_PLAIN), "sha256": sha(SDAT_U_PLAIN)}}
        self.files["USRDIR/u.sdat"] = SDAT_U_PLAIN
        f = self.installed("hdd/game/" + TITLE)
        self.assertEqual(game_update.bad_files(f, self.want), ["USRDIR/u.sdat"])
        self.assertEqual(game_update.bad_files(f, self.want, plain=plain), [])
        dest = self.dir / "dest"
        game_update.install(f, dest, self.want, plain=plain)
        self.assertEqual((dest / "USRDIR" / "u.sdat").read_bytes(), SDAT_U_PLAIN)

    def test_a_plain_sdat_of_other_bytes_is_not_good(self):
        self.want["files"].append({"path": "USRDIR/u.sdat", "size": len(SDAT_U), "sha256": sha(SDAT_U)})
        plain = {"USRDIR/u.sdat": {"size": len(SDAT_U_PLAIN), "sha256": sha(SDAT_U_PLAIN)}}
        self.files["USRDIR/u.sdat"] = b"DCX\0" + b"q" * 20
        f = self.installed("hdd/game/" + TITLE)
        self.assertEqual(game_update.bad_files(f, self.want, plain=plain), ["USRDIR/u.sdat"])


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

    def with_update_sdat(self, form):
        """A table with a .sdat the update brings, as the real ones are, and the update's folder holding it in `form`."""
        self.want["files"].append({"path": "USRDIR/u.sdat", "size": len(SDAT_U), "sha256": sha(SDAT_U)})
        t = self.game()
        t["originals"].append({"path": "PS3_GAME/USRDIR/u.sdat", "size": len(SDAT_U), "sha256": sha(SDAT_U)})
        t["decrypted"].append({"path": "PS3_GAME/USRDIR/u.sdat.unedat", "of": "PS3_GAME/USRDIR/u.sdat",
                               "size": len(SDAT_U_PLAIN), "sha256": sha(SDAT_U_PLAIN), "magic": "44435800"})
        t["titles"][TITLE]["files"]["PS3_GAME/USRDIR/u.sdat"] = {"from": "update"}
        f = self.unpacked()
        self.put("update/USRDIR/u.sdat", form)
        return t, f

    def test_a_sdat_as_rpcs3_installs_it(self):
        t, f = self.with_update_sdat(SDAT_U_PLAIN)
        rep = gamefiles.check(self.dir / "dump", table=t, update=f)
        self.assertTrue(rep.ok, [i.message for i in rep.problems()])
        item = next(i for i in rep.items if i.name == "PS3_GAME/USRDIR/u.sdat.unedat")
        self.assertTrue(item.good)
        self.assertEqual(Path(item.found).name, "u.sdat")

    def test_a_sdat_still_encrypted_is_for_the_unlock(self):
        t, f = self.with_update_sdat(SDAT_U)
        rep = gamefiles.check(self.dir / "dump", table=t, update=f, unlocks=True)
        self.assertEqual(next(i for i in rep.items if i.kind == "update").state, "ok")
        self.assertTrue(next(i for i in rep.items if i.name == "PS3_GAME/USRDIR/u.sdat").good)
        self.assertEqual(next(i for i in rep.items if i.name == "PS3_GAME/USRDIR/u.sdat.unedat").state, "missing")

    def test_a_plain_sdat_of_other_bytes(self):
        t, f = self.with_update_sdat(b"DCX\0" + b"q" * 20)
        rep = gamefiles.check(self.dir / "dump", table=t, update=f)
        self.assertFalse(rep.ok)
        self.assertEqual(next(i for i in rep.items if i.kind == "update").state, "mismatch")

    def test_a_package_needs_rpcs3(self):
        t = self.game()
        pkg = self.put("u.pkg", self.pkg)
        jr = journal.Journal(self.dir / "journal.json")
        emit = types.SimpleNamespace(text=lambda s: None)
        with self.assertRaises(game_update.UpdateError) as c:
            pipeline._install_update({"game": str(self.dir / "dump"), "update": str(pkg)}, self.dir, jr, emit, None, t)
        self.assertIn("needs RPCS3", str(c.exception))
        self.assertIn("dev_hdd0/game/" + TITLE, str(c.exception))
        self.assertFalse((self.dir / "update").exists())

    def test_a_changed_update_file(self):
        t = self.game()
        f = self.unpacked()
        (f / "USRDIR" / "msg" / "bonus.bin").write_bytes(b"changed")
        rep = gamefiles.check(self.dir / "dump", table=t, update=f)
        self.assertFalse(rep.ok)
        self.assertEqual(next(i for i in rep.items if i.kind == "update").state, "mismatch")


if __name__ == "__main__":
    unittest.main()
