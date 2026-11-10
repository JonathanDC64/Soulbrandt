"""A game folder setup isn't allowed to read (issue #473): the check says which folder and what to do, and setup
doesn't stop on an unexpected error. Access is denied by stand-ins for os.stat, os.listdir and os.scandir, as Windows
denies it for a folder whose permissions leave the player's account out.

  cd tools/setup && python -I -m unittest discover -s tests -p "test_folder_access.py"
"""
import contextlib
import os
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from des_setup import gamefiles  # noqa: E402
from test_rpcs3_route import Base  # noqa: E402


@contextlib.contextmanager
def denied(folder):
    """os.stat, os.listdir and os.scandir of `folder` and of everything under it raise PermissionError."""
    top = os.path.normcase(os.path.abspath(folder))

    def under(p) -> bool:
        try:
            q = os.path.normcase(os.path.abspath(os.fspath(p)))
        except TypeError:
            return False                    # (a file descriptor)
        return q == top or q.startswith(top + os.sep)

    def wrap(real):
        def f(p=".", *a, **k):
            if under(p):
                raise PermissionError(13, "Access is denied", os.fspath(p))
            return real(p, *a, **k)
        return f

    with mock.patch("os.stat", wrap(os.stat)), mock.patch("os.listdir", wrap(os.listdir)), \
            mock.patch("os.scandir", wrap(os.scandir)):
        yield


class FolderAccessTest(Base):
    def setUp(self):
        super().setUp()
        self.lib.standard()

    def assert_says(self, folder, rc, lines):
        self.assertFalse(any(x.startswith("@error internal") for x in lines), lines)
        self.assertNotEqual(rc, 0, lines)
        said = [x for x in lines if "access denied" in x]
        self.assertTrue(said and all(str(folder) in x for x in said), lines)
        self.assertFalse(any("has no PS3_GAME folder" in x for x in lines), lines)

    def test_the_game_folder(self):
        with denied(self.t.dump):
            rc, lines = self.run_route(library=False)
        self.assert_says(self.t.dump, rc, lines)

    def test_its_ps3_game_folder(self):
        with denied(self.t.dump / "PS3_GAME"):
            rc, lines = self.run_route(library=False)
        self.assert_says(self.t.dump / "PS3_GAME", rc, lines)

    def test_the_lookups_do_not_raise(self):
        for folder in (self.t.dump, self.t.dump / "PS3_GAME"):
            with self.subTest(folder=folder), denied(folder):
                self.assertIsNone(gamefiles.find_root(self.t.dump))
                self.assertEqual(gamefiles.title_of(self.t.dump), "")
                self.assertEqual(gamefiles.version_of(self.t.dump), "")
                self.assertEqual(gamefiles.installed_disc(self.t.dump), "")
                self.assertTrue(gamefiles.copy_key(self.t.dump))
                self.assertEqual(gamefiles.unreadable_part(self.t.dump), str(folder))

    def test_a_folder_that_can_be_read_names_nothing(self):
        self.assertEqual(gamefiles.unreadable_part(self.t.dump), "")
        self.assertEqual(gamefiles.unreadable_part(self.t.base / "nowhere"), "")
        self.assertEqual(gamefiles.find_root(self.t.dump), self.t.dump)


if __name__ == "__main__":
    unittest.main()
