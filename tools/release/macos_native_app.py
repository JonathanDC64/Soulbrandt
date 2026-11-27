#!/usr/bin/env python3
"""The native Mac app (docs/178 route B, step M9): the Mac program in a macOS app, with the Vulkan loader and MoltenVK.

  python tools/release/macos_native_app.py build --program TAR --loader FILE --moltenvk FILE [--icns FILE] --out DIR
  python tools/release/macos_native_app.py check

`build` makes <out>/<PRODUCT_NAME>.app from:
  --program   package.py's DemonsSoulsRecomp-<version>-macos.tar.xz (--macos-host): the program, the FFmpeg libraries
              beside it and its setup/ folder;
  --loader    the Vulkan loader, libvulkan.1.dylib, built from the commit macos_native_app.json pins;
  --moltenvk  MoltenVK's release archive, checked against the SHA-256 and size macos_native_app.json pins;
  --icns      the app's icon (made with sips and iconutil on a Mac; without it the app has macOS's plain one).

The app:
  Contents/Info.plist, Contents/PkgInfo
  Contents/MacOS/<PRODUCT_NAME>                       the program (arm64)
  Contents/MacOS/setup                                a link to ../Resources/setup: the program finds setup's engine
                                                      in the setup folder beside it, as in the Linux download
  Contents/Resources/setup/                           the download's setup folder
  Contents/Resources/README.md, LICENSE, licenses/    the download's other files
  Contents/Frameworks/libvulkan.1.dylib               the Vulkan loader: the runtime looks here first (gpu_vk.c)
  Contents/Frameworks/libMoltenVK.dylib               Vulkan on Metal
  Contents/Frameworks/libavutil.N.dylib, libavcodec.N.dylib
                                                      the download's FFmpeg (the movies): the runtime looks here
                                                      first (ffmpeg_runtime.c)
  Contents/Resources/vulkan/icd.d/MoltenVK_icd.json   where the loader looks for a driver inside an app
  Contents/Resources/THIRD-PARTY.txt                  what else the app carries, its licences and sources

The player's files are not in the app: the program keeps them in ~/Library/Application Support/Soulbrandt. Nothing is
signed or zipped here: mac-native.yml signs the app ad hoc and zips it with ditto, on a Mac. `check` checks
macos_native_app.json. Standard library only. A tester build: no update reads it yet.
"""
import argparse
import json
import os
import plistlib
import posixpath
import re
import shutil
import struct
import sys
import tarfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from macos_app import BUNDLE_ID, VERSION_RE, Error, product_name, sha256_file  # noqa: E402

PINS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "macos_native_app.json")
MIN_MACOS = "13.3"                   # port/CMakeLists.txt's CMAKE_OSX_DEPLOYMENT_TARGET
INFO = "setup/release-info.txt"
SHA1_RE = re.compile(r"^[0-9a-f]{40}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
MACHO_64 = 0xFEEDFACF
FAT = 0xCAFEBABE
CPU_ARM64 = 0x0100000C


def load_pins(path=PINS):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def check_pins(pins):
    """macos_native_app.json's problems, as sentences."""
    problems = []
    mvk = pins.get("moltenvk") if isinstance(pins, dict) else None
    if not isinstance(mvk, dict):
        problems.append("moltenvk: missing")
    else:
        if not str(mvk.get("url", "")).startswith("https://") or not str(mvk.get("url", "")).endswith(
                "/" + str(mvk.get("name", "?"))):
            problems.append("moltenvk.url: not an https address ending in moltenvk.name")
        if not SHA256_RE.match(str(mvk.get("sha256", ""))):
            problems.append("moltenvk.sha256: not a SHA-256")
        if not isinstance(mvk.get("size"), int) or mvk["size"] <= 0:
            problems.append("moltenvk.size: not a size")
        for k in ("dylib", "icd", "licence"):
            v = mvk.get(k)
            if not isinstance(v, str) or not v or v.startswith("/") or ".." in v.split("/"):
                problems.append(f"moltenvk.{k}: not a path in the archive")
        if not mvk.get("source"):
            problems.append("moltenvk.source: where its source is must be listed")
    loader = pins.get("vulkan_loader") if isinstance(pins, dict) else None
    if not isinstance(loader, dict):
        problems.append("vulkan_loader: missing")
    else:
        for k in ("commit", "headers_commit"):
            if not SHA1_RE.match(str(loader.get(k, ""))):
                problems.append(f"vulkan_loader.{k}: not a commit")
        for k in ("repo", "headers_repo"):
            if not str(loader.get(k, "")).startswith("https://"):
                problems.append(f"vulkan_loader.{k}: not an https address")
    return problems


def macho_arches(data):
    """The CPU types of a Mach-O file (thin or fat), or [] for anything else."""
    if len(data) < 8:
        return []
    if struct.unpack("<I", data[:4])[0] == MACHO_64:
        return [struct.unpack("<I", data[4:8])[0]]
    if struct.unpack(">I", data[:4])[0] == FAT:
        n = struct.unpack(">I", data[4:8])[0]
        return [struct.unpack(">I", data[8 + 20 * i:12 + 20 * i])[0] for i in range(min(n, 16))]
    return []


def need_arm64(data, what):
    if CPU_ARM64 not in macho_arches(data):
        raise Error(f"{what}: not a Mach-O file for arm64")


def read_program(tar_path):
    """The macOS download's entries: (top folder, version, program name, {path below the top: (mode, data)})."""
    files = {}
    tops = set()
    with tarfile.open(tar_path) as tf:
        for m in tf.getmembers():
            name = m.name
            if name.startswith("/") or ".." in name.split("/"):
                raise Error(f"--program: {name} is outside the archive's folder")
            if m.isdir():
                continue
            if not m.isfile():
                raise Error(f"--program: {name} is not a regular file")
            top, _, rel = name.partition("/")
            tops.add(top)
            files[rel] = (m.mode & 0o777, tf.extractfile(m).read())
    if len(tops) != 1:
        raise Error(f"--program: expected one top folder, found {sorted(tops)}")
    info = files.get(INFO)
    if not info:
        raise Error(f"--program: no {INFO}: not package.py's macOS download")
    pairs = dict(line.split("=", 1) for line in info[1].decode("utf-8").splitlines()
                 if "=" in line and not line.startswith("#"))
    version, exe = pairs.get("version", ""), pairs.get("exe", "")
    if not VERSION_RE.match(version) or not exe or "/" in exe:
        raise Error(f"--program: {INFO} has no version or exe")
    if exe not in files:
        raise Error(f"--program: {exe} is not in the download")
    need_arm64(files[exe][1], f"--program: {exe}")
    return tops.pop(), version, exe, files


def read_moltenvk(tar_path, mvk):
    """MoltenVK's dylib, its ICD file and its licence, from the release archive the pins name."""
    if os.path.getsize(tar_path) != mvk["size"] or sha256_file(tar_path) != mvk["sha256"]:
        raise Error(f"--moltenvk: not {mvk['name']} as macos_native_app.json pins it (size or SHA-256 differs)")
    out = {}
    with tarfile.open(tar_path) as tf:
        for key in ("dylib", "icd", "licence"):
            try:
                m = tf.getmember(mvk[key])
            except KeyError:
                raise Error(f"--moltenvk: no {mvk[key]} in it") from None
            if not m.isfile():
                raise Error(f"--moltenvk: {mvk[key]} is not a regular file")
            out[key] = tf.extractfile(m).read()
    need_arm64(out["dylib"], "--moltenvk: libMoltenVK.dylib")
    return out


def icd_manifest(theirs):
    """MoltenVK's own ICD file, its library named from Resources/vulkan/icd.d (the loader reads a relative path from
    the file's own folder)."""
    try:
        d = json.loads(theirs.decode("utf-8"))
        d["ICD"]["library_path"] = "../../../Frameworks/libMoltenVK.dylib"
    except (ValueError, KeyError, TypeError) as e:
        raise Error(f"--moltenvk: its ICD file is not one ({e})") from None
    return (json.dumps(d, indent=4) + "\n").encode("utf-8")


def info_plist(product, version, icon):
    """Info.plist's entries. The bundle's versions are X.Y.Z: macOS takes no pre-release part there."""
    short = VERSION_RE.match(version).group(1)
    plist = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": product,
        "CFBundleName": product,
        "CFBundleExecutable": product,
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": short,
        "CFBundleVersion": short,
        "LSMinimumSystemVersion": MIN_MACOS,
        "LSArchitecturePriority": ["arm64"],
        "LSApplicationCategoryType": "public.app-category.role-playing-games",
        "NSHighResolutionCapable": True,
        "NSSupportsAutomaticGraphicsSwitching": True,
    }
    if icon:
        plist["CFBundleIconFile"] = "AppIcon"
    return plist


def is_library(rel):
    """A library at the top of the download (the FFmpeg ones beside the program): the app's Contents/Frameworks."""
    return "/" not in rel and rel.endswith(".dylib")


def third_party_text(product, mvk, loader, ffmpeg=False):
    lines = [
        f"{product} for macOS draws through Vulkan, which this app carries in Contents/Frameworks:",
        "",
        f"MoltenVK ({mvk.get('version', mvk['name'])}): libMoltenVK.dylib, Vulkan on Metal. Licence: Apache 2.0",
        "(Resources/licenses/MoltenVK/LICENSE).",
        f"This build: {mvk['url']} (SHA-256 {mvk['sha256']}). Source: {mvk['source']}",
        "",
        f"The Vulkan loader ({loader.get('tag', loader['commit'])}): libvulkan.1.dylib. Licence: Apache 2.0",
        "(https://github.com/KhronosGroup/Vulkan-Loader/blob/main/LICENSE.txt).",
        f"Built for this app from {loader['repo']} at {loader['commit']}.",
        "",
    ]
    if ffmpeg:
        lines += [
            "FFmpeg: libavcodec (only its MPEG-2 video decoder and parser) and libavutil, in Contents/Frameworks, for",
            "the movies. Licence: LGPL 2.1 or later; how it was built and where its source is:",
            "Resources/licenses/ffmpeg/README.txt. You may replace them with your own build of the same majors.",
            "",
        ]
    lines += [
        f"{product} itself: see Resources/LICENSE and Resources/licenses/.",
    ]
    return "\n".join(lines) + "\n"


def write(path, data, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    os.chmod(path, mode)


def build(program_tar, loader_path, moltenvk_tar, icns, out, pins, product):
    """Makes <out>/<product>.app (the module's docstring) and returns its path."""
    _top, version, exe, files = read_program(program_tar)
    with open(loader_path, "rb") as f:
        loader = f.read()
    need_arm64(loader, "--loader")
    mvk = read_moltenvk(moltenvk_tar, pins["moltenvk"])

    app = os.path.join(out, product + ".app")
    if os.path.exists(app):
        raise Error(f"{app} is there already: not writing over it")
    contents = os.path.join(app, "Contents")
    macos, res, fw = (os.path.join(contents, d) for d in ("MacOS", "Resources", "Frameworks"))
    for rel, (mode, data) in sorted(files.items()):
        if rel == exe:
            write(os.path.join(macos, product), data, 0o755)
        elif is_library(rel):
            need_arm64(data, f"--program: {rel}")
            write(os.path.join(fw, rel), data, 0o755)
        else:
            write(os.path.join(res, *rel.split("/")), data, 0o755 if mode & 0o100 else 0o644)
    if not os.path.isdir(os.path.join(res, "setup")):
        raise Error("--program: no setup folder in the download")
    os.symlink(posixpath.join("..", "Resources", "setup"), os.path.join(macos, "setup"))
    write(os.path.join(fw, "libvulkan.1.dylib"), loader, 0o755)
    write(os.path.join(fw, "libMoltenVK.dylib"), mvk["dylib"], 0o755)
    write(os.path.join(res, "vulkan", "icd.d", "MoltenVK_icd.json"), icd_manifest(mvk["icd"]))
    write(os.path.join(res, "licenses", "MoltenVK", "LICENSE"), mvk["licence"])
    ffmpeg = any(is_library(rel) and rel.startswith("libav") for rel in files)
    write(os.path.join(res, "THIRD-PARTY.txt"),
          third_party_text(product, pins["moltenvk"], pins["vulkan_loader"], ffmpeg).encode("utf-8"))
    if icns:
        shutil.copyfile(icns, os.path.join(res, "AppIcon.icns"))
    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(info_plist(product, version, bool(icns)), f)
    with open(os.path.join(contents, "PkgInfo"), "w", encoding="ascii", newline="") as f:
        f.write("APPL????")
    print(f"macos_native_app: {app}: program {version} ({exe} as {product}), {len(files)} files, "
          f"{pins['moltenvk'].get('version', 'MoltenVK')}, the loader at {pins['vulkan_loader']['commit'][:12]}")
    return app


def cmd_build(args):
    pins = load_pins()
    problems = check_pins(pins)
    if problems:
        raise Error("macos_native_app.json: " + "; ".join(problems))
    build(args.program, args.loader, args.moltenvk, args.icns, args.out, pins, product_name())
    return 0


def cmd_check(_args):
    problems = check_pins(load_pins())
    for p in problems:
        print(f"macos_native_app.json: {p}", file=sys.stderr)
    if not problems:
        print("macos_native_app.json: ok")
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="make the app")
    b.add_argument("--program", required=True, help="package.py's -macos.tar.xz")
    b.add_argument("--loader", required=True, help="libvulkan.1.dylib, built at the pinned commit")
    b.add_argument("--moltenvk", required=True, help="MoltenVK's release archive (macos_native_app.json)")
    b.add_argument("--icns", help="the app's icon")
    b.add_argument("--out", required=True, help="the folder the app goes in")
    b.set_defaults(fn=cmd_build)
    c = sub.add_parser("check", help="check macos_native_app.json")
    c.set_defaults(fn=cmd_check)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except Error as e:
        print(f"macos_native_app: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
