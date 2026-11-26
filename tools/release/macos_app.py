#!/usr/bin/env python3
"""The Mac app: the Windows program in a macOS app that carries Wine (docs/178 section 1).

  python tools/release/macos_app.py build --program DIR --wine FILE --launcher FILE [--icns FILE] --out DIR
  python tools/release/macos_app.py check

`build` makes <out>/<PRODUCT_NAME>.app from:
  --program   the Windows download's folder, unpacked (its setup/release-info.txt names the exe and the version);
  --wine      the Wine build macos_app.json pins, checked against its SHA-256 and size;
  --launcher  tools/release/macos/launcher.c built for x86-64 (the app's own program: macOS then offers Rosetta);
  --icns      the app's icon (made with sips and iconutil on a Mac; without it the app has macOS's plain one).

The app:
  Contents/Info.plist, Contents/PkgInfo
  Contents/MacOS/<PRODUCT_NAME>      the launcher
  Contents/Resources/wine/           Wine's folder from the build, without the parts macos_app.json leaves out
  Contents/Resources/program/        the Windows program's files, unchanged
  Contents/Resources/THIRD-PARTY.txt what else the app carries, its licences and where its sources are

Nothing is signed or zipped here: the workflow (mac-app.yml) signs the app ad hoc and zips it with ditto, on a Mac.
`check` checks macos_app.json. Standard library only.
"""
import argparse
import hashlib
import json
import os
import plistlib
import posixpath
import re
import shutil
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PINS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "macos_app.json")
NAME = "DemonsSoulsRecomp"                    # the technical name (package.py's NAME)
BUNDLE_ID = f"org.{NAME.lower()}.game"         # fixed: a rename of the project must not make it another app
MIN_MACOS = "11.0"
INFO = "setup/release-info.txt"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
VERSION_RE = re.compile(r"^([0-9]+\.[0-9]+\.[0-9]+)(?:-[0-9A-Za-z.-]+)?$")
MACHO_64 = b"\xcf\xfa\xed\xfe"
CPU_X86_64 = 0x01000007


class Error(Exception):
    pass


def product_name():
    with open(os.path.join(ROOT, "PRODUCT_NAME"), encoding="utf-8") as f:
        return f.readline().strip()


def load_pins(path=PINS):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def check_pins(pins):
    """macos_app.json's problems, as sentences."""
    problems = []
    wine = pins.get("wine") if isinstance(pins, dict) else None
    if not isinstance(wine, dict):
        return ["wine: missing"]
    if not str(wine.get("url", "")).startswith("https://") or not str(wine.get("url", "")).endswith(
            "/" + str(wine.get("name", "?"))):
        problems.append("wine.url: not an https address ending in wine.name")
    if not SHA256_RE.match(str(wine.get("sha256", ""))):
        problems.append("wine.sha256: not a SHA-256")
    if not isinstance(wine.get("size"), int) or wine["size"] <= 0:
        problems.append("wine.size: not a size")
    if not str(wine.get("inside", "")).endswith("/wine"):
        problems.append("wine.inside: not the build's wine folder")
    leave = wine.get("leave_out")
    if not isinstance(leave, list) or not all(isinstance(p, str) and p and ".." not in p.split("/") for p in leave):
        problems.append("wine.leave_out: not a list of folders")
    if not isinstance(wine.get("source"), list) or not wine["source"]:
        problems.append("wine.source: where the sources are must be listed (Wine is LGPL)")
    return problems


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def release_info(program):
    """The Windows program folder's version and exe (setup/release-info.txt)."""
    path = os.path.join(program, *INFO.split("/"))
    try:
        with open(path, encoding="utf-8") as f:
            pairs = dict(line.strip().split("=", 1) for line in f if "=" in line and not line.startswith("#"))
    except OSError as e:
        raise Error(f"--program: no {INFO} ({e}): not a Windows download's folder") from e
    version, exe = pairs.get("version", ""), pairs.get("exe", "")
    if not VERSION_RE.match(version) or not exe.endswith(".exe") or "/" in exe or "\\" in exe:
        raise Error(f"--program: {INFO} has no version or exe")
    if not os.path.isfile(os.path.join(program, exe)):
        raise Error(f"--program: {exe} is not in the folder")
    return version, exe


def check_launcher(path):
    """The launcher is a 64-bit Mach-O program for x86-64 (so macOS offers Rosetta for it)."""
    with open(path, "rb") as f:
        head = f.read(8)
    if len(head) < 8 or head[:4] != MACHO_64 or int.from_bytes(head[4:8], "little") != CPU_X86_64:
        raise Error("--launcher: not a Mach-O program for x86-64 (build launcher.c with -arch x86_64)")


def extract_wine(tar_path, inside, leave_out, dest):
    """Writes the tar's `inside` folder into `dest`, without `leave_out` (paths inside it). Symbolic links stay links,
    and may point only within the folder. The number of entries written."""
    prefix = inside.strip("/") + "/"
    count = 0
    with tarfile.open(tar_path, "r:*") as tar:
        for m in tar:
            name = (m.name[2:] if m.name.startswith("./") else m.name).rstrip("/")
            if not name.startswith(prefix):
                continue
            rel = name[len(prefix):]
            if not rel or any(rel == d or rel.startswith(d.rstrip("/") + "/") for d in leave_out):
                continue
            parts = rel.split("/")
            if any(p in ("", ".", "..") for p in parts):
                raise Error(f"--wine: {m.name}: not a plain path")
            target = os.path.join(dest, *parts)
            if m.isdir():
                os.makedirs(target, exist_ok=True)
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if m.issym():
                link = posixpath.normpath(posixpath.join(posixpath.dirname(rel), m.linkname))
                if m.linkname.startswith("/") or link == ".." or link.startswith("../"):
                    raise Error(f"--wine: {m.name} points outside the folder ({m.linkname})")
                os.symlink(m.linkname, target)
            elif m.islnk():
                src = m.linkname.rstrip("/")
                if not src.startswith(prefix):
                    raise Error(f"--wine: {m.name} is a hard link to {m.linkname}, outside the folder")
                shutil.copy2(os.path.join(dest, *src[len(prefix):].split("/")), target)
            elif m.isfile():
                with tar.extractfile(m) as fin, open(target, "wb") as fout:
                    shutil.copyfileobj(fin, fout)
                os.chmod(target, 0o755 if m.mode & 0o111 else 0o644)
            else:
                raise Error(f"--wine: {m.name}: not a file, folder or link")
            count += 1
    if not count:
        raise Error(f"--wine: nothing in {inside}")
    return count


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
        "LSArchitecturePriority": ["x86_64"],          # the launcher and Wine are Intel programs: Rosetta
        "LSApplicationCategoryType": "public.app-category.role-playing-games",
        "NSHighResolutionCapable": True,
        "NSSupportsAutomaticGraphicsSwitching": True,
    }
    if icon:
        plist["CFBundleIconFile"] = "AppIcon"
    return plist


def third_party_text(product, wine, libs):
    lines = [
        f"{product} for macOS runs the Windows program in Wine, which this app carries in Resources/wine.",
        "",
        f"Wine: {wine.get('version', wine['name'])}. Licence: GNU LGPL 2.1 or later (https://www.winehq.org/license).",
        f"This build: {wine['url']} (SHA-256 {wine['sha256']}).",
        "Sources:",
    ]
    lines += [f"  {s}" for s in wine["source"]]
    lines += [
        "",
        "MoltenVK (Resources/wine/lib/libMoltenVK.dylib), Vulkan on Metal. Licence: Apache 2.0",
        "(https://github.com/KhronosGroup/MoltenVK/blob/main/LICENSE).",
        "",
        "The build's other libraries, in Resources/wine/lib, come from MacPorts with their own licences (among them",
        "the GNU LGPL, GPL with exceptions, BSD, MIT, zlib and ICU licences); their sources are MacPorts' ports",
        "(https://ports.macports.org) at the versions in their names:",
    ]
    lines += [f"  {n}" for n in libs]
    lines += ["", f"{product} itself: see Resources/program/LICENSE."]
    return "\n".join(lines) + "\n"


def build(program, wine_path, launcher_path, icns, out, wine, product):
    """Makes <out>/<product>.app (the module's docstring) and returns its path. wine: macos_app.json's "wine"."""
    version, exe = release_info(program)
    check_launcher(launcher_path)
    if os.path.getsize(wine_path) != wine["size"] or sha256_file(wine_path) != wine["sha256"]:
        raise Error(f"--wine: not {wine['name']} as macos_app.json pins it (size or SHA-256 differs)")

    app = os.path.join(out, product + ".app")
    if os.path.exists(app):
        raise Error(f"{app} is there already: not writing over it")
    contents = os.path.join(app, "Contents")
    res = os.path.join(contents, "Resources")
    os.makedirs(os.path.join(contents, "MacOS"))
    os.makedirs(res)

    entries = extract_wine(wine_path, wine["inside"], wine["leave_out"], os.path.join(res, "wine"))
    if not os.path.isfile(os.path.join(res, "wine", "bin", "wine")):
        raise Error("--wine: the build has no bin/wine")
    shutil.copytree(program, os.path.join(res, "program"), symlinks=True)
    launcher = os.path.join(contents, "MacOS", product)
    shutil.copyfile(launcher_path, launcher)
    os.chmod(launcher, 0o755)
    if icns:
        shutil.copyfile(icns, os.path.join(res, "AppIcon.icns"))
    with open(os.path.join(contents, "Info.plist"), "wb") as f:
        plistlib.dump(info_plist(product, version, bool(icns)), f)
    with open(os.path.join(contents, "PkgInfo"), "w", encoding="ascii", newline="") as f:
        f.write("APPL????")
    lib = os.path.join(res, "wine", "lib")
    libs = sorted(n for n in os.listdir(lib) if n.endswith(".dylib")) if os.path.isdir(lib) else []
    with open(os.path.join(res, "THIRD-PARTY.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(third_party_text(product, wine, libs))
    print(f"macos_app: {app}: program {version} ({exe}), {entries} entries of {wine['name']}")
    return app


def cmd_build(args):
    pins = load_pins()
    problems = check_pins(pins)
    if problems:
        raise Error("macos_app.json: " + "; ".join(problems))
    build(args.program, args.wine, args.launcher, args.icns, args.out, pins["wine"], product_name())
    return 0


def cmd_check(_args):
    problems = check_pins(load_pins())
    for p in problems:
        print(f"macos_app.json: {p}", file=sys.stderr)
    if not problems:
        print("macos_app.json: the Wine build is pinned")
    return 1 if problems else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="make the app")
    b.add_argument("--program", required=True, help="the Windows download's folder, unpacked")
    b.add_argument("--wine", required=True, help="the Wine build macos_app.json pins")
    b.add_argument("--launcher", required=True, help="launcher.c, built for x86-64")
    b.add_argument("--icns", help="the app's icon")
    b.add_argument("--out", required=True, help="the folder the app goes in")
    b.set_defaults(func=cmd_build)
    c = sub.add_parser("check", help="check macos_app.json")
    c.set_defaults(func=cmd_check)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except Error as e:
        print(f"macos_app: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
