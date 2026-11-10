"""Check the player's game folder: BLUS30443 or BLES00932, version 1.00, or BCJS30022 or BCAS20071 with their 1.04
update, by SHA-256.

Setup accepts only files the player has already decrypted (docs/51, plan (c)):
EBOOT.elf beside EBOOT.BIN and a .sdat.unedat beside each of the 26 .sdat
files, as RPCS3's Decrypt PS3 Binaries writes them, or all of them in one
folder the player picks. It never decrypts anything itself. On the RPCS3
route (docs/90) the player's own RPCS3 makes the missing ones in the unlock
phase (rpcs3.py), and libsre.prx too when RPCS3 has the PS3 system software,
so the check only says which.

The firmware's libsre.prx (one file of the PS3 system software) is optional
(docs/133): the game runs the part of libsre it used on ps3recomp's own code.
A good copy, given or found, is still staged, so the build links the lifted
libsre for the LIBSRE_OWN=0 fallback. When none is found there is no item;
one that is not right is "unused", which is not a problem: the build goes on
without it.

The table (game_table.json) holds sizes and SHA-256 sums only. A decrypted
file whose sum is not in the table yet (null) is checked by its first bytes
instead, and reported as such. Its top level is the US disc's; "titles" has the
European disc's, as the files that differ from the US disc's (for_title).

The rest of the disc's files are checked by name and size against the disc's
list (disc.py, issue #418): Report.disc, a warning and not a problem.

A disc whose table has an "update" (the Japanese and Asian discs, docs/139) is
used with that update: the files marked "from": "update" are looked for in
the update's folder (game_update.py), and an item of kind "update" says
whether it is there.

The US and European discs also come as PlayStation Store versions, which have
a "digital" block in the table (the store copy's own PARAM.SFO and EBOOT.BIN;
digital_of tells one by its marker file or its EBOOT.BIN's size). The store
copy is that disc's program and data: its EBOOT.elf is the disc's, and after
RPCS3 installs the package its 26 .sdat files are plain, the table's decrypted
files under their own names. The player decrypts its EBOOT.BIN in RPCS3, which
leaves EBOOT.elf beside it; until then the check says so (state "decrypt").
"""
import hashlib
import json
import os
from dataclasses import dataclass, field
from pathlib import Path

from . import disc as disc_files
from . import firmware as fwver
from . import game_update
from . import sfo

TABLE = Path(__file__).with_name("game_table.json")

# First bytes of files that are still encrypted.
ENCRYPTED_MAGIC = {b"SCE\0": "an encrypted SELF", b"NPD\0": "an encrypted SDATA file"}
# The end of an optional file's sentence when setup leaves it out (the firmware's, docs/133).
LEFT_OUT = "The game does not need it; setup leaves it out."
UPDATE_MISSING = ("{disc} needs its {ver} update. Put the update's .pkg file beside your disc copy, or choose it or the "
                  "folder your PS3 or RPCS3 installed it to.")
EBOOT_BIN = "PS3_GAME/USRDIR/EBOOT.BIN"
STORE_MARK = "ISO2PKG.DAT"   # beside PARAM.SFO in a PlayStation Store copy of a disc, never on the disc
# The store copy's EBOOT.elf is the player's to make: a state of its own, which the unlock does not take.
DECRYPT = "decrypt"
DECRYPT_FIRST = ("In RPCS3, open Utilities, then Decrypt PS3 Binaries, and choose {eboot}. It writes EBOOT.elf beside "
                 "it. Then check again.")
DECRYPT_AGAIN = "EBOOT.elf does not match. " + DECRYPT_FIRST


@dataclass
class Item:
    kind: str        # "game", "update", "original", "decrypted", "firmware"
    name: str        # the table's path or name
    state: str       # "ok", "unmeasured", "missing", "mismatch", "encrypted", "wrong", "decrypt" (the store copy's
                     # EBOOT.elf, the player's to make), or "unused": an optional file (the firmware's) that is not
                     # right, so the build goes on without it
    message: str     # what to tell the player, in plain words
    found: str = ""  # where the file was found
    sha256: str = "" # its SHA-256, when it was hashed
    unlock: bool = False  # the RPCS3 route: the unlock makes this file, so it is not the player's to fix
    fix: str = ""    # the firmware's file from another system software version: the table's version (hex) to set in
                     # it as it is staged (firmware.py); its sha256 is then the table's

    @property
    def good(self) -> bool:
        return self.state in ("ok", "unmeasured")

    @property
    def unlock_makes(self) -> bool:
        """On the RPCS3 route the unlock makes this file, so it is not the player's to fix. Not a store copy's
        EBOOT.elf, which is (state "decrypt")."""
        return self.unlock or (self.kind == "decrypted" and self.state != DECRYPT)


@dataclass
class Report:
    root: Path = None
    items: list = field(default_factory=list)
    table: dict = None   # the disc's table (for_title), once PARAM.SFO is read
    disc: object = None  # every file of the disc copy against the disc's list (disc.py), once the title is known: a
                         # warning only, so not one of problems()
    digital: dict = None # the store copy's table block when this is one (digital_of), else None

    @property
    def title_id(self) -> str:
        return (self.table or {}).get("title_id", "")

    @property
    def ok(self) -> bool:
        return self.root is not None and bool(self.items) and not self.problems()

    def problems(self) -> list:
        return [i for i in self.items if not i.good and i.state != "unused"]

    def inputs(self) -> list:
        """The decrypted files and the firmware file the build reads: their items."""
        return [i for i in self.items if i.kind in ("decrypted", "firmware") and i.good]


def load_table(path=None) -> dict:
    with open(path or TABLE, encoding="utf-8") as f:
        return json.load(f)


def sha256_file(path, progress=None, bufsize=1 << 20) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(bufsize)
            if not b:
                break
            h.update(b)
            if progress:
                progress(len(b))
    return h.hexdigest()


def find_root(folder) -> Path:
    """The folder that holds PS3_GAME, given that folder, PS3_GAME itself, or None."""
    p = Path(folder)
    if _find_ci(p, "PS3_GAME") and _find_ci(p / _find_ci(p, "PS3_GAME"), "PARAM.SFO"):
        return p
    if p.name.upper() == "PS3_GAME" and _find_ci(p, "PARAM.SFO"):
        return p.parent
    return None


def title_of(folder) -> str:
    """The TITLE_ID of the game in a folder (find_root's, or a disc game kept in dev_hdd0/game: installed_disc), or ""
    when there is none or it cannot be read."""
    root = find_root(folder) if folder else None
    if root is None:
        return installed_disc(folder)
    p = _resolve(root, "PS3_GAME/PARAM.SFO")
    try:
        return sfo.parse(p.read_bytes()).get("TITLE_ID", "") if p else ""
    except (OSError, sfo.SfoError):
        return ""


def version_of(folder) -> str:
    """The APP_VER of the game in a folder, as title_of finds it, or "" (an ISO file, one that cannot be read)."""
    if not folder or not Path(folder).is_dir():
        return ""
    root = find_root(folder)
    p = _resolve(root, "PS3_GAME/PARAM.SFO") if root is not None else _resolve(Path(folder), "PARAM.SFO")
    try:
        return sfo.parse(p.read_bytes()).get("APP_VER", "") if p else ""
    except (OSError, sfo.SfoError):
        return ""


def installed_disc(folder) -> str:
    """The TITLE_ID when `folder` is a disc game RPCS3 keeps in dev_hdd0/game: the files of PS3_GAME right in the
    folder, with no PS3_GAME folder (RPCS3's System.cpp, "Disc game located in dev_hdd0/game"). Its PARAM.SFO's
    CATEGORY is DG and it has USRDIR/EBOOT.BIN. "" for anything else, also for an update kept there (category GD)."""
    if not folder or find_root(folder) is not None:
        return ""
    p = _resolve(Path(folder), "PARAM.SFO")
    if p is None or _resolve(Path(folder), "USRDIR/EBOOT.BIN") is None:
        return ""
    try:
        vals = sfo.parse(p.read_bytes())
    except (OSError, sfo.SfoError):
        return ""
    return vals.get("TITLE_ID", "") if vals.get("CATEGORY") == "DG" else ""


def disc_view(game, view) -> Path:
    """`view` as a folder that holds PS3_GAME for a disc game kept in dev_hdd0/game (installed_disc): the check, the
    build and the game all read a disc's layout, and nothing may be written in RPCS3's folders. So PS3_GAME here is a
    link to that game's own folder: on Windows a junction, which needs no special rights, and elsewhere a symbolic
    link. One that points there already stays; one that points elsewhere (dev_hdd0 moved) is replaced. Raises OSError
    when the link cannot be made."""
    target, view = os.path.realpath(game), Path(view)
    link = view / "PS3_GAME"
    if os.path.lexists(link):
        if os.path.normcase(os.path.realpath(link)) == os.path.normcase(target):
            return view
        os.unlink(link)                     # (the link only, on Windows a junction too: never what it points to)
    view.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        import _winapi
        _winapi.CreateJunction(target, str(link))
    else:
        os.symlink(target, link, target_is_directory=True)
    return view


def _find_ci(folder: Path, name: str) -> str:
    """`name` in `folder`, ignoring case (a folder dump may say EBOOT.ELF or EBOOT.elf)."""
    if (folder / name).exists():
        return name
    try:
        low = name.lower()
        for e in os.listdir(folder):
            if e.lower() == low:
                return e
    except OSError:
        pass
    return ""


def _resolve(root: Path, rel: str) -> Path:
    """`rel` (a table path, "/"-separated) under root, matching each part ignoring case."""
    p = root
    for part in rel.split("/"):
        hit = _find_ci(p, part)
        if not hit:
            return None
        p = p / hit
    return p


def _find_decrypted(folder: Path, rel: str) -> Path:
    """A decrypted file in a folder the player picked: by its name at the top, or at
    its own path under the folder (RPCS3 writes beside each input, so a decrypted
    copy of USRDIR, PS3_GAME or the whole dump keeps the game's subfolders)."""
    hit = _find_ci(folder, rel.rsplit("/", 1)[-1])
    if hit:
        return folder / hit
    for cut in (rel, rel.split("PS3_GAME/", 1)[-1], rel.split("USRDIR/", 1)[-1]):
        p = _resolve(folder, cut)
        if p is not None:
            return p
    return None


def _magic(path: Path, n=4) -> bytes:
    with open(path, "rb") as f:
        return f.read(n)


def plain_forms(t) -> dict:
    """{a .sdat's path in the update: the table's entry for its decrypted form}. RPCS3 decrypts these as it installs an
    update and keeps their names, so that is the form they have in a folder it installed (game_update.py)."""
    return {d["of"].split("PS3_GAME/", 1)[-1]: d for d in t["decrypted"] if d["of"].endswith(".sdat")}


def _plain_in_update(up: Path, rel: str):
    """The update's file `rel` if it is there and not encrypted, else None."""
    p = _resolve(up, rel)
    return p if p is not None and p.is_file() and _magic(p) not in ENCRYPTED_MAGIC else None


def titles(t) -> list:
    """The TITLE_IDs of the discs the port supports: the US disc first."""
    return [t["title_id"]] + list(t.get("titles", {}))


def for_title(t, tid):
    """The table of the disc `tid`: the US disc's as it is, another supported disc's with its own files in place of
    the US disc's (its "files"), or None."""
    if tid == t["title_id"]:
        return t
    o = t.get("titles", {}).get(tid)
    if o is None:
        return None
    files = o.get("files", {})
    tt = dict(t, title_id=tid, app_ver=o["app_ver"], version=o["version"], disc=o["disc"],
              disc_files=o.get("disc_files"), update=o.get("update"), digital=o.get("digital"))
    for k in ("originals", "decrypted"):
        tt[k] = [dict(e, **files.get(e["path"], {})) for e in t[k]]
    return tt


def supported(t) -> str:
    """The supported discs in words: "the US disc (BLUS30443) or the European disc (BLES00932)"."""
    return " or ".join(f"{for_title(t, tid).get('disc', 'the US disc')} ({tid})" for tid in titles(t))


def other_title(t, tid) -> str:
    """The sentence for a copy of another title or version of the game, by its TITLE_ID."""
    what = t["other_titles"].get(tid, f"a game with the ID {tid or 'none'}")
    return f"This is {what} ({tid}). The port supports {supported(t)}."


def store_copy(folder) -> bool:
    """`folder` (the game's own folder in dev_hdd0/game, or the folder that holds PS3_GAME) has the marker file of a
    PlayStation Store copy of a disc."""
    if not folder:
        return False
    return any(_resolve(Path(folder), rel) is not None for rel in (STORE_MARK, "PS3_GAME/" + STORE_MARK))


def digital_of(root: Path, t: dict):
    """The table's "digital" block when the copy in `root` (the folder that holds PS3_GAME) is that disc's PlayStation
    Store version, else None: by its marker file, or its EBOOT.BIN's size if the marker was left behind."""
    d = t.get("digital")
    if not d:
        return None
    if _resolve(root, "PS3_GAME/" + STORE_MARK) is not None:
        return d
    p = _resolve(root, EBOOT_BIN)
    return d if p is not None and p.stat().st_size == d["files"][EBOOT_BIN]["size"] else None


def copy_key(folder) -> str:
    """One name for a copy of the game wherever it is reached from: its own folder's real path. `folder` is the folder
    that holds PS3_GAME, the game's folder in dev_hdd0/game, or setup's link folder (disc_view) to one."""
    p = Path(folder)
    inner = p / "PS3_GAME"
    if inner.is_dir():
        real = os.path.realpath(inner)
        p = Path(real).parent if Path(real).name.upper() == "PS3_GAME" else Path(real)
    return os.path.normcase(os.path.realpath(p))


def check(folder, decrypted_dirs=(), firmware=None, table=None, progress=None, unlocks=False, also=None,
          firmware_unlock=None, update=None) -> Report:
    """Check a game folder. decrypted_dirs: more folders to look in for the
    decrypted files, by file name. firmware: the decrypted libsre.prx, or None
    to look for it by name in the game's folder and in decrypted_dirs (and
    also's places). progress(bytes) is called while hashing.
    unlocks: the RPCS3 route, where the unlock makes a missing decrypted file
    (the sentences say so). also: {table path or firmware name: [files]}, more
    files to try (PPU Debug's dump of the EBOOT, a copy an earlier run kept).
    A file is good when any of the places has a good copy.
    firmware_unlock, on the RPCS3 route: True when RPCS3 has the PS3 system
    software, so the unlock makes a missing or wrong libsre.prx; False when it
    has none (or the unlock has run). None: the folder route. libsre.prx is
    optional (the module's docstring): unless the unlock makes it, none found
    gives no item, and one that is not right an "unused" one.
    update: the folder holding the update's files (game_update.py), for a disc
    that needs one."""
    t = table or load_table()
    rep = Report()
    root = find_root(folder)
    if root is None:
        rep.items.append(Item("game", "PS3_GAME", "missing",
                              "This folder has no PS3_GAME folder with a PARAM.SFO in it. Pick the folder "
                              "of your disc dump that holds PS3_GAME."))
        return rep
    rep.root = root

    # The title and version, from PARAM.SFO.
    try:
        vals = sfo.parse(_resolve(root, "PS3_GAME/PARAM.SFO").read_bytes())
    except (OSError, sfo.SfoError) as e:
        rep.items.append(Item("game", "PARAM.SFO", "wrong", f"PARAM.SFO cannot be read ({e})."))
        return rep
    tid = vals.get("TITLE_ID", "")
    tt = for_title(t, tid)
    if tt is None:
        rep.items.append(Item("game", "TITLE_ID", "wrong", other_title(t, tid)))
        return rep
    t = rep.table = tt
    ver = vals.get("APP_VER", ""), vals.get("VERSION", "")
    if ver != (t["app_ver"], t["version"]):
        rep.items.append(Item("game", "APP_VER", "wrong",
                              f"This copy is version {ver[0] or '?'}. The port supports version "
                              f"{t['app_ver']} only."))
        return rep
    dig = rep.digital = digital_of(root, t)
    disc = dig["what"] if dig else t.get("disc", "the US disc")
    rep.items.append(Item("game", "TITLE_ID", "ok", f"{t['title_id']}, version {t['app_ver']} ({disc})."))
    if dig:
        rep.items.append(Item("game", "STORE", "ok", f"{dig['id']}, {dig['what']}."))
    needed = t.get("update")
    up = Path(update) if update and needed else None
    plain = plain_forms(t) if needed or dig else {}
    if needed:
        wrong = game_update.bad_files(up, needed, plain=plain) if up else None
        state, why = (("ok", f"Its update, {needed['app_ver']}, is there.") if up and not wrong else
                      ("missing", UPDATE_MISSING.format(disc=disc, ver=needed["app_ver"])) if not up else
                      ("mismatch", f"{len(wrong)} of the update's files do not match. Get the update again."))
        rep.items.append(Item("update", "update", state, why, str(up or "")))

    # The disc's own files, and the update's: proof that the dump is this version and intact.
    for e in t["originals"]:
        from_update = e.get("from") == "update"
        place = up if from_update else root
        rel = e["path"].split("PS3_GAME/", 1)[-1] if from_update else e["path"]
        if from_update and rel in plain and up and _plain_in_update(up, rel):
            e = dict(plain[rel], path=e["path"])    # (RPCS3 installed it: compared as the plain file it now is)
        elif dig:                                   # (the store copy's own files, or the plain form of a .sdat)
            own = dig["files"].get(e["path"]) or plain.get(e["path"].split("PS3_GAME/", 1)[-1])
            e = dict(e, size=own["size"], sha256=own["sha256"]) if own else e
        again = "The install may be damaged: install the package again in RPCS3." if dig else \
            "The dump may be damaged: dump the disc again."
        rep.items.append(_check_file("original", e, _resolve(place, rel) if place else None, progress,
                                     missing=f"{e['path']} is missing from the " +
                                             ("update." if from_update else "game folder."),
                                     mismatch=f"{e['path']} does not match {disc}, version {t['app_ver']}. {again}"))

    # The decrypted files the build reads: the first good copy, else what is wrong with the first one found.
    for e in t["decrypted"]:
        base = e["path"].rsplit("/", 1)[-1]
        src = e["of"].rsplit("/", 1)[-1]
        if unlocks:
            texts = dict(missing=f"{base} is missing: your RPCS3 unlocks it next.",
                         mismatch=f"{base} does not match: your RPCS3 unlocks it again.")
        else:
            texts = dict(missing=f"{base} is missing: decrypt {src} first (in RPCS3: Utilities, "
                                 f"Decrypt PS3 Binaries).",
                         mismatch=f"{base} does not match: decrypt {src} again.")
        places = [_resolve(root, e["path"])] + [_find_decrypted(Path(d), e["path"]) for d in decrypted_dirs]
        if dig and e["of"].endswith(".sdat"):   # (a store copy's .sdat files are the plain files already)
            places.append(_resolve(root, e["of"]))
        if up:   # (RPCS3 writes beside what it was given, so beside the update's files too)
            places.append(_resolve(up, e["path"].split("PS3_GAME/", 1)[-1]))
            of = e["of"].split("PS3_GAME/", 1)[-1]
            if of in plain:                     # (or an update RPCS3 installed has it, under the .sdat's own name)
                places.append(_plain_in_update(up, of))
        places += [Path(p) for p in (also or {}).get(e["path"], ())]
        first = None
        for p in places:
            if p is None or not p.is_file():
                continue
            item = _check_file("decrypted", e, p, progress, **texts)
            if item.good:
                break
            first = first or item
        else:
            item = first or _check_file("decrypted", e, None, progress, **texts)
        if dig and not item.good and e["of"] == EBOOT_BIN:
            # The store copy's EBOOT.elf is the player's to make: a sentence for RPCS3's menu, not the unlock's.
            eboot = _resolve(root, EBOOT_BIN)
            words = DECRYPT_AGAIN if item.state == "mismatch" else DECRYPT_FIRST
            item = Item("decrypted", e["path"], DECRYPT,
                        words.format(eboot=os.path.realpath(eboot) if eboot else "this game's EBOOT.BIN"), item.found)
        elif not dig or e["of"] != EBOOT_BIN:
            item.unlock = unlocks
        rep.items.append(item)

    # The firmware's file, optional (the module's docstring): the one given, else one by its name in the game's folder
    # (beside PS3_GAME) or in a folder of decrypted files. On the RPCS3 route the unlock makes it when RPCS3 has the
    # PS3 system software; otherwise a copy that is not right is left out, and none found is not mentioned.
    for e in t.get("firmware", []):
        unlockable = firmware is None and firmware_unlock is True
        if firmware is not None:
            places = [Path(firmware)]
        else:
            places = [d / _find_ci(d, e["name"]) for d in [root] + [Path(x) for x in decrypted_dirs]
                      if _find_ci(d, e["name"])]
            places += [Path(p) for p in (also or {}).get(e["name"], ())]
        places = [p for p in places if p.is_file()]
        if not places and not unlockable and firmware is None:
            continue
        if unlockable:
            texts = dict(missing=f"{e['name']} is missing: your RPCS3 unlocks it next.",
                         mismatch=f"{e['name']} does not match: your RPCS3 unlocks it again.")
        else:
            texts = dict(missing=f"{e['name']} was not found at {firmware}.",
                         mismatch=f"{e['name']} does not match the known good version.")
        first = None
        for p in places:
            item = _check_file("firmware", e, p, progress, **texts)
            if item.good:
                break
            first = first or item
        else:
            item = first or _check_file("firmware", e, None, progress, **texts)
        if not item.good and not unlockable:
            why = (f"{Path(item.found).name} is still encrypted." if item.state == "encrypted" else item.message)
            item = Item("firmware", item.name, "unused", f"{why} {LEFT_OUT}", item.found, item.sha256)
        item.unlock = unlockable
        rep.items.append(item)

    # The rest of the disc's files, by name and size (disc.py): a warning, which the build asks the player about.
    rep.disc = disc_files.check(root, t.get("disc_files"),
                                sized=[e["path"] for e in t["originals"] if e["path"] != "PS3_GAME/PARAM.SFO"] if dig
                                else ())
    return rep


def _check_file(kind, e, path, progress, missing, mismatch) -> Item:
    name = e.get("path") or e["name"]
    if path is None or not path.is_file():
        return Item(kind, name, "missing", missing)
    size = path.stat().st_size
    head = _magic(path)
    if head in ENCRYPTED_MAGIC and kind in ("decrypted", "firmware"):
        return Item(kind, name, "encrypted", f"{path.name} is still {ENCRYPTED_MAGIC[head]}: decrypt it first.",
                    str(path))
    if e.get("magic") and head != bytes.fromhex(e["magic"]):
        return Item(kind, name, "mismatch", mismatch, str(path))
    if kind == "firmware" and e.get("also"):
        other = fwver.other_copy(path, e)   # (another version's copy, built as it is: firmware.py)
        if other:
            return Item(kind, name, "ok", f"{path.name} matches. It is from PS3 system software "
                        f"{fwver.name(int(other['version'], 16))}, which the port is tested with too.", str(path),
                        other["sha256"])
    if e.get("size") is not None and size != e["size"]:
        return Item(kind, name, "mismatch", mismatch, str(path))
    got = sha256_file(path, progress)
    if e.get("sha256") is None:
        return Item(kind, name, "unmeasured", f"{path.name} found. Its sum is not in the table yet, so only its "
                                              f"first bytes were checked.", str(path), got)
    if got != e["sha256"] and kind == "firmware":
        fixed = fwver.matched(path, e)
        if fixed:
            want = int(e["version"], 16)
            return Item(kind, name, "ok", f"{path.name} matches. It is from PS3 system software "
                        f"{fwver.name(fixed[1])}; setup sets its version to {fwver.name(want)}, the one the port is "
                        f"tested with.", str(path), e["sha256"], fix=e["version"])
    if got != e["sha256"]:
        return Item(kind, name, "mismatch", mismatch, str(path), got)
    return Item(kind, name, "ok", f"{path.name} matches.", str(path), got)
