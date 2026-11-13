"""Tests for the check of the whole disc copy (des_setup/disc.py, issue #418) and its lists, and how `run` stops for a
copy that is not complete, goes on with --disc-anyway and notes both (pipeline.py). On made-up dumps: no game data.

  cd tools/setup && python -m unittest discover -s tests
"""
import gzip
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import disc_list  # noqa: E402
from des_setup import disc, gamefiles, pipeline, protocol  # noqa: E402
from test_pipeline import Setup  # noqa: E402

def write_list(folder: Path, files: dict, name="TEST00000.txt.gz") -> str:
    """A list as disc_list writes one, for {path: size}; returns its full path (a table's disc_files may be one)."""
    text = disc_list.disc_list("TEST00000", "A test disc", "01.00", list(files.items()))
    path = folder / name
    path.write_bytes(gzip.compress(text))
    return str(path)


class DiscCheckTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "dump"
        self.files = {"PS3_GAME/PARAM.SFO": 4, "PS3_GAME/USRDIR/EBOOT.BIN": 8,
                      "PS3_GAME/USRDIR/sound/ds_se_main.mowb": 3, "PS3_GAME/USRDIR/sound/ds_se_m08.mowb": 2}
        for p, n in self.files.items():
            (self.root / p).parent.mkdir(parents=True, exist_ok=True)
            (self.root / p).write_bytes(b"x" * n)
        self.list = write_list(Path(self.tmp.name), self.files)

    def check(self):
        return disc.check(self.root, self.list)

    def test_a_made_list_reads_back(self):
        got = disc.load(self.list)
        self.assertEqual(got["PS3_GAME"], {"PARAM.SFO": 4})
        self.assertEqual(got["PS3_GAME/USRDIR/sound"], {"ds_se_m08.mowb": 2, "ds_se_main.mowb": 3})

    def test_the_shipped_lists_read(self):
        """Each disc's list in des_setup/disc/ reads, and the table names it."""
        t = gamefiles.load_table()
        for tid in gamefiles.titles(t):
            name = gamefiles.for_title(t, tid)["disc_files"]
            self.assertEqual(name, f"{tid}.txt.gz")
            got = disc.load(name)
            self.assertIsNotNone(got, name)
            self.assertGreater(sum(len(f) for f in got.values()), 12000)
            self.assertIn("EBOOT.BIN", got["PS3_GAME/USRDIR"])
            self.assertIn("PARAM.SFO", got["PS3_GAME"])

    def test_no_shipped_list_names_a_file_unpackers_rename(self):
        """Issue #499: the Japanese and Asian lists, made from a copy's folder, named the disc's m99_99_98_06mcp. as
        that copy's 7-Zip wrote it, m99_99_98_06mcp_. A copy that names it m99_99_98_06mcp (or keeps the dot) was one
        file short. Each list leaves out every spelling, as the IRD lists do, and every name it has is one a copy
        can hold on Windows."""
        t = gamefiles.load_table()
        for tid in gamefiles.titles(t):
            name = gamefiles.for_title(t, tid)["disc_files"]
            got = disc.load(name)
            for dotted in disc_list.DOTTED:
                folder, file = dotted.rsplit("/", 1)
                self.assertIn("m99_99_98_06.mcp", got[folder], name)       # (the file the game opens)
                for spelt in disc_list.spellings(file):
                    self.assertNotIn(spelt, got[folder], name)
            for folder, files in got.items():
                for file in files:
                    self.assertFalse(disc_list.left_out(f"{folder}/{file}"), f"{name}: {folder}/{file}")
                    self.assertFalse(file.endswith((".", " ")) or set(file) & set('<>:"\\|?*'),
                                     f"{name}: {folder}/{file}")

    def test_the_dotted_name_in_any_spelling(self):
        """Issue #499 from end to end: a list made from a copy whose unpacker wrote m99_99_98_06mcp_ (7-Zip), and a
        copy that has the file as m99_99_98_06mcp, or as m99_99_98_06mcp_, or not at all. Each is complete; a file
        the list has is still missing when it is gone."""
        folder = self.root / "PS3_GAME" / "USRDIR" / "map" / "m99_99_98_06"
        folder.mkdir(parents=True)
        (folder / "m99_99_98_06.mcp").write_bytes(b"x" * 56)
        made = dict(self.files, **{"PS3_GAME/USRDIR/map/m99_99_98_06/m99_99_98_06.mcp": 56,
                                   "PS3_GAME/USRDIR/map/m99_99_98_06/m99_99_98_06mcp_": 56})
        self.list = write_list(Path(self.tmp.name), made)
        self.assertEqual(self.check().state, "complete")                     # (no copy of the stray file)
        for spelt in ("m99_99_98_06mcp", "m99_99_98_06mcp_"):
            (folder / spelt).write_bytes(b"x" * 56)
            d = self.check()
            self.assertEqual((d.state, d.files, d.missing), ("complete", 5, []), spelt)
            (folder / spelt).unlink()
        (folder / "m99_99_98_06.mcp").unlink()
        d = self.check()
        self.assertEqual(d.missing, ["PS3_GAME/USRDIR/map/m99_99_98_06/m99_99_98_06.mcp"])
        self.assertEqual(d.sentence(), "1 of the disc's 5 files is missing from the game folder.")

    def test_complete(self):
        (self.root / "PS3_GAME" / "USRDIR" / "EBOOT.elf").write_bytes(b"extra")    # (extra files are not looked at)
        d = self.check()
        self.assertEqual((d.state, d.files, d.absent, d.differ), ("complete", 4, 0, []))
        self.assertEqual(d.sentence(), "All 4 of the disc's files are in the game folder.")

    def test_missing_a_file_and_a_folder_and_a_size(self):
        (self.root / "PS3_GAME" / "USRDIR" / "sound" / "ds_se_main.mowb").unlink()
        (self.root / "PS3_GAME" / "PARAM.SFO").write_bytes(b"xx")
        d = self.check()
        self.assertEqual(d.state, "incomplete")
        self.assertEqual(d.missing, ["PS3_GAME/USRDIR/sound/ds_se_main.mowb"])
        self.assertEqual(d.differ, [("PS3_GAME/PARAM.SFO", 4, 2)])
        self.assertEqual(d.sentence(), "1 of the disc's 4 files is missing from the game folder, and 1 is not the "
                                       "size it is on the disc.")
        for p in (self.root / "PS3_GAME" / "USRDIR" / "sound").iterdir():
            p.unlink()
        (self.root / "PS3_GAME" / "USRDIR" / "sound").rmdir()
        d = self.check()
        self.assertEqual(d.missing, ["PS3_GAME/USRDIR/sound/ds_se_m08.mowb", "PS3_GAME/USRDIR/sound/ds_se_main.mowb"])
        self.assertEqual(d.problems()[0], ("size", "PS3_GAME/PARAM.SFO"))

    def test_case_as_the_game_opens_names(self):
        sound = self.root / "PS3_GAME" / "USRDIR" / "sound"
        sound.rename(sound.with_name("SOUND"))
        old = disc.CASELESS
        self.addCleanup(setattr, disc, "CASELESS", old)
        disc.CASELESS = True                   # Windows, macOS: the same file
        self.assertEqual(self.check().state, "complete")
        disc.CASELESS = False                  # Linux: the game cannot open it
        d = self.check()
        self.assertEqual(d.state, "incomplete")
        self.assertEqual([p for p, _ in d.case], ["PS3_GAME/USRDIR/sound/ds_se_m08.mowb",
                                                  "PS3_GAME/USRDIR/sound/ds_se_main.mowb"])
        self.assertEqual(d.absent, 2)

    def test_no_list(self):
        self.assertEqual(disc.check(self.root, None).state, "unknown")
        self.assertEqual(disc.check(self.root, str(Path(self.tmp.name) / "none.txt.gz")).state, "unknown")

    def test_record(self):
        (self.root / "PS3_GAME" / "USRDIR" / "sound" / "ds_se_main.mowb").unlink()
        data = Path(self.tmp.name) / "data"
        disc.record(data, "BLUS30443", self.check(), True)
        text = (data / "setup" / disc.RECORD).read_text(encoding="utf-8")
        kv = dict(line.split("=", 1) for line in text.splitlines() if "=" in line and not line.startswith("#"))
        self.assertEqual((kv["state"], kv["files"], kv["missing"], kv["differ"], kv["continued_anyway"]),
                         ("incomplete", "4", "1", "0", "1"))
        self.assertEqual(kv["missing_file"], "PS3_GAME/USRDIR/sound/ds_se_main.mowb")
        disc.record(data, "BLUS30443", disc.check(self.root, None), True)     # (nothing to go on past)
        self.assertIn("continued_anyway=0", (data / "setup" / disc.RECORD).read_text(encoding="utf-8"))


class RunDiscTest(unittest.TestCase):
    """`run` on a copy that lacks a file of the disc's list."""

    def setUp(self):
        self.s = Setup()
        self.addCleanup(self.s.close)
        self._free = pipeline.free_bytes
        pipeline.free_bytes = lambda p: 1 << 50
        self.addCleanup(setattr, pipeline, "free_bytes", self._free)
        root = Path(self.s.game.root)
        files = {p.relative_to(root).as_posix(): p.stat().st_size for p in (root / "PS3_GAME").rglob("*")
                 if p.is_file()}
        files["PS3_GAME/USRDIR/sound/ds_se_main.mowb"] = 10
        self.s.game.table = dict(self.s.game.table, disc_files=write_list(Path(self.s.game.tmp.name), files))

    def test_check_only_warns_and_is_ok(self):
        rc, lines = self.s.run(check_only=True)
        self.assertEqual(rc, 0, lines)
        self.assertIn("@disc incomplete", " ".join(lines))
        self.assertIn("@discfile missing PS3_GAME/USRDIR/sound/ds_se_main.mowb", lines)
        self.assertEqual(lines[-1], "@done ok")
        self.assertFalse((self.s.data / "setup" / disc.RECORD).exists())     # (only a build notes it)

    def test_a_build_stops_then_goes_on_anyway(self):
        rc, lines = self.s.run()
        self.assertEqual(rc, 1, lines)
        self.assertIn("@error disc check 1 of the disc's", " ".join(lines))
        self.assertEqual(lines[-1], "@done fix")
        self.assertNotIn("@step stage start", lines)

        rc, lines = self.s.run(disc_anyway=True)
        self.assertEqual(rc, 0, lines)
        rec = (self.s.data / "setup" / disc.RECORD).read_text(encoding="utf-8")
        self.assertIn("state=incomplete", rec)
        self.assertIn("continued_anyway=1", rec)
        self.assertIn("missing_file=PS3_GAME/USRDIR/sound/ds_se_main.mowb", rec)
        saved = json.loads((self.s.data / "setup" / "request.json").read_text(encoding="utf-8"))
        self.assertTrue(saved["disc_anyway"])

        # The word is kept for that folder: a later run (Update now: the last run's folders) and one that names the
        # same folder go on; one with another folder asks again.
        rc, lines = self.s.run(request=None)
        self.assertEqual(rc, 0, lines)
        rc, lines = self.s.run()
        self.assertEqual(rc, 0, lines)
        moved = Path(str(self.s.game.root) + "_copy")
        shutil.copytree(self.s.game.root, moved)
        rc, lines = self.s.run(request=pipeline.make_request(moved))
        self.assertEqual(rc, 1, lines)
        self.assertIn("@error disc check", " ".join(lines))

    def test_a_whole_copy_is_noted_too(self):
        (Path(self.s.game.root) / "PS3_GAME" / "USRDIR" / "sound").mkdir()
        (Path(self.s.game.root) / "PS3_GAME" / "USRDIR" / "sound" / "ds_se_main.mowb").write_bytes(b"x" * 10)
        rc, lines = self.s.run()
        self.assertEqual(rc, 0, lines)
        self.assertIn("@disc complete", " ".join(lines))
        rec = (self.s.data / "setup" / disc.RECORD).read_text(encoding="utf-8")
        self.assertIn("state=complete", rec)
        self.assertIn("continued_anyway=0", rec)


class ProtocolDiscTest(unittest.TestCase):
    def test_lines(self):
        out = io.StringIO()
        d = disc.Disc("incomplete", files=12509, missing=[f"PS3_GAME/USRDIR/f{i:03}" for i in range(150)],
                      differ=[("PS3_GAME/PARAM.SFO", 1008, 1000)])
        protocol.Emitter(out).disc(d)
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0], "@disc incomplete 12509 150 1 150 of the disc's 12,509 files are missing from the "
                                   "game folder, and 1 is not the size it is on the disc.")
        self.assertEqual(lines[1], "@discfile size PS3_GAME/PARAM.SFO")
        self.assertEqual(len(lines), 1 + protocol.SHOWN)


if __name__ == "__main__":
    unittest.main()
