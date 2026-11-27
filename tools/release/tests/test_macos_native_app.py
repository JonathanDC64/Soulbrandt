"""Tests for the native Mac app's build (tools/release/macos_native_app.py), on a made-up program download, loader and
MoltenVK archive.

  python -m unittest discover -s tools/release/tests
"""
import hashlib
import io
import json
import os
import plistlib
import struct
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import macos_native_app as n  # noqa: E402

ARM64 = struct.pack("<II", n.MACHO_64, n.CPU_ARM64) + b"\0" * 24
X86 = struct.pack("<II", n.MACHO_64, 0x01000007) + b"\0" * 24
FAT = struct.pack(">II", n.FAT, 2) + struct.pack(">5I", 0x01000007, 3, 4096, 32, 12) + \
    struct.pack(">5I", n.CPU_ARM64, 0, 8192, 32, 14)
TOP = "DemonsSoulsRecomp-0.2.1-dev+gabc"


def can_link():
    with tempfile.TemporaryDirectory() as d:
        try:
            os.symlink("a", os.path.join(d, "b"))
            return True
        except (OSError, NotImplementedError):
            return False


def make_tar(path, files, mode="w:xz"):
    with tarfile.open(path, mode) as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mode = 0o755 if name.endswith("/TestGame") else 0o644
            tar.addfile(info, io.BytesIO(data))


class NativeAppTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.d = Path(self.tmp.name)
        self.program = self.d / "program.tar.xz"
        make_tar(self.program, {
            f"{TOP}/TestGame": ARM64,
            f"{TOP}/README.md": b"readme\n",
            f"{TOP}/LICENSE": b"licence\n",
            f"{TOP}/setup/release-info.txt": b"# x\nversion=0.2.1-dev+gabc\ncommit=abc\nexe=TestGame\n",
            f"{TOP}/setup/tools/setup/run_setup.py": b"print('setup')\n",
            f"{TOP}/libavutil.60.dylib": ARM64,
            f"{TOP}/libavcodec.62.dylib": ARM64,
            f"{TOP}/licenses/ffmpeg/README.txt": b"ffmpeg\n",
        })
        self.loader = self.d / "libvulkan.1.dylib"
        self.loader.write_bytes(ARM64)
        icd = json.dumps({"file_format_version": "1.0.0",
                          "ICD": {"library_path": "./libMoltenVK.dylib", "api_version": "1.4.0",
                                  "is_portability_driver": True}}).encode()
        self.mvk = self.d / "MoltenVK-macos.tar"
        make_tar(self.mvk, {"MoltenVK/LICENSE": b"apache\n",
                            "MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib": FAT,
                            "MoltenVK/MoltenVK/dynamic/dylib/macOS/MoltenVK_icd.json": icd}, "w")
        data = self.mvk.read_bytes()
        self.pins = n.load_pins()
        self.pins["moltenvk"] = dict(self.pins["moltenvk"], size=len(data), sha256=hashlib.sha256(data).hexdigest())

    def build(self, **kw):
        args = dict(program_tar=self.program, loader_path=self.loader, moltenvk_tar=self.mvk, icns=None,
                    out=self.d / "out", pins=self.pins, product="TestGame")
        args.update(kw)
        return Path(n.build(**args))

    def test_pins(self):
        self.assertEqual(n.check_pins(n.load_pins()), [])
        self.assertTrue(n.check_pins({"moltenvk": {"url": "http://x"}, "vulkan_loader": {"commit": "main"}}))

    def test_macho_arches(self):
        self.assertEqual(n.macho_arches(ARM64), [n.CPU_ARM64])
        self.assertEqual(n.macho_arches(FAT), [0x01000007, n.CPU_ARM64])
        self.assertEqual(n.macho_arches(b"\x7fELF" + b"\0" * 12), [])

    @unittest.skipUnless(can_link(), "needs symbolic links")
    def test_the_app(self):
        app = self.build()
        c = app / "Contents"
        self.assertEqual((c / "MacOS" / "TestGame").read_bytes(), ARM64)
        self.assertEqual(os.readlink(c / "MacOS" / "setup"), "../Resources/setup")
        self.assertTrue((c / "Resources" / "setup" / "tools" / "setup" / "run_setup.py").is_file())
        self.assertTrue((c / "Resources" / "README.md").is_file())
        self.assertFalse((c / "Resources" / "TestGame").exists())
        self.assertEqual((c / "Frameworks" / "libvulkan.1.dylib").read_bytes(), ARM64)
        self.assertEqual((c / "Frameworks" / "libMoltenVK.dylib").read_bytes(), FAT)
        icd = json.loads((c / "Resources" / "vulkan" / "icd.d" / "MoltenVK_icd.json").read_text(encoding="utf-8"))
        self.assertEqual(icd["ICD"]["library_path"], "../../../Frameworks/libMoltenVK.dylib")
        self.assertTrue(icd["ICD"]["is_portability_driver"])
        # (the ICD file's library, from the file's own folder, is the app's MoltenVK)
        lib = (c / "Resources" / "vulkan" / "icd.d" / icd["ICD"]["library_path"]).resolve()
        self.assertEqual(lib, (c / "Frameworks" / "libMoltenVK.dylib").resolve())
        self.assertEqual((c / "Resources" / "licenses" / "MoltenVK" / "LICENSE").read_bytes(), b"apache\n")
        # (the download's FFmpeg beside the program goes in Frameworks, its licence stays in Resources)
        for lib in ("libavutil.60.dylib", "libavcodec.62.dylib"):
            self.assertEqual((c / "Frameworks" / lib).read_bytes(), ARM64)
            self.assertFalse((c / "Resources" / lib).exists())
        self.assertTrue((c / "Resources" / "licenses" / "ffmpeg" / "README.txt").is_file())
        with open(c / "Info.plist", "rb") as f:
            p = plistlib.load(f)
        self.assertEqual((p["CFBundleExecutable"], p["CFBundleShortVersionString"]), ("TestGame", "0.2.1"))
        self.assertEqual(p["LSMinimumSystemVersion"], "13.3")
        self.assertEqual(p["LSArchitecturePriority"], ["arm64"])
        third = (c / "Resources" / "THIRD-PARTY.txt").read_text(encoding="utf-8")
        self.assertIn("MoltenVK 1.4.2", third)
        self.assertIn("FFmpeg", third)
        if os.name != "nt":
            self.assertTrue(os.access(c / "MacOS" / "TestGame", os.X_OK))
        with self.assertRaises(n.Error):
            self.build()        # (never over an app that is there)

    def test_refusals(self):
        self.loader.write_bytes(X86)
        with self.assertRaises(n.Error):
            self.build(out=self.d / "o1")
        self.loader.write_bytes(ARM64)
        bad = dict(self.pins["moltenvk"], sha256="0" * 64)
        with self.assertRaises(n.Error):
            self.build(out=self.d / "o2", pins=dict(self.pins, moltenvk=bad))
        x86_program = self.d / "x86.tar.xz"
        make_tar(x86_program, {f"{TOP}/TestGame": X86,
                               f"{TOP}/setup/release-info.txt": b"version=0.2.1\nexe=TestGame\n"})
        with self.assertRaises(n.Error):
            self.build(out=self.d / "o3", program_tar=x86_program)


if __name__ == "__main__":
    unittest.main()
