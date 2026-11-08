"""Every file of the player's disc copy, by name and size (issue #418).

Setup reads 28 of the disc's files (game_table.json's originals). The game reads thousands more while it runs, and a
copy that lacks some of those still passed setup's check. Then the game broke later: without its sound files its
sound engine never starts, and its memory breaks at the first change of area (the warp at the end of the tutorial).

The lists in disc/ name every file under PS3_GAME on each supported disc, with its size, from the disc's IRD
(tools/setup/disc_list.py makes them; game_table.json's "disc_files" names each disc's). The check reads the copy's
folder listings only, never a file's bytes, so it takes a second or two.

What it finds is a warning, not a fault. A copy with files missing (or of another size: a damaged or modded file)
stops a build until the player says to go on: `run --disc-anyway`, the setup screens' Continue anyway. The choice is
kept in request.json for that game folder. Each build writes what its check found, and whether the player chose to go
on, to setup/disc_check.txt (RECORD), which the game notes in its log at each start and problem reports carry.

Names are matched as the game opens them: exactly on Linux, ignoring case on Windows and macOS, whose file systems
do (a copy whose name differs only in case is "case" on Linux, as the game cannot open it there). Files the copy has
and the list does not (the decrypted copies beside their originals, a mod's) are not looked at.
"""
import gzip
import os
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

LISTS = Path(__file__).with_name("disc")
RECORD = "disc_check.txt"
CASELESS = os.name == "nt" or sys.platform == "darwin"   # the file systems that match names ignoring case
LISTED = 500          # the files RECORD names, at most (the counts are whole)


@dataclass
class Disc:
    state: str                      # "complete", "incomplete", or "unknown" (no list for this disc)
    files: int = 0                  # the files in the disc's list
    missing: list = field(default_factory=list)   # their paths ("PS3_GAME/USRDIR/sound/ds_se_main.mowb")
    case: list = field(default_factory=list)      # (path, the copy's name) whose names differ in case only (Linux)
    differ: list = field(default_factory=list)    # (path, the list's size, the copy's size)

    @property
    def absent(self) -> int:
        """The files the game cannot open: missing, or (on Linux) named in other letters' case."""
        return len(self.missing) + len(self.case)

    def problems(self) -> list:
        """(kind, path) of each file that is not right: missing, case, size. Sorted by path."""
        out = [("missing", p) for p in self.missing] + [("case", p) for p, _ in self.case] + \
              [("size", p) for p, _, _ in self.differ]
        return sorted(out, key=lambda kp: kp[1])

    def sentence(self) -> str:
        """The finding in words: "6 of the disc's 12,509 files are missing from the game folder."."""
        if self.state == "unknown":
            return "Setup has no list of this disc's files, so it did not check them all."
        if self.state == "complete":
            return f"All {self.files:,} of the disc's files are in the game folder."
        n, of = len(self.differ), f"of the disc's {self.files:,} files"
        sized = f"{'is' if n == 1 else 'are'} not the size {'it is' if n == 1 else 'they are'} on the disc"
        if not self.absent:
            return f"{n:,} {of} {sized}."
        missing = f"{self.absent:,} {of} {'is' if self.absent == 1 else 'are'} missing from the game folder"
        return missing + (f", and {n:,} {sized}." if n else ".")


def load(name, lists=None):
    """{folder: {file: size}} of a list in disc/ by its file name ("BLUS30443.txt.gz"), or None when there is none or
    it cannot be read."""
    if not name:
        return None
    try:
        text = gzip.decompress((Path(lists or LISTS) / name).read_bytes()).decode("utf-8")
    except (OSError, EOFError, gzip.BadGzipFile, UnicodeDecodeError):
        return None
    out, folder = {}, None
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        if line.endswith("/"):
            folder = out.setdefault(line[:-1], {})
            continue
        file, _, size = line.rpartition("\t")
        if folder is None or not file or not size.isdigit():
            return None
        folder[file] = int(size)
    return out


class _Listings:
    """Each folder's names, read once: {name: (size or None for a folder)} as they are and by lower case."""

    def __init__(self):
        self._seen = {}

    def get(self, path: Path):
        key = str(path)
        if key not in self._seen:
            self._seen[key] = self._read(path)
        return self._seen[key]

    @staticmethod
    def _read(path):
        exact = {}
        try:
            with os.scandir(path) as it:
                for e in it:
                    try:
                        exact[e.name] = None if e.is_dir() else e.stat().st_size
                    except OSError:
                        continue
        except OSError:
            return None
        lower = {}
        for n in sorted(exact):
            lower.setdefault(n.lower(), n)
        return exact, lower


def _find(listings, folder: Path, name: str):
    """(the copy's name, its size or None for a folder, whether only its case matches) of `name` in `folder`, or
    None. On Windows and macOS a name in other case is the same file."""
    got = listings.get(folder)
    if got is None:
        return None
    exact, lower = got
    if name in exact:
        return name, exact[name], False
    other = lower.get(name.lower())
    if other is None:
        return None
    return other, exact[other], not CASELESS


def check(root, name, lists=None) -> Disc:
    """The copy in `root` (the folder that holds PS3_GAME) against the list `name` (game_table.json's "disc_files")."""
    want = load(name, lists)
    if want is None:
        return Disc("unknown")
    d = Disc("complete", files=sum(len(f) for f in want.values()))
    listings = _Listings()
    for folder in sorted(want):
        files = want[folder]
        host, case_of = Path(root), None          # case_of: the first folder on the way whose case differs (Linux)
        for part in folder.split("/"):
            hit = _find(listings, host, part)
            if hit is None or hit[1] is not None:  # (none, or a file of that name)
                host = None
                break
            host = host / hit[0]
            if hit[2] and case_of is None:
                case_of = str(host)
        for file in sorted(files):
            path = f"{folder}/{file}"
            hit = _find(listings, host, file) if host is not None else None
            if hit is None or hit[1] is None:
                d.missing.append(path)
            elif hit[2] or case_of:
                d.case.append((path, hit[0]))
            elif hit[1] != files[file]:
                d.differ.append((path, files[file], hit[1]))
    if d.missing or d.case or d.differ:
        d.state = "incomplete"
    return d


def stops(d, anyway) -> bool:
    """Whether the build waits for the player's word: the copy is not whole and they have not said to go on."""
    return d is not None and d.state == "incomplete" and not anyway


def record(data: Path, title_id, d, anyway):
    """setup/disc_check.txt: key=value lines the host reads without Python (setup_engine.cpp disc_summary), then the
    files that are not right, one per line, LISTED at most."""
    lines = ["# The game folder's files against the disc's list, as the last build's check found them",
             "# (tools/setup/des_setup/disc.py). The game notes this in its log at each start; reports carry it.",
             "format=1", f"title={title_id or '-'}", f"state={d.state}", f"files={d.files}",
             f"missing={d.absent}", f"differ={len(d.differ)}",
             f"continued_anyway={1 if d.state == 'incomplete' and anyway else 0}",
             "checked=" + time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())]
    probs = d.problems()
    sizes = {p: (w, h) for p, w, h in d.differ}
    for kind, path in probs[:LISTED]:
        sized = " (disc {0}, here {1} bytes)".format(*sizes[path]) if kind == "size" else ""
        lines.append(f"{kind}_file={path}{sized}")
    if len(probs) > LISTED:
        lines.append(f"more={len(probs) - LISTED}")
    path = data / "setup" / RECORD
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    os.replace(tmp, path)
