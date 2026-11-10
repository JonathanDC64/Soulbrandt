"""PlayStation Store copies of the game in RPCS3, and several copies of the game in one RPCS3: gamefiles.py's digital
check, rpcs3_library.py's installs and pipeline.py's @install lines. Made-up files with a table of their own, shaped as
game_table.json's real entries are. No game data, no RPCS3: the unlock runs fake_rpcs3.py.

  cd tools/setup && python -I -m unittest discover -s tests -p "test_store_copies.py"
"""
import gzip
import io
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from des_setup import disc as disc_files, gamefiles, pipeline, protocol, sfo  # noqa: E402
from test_rpcs3_layouts import ASIA, EBOOT, EU, JP, PLAIN, US, Layouts  # noqa: E402
from test_unlock import EBOOT_ELF, FAST, M01_DEC, M02_DEC, put, snapshot  # noqa: E402

STORE = {US: "NPUB30910", EU: "NPEB01202"}
WHAT = {US: "the US PlayStation Store version", EU: "the European PlayStation Store version"}
ELF = {US: EBOOT_ELF, EU: b"\x7fELF" + b"X" * 60}
ELF_PATH = EBOOT.replace(".BIN", ".elf")
SDATS = {"USRDIR/chr/m02.chresdbnd.dcx.sdat": M02_DEC, "USRDIR/script/m01.luabnd.dcx.sdat": M01_DEC}
SOURCE = Path(__file__).resolve().parents[1] / "des_setup"
HOST = Path(__file__).resolve().parents[3] / "port" / "ui" / "src"


def store_param(title) -> bytes:
    """The package's PARAM.SFO: the disc's TITLE_ID, and another system version than the disc's."""
    return sfo.build({"TITLE_ID": title, "APP_VER": "01.00", "VERSION": "01.00", "CATEGORY": "DG",
                      "PS3_SYSTEM_VER": "03.7000"})


def store_eboot(title) -> bytes:
    return b"SCE\0" + STORE[title].encode() + b"n" * 70


class Copies(Layouts):
    def setUp(self):
        super().setUp()
        titles = dict(self.t.table["titles"])
        files = dict(titles[EU]["files"], **{ELF_PATH: self.sum(ELF[EU])})
        titles[EU] = dict(titles[EU], files=files, digital=self.block(EU))
        self.t.table = dict(self.t.table, digital=self.block(US), titles=titles)

    def block(self, title) -> dict:
        return {"id": STORE[title], "what": WHAT[title],
                "files": {"PS3_GAME/PARAM.SFO": self.sum(store_param(title)), EBOOT: self.sum(store_eboot(title))}}

    def store_copy(self, title, elf=True, marker=True, where=None) -> Path:
        """A PlayStation Store copy as RPCS3 installs it: the game's files right in dev_hdd0/game/<its own ID>, its .sdat
        files plain. elf: the player has already decrypted EBOOT.BIN in RPCS3."""
        dest = where or self.game_folder(STORE[title])
        put(dest / "PARAM.SFO", store_param(title))
        put(dest / "USRDIR" / "EBOOT.BIN", store_eboot(title))
        for rel, plain in SDATS.items():
            put(dest / rel, plain)
        if marker:
            put(dest / gamefiles.STORE_MARK, b"marker")
        if elf:
            put(dest / "USRDIR" / "EBOOT.elf", ELF[title])
        return dest

    def run_game(self, game, phases=("check",), **kw):
        """A run with this folder chosen: what the host asks once the player has picked a copy."""
        req = pipeline.make_request(game, rpcs3_path=self.t.rpcs3)
        out = io.StringIO()
        rc = pipeline.run(self.t.data, req, app="1.0", emit=protocol.Emitter(out), table=self.t.table,
                          manifest={"tools": []}, phases=list(phases), quiet=False,
                          unlock_kw=dict(FAST, launch=[sys.executable, "-I", str(self.t.rpcs3 / "fake_rpcs3.py")]),
                          from_library=False, gamedata=self.gamedata, **kw)
        return rc, out.getvalue().splitlines()

    @staticmethod
    def installs(lines) -> list:
        """The @install lines: [state, kind, title, version, chosen, path, why]."""
        out = []
        for line in lines:
            if line.startswith("@install "):
                state, kind, title, version, chosen, path = line.split(" ", 6)[1:]
                out.append([state, kind, title, version, chosen == "1", path, ""])
            elif line.startswith("@installwhy "):
                out[-1][6] = line.split(" ", 1)[1]
        return out

    def assert_store(self, title, lines, rc, hdd):
        self.assertEqual(rc, 0, lines)
        self.assertIn(f"@library folder {hdd}", lines)
        self.assertIn(f"@check ok TITLE_ID {title}, version 01.00 ({WHAT[title]}).", lines)
        self.assertIn(f"@check ok STORE {STORE[title]}, {WHAT[title]}.", lines)
        self.assertEqual(lines[-1], "@done ok")


class StoreCopyTest(Copies):
    def test_never_started_in_rpcs3(self):
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd()
                self.games("")
                hdd = self.store_copy(title)
                rc, lines = self.run_library(check_only=True)
                self.assert_store(title, lines, rc, hdd)

    def test_started_in_rpcs3(self):
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd()
                hdd = self.store_copy(title)
                self.games(f"{title}: {hdd.as_posix()}//./\n")      # (the line RPCS3 writes: System.cpp)
                rc, lines = self.run_library(check_only=True)
                self.assert_store(title, lines, rc, hdd)

    def test_the_folder_chosen_by_hand(self):
        hdd = self.store_copy(EU)
        rc, lines = self.run_game(hdd, check_only=True)
        self.assertEqual(rc, 0, lines)
        self.assertIn(f"@check ok STORE {STORE[EU]}, {WHAT[EU]}.", lines)

    def test_known_by_its_program_when_the_marker_is_gone(self):
        hdd = self.store_copy(US, marker=False)
        rc, lines = self.run_library(check_only=True)
        self.assert_store(US, lines, rc, hdd)

    def test_elf_missing_says_what_to_do_in_rpcs3(self):
        hdd = self.store_copy(US, elf=False)
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1, lines)
        self.assertEqual(lines[-1], "@done fix")
        said = next(x for x in lines if x.startswith("@check decrypt "))
        self.assertIn("Utilities", said)
        self.assertIn("Decrypt PS3 Binaries", said)
        self.assertIn(os.path.realpath(hdd / "USRDIR" / "EBOOT.BIN"), said)

    def test_a_wrong_elf_says_so(self):
        hdd = self.store_copy(US)
        put(hdd / "USRDIR" / "EBOOT.elf", b"\x7fELF" + b"Z" * 60)
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1, lines)
        self.assertIn("EBOOT.elf does not match", next(x for x in lines if x.startswith("@check decrypt ")))

    def test_setup_never_makes_the_elf_itself(self):
        self.store_copy(US, elf=False)
        rc, lines = self.run_library(phases=("check", "unlock", "stage"))
        self.assertEqual(rc, 1, lines)
        self.assertEqual(self.t.calls(), [])
        self.assertFalse((self.t.data / "inputs" / ELF_PATH).exists())

    def test_nothing_mentions_a_licence_file(self):
        hdd = self.store_copy(EU, elf=False)
        put(self.lib.hdd0 / "home" / "00000001" / "exdata" / "game.rap", b"not to be looked at")
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1, lines)
        text = "\n".join(lines).lower()
        self.assertNotIn(".rap", text)
        self.assertNotIn("exdata", text)
        self.assertTrue(hdd.is_dir())

    def test_no_code_looks_for_one(self):
        for f in [*SOURCE.glob("*.py"), *HOST.glob("setup_*")]:
            with self.subTest(file=f.name):
                text = f.read_text(encoding="utf-8").lower()
                self.assertNotIn(".rap", text)
                self.assertNotIn("exdata", text)

    def test_a_damaged_program_is_caught(self):
        hdd = self.store_copy(US)
        data = bytearray(store_eboot(US))
        data[-1] ^= 1
        put(hdd / "USRDIR" / "EBOOT.BIN", bytes(data))
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1, lines)
        self.assertTrue(any(x.startswith(f"@check mismatch {EBOOT}") and "install the package again" in x
                            for x in lines), lines)

    def test_a_damaged_script_is_caught(self):
        hdd = self.store_copy(EU)
        rel, plain = next(iter(SDATS.items()))
        data = bytearray(plain)
        data[-1] ^= 1
        put(hdd / rel, bytes(data))
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1, lines)
        self.assertTrue(any(x.startswith(f"@check mismatch PS3_GAME/{rel}") for x in lines), lines)

    def test_built_from_the_plain_files(self):
        self.store_copy(US)
        rc, lines = self.run_library(phases=("check", "unlock", "stage"))
        self.assertEqual(rc, 0, lines)
        self.assertEqual(self.t.calls(), [])                # (nothing for RPCS3 to unlock)
        inputs = self.t.data / "inputs"
        self.assertEqual((inputs / ELF_PATH).read_bytes(), ELF[US])
        for rel, plain in SDATS.items():
            self.assertEqual((inputs / "PS3_GAME" / (rel + ".unedat")).read_bytes(), plain)

    def test_nothing_is_written_in_rpcs3s_folder(self):
        self.store_copy(EU)
        before = snapshot(self.lib.root)
        rc, lines = self.run_library(phases=("check", "unlock", "stage"))
        self.assertEqual(rc, 0, lines)
        self.assertEqual(snapshot(self.lib.root), before)

    def test_only_a_store_copy_has_files_of_other_sizes_let_through(self):
        hdd = self.store_copy(US)
        real = disc_files.check
        with mock.patch.object(gamefiles.disc_files, "check", side_effect=real) as seen:
            self.run_game(hdd, check_only=True)
            self.assertEqual(sorted(seen.call_args.kwargs["sized"]),
                             sorted(e["path"] for e in self.t.table["originals"] if e["path"] != "PS3_GAME/PARAM.SFO"))
            shutil.rmtree(hdd)
            self.games(f"{US}: {self.disc(US)}/\n")
            self.run_library(check_only=True)
            self.assertEqual(list(seen.call_args.kwargs["sized"]), [])


class DiscListTest(unittest.TestCase):
    def test_only_the_named_files_may_differ(self):
        with tempfile.TemporaryDirectory() as d:
            lists, root = Path(d) / "lists", Path(d) / "copy"
            put(lists / "x.txt.gz", gzip.compress(b"PS3_GAME/USRDIR/\nA\t1\nB\t1\n"))
            put(root / "PS3_GAME" / "USRDIR" / "A", b"aa")
            put(root / "PS3_GAME" / "USRDIR" / "B", b"bb")
            got = disc_files.check(root, "x.txt.gz", lists, sized=["PS3_GAME/USRDIR/A"])
            self.assertEqual(got.differ, [("PS3_GAME/USRDIR/B", 1, 2)])
            self.assertEqual(got.state, "incomplete")
            self.assertEqual(disc_files.check(root, "x.txt.gz", lists, sized=["PS3_GAME/USRDIR/A", "PS3_GAME/USRDIR/B"]).state,
                             "complete")


class SeveralCopiesTest(Copies):
    def test_one_copy_changes_nothing(self):
        self.store_copy(US)
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 0, lines)
        self.assertEqual(self.installs(lines), [])

    def test_one_copy_and_an_iso_change_nothing(self):
        put(self.t.base / "eu.iso", b"iso")
        self.games(f"{US}: {self.disc(US)}/\n{EU}: {(self.t.base / 'eu.iso').as_posix()}\n")
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(self.installs(lines), [])
        self.assertIn(f"@library folder {self.discs[US]}", lines)

    def assert_both(self, title, lines, *folders):
        got = self.installs(lines)
        self.assertEqual(sorted(g[1] for g in got), ["disc", "store"], lines)
        self.assertEqual({g[5] for g in got}, {str(f) for f in folders})
        self.assertEqual({g[0] for g in got}, {"ready"})
        self.assertEqual(sum(g[4] for g in got), 1)

    def test_the_line_names_the_disc_and_the_store_copy_is_found_by_looking(self):
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd()
                hdd = self.store_copy(title)
                self.games(f"{title}: {self.disc(title)}/\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_both(title, lines, self.discs[title], hdd)

    def test_the_line_names_the_store_copy_and_a_disc_in_dev_hdd0_is_found_by_looking(self):
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd()
                hdd, disc = self.store_copy(title), self.installed(title)
                self.games(f"{title}: {hdd.as_posix()}//./\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_both(title, lines, hdd, disc)

    def test_a_disc_folder_whose_line_was_overwritten_is_lost_to_rpcs3_too(self):
        hdd = self.store_copy(US)
        self.games(f"{US}: {hdd.as_posix()}//./\n")      # (nothing says where the disc copy's folder is any more)
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(self.installs(lines), [])
        self.assert_store(US, lines, rc, hdd)

    def test_every_kind_at_once(self):
        self.update_in_hdd(JP)
        self.games("".join(f"{t}: {self.disc(t)}/\n" for t in (US, EU, JP, ASIA)))
        us, eu = self.store_copy(US), self.store_copy(EU, elf=False)
        rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual([(g[1], g[2], g[3], g[0]) for g in got], [
            ("disc", US, "01.00", "ready"),
            ("disc", EU, "01.00", "ready"),
            ("disc", JP, "01.00", "ready"),
            ("disc", ASIA, "01.00", "update"),
            ("store", US, "01.00", "ready"),
            ("store", EU, "01.00", "decrypt"),
        ])
        self.assertEqual([g[5] for g in got[-2:]], [str(us), str(eu)])
        self.assertEqual([g[4] for g in got], [True, False, False, False, False, False])
        self.assertIn("Decrypt PS3 Binaries", got[5][6])
        self.assertIn("update", got[3][6])
        self.assertEqual(got[0][6], "")
        self.assertEqual(rc, 0, lines)

    def test_two_copies_of_one_disc(self):
        hdd = self.installed(US)
        self.games(f"{US}: {self.disc(US)}/\n")
        rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual([g[5] for g in got], [str(self.discs[US]), str(hdd)])
        self.assertEqual([g[4] for g in got], [True, False])

    def test_the_same_copy_twice_is_one_copy(self):
        hdd = self.store_copy(US)
        self.games(f"{US}: {hdd.as_posix()}//./\n")
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(self.installs(lines), [])

    def test_the_first_ready_copy_is_the_default(self):
        hdd = self.store_copy(US, elf=False)
        self.games(f"{US}: {hdd.as_posix()}//./\n{EU}: {self.disc(EU)}/\n")
        rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual([(g[0], g[4]) for g in got], [("decrypt", False), ("ready", True)])
        self.assertIn(f"@library folder {self.discs[EU]}", lines)
        self.assertEqual(rc, 0, lines)

    def test_with_none_ready_the_first_folder_says_what_to_do(self):
        us, eu = self.store_copy(US, elf=False), self.store_copy(EU, elf=False)
        rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual([(g[0], g[4]) for g in got], [("decrypt", True), ("decrypt", False)])
        self.assertIn(os.path.realpath(us / "USRDIR" / "EBOOT.BIN"), got[0][6])
        self.assertIn(os.path.realpath(eu / "USRDIR" / "EBOOT.BIN"), got[1][6])
        self.assertEqual(rc, 1, lines)

    def test_the_players_pick_wins(self):
        hdd = self.store_copy(EU)
        self.games(f"{US}: {self.disc(US)}/\n")
        rc, lines = self.run_game(hdd, check_only=True)
        got = self.installs(lines)
        self.assertEqual([(g[1], g[2], g[4]) for g in got], [("disc", US, False), ("store", EU, True)])
        self.assertEqual(rc, 0, lines)
        self.assertIn(f"@check ok STORE {STORE[EU]}, {WHAT[EU]}.", lines)

    def test_a_pick_that_is_not_ready_is_checked_and_says_why(self):
        hdd = self.store_copy(EU, elf=False)
        self.games(f"{US}: {self.disc(US)}/\n")
        rc, lines = self.run_game(hdd, check_only=True)
        self.assertEqual([g[4] for g in self.installs(lines)], [False, True])
        self.assertEqual(rc, 1, lines)
        self.assertEqual(lines[-1], "@done fix")

    def test_the_copy_already_built_is_the_default(self):
        eu = self.store_copy(EU)
        self.games(f"{US}: {self.disc(US)}/\n")
        with mock.patch.object(pipeline, "read_ready", return_value={"game": str(eu)}):
            rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual([g[4] for g in got], [False, True], lines)
        self.assertIn(f"@library folder {eu}", lines)

    def test_a_build_through_setups_link_folder_counts_as_that_copy(self):
        eu = self.store_copy(EU)
        self.games(f"{US}: {self.disc(US)}/\n")
        view = gamefiles.disc_view(eu, self.t.data / "setup" / pipeline.DISC_VIEW / EU)
        with mock.patch.object(pipeline, "read_ready", return_value={"game": str(view)}):
            rc, lines = self.run_library(check_only=True)
        self.assertEqual([g[4] for g in self.installs(lines)], [False, True], lines)

    def test_a_damaged_copy_says_why(self):
        hdd = self.store_copy(EU)
        data = bytearray(store_eboot(EU))
        data[-1] ^= 1
        put(hdd / "USRDIR" / "EBOOT.BIN", bytes(data))
        self.games(f"{US}: {self.disc(US)}/\n")
        rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual(got[1][0], "bad")
        self.assertIn("install the package again", got[1][6])
        self.assertTrue(got[0][4])

    def test_an_iso_is_listed_and_never_chosen(self):
        put(self.t.base / "eu.iso", b"iso")
        hdd = self.store_copy(US)
        self.games(f"{US}: {self.disc(US)}/\n{EU}: {(self.t.base / 'eu.iso').as_posix()}\n")
        rc, lines = self.run_library(check_only=True)
        got = self.installs(lines)
        self.assertEqual([(g[0], g[1]) for g in got], [("ready", "disc"), ("ready", "store"), ("iso", "iso")])
        self.assertFalse(got[2][4])
        self.assertIn("ISO", got[2][6])
        self.assertTrue(hdd.is_dir())

    def test_reading_the_copies_writes_nothing_in_rpcs3(self):
        self.store_copy(US)
        self.store_copy(EU)
        self.games(f"{JP}: {self.disc(JP)}/\n")
        before = snapshot(self.lib.root)
        self.run_library(check_only=True)
        self.assertEqual(snapshot(self.lib.root), before)

    def test_the_other_store_id_is_still_turned_away(self):
        (self.game_folder("NPJA00102")).mkdir(parents=True)
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1, lines)
        self.assertIn(f"@library store {self.game_folder('NPJA00102')}", lines)


class ChangingCopyTest(Copies):
    def test_another_region_replaces_the_staged_game(self):
        us, eu = self.store_copy(US), self.store_copy(EU)
        inputs = self.t.data / "inputs"
        rc, lines = self.run_game(us, phases=("check", "unlock", "stage"))
        self.assertEqual(rc, 0, lines)
        self.assertEqual((inputs / ELF_PATH).read_bytes(), ELF[US])
        rc, lines = self.run_game(eu, phases=("check", "unlock", "stage"))
        self.assertEqual(rc, 0, lines)
        self.assertEqual((inputs / ELF_PATH).read_bytes(), ELF[EU])

    def test_the_views_of_two_titles_are_apart(self):
        us, eu = self.store_copy(US), self.store_copy(EU)
        self.run_game(us)
        self.run_game(eu)
        base = self.t.data / "setup" / pipeline.DISC_VIEW
        for title, hdd in ((US, us), (EU, eu)):
            self.assertEqual(os.path.normcase(os.path.realpath(base / title / "PS3_GAME")),
                             os.path.normcase(os.path.realpath(hdd)))

    def test_a_copy_that_is_gone_reads_as_moved(self):
        us = self.store_copy(US)
        view = gamefiles.disc_view(us, self.t.data / "setup" / pipeline.DISC_VIEW / US)
        self.assertFalse(pipeline._game_gone(view))
        shutil.rmtree(us)
        self.assertTrue(pipeline._game_gone(view))
        self.assertTrue(pipeline._game_gone(self.t.base / "no such folder"))
        self.assertFalse(pipeline._game_gone(self.discs[US]))


if __name__ == "__main__":
    unittest.main()
