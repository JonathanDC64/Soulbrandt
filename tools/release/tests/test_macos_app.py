"""Tests for the Mac app's build (tools/release/macos_app.py), on a made-up Wine build and program folder.

  python -m unittest discover -s tools/release/tests
"""
import hashlib
import io
import os
import plistlib
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import macos_app as m  # noqa: E402

INSIDE = "Wine Stable.app/Contents/Resources/wine"
LAUNCHER = m.MACHO_64 + m.CPU_X86_64.to_bytes(4, "little") + b"\0" * 24


def can_link():
    with tempfile.TemporaryDirectory() as d:
        try:
            os.symlink("a", os.path.join(d, "b"))
            return True
        except (OSError, NotImplementedError):
            return False


def make_tar(path, entries):
    """entries: (name, kind, data or link target, mode); kind is 'file', 'dir', 'sym' or 'hard'."""
    with tarfile.open(path, "w:xz") as tar:
        for name, kind, data, mode in entries:
            info = tarfile.TarInfo(name)
            info.mode = mode
            if kind == "file":
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
                continue
            info.type = {"dir": tarfile.DIRTYPE, "sym": tarfile.SYMTYPE, "hard": tarfile.LNKTYPE}[kind]
            if kind in ("sym", "hard"):
                info.linkname = data
            tar.addfile(info)


def wine_entries():
    return [
        (f"{INSIDE}/bin/wine", "file", b"#wine", 0o755),
        (f"{INSIDE}/lib/libMoltenVK.dylib", "file", b"mvk", 0o644),
        (f"{INSIDE}/share/wine/nls/a.nls", "file", b"nls", 0o644),
        (f"{INSIDE}/share/wine/mono/big.msi", "file", b"mono", 0o644),
        (f"{INSIDE}/share/wine/gecko/big.msi", "file", b"gecko", 0o644),
        ("Wine Stable.app/Contents/MacOS/wine", "file", b"their launcher", 0o755),
    ]


def program_folder(root, info="version=0.2.1-rc.1\ncommit=abc\nexe=Game.exe\n"):
    os.makedirs(os.path.join(root, "setup"))
    with open(os.path.join(root, "setup", "release-info.txt"), "w", encoding="utf-8") as f:
        f.write("# a comment=with an equals sign\n" + info)
    with open(os.path.join(root, "Game.exe"), "wb") as f:
        f.write(b"MZ")
    with open(os.path.join(root, "LICENSE"), "w", encoding="utf-8") as f:
        f.write("licence\n")


class PinsTest(unittest.TestCase):
    def test_the_file_is_good(self):
        self.assertEqual(m.check_pins(m.load_pins()), [])

    def test_problems(self):
        wine = dict(m.load_pins()["wine"])
        wine.update(url="http://example.com/other.tar.xz", sha256="nope", size=0, leave_out=["../x"], source=[])
        found = m.check_pins({"wine": wine})
        for text in ("wine.url", "wine.sha256", "wine.size", "wine.leave_out", "wine.source"):
            self.assertTrue(any(p.startswith(text) for p in found), (text, found))
        self.assertEqual(m.check_pins({}), ["wine: missing"])


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.tar = os.path.join(self.dir, "wine.tar.xz")
        self.out = os.path.join(self.dir, "out")

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_folder_without_the_parts_left_out(self):
        make_tar(self.tar, wine_entries())
        n = m.extract_wine(self.tar, INSIDE, ["share/wine/mono", "share/wine/gecko"], self.out)
        self.assertEqual(n, 3)
        with open(os.path.join(self.out, "bin", "wine"), "rb") as f:
            self.assertEqual(f.read(), b"#wine")
        self.assertFalse(os.path.exists(os.path.join(self.out, "share", "wine", "mono")))
        self.assertFalse(os.path.exists(os.path.join(self.out, "MacOS")))   # outside `inside`: not taken
        if os.name != "nt":
            self.assertTrue(os.stat(os.path.join(self.out, "bin", "wine")).st_mode & 0o100)

    def test_a_hard_link_is_a_copy(self):
        make_tar(self.tar, wine_entries() + [(f"{INSIDE}/bin/wine64", "hard", f"{INSIDE}/bin/wine", 0o755)])
        m.extract_wine(self.tar, INSIDE, [], self.out)
        with open(os.path.join(self.out, "bin", "wine64"), "rb") as f:
            self.assertEqual(f.read(), b"#wine")

    def test_links_must_stay_inside(self):
        for target in ("/usr/lib/libz.dylib", "../../outside", "../../../../etc/passwd"):
            make_tar(self.tar, [(f"{INSIDE}/lib/libz.dylib", "sym", target, 0o777)])
            with self.assertRaises(m.Error):
                m.extract_wine(self.tar, INSIDE, [], self.out)

    def test_a_path_that_climbs_out(self):
        make_tar(self.tar, [(f"{INSIDE}/lib/../../evil", "file", b"x", 0o644)])
        with self.assertRaises(m.Error):
            m.extract_wine(self.tar, INSIDE, [], self.out)

    @unittest.skipUnless(can_link(), "this system makes no symbolic links without rights")
    def test_a_link_inside_stays_a_link(self):
        make_tar(self.tar, wine_entries() + [(f"{INSIDE}/lib/libvk.dylib", "sym", "libMoltenVK.dylib", 0o777)])
        m.extract_wine(self.tar, INSIDE, [], self.out)
        self.assertEqual(os.readlink(os.path.join(self.out, "lib", "libvk.dylib")), "libMoltenVK.dylib")

    def test_nothing_inside(self):
        make_tar(self.tar, [("other/file", "file", b"x", 0o644)])
        with self.assertRaises(m.Error):
            m.extract_wine(self.tar, INSIDE, [], self.out)


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = self.tmp.name
        self.program = os.path.join(d, "program")
        program_folder(self.program)
        self.tar = os.path.join(d, "wine.tar.xz")
        make_tar(self.tar, wine_entries())
        with open(self.tar, "rb") as f:
            data = f.read()
        self.wine = dict(m.load_pins()["wine"], size=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.launcher = os.path.join(d, "launcher")
        with open(self.launcher, "wb") as f:
            f.write(LAUNCHER)
        self.out = os.path.join(d, "out")

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_app(self):
        app = m.build(self.program, self.tar, self.launcher, None, self.out, self.wine, "Game Name")
        contents = os.path.join(app, "Contents")
        with open(os.path.join(contents, "Info.plist"), "rb") as f:
            plist = plistlib.load(f)
        self.assertEqual(plist["CFBundleExecutable"], "Game Name")
        self.assertEqual(plist["CFBundleShortVersionString"], "0.2.1")   # no pre-release part
        self.assertEqual(plist["CFBundleIdentifier"], m.BUNDLE_ID)
        self.assertEqual(plist["LSArchitecturePriority"], ["x86_64"])
        self.assertNotIn("CFBundleIconFile", plist)
        self.assertTrue(os.path.isfile(os.path.join(contents, "MacOS", "Game Name")))
        self.assertTrue(os.path.isfile(os.path.join(contents, "Resources", "program", "Game.exe")))
        self.assertTrue(os.path.isfile(os.path.join(contents, "Resources", "wine", "bin", "wine")))
        self.assertFalse(os.path.exists(os.path.join(contents, "Resources", "wine", "share", "wine", "mono")))
        with open(os.path.join(contents, "Resources", "THIRD-PARTY.txt"), encoding="utf-8") as f:
            text = f.read()
        self.assertIn("libMoltenVK.dylib", text)
        self.assertIn(self.wine["sha256"], text)
        with self.assertRaises(m.Error):   # never over an app that is there
            m.build(self.program, self.tar, self.launcher, None, self.out, self.wine, "Game Name")

    def test_a_wine_build_that_is_not_the_pinned_one(self):
        with self.assertRaises(m.Error):
            m.build(self.program, self.tar, self.launcher, None, self.out, dict(self.wine, sha256="0" * 64), "G")

    def test_a_launcher_for_arm64(self):
        with open(self.launcher, "wb") as f:
            f.write(m.MACHO_64 + (0x0100000C).to_bytes(4, "little") + b"\0" * 24)
        with self.assertRaises(m.Error):
            m.build(self.program, self.tar, self.launcher, None, self.out, self.wine, "G")

    def test_a_folder_that_is_not_a_download(self):
        os.remove(os.path.join(self.program, "Game.exe"))
        with self.assertRaises(m.Error):
            m.build(self.program, self.tar, self.launcher, None, self.out, self.wine, "G")


if __name__ == "__main__":
    unittest.main()
