"""The update a disc needs before the port can use it (the Japanese and Asian discs' 1.04, docs/140).

Those two discs are an earlier build of the game than the US and European discs. Their 1.04 update brings the program
and its data up to the US disc's, so setup asks for it. It takes the update as either of the forms a player has it in:

  - the update's .pkg file, as Sony's servers give it. Setup has no key and no decryptor (docs/51), so the player's
    RPCS3 installs it, in a folder of setup's own (rpcs3.install_package);
  - the folder a PS3 or RPCS3 installed it to (dev_hdd0/game/<TITLE_ID>, with PARAM.SFO and USRDIR/).

Either way its files end up in <data>/update/<TITLE_ID>, the layout of the installed folder. A later rebuild needs
nothing from the player. The game reads them there in place of the disc's copies (PS3_VFS_UPDATE, ps3recomp's
vfs_path.h). The game table has every file's size and SHA-256 (the "update" of the disc's entry), so what is there is
exactly the update the port was made for.

RPCS3 decrypts a package's .sdat files as it installs them and keeps their names, so in a folder it installed they are
plain. The runtime reads a plain .sdat as it is. So a .sdat is good in either form: as the update's list has it, or as
the table's decrypted file (`plain`, gamefiles.plain_forms).
"""
import hashlib
import os
import shutil
from pathlib import Path

from . import sfo

FOLDER = "update"        # <data>/update/<TITLE_ID>


class UpdateError(ValueError):
    """Not the update it should be. The message is the sentence for the player."""


# ---- finding it ----

def sha256(path: Path, block=1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(block)
            if not b:
                return h.hexdigest()
            h.update(b)


def _matches(path: Path, e: dict) -> bool:
    """`path` has the size and sum of the table's entry `e`."""
    return path.stat().st_size == e["size"] and sha256(path) == e["sha256"]


def _is_pkg(path: Path, want: dict) -> bool:
    """The package with the table's size and sum."""
    try:
        return _matches(path, want)
    except OSError:
        return False


def _installed(folder: Path, title: str, want: dict) -> bool:
    """A folder holding the installed update: its PARAM.SFO has this title at the update's version."""
    try:
        vals = sfo.parse((folder / "PARAM.SFO").read_bytes())
    except (OSError, sfo.SfoError):
        return False
    return vals.get("TITLE_ID") == title and vals.get("APP_VER") == want["app_ver"] and (folder / "USRDIR").is_dir()


def find_in(place: Path, title: str, want: dict):
    """The update in `place`: a .pkg file, the installed folder itself, a folder of it named for the title, or a .pkg
    among the folder's files. ("pkg" or "folder", its path), or None."""
    place = Path(place)
    if place.is_file():
        return ("pkg", place) if _is_pkg(place, want["pkg"]) else None
    if not place.is_dir():
        return None
    for folder in (place, place / title):
        if _installed(folder, title, want):
            return "folder", folder
    try:
        names = sorted(n for n in os.listdir(place) if n.lower().endswith(".pkg"))
    except OSError:
        return None
    for n in names:
        if _is_pkg(place / n, want["pkg"]):
            return "pkg", place / n
    return None


def places(game_root: Path, given, hdd0=None) -> list:
    """Where to look, in order: what the player gave; RPCS3's dev_hdd0/game (the RPCS3 route); the game folder and the
    two folders above it, where a package is often kept beside the dump."""
    out = [Path(given)] if given else []
    if hdd0:
        out.append(Path(hdd0) / "game")
    if game_root:
        root = Path(game_root)
        out += [root, root.parent, root.parent.parent]
    return out


def bad_files(folder: Path, want: dict, progress=None, plain=None) -> list:
    """The update's files in `folder` that are missing or not the table's: their paths. plain: {a file's path: the
    table's entry for its decrypted form}, the other form it may be in (the module's docstring)."""
    plain = plain or {}
    bad = []
    for e in want["files"]:
        p = folder.joinpath(*e["path"].split("/"))
        try:
            if not (_matches(p, e) or (e["path"] in plain and _matches(p, plain[e["path"]]))):
                bad.append(e["path"])
        except OSError:
            bad.append(e["path"])
        if progress:
            progress(e["size"])
    return bad


def install(folder: Path, dest: Path, want: dict, progress=None, plain=None):
    """The update in `folder`, an installed one, into dest, checked file by file. A folder replaced only when it is
    whole, so a run stopped here starts again. Raises UpdateError."""
    tmp = dest.with_name(dest.name + ".tmp")
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    try:
        for e in want["files"]:
            s = Path(folder).joinpath(*e["path"].split("/"))
            d = tmp.joinpath(*e["path"].split("/"))
            d.parent.mkdir(parents=True, exist_ok=True)
            try:
                shutil.copyfile(s, d)
            except OSError as err:
                raise UpdateError(f"{e['path']} could not be copied from the update's folder ({err}).")
        wrong = bad_files(tmp, want, progress, plain)
        if wrong:
            raise UpdateError(f"{len(wrong)} of the update's files do not match, the first {wrong[0]}. "
                              "Get the update again.")
        if dest.exists():
            shutil.rmtree(dest)
        os.replace(tmp, dest)
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise
