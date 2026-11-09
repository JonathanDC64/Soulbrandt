"""The ways RPCS3 can hold each supported disc (issue #453): rpcs3_library.py's library, gamefiles.py's installed_disc
and disc_view, and their part in pipeline.py. The four discs are made-up files with a table of their own, shaped as
game_table.json's real entries are. No game data, no RPCS3: the unlock runs fake_rpcs3.py.

  cd tools/setup && python -I -m unittest discover -s tests -p "test_rpcs3_layouts.py"
"""
import contextlib
import hashlib
import io
import os
import shutil
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from des_setup import __main__ as cli, gamefiles, pipeline, protocol, rpcs3_library as lib, sfo  # noqa: E402
from test_rpcs3_route import Base  # noqa: E402
from test_unlock import FAST, FILES, put, snapshot  # noqa: E402

US, EU, JP, ASIA = lib.US_ID, lib.EU_ID, lib.JP_ID, lib.ASIA_ID
DISCS = {US: "the US disc", EU: "the European disc", JP: "the Japanese disc", ASIA: "the Asian disc"}
PLAIN = (US, EU)        # the discs that need no update
UPDATED = (JP, ASIA)    # the discs with a 1.04 update, kept in dev_hdd0/game/<TITLE_ID> by RPCS3
EBOOT = next(f for f in FILES if f[0].endswith("EBOOT.BIN"))[0]


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def param(title, ver="01.00", category="DG") -> bytes:
    return sfo.build({"TITLE_ID": title, "APP_VER": ver, "VERSION": ver, "CATEGORY": category})


def update_files(title) -> dict:
    """A made-up 1.04 update's files, by their paths under its folder."""
    return {"PARAM.SFO": param(title, "01.04", "GD"), "USRDIR/EBOOT.BIN": b"SCE\0" + title.encode() + b"u" * 40}


class Layouts(Base):
    def setUp(self):
        super().setUp()
        self.lib.user("00000001", "User")
        self.discs = {US: self.t.dump}
        for title in (EU, JP, ASIA):
            shutil.copytree(self.t.dump, self.t.base / "roms" / title)
            self.discs[title] = self.t.base / "roms" / title
            put(self.discs[title] / "PS3_GAME" / "PARAM.SFO", param(title))
        self.t.table = dict(self.t.table, disc=DISCS[US], titles={t: self.entry(t) for t in (EU, JP, ASIA)})

    def entry(self, title) -> dict:
        """A disc's "titles" entry: its own PARAM.SFO; the Japanese and Asian discs' EBOOT.BIN comes from the update."""
        files = {"PS3_GAME/PARAM.SFO": self.sum(param(title))}
        out = {"disc": DISCS[title], "app_ver": "01.00", "version": "01.00", "files": files}
        if title in UPDATED:
            up = update_files(title)
            files[EBOOT] = dict(self.sum(up["USRDIR/EBOOT.BIN"]), **{"from": "update"})
            out["update"] = {"app_ver": "01.04", "pkg": {"size": 4, "sha256": sha(b"none")},
                             "files": [dict(self.sum(b), path=p) for p, b in up.items()]}
        return out

    @staticmethod
    def sum(data) -> dict:
        return {"size": len(data), "sha256": sha(data)}

    def games(self, text):
        self.lib.games(text)

    def disc(self, title) -> str:
        return self.discs[title].as_posix()

    def game_folder(self, title) -> Path:
        return self.lib.hdd0 / "game" / title

    def clear_hdd(self, hdd=None):
        """No disc in dev_hdd0/game any more: each sub-test starts with one disc there."""
        shutil.rmtree((hdd or self.lib.hdd0) / "game", ignore_errors=True)

    def installed(self, title, category="DG", where=None) -> Path:
        """A disc game kept in dev_hdd0/game/<title>: PS3_GAME's files right in the folder."""
        dest = where or self.game_folder(title)
        shutil.copytree(self.discs[title] / "PS3_GAME", dest)
        (dest / "PARAM.SFO").write_bytes(param(title, category=category))
        return dest

    def update_in_hdd(self, title, where=None):
        for p, b in update_files(title).items():
            put((where or self.game_folder(title)) / p, b)

    def run_library(self, phases=("check",), update=None, **kw):
        """A run on the RPCS3 route with the game from the library, as the host's runs: check only unless phases says."""
        req = pipeline.make_request(None, rpcs3_path=self.t.rpcs3, update=update)
        kw.setdefault("gamedata", self.gamedata)
        out = io.StringIO()
        rc = pipeline.run(self.t.data, req, app="1.0", emit=protocol.Emitter(out), table=self.t.table,
                          manifest={"tools": []}, phases=list(phases), quiet=False,
                          unlock_kw=dict(FAST, launch=[sys.executable, "-I", str(self.t.rpcs3 / "fake_rpcs3.py")]),
                          from_library=True, **kw)
        return rc, out.getvalue().splitlines()

    def assert_found(self, title, lines, rc, folder):
        self.assertEqual(rc, 0, lines)
        self.assertIn(f"@library folder {folder}", lines)
        self.assertIn(f"@check ok TITLE_ID {title}, version 01.00 ({DISCS[title]}).", lines)
        self.assertEqual(lines[-1], "@done ok")

    def prepare_update(self, title):
        if title in UPDATED:
            self.update_in_hdd(title)


class DiscFolderTest(Layouts):
    """The usual way: the disc copy's folder in games.yml."""

    def test_each_disc(self):
        for title in DISCS:
            with self.subTest(title=title):
                self.prepare_update(title)
                self.games(f"{title}: {self.disc(title)}/\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_found(title, lines, rc, self.discs[title])

    def test_each_disc_by_its_ps3_game_folder(self):
        for title in DISCS:
            with self.subTest(title=title):
                self.prepare_update(title)
                self.games(f"{title}: {self.disc(title)}/PS3_GAME/./\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_found(title, lines, rc, self.discs[title])

    def test_each_disc_in_the_older_games_yml(self):
        for title in DISCS:
            with self.subTest(title=title):
                self.prepare_update(title)
                put(self.lib.root / "games.yml", f"{title}: {self.disc(title)}/\n".encode())
                rc, lines = self.run_library(check_only=True)
                self.assert_found(title, lines, rc, self.discs[title])


class InstalledDiscTest(Layouts):
    """A disc game RPCS3 keeps in dev_hdd0/game/<TITLE_ID> (issue #453): PS3_GAME's files in the folder, no PS3_GAME."""

    def assert_through_link(self, title, lines, rc, hdd):
        self.assert_found(title, lines, rc, hdd)
        link = self.t.data / "setup" / pipeline.DISC_VIEW / title / "PS3_GAME"
        self.assertEqual(os.path.normcase(os.path.realpath(link)), os.path.normcase(os.path.realpath(hdd)))
        self.assertTrue((link / "PARAM.SFO").is_file())
        self.assertTrue(any(x.startswith("ok   RPCS3 keeps this disc") and str(hdd) in x for x in lines), lines)
        self.assertTrue(any(x.startswith(f"@check ok {EBOOT}") or x.startswith(f"@check missing ") for x in lines))

    def test_registered_in_games_yml(self):
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd()
                hdd = self.installed(title)
                # The entry RPCS3 writes (System.cpp): the folder and "/./", which marks a PS3_GAME folder.
                self.games(f"{title}: {hdd.as_posix()}//./\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_through_link(title, lines, rc, hdd)

    def test_not_registered_yet(self):
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd()
                hdd = self.installed(title)
                self.games("BLUS30027: C:/somewhere else/\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_through_link(title, lines, rc, hdd)

    def test_a_custom_dev_hdd0(self):
        elsewhere = self.t.base / "hdd elsewhere"
        put(self.lib.conf / "vfs.yml", f"$(EmulatorDir): \"\"\n/dev_hdd0/: {elsewhere.as_posix()}/\n".encode())
        for title in PLAIN:
            with self.subTest(title=title):
                self.clear_hdd(elsewhere)
                hdd = self.installed(title, where=elsewhere / "game" / title)
                self.games("")
                rc, lines = self.run_library(check_only=True)
                self.assert_through_link(title, lines, rc, hdd)

    def test_nothing_is_written_in_rpcs3s_folder(self):
        hdd = self.installed(EU)
        self.games(f"{EU}: {hdd.as_posix()}//./\n")
        before = snapshot(self.lib.root), snapshot(hdd)
        rc, lines = self.run_library(phases=("check", "unlock", "stage"), unlock_only=True)
        self.assertEqual(rc, 0, lines)
        self.assertEqual((snapshot(self.lib.root), snapshot(hdd)), before)

    def test_the_whole_route_through_the_link(self):
        for title in PLAIN:
            with self.subTest(title=title):
                hdd = self.installed(title)
                self.games(f"{title}: {hdd.as_posix()}/./\n")
                rc, lines = self.run_library(phases=("check", "unlock", "stage"))
                self.assertEqual(rc, 0, lines)
                self.assertIn(f"@library folder {hdd}", lines)
                self.assertTrue((self.t.data / "inputs" / EBOOT.replace(".BIN", ".elf")).is_file())

    def test_ready_txt_names_the_link_folder(self):
        hdd = self.installed(US)                         # (the US disc: the fake lift has no region tables)
        self.games(f"{US}: {hdd.as_posix()}//./\n")
        rc, lines = self.run_route(phases=None)
        self.assertEqual(rc, 0, lines)
        ready = pipeline.read_ready(self.t.data)
        self.assertEqual(ready["game"].replace("\\", "/"), f"setup/{pipeline.DISC_VIEW}/{US}")
        self.assertEqual(pipeline.load_request(self.t.data)["game"],
                         str(self.t.data / "setup" / pipeline.DISC_VIEW / US))

    def test_a_japanese_or_asian_disc_with_its_update_given(self):
        for title in UPDATED:
            with self.subTest(title=title):
                self.clear_hdd()
                hdd = self.installed(title)
                update = self.t.base / "update" / title
                self.update_in_hdd(title, where=update)
                self.games(f"{title}: {hdd.as_posix()}//./\n")
                rc, lines = self.run_library(check_only=True, update=update)
                self.assert_through_link(title, lines, rc, hdd)
                self.assertIn(f"@check ok update Its update, 01.04, is there.", lines)

    def test_the_folder_chosen_by_hand(self):
        """Not only from RPCS3's list: the same folder chosen on the page."""
        hdd = self.installed(EU)
        req = pipeline.make_request(hdd, rpcs3_path=self.t.rpcs3)
        out = io.StringIO()
        rc = pipeline.run(self.t.data, req, app="1.0", emit=protocol.Emitter(out), table=self.t.table,
                          manifest={"tools": []}, phases=["check"], quiet=False, check_only=True,
                          unlock_kw=dict(FAST, launch=[sys.executable, "-I", str(self.t.rpcs3 / "fake_rpcs3.py")]),
                          from_library=False, gamedata=self.gamedata)
        lines = out.getvalue().splitlines()
        self.assertEqual(rc, 0, lines)
        self.assertIn(f"@check ok TITLE_ID {EU}, version 01.00 ({DISCS[EU]}).", lines)

    def test_no_link_can_be_made(self):
        hdd = self.installed(EU)
        self.games(f"{EU}: {hdd.as_posix()}//./\n")
        with mock.patch.object(gamefiles, "disc_view", side_effect=OSError("no links here")):
            rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1)
        self.assertIn(f"@library folder {hdd}", lines)
        self.assertEqual(lines[-1], "@done fix")
        self.assertTrue(lines[-2].startswith("@error game check ") and str(hdd) in lines[-2]
                        and "PS3_GAME" in lines[-2], lines[-2])

    def test_check_command(self):
        """`check --rpcs3`: the real table does not know this made-up disc's files, but the disc is read."""
        self.installed(EU)
        self.games("")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = cli.main(["check", "--rpcs3", str(self.t.rpcs3), "--gamedata", str(self.gamedata)])
        self.assertEqual(rc, 1)
        self.assertIn("BLES00932, version 01.00 (the European disc).", out.getvalue())
        self.assertNotIn("no PS3_GAME", out.getvalue())


class NotInstalledDiscTest(Layouts):
    """What is in dev_hdd0/game but is not the disc."""

    def test_an_update_alone_is_not_the_game(self):
        self.update_in_hdd(JP)
        self.games("")
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1)
        self.assertEqual(lines[-2:], ["@error game check Demon's Souls is not in RPCS3's library yet.", "@done fix"])

    def test_an_update_under_the_disc_in_the_list(self):
        """With a disc in games.yml, the update's folder under dev_hdd0/game is not taken for the disc."""
        self.update_in_hdd(JP)
        self.games(f"{JP}: {self.disc(JP)}/\n")
        rc, lines = self.run_library(check_only=True)
        self.assert_found(JP, lines, rc, self.discs[JP])

    def test_a_store_format_game(self):
        """A game kept as the PlayStation Store's kind (category HG) is not taken for a disc copy."""
        self.installed(EU, category="HG")
        self.games("")
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(lines[-2:], ["@error game check Demon's Souls is not in RPCS3's library yet.", "@done fix"])
        self.assertEqual(rc, 1)

    def test_installed_disc_says_which(self):
        hdd = self.installed(EU)
        self.assertEqual(gamefiles.installed_disc(hdd), EU)
        self.assertEqual(gamefiles.title_of(hdd), EU)
        for bad in ("", None, str(hdd / "nowhere")):
            self.assertEqual(gamefiles.installed_disc(bad), "")
        (hdd / "PARAM.SFO").write_bytes(param(EU, category="GD"))
        self.assertEqual(gamefiles.installed_disc(hdd), "")
        (hdd / "PARAM.SFO").write_bytes(param(EU))
        (hdd / "USRDIR" / "EBOOT.BIN").unlink()
        self.assertEqual(gamefiles.installed_disc(hdd), "")

    def test_a_disc_root_in_dev_hdd0_is_not_an_installed_disc(self):
        """A dump with a real PS3_GAME folder there is a disc root (RPCS3 moves it at boot; System.cpp)."""
        root = self.t.base / "h" / EU
        shutil.copytree(self.discs[EU], root)
        self.assertEqual(gamefiles.installed_disc(root), "")
        self.assertEqual(gamefiles.find_root(root), root)

    def test_disc_view_replaces_a_stale_link(self):
        one, two = self.installed(EU), self.installed(EU, where=self.t.base / "moved" / EU)
        view = self.t.base / "views" / EU
        self.assertEqual(gamefiles.disc_view(one, view), view)
        self.assertEqual(gamefiles.find_root(view), view)
        gamefiles.disc_view(one, view)                   # (already right: nothing happens)
        self.assertEqual(os.path.normcase(os.path.realpath(view / "PS3_GAME")), os.path.normcase(os.path.realpath(one)))
        gamefiles.disc_view(two, view)                   # (the game moved)
        self.assertEqual(os.path.normcase(os.path.realpath(view / "PS3_GAME")), os.path.normcase(os.path.realpath(two)))
        self.assertEqual(gamefiles.title_of(view), EU)


class LeftoverEntryTest(Layouts):
    """games.yml keeps an entry after its game has gone (issue #453): it must not hide a copy the player has."""

    def test_a_gone_entry_does_not_hide_a_copy_that_is_there(self):
        gone = (self.t.base / "gone").as_posix()
        for title in (EU, JP, ASIA):
            with self.subTest(title=title):
                self.prepare_update(title)
                self.games(f"{US}: {gone}/\n{title}: {self.disc(title)}/\n")
                rc, lines = self.run_library(check_only=True)
                self.assert_found(title, lines, rc, self.discs[title])

    def test_a_gone_entry_does_not_hide_a_disc_in_dev_hdd0(self):
        hdd = self.installed(EU)
        self.games(f"{US}: {(self.t.base / 'gone').as_posix()}/\n")
        rc, lines = self.run_library(check_only=True)
        self.assert_found(EU, lines, rc, hdd)

    def test_with_two_copies_there_the_us_disc_is_first(self):
        self.games(f"{EU}: {self.disc(EU)}/\n{US}: {self.disc(US)}/\n")
        rc, lines = self.run_library(check_only=True)
        self.assert_found(US, lines, rc, self.discs[US])

    def test_with_only_gone_entries_the_page_says_where_it_looked(self):
        gone = self.t.base / "gone"
        self.games(f"{EU}: {gone.as_posix()}/\n")
        rc, lines = self.run_library(check_only=True)
        self.assertEqual(rc, 1)
        self.assertIn(f"@library folder {gone}", lines)
        sentence = next(x for x in lines if x.startswith("@check missing PS3_GAME "))
        self.assertIn(str(gone), sentence)
        self.assertIn("PS3_GAME", sentence)
        self.assertEqual(lines[-1], "@done fix")

    def test_a_gone_iso_counts_last(self):
        hdd = self.installed(EU)
        self.games(f"{US}: {(self.t.base / 'gone.iso').as_posix()}\n")
        self.assertEqual(lib.library(lib.folders(self.lib.exe), [US, EU]).path, hdd)
        self.assertEqual(lib.library(lib.folders(self.lib.exe), [US]).kind, "iso")

    def test_an_iso_that_is_there_does_not_hide_a_folder(self):
        """The ISO is what setup cannot use, so a copy it can use comes first, whatever the titles' order."""
        iso = self.t.base / "us.iso"
        put(iso, b"an ISO image")
        for title in (EU, JP, ASIA):
            with self.subTest(title=title):
                self.games(f"{US}: {iso.as_posix()}\n{title}: {self.disc(title)}/\n")
                got = lib.library(lib.folders(self.lib.exe), [US, EU, JP, ASIA])
                self.assertEqual((got.kind, got.title), ("folder", title))
        hdd = self.installed(EU)
        self.games(f"{US}: {iso.as_posix()}\n")
        self.assertEqual(lib.library(lib.folders(self.lib.exe), [US, EU, JP, ASIA]).path, hdd)
        self.clear_hdd()
        got = lib.library(lib.folders(self.lib.exe), [US, EU, JP, ASIA])    # (only the ISO left: the page says so)
        self.assertEqual((got.kind, got.title), ("iso", US))


class NamesTest(Layouts):
    def test_every_disc_of_the_table_has_its_names(self):
        """Each disc the table supports names its save folder and trophy set, so its import finds them."""
        table = gamefiles.load_table()
        self.assertEqual(sorted(gamefiles.titles(table)), sorted(lib.TITLES))
        for tid, t in lib.TITLES.items():
            self.assertEqual((t.id, t.save_dir[:9]), (tid, tid))
            self.assertRegex(t.trophy_dir, r"^NPWR\d{5}_00$")

    def test_each_disc_finds_its_own_save_and_trophies(self):
        from test_rpcs3_route import T0
        table = gamefiles.load_table()
        for tid, t in lib.TITLES.items():
            with self.subTest(title=tid):
                self.games(f"{tid}: {self.disc(tid)}/\n")
                self.lib.active("00000001")
                self.lib.save("00000001", save_dir=t.save_dir)
                self.lib.trophies("00000001", {3: T0}, trophy_dir=t.trophy_dir)
                got = lib.read(self.lib.exe, table)
                self.assertEqual(got.title, tid)
                self.assertEqual(got.save.state, "ok")
                self.assertEqual(got.trophies.unlocked, {3: T0})

    def test_the_check_line_names_the_disc(self):
        rep = gamefiles.Report(root=Path("."), table={"disc": "the European disc", "app_ver": "01.00"})
        rep.items.append(gamefiles.Item("original", "PS3_GAME/PARAM.SFO", "ok", "PARAM.SFO matches."))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            cli._print_report(rep)
        self.assertIn("1 of the disc's files match the European disc, version 01.00.", out.getvalue())
        self.assertNotIn("the US disc", out.getvalue())


if __name__ == "__main__":
    unittest.main()
