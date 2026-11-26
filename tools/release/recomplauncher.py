#!/usr/bin/env python3
"""Checks recomplauncher.json, RecompLauncher's file (docs/166), against the program it describes.

  python tools/release/recomplauncher.py check [--file PATH]

RecompLauncher (recomplauncher.com) reads recomplauncher.json from the top of the player's download (package.py puts
it there). It starts the program from it, and shows some of the program's settings on the game's page, written into
settings.ini as the in-game menu writes them. This fails, with each problem named, when the file says something the
program does not do:

- a setting's key is not in settings.cpp's kDefs, is a developer build's (#if DES_DEV), or is a per-session one
  (Apply::Session: never read from settings.ini);
- its default is not kDefs' default for that key, or the key has another default on another system;
- a choice's value is not one of the values of the key's row in settings_catalog.cpp (a toggle's are 1 and 0), or a
  value of the row has no choice, so a value the menu writes would show as nothing in the launcher;
- two settings change the same key, or the choices of one do not all set the same keys;
- the file has a field this script does not check (KNOWN): a new field gets its checks here first;
- the start has arguments or environment variables. The program takes any argument as the game's file to run, and a
  variable set in the environment wins over settings.ini and locks its row in the menu;
- the name or the program is not PRODUCT_NAME's (the program's file name: package.py's program_name);
- a place in {config} or {local} (config.file's, saves') is in neither of setup_folder.hpp's folders (kFolderName:
  the earlier versions', kOneFolderName: the one data folder, docs/164), the last settings.ini is not the one data
  folder's, or no save folder is in it.

It reads the C++ sources as text. A change to how kDefs or a catalog row is written can make it miss a key; it then
says that it cannot find the key, rather than pass. Standard library only.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FILE = "recomplauncher.json"
SETTINGS = "port/ui/src/settings.cpp"
CATALOG = "port/ui/src/settings_catalog.cpp"
VALUES = "port/ui/src/settings_values.hpp"
FOLDERS = "port/ui/src/setup_folder.hpp"
SCHEMA = "https://recomplauncher.com/schema/recomplauncher.json"

# The fields this script checks, at the top and in each part. Anything else fails until it has its checks here.
KNOWN = {"$schema", "format", "name", "start", "config", "settings", "saves", "looks", "system"}
SETTING_KNOWN = {"id", "label", "help", "group", "choices", "defaults"}
CONFIG_KNOWN = {"id", "format", "file"}
LOOKS_KNOWN = {"allowed", "reason"}
SYSTEMS = {"windows": {"version", "graphics"}, "linux": {"glibc", "graphics"}}
VERSION_RE = re.compile(r"^\d+(\.\d+)*$")
GRAPHICS = {"vulkan", "opengl", "d3d12", "d3d11", "d3d9", "metal"}

STRING = r'"(?:[^"\\]|\\.)*"'
DEF_RE = re.compile(r'^\s*\{\s*"([A-Za-z0-9_]+)"\s*,\s*(' + STRING + r'|[^,{}]+?)\s*,\s*Apply::(\w+)\s*\}')


def unquote(literal):
    """A C string literal's text (the escapes these tables use: \\" and \\\\)."""
    return re.sub(r'\\(.)', r'\1', literal[1:-1])


class Block:
    """Where a line is, as the preprocessor lines around it say: the system it is for (None for both) and whether it
    is a developer build's only."""

    def __init__(self):
        self.stack = []   # (condition, in its #else)

    def line(self, text):
        """Follows a preprocessor line; True if it was one."""
        m = re.match(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", text)
        if not m:
            return False
        what, rest = m.group(1), re.sub(r"//.*$", "", m.group(2)).strip()
        if what in ("if", "ifdef"):
            self.stack.append((rest, False))
        elif what == "ifndef":
            self.stack.append((rest, True))
        elif what == "else" and self.stack:
            cond, negated = self.stack.pop()
            self.stack.append((cond, not negated))
        elif what == "elif" and self.stack:
            self.stack.pop()
            self.stack.append((rest, False))
        elif what == "endif" and self.stack:
            self.stack.pop()
        return True

    def system(self):
        for cond, negated in self.stack:
            if re.fullmatch(r"(defined\s*\(\s*)?_WIN32\s*\)?", cond) or cond == "_WIN32":
                return "other" if negated else "windows"
            if "_WIN32" in cond and "&&" in cond and not negated:
                return "windows"
        return None

    def dev(self):
        return any(cond == "DES_DEV" and not negated for cond, negated in self.stack)


def strip_comments(text):
    """The text with // and /* */ comments blanked (strings kept), line breaks and offsets as they were."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def parse_defs(text):
    """settings.cpp's kDefs: {key: [(default or None, apply, system, dev)]}, a row per #if branch the key is in. A
    default that is not a string literal (kNetServers[0].key) is None."""
    m = re.search(r"\bkDefs\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise ValueError(f"{SETTINGS}: no kDefs table")
    defs, block = {}, Block()
    for line in m.group(1).splitlines():
        if block.line(line):
            continue
        d = DEF_RE.match(line)
        if d:
            value = unquote(d.group(2)) if d.group(2).startswith('"') else None
            defs.setdefault(d.group(1), []).append((value, d.group(3), block.system(), block.dev()))
    if not defs:
        raise ValueError(f"{SETTINGS}: no row read in kDefs")
    return defs


def balanced(text, start):
    """The text from the brace at `start` to the brace that closes it, strings skipped; None if it does not close."""
    depth, i, n = 0, start, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            i += 1
            while i < n and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return None


def parse_value_tables(text):
    """settings_values.hpp's value tables: {name: [values]} of each `const char* NAME[] = {...};`."""
    tables = {}
    for m in re.finditer(r"const\s+char\s*\*\s*(?:const\s+)?(\w+)\[\]\s*=\s*\{([^}]*)\}", strip_comments(text)):
        tables[m.group(1)] = [unquote(s) for s in re.findall(STRING, m.group(2))]
    return tables


ROW_RE = re.compile(r'\b(choice|toggle)\(\s*' + STRING + r'\s*,\s*"([A-Za-z0-9_]+)"\s*,')
FIELD_RE = re.compile(r'\.key\s*=\s*"([A-Za-z0-9_]+)"\s*,\s*\.kind\s*=\s*(\w+)\s*\{')
PAIR_RE = re.compile(r'\{\s*(' + STRING + r')\s*,\s*' + STRING)


def parse_rows(catalog, tables):
    """settings_catalog.cpp's rows: {key: [(kind, values, system, dev)]}, kind "choice", "toggle" or another row's
    kind (NumberRow), values the stored values its choices write (a toggle's "1" and "0")."""
    text = strip_comments(catalog)
    # Each offset's #if context, from the lines before it.
    contexts, block, offset = [], Block(), 0
    for line in text.splitlines(keepends=True):
        block.line(line)
        contexts.append((offset, block.system(), block.dev()))
        offset += len(line)

    def context(at):
        found = contexts[0]
        for c in contexts:
            if c[0] > at:
                break
            found = c
        return found[1], found[2]

    rows = {}

    def choices_at(start, what):
        """The values of the list of choices written out from `start` on; None for choices made another way (a
        function's, as Server's)."""
        m = re.compile(r"\s*\{").match(text, start)
        if not m:
            return None
        body = balanced(text, m.end() - 1)
        if body is None:
            raise ValueError(f"{CATALOG}: {what}: its choices do not close")
        return [unquote(v) for v in PAIR_RE.findall(body[1:])]

    for m in ROW_RE.finditer(text):
        system, dev = context(m.start())
        key = m.group(2)
        if m.group(1) == "toggle":
            rows.setdefault(key, []).append(("toggle", ["1", "0"], system, dev))
        else:
            values = choices_at(m.end(), key)
            rows.setdefault(key, []).append(("choice" if values is not None else "made", values or [], system, dev))
    for m in FIELD_RE.finditer(text):
        system, dev = context(m.start())
        key, kind = m.group(1), m.group(2)
        if kind != "ChoiceRow":
            rows.setdefault(key, []).append((kind, [], system, dev))
            continue
        body = balanced(text, m.end() - 1) or ""
        w = re.search(r"\.choices\s*=\s*with_values\(\s*(?:\w+::)?(\w+)", body)
        if w:
            if w.group(1) not in tables:
                raise ValueError(f"{CATALOG}: {key}: its values {w.group(1)} are not in {VALUES}")
            rows.setdefault(key, []).append(("choice", list(tables[w.group(1)]), system, dev))
        elif re.search(r"\.choices\s*=\s*\{", body):
            at = re.search(r"\.choices\s*=\s*\{", body)
            rows.setdefault(key, []).append(("choice", choices_at(m.end() - 1 + at.end() - 1, key), system, dev))
        else:
            rows.setdefault(key, []).append(("made", [], system, dev))   # (live choices, or a function's)
    return rows


def parse_folder_names(text):
    """setup_folder.hpp's folder names: (kFolderName, kOneFolderName), the earlier versions' and the one data
    folder's."""
    names = []
    for name in ("kFolderName", "kOneFolderName"):
        m = re.search(r'inline constexpr const char\*\s*' + name + r'\s*=\s*(' + STRING + r')\s*;', text)
        if not m:
            raise ValueError(f"{FOLDERS}: cannot find {name}")
        names.append(unquote(m.group(1)))
    return tuple(names)


def check_places(doc, old, one):
    """The places' problems: each {config} or {local} place is in `old` (the earlier versions' folder) or `one` (the
    one data folder), the last settings.ini is the one folder's, and a save folder is in it."""
    configs = doc.get("config")
    files = configs[0].get("file") if isinstance(configs, list) and configs and isinstance(configs[0], dict) else None
    files = [f for f in files if isinstance(f, dict)] if isinstance(files, list) else []
    saves = [s for s in doc.get("saves", []) if isinstance(s, str)] if isinstance(doc.get("saves"), list) else []
    problems = []
    for where, place in ([("config.file", f.get(k)) for f in files for k in ("path", "if") if k in f] +
                         [("saves", s) for s in saves]):
        parts = str(place).split("/")
        if parts[0] in ("{config}", "{local}") and (len(parts) < 2 or parts[1] not in (old, one)):
            problems.append(f"{where}: {place!r} is in neither {old!r} (the earlier versions' folder) nor {one!r} "
                            f"(the one data folder), {FOLDERS}'s names")
    last = f"{{local}}/{one}/settings.ini"
    if files and files[-1].get("path") != last:
        problems.append(f"config.file: the last place is not {last}, the one data folder's (docs/164)")
    if not any(s.startswith(f"{{local}}/{one}/gamedata/") for s in saves):
        problems.append(f"saves: none in the one data folder ({{local}}/{one}/gamedata/...)")
    return problems


def check(doc, defs, rows, product, folders=None):
    """The problems of `doc` (recomplauncher.json, parsed), as sentences; none when it says only what the program
    does. folders: parse_folder_names' pair, to check the places too."""
    problems = []
    if not isinstance(doc, dict):
        return ["it is not a JSON object"]
    for field in sorted(set(doc) - KNOWN):
        problems.append(f"{field}: a field this check does not know (add its checks to KNOWN first)")
    if doc.get("$schema") != SCHEMA:
        problems.append(f"$schema: not {SCHEMA}")
    if doc.get("format") != 1:
        problems.append("format: not 1")
    if doc.get("name") != product:
        problems.append(f"name: {doc.get('name')!r}, PRODUCT_NAME says {product!r}")

    start = doc.get("start")
    if not isinstance(start, dict):
        problems.append("start: missing")
    else:
        if start.get("program") != [product + ".exe", product]:
            problems.append(f"start.program: not [{product + '.exe'!r}, {product!r}], the program on Windows and Linux")
        for field in sorted(set(start) - {"program"}):
            problems.append(f"start.{field}: the program takes any argument as a game file to run, and a variable in "
                            "the environment wins over settings.ini: leave it out")

    configs = doc.get("config", [])
    if not isinstance(configs, list) or len(configs) != 1 or not isinstance(configs[0], dict):
        problems.append("config: not one settings file")
    else:
        c = configs[0]
        for field in sorted(set(c) - CONFIG_KNOWN):
            problems.append(f"config.{field}: not checked here (the launcher would write those keys itself)")
        if c.get("id", "main") != "main" or c.get("format") != "ini":
            problems.append("config: not id main, format ini (settings.ini)")
        places = c.get("file")
        if not isinstance(places, list) or not places:
            problems.append("config.file: no place")
        else:
            for p in places:
                if not isinstance(p, dict) or not str(p.get("path", "")).endswith("/settings.ini"):
                    problems.append(f"config.file: {p!r} is not a settings.ini")
            if "if" in places[-1]:
                problems.append("config.file: the last place has an if, so a first start may find none")

    saves = doc.get("saves", [])
    if not isinstance(saves, list) or not all(isinstance(s, str) and s for s in saves):
        problems.append("saves: not a list of places")
    if folders:
        problems += check_places(doc, *folders)
    looks = doc.get("looks", {})
    if not isinstance(looks, dict) or set(looks) - LOOKS_KNOWN:
        problems.append(f"looks: only {sorted(LOOKS_KNOWN)} are checked here")
    elif looks.get("allowed") is False and not looks.get("reason"):
        problems.append("looks: off without a reason for the player")
    system = doc.get("system", {})
    if not isinstance(system, dict):
        problems.append("system: not an object")
    else:
        for name, value in system.items():
            if name not in SYSTEMS or not isinstance(value, dict) or set(value) - SYSTEMS[name]:
                problems.append(f"system.{name}: not checked here")
                continue
            for field, v in value.items():
                if field == "graphics":
                    if not isinstance(v, dict) or set(v) - GRAPHICS or not all(VERSION_RE.match(str(x)) for x in
                                                                                  v.values()):
                        problems.append(f"system.{name}.graphics: not an API and its version")
                elif not VERSION_RE.match(str(v)):
                    problems.append(f"system.{name}.{field}: not a version")

    settings = doc.get("settings", [])
    if not isinstance(settings, list):
        return problems + ["settings: not a list"]
    ids, owner = set(), {}
    for n, s in enumerate(settings):
        where = f"settings[{n}]"
        if not isinstance(s, dict):
            problems.append(f"{where}: not an object")
            continue
        where = f"settings {s.get('id', n)!r}"
        for field in sorted(set(s) - SETTING_KNOWN):
            problems.append(f"{where}: {field} is not checked here (only choices written to settings.ini are)")
        if not s.get("id") or s.get("id") in ids:
            problems.append(f"{where}: no id, or one used twice")
        ids.add(s.get("id"))
        if not s.get("label"):
            problems.append(f"{where}: no label")
        choices = s.get("choices")
        if not isinstance(choices, list) or len(choices) < 2:
            problems.append(f"{where}: fewer than two choices")
            continue
        keysets = [frozenset(c.get("set", {})) if isinstance(c, dict) and isinstance(c.get("set"), dict) else None
                   for c in choices]
        if None in keysets or len(set(keysets)) != 1 or not keysets[0]:
            problems.append(f"{where}: its choices do not all set the same keys")
            continue
        if not all(isinstance(v, str) for c in choices for v in c["set"].values()):
            problems.append(f"{where}: a value that is not a string (the launcher writes values as given)")
            continue
        labels = [c.get("label") for c in choices]
        if not all(labels) or len(set(labels)) != len(labels):
            problems.append(f"{where}: a choice without a label, or two with the same one")
        defaults = s.get("defaults")
        if not isinstance(defaults, dict) or set(defaults) != set(keysets[0]):
            problems.append(f"{where}: its defaults are not for exactly the keys its choices set")
            defaults = defaults if isinstance(defaults, dict) else {}
        for key in sorted(keysets[0]):
            if key in owner:
                problems.append(f"{where}: {key} is also {owner[key]!r}'s")
            owner[key] = s.get("id")
            problems += check_key(where, key, [c["set"][key] for c in choices], defaults.get(key), defs, rows)
        values = [tuple(c["set"][k] for k in sorted(keysets[0])) for c in choices]
        if len(set(values)) != len(values):
            problems.append(f"{where}: two choices write the same values")
        if tuple(defaults.get(k) for k in sorted(keysets[0])) not in values:
            problems.append(f"{where}: its default is none of its choices")
    return problems


def check_key(where, key, values, default, defs, rows):
    problems = []
    found = defs.get(key)
    if not found:
        return [f"{where}: {key} is not a setting in {SETTINGS}'s kDefs"]
    if any(dev for _v, _a, _s, dev in found):
        problems.append(f"{where}: {key} is a developer build's")
    if any(apply == "Session" for _v, apply, _s, _d in found):
        problems.append(f"{where}: {key} is per session, never read from settings.ini")
    known = {v for v, _a, _s, _d in found}
    if None in known or len(known) != 1:
        problems.append(f"{where}: {key} has no single default in kDefs (one per system, or not a string)")
    elif default != next(iter(known)):
        problems.append(f"{where}: {key}'s default is {next(iter(known))!r} in kDefs, not {default!r}")
    if any(r[0] == "made" and not r[3] for r in rows.get(key, [])):
        problems.append(f"{where}: {key}'s row makes its choices as it runs, so they cannot be checked here")
    menu = [r for r in rows.get(key, []) if r[0] in ("choice", "toggle") and not r[3]]
    if not menu:
        return problems + [f"{where}: {key} has no choice or toggle row in {CATALOG}"]
    offered = set()
    for _kind, row_values, _system, _dev in menu:
        offered.update(row_values)
    for v in values:
        if v not in offered:
            problems.append(f"{where}: {key}={v!r} is not one of its row's values {sorted(offered)}")
    for v in sorted(offered - set(values)):
        problems.append(f"{where}: {key}={v!r}, which the menu can write, has no choice")
    return problems


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def cmd_check(args):
    path = args.file or os.path.join(ROOT, FILE)
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        problems = [f"cannot read it: {e}"]
    else:
        try:
            defs = parse_defs(read(SETTINGS))
            rows = parse_rows(read(CATALOG), parse_value_tables(read(VALUES)))
            problems = check(doc, defs, rows, read("PRODUCT_NAME").splitlines()[0].strip(),
                             parse_folder_names(read(FOLDERS)))
        except (OSError, ValueError) as e:
            problems = [str(e)]
    name = os.path.relpath(path, ROOT).replace(os.sep, "/")
    for p in problems:
        print(f"::error file={name}::{p}" if os.environ.get("GITHUB_ACTIONS") == "true" else f"{name}: {p}",
              file=sys.stderr)
    if not problems:
        print(f"{name}: {len(doc.get('settings', []))} settings, each as {SETTINGS} and {CATALOG} have it")
    return 1 if problems else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="check recomplauncher.json against the settings")
    c.add_argument("--file", help=f"the file to check (default: {FILE} at the root)")
    args = ap.parse_args(argv)
    return {"check": cmd_check}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
