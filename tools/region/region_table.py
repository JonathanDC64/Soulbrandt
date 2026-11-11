#!/usr/bin/env python3
"""The European disc's address table, port/regions/BLES00932.inc (port/des_region.h, scripts/des_region.py).

  region_table.py check
      For CI. The table is sorted, with one line per address, and every line is used. Every guest address in the
      port (its sources, the hook list, the lift's overrides and the boot patch) is in the table. In the port's
      sources, each one is read through DES_AT or DES_REGISTER_FUNCTION, unless it is a hook key (`case`, `addr ==`),
      which stays the US address. No hook handler reads guest memory at its key without des_region_addr.

  region_table.py find US.elf EU.elf 0xADDR [0xADDR ...]
      The European address of each US address, from both decrypted EBOOTs, for a new table line.

  region_table.py verify US.elf EU.elf
      Every table line checked against both decrypted EBOOTs. Lines it has no automatic answer for are listed.

How find works:
  - Code: the instructions around the address, with branch offsets and address halves masked, matched near the same
    place in the European EBOOT.
  - Data: through the address tables that hold it (code loads a table's base from the TOC, then the table's word:
    the code is matched and the other disc's own TOC slot read), through the TOC slot that holds the address or a
    base below it, or through the lis/addi pairs that build it, at the matched code. An address table names the
    address itself, so it wins over references that only name a base below it.

Addresses only: this prints and checks addresses, nothing from the game's bytes.
"""
import argparse
import bisect
import json
import re
import struct
import sys
import tokenize
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import des_region  # noqa: E402

# The US EBOOT's loaded image (BLUS30443 1.00's program headers): code and read-only data, then data and bss. A
# literal outside these is not a guest image address.
US_IMAGE = ((0x00010000, 0x01842D48), (0x01850000, 0x01D2B5D8))
# In-image numbers in the port's sources that are not addresses, and why.
NOT_ADDRESSES = {0x00010000: "the guest memory's floor, in range checks"}
# Files that hold the table's own machinery: their addresses are checked against the table, not for DES_AT.
MACHINERY = {"des_region.h", "des_region.cpp", "des_game_host.cpp", "des_game_title.h", "des_game_calls.h"}
SOURCE_DIRS = ("port", "port/compat", "port/tests")   # (port/ui has no guest addresses: it is built without them)
SOURCE_EXT = {".cpp", ".c", ".h", ".hpp", ".inc"}
WRAPPERS = ("DES_AT", "DES_REGISTER_FUNCTION")
# Calls that may take the US address as it is: DS_FRAME's address only labels a shadow-stack backtrace.
LABELS = ("DS_FRAME",)

_LIT = re.compile(r"(?<![\w.])0[xX]([0-9A-Fa-f]{8})(?:[uU][lL]{0,2}|[lL]{1,2}[uU]?)?(?![\w.])")
_FUNC = re.compile(r"\bfunc_([0-9A-Fa-f]{8})\b")
_GAME_CALL = re.compile(r"\bX\(([0-9A-Fa-f]{8})\)")
_CONST_DEF = re.compile(r"\bconstexpr\s+(?:const\s+)?(?:std::)?(?:uint32_t|uint64_t|unsigned(?:\s+int)?)\s+(k\w+)\s*"
                        r"(?:\[[^\]]*\])?\s*=\s*\{?[^;]*$", re.S)
_CONST_TYPE = re.compile(r"\bconstexpr\s+(?:const\s+)?(?:std::)?(?:uint32_t|uint64_t|unsigned(?:\s+int)?)\s+$")
_HOOK_KEY = re.compile(r"(?:\bcase\s*|\baddr\s*[!=]=\s*)$")


def in_image(v):
    return any(a <= v < b for a, b in US_IMAGE)


def code_only(text):
    """The text with comments, strings and character literals blanked (newlines and offsets kept)."""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])+\'', blank, text, flags=re.S)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def enclosing_call(code, pos):
    """The name of the call whose parentheses hold `pos` directly (DES_AT(...)), or None."""
    depth = 0
    for i in range(pos - 1, -1, -1):
        c = code[i]
        if c == ")":
            depth += 1
        elif c == "(":
            if depth == 0:
                m = re.search(r"(\w+)\s*$", code[max(0, i - 64):i])
                return m.group(1) if m else None
            depth -= 1
        elif c in ";{}" and depth == 0:
            return None
    return None


def statement_head(code, pos):
    """The current statement's text up to `pos`: from the last `;` or block brace outside an initializer."""
    start = max(code.rfind(";", 0, pos), code.rfind("}", 0, pos)) + 1
    head = code[start:pos]
    # an initializer's own brace (kX[] = {...}) belongs to the statement; a block's does not
    brace = head.rfind("{")
    if brace >= 0 and not re.search(r"=\s*$", head[:brace]):
        head = head[brace + 1:]
    return head


def wrapped(code, pos):
    return enclosing_call(code, pos) in WRAPPERS + LABELS


def hook_key(code, pos):
    return bool(_HOOK_KEY.search(code[max(0, pos - 40):pos]))


def sources(root=ROOT):
    for d in SOURCE_DIRS:
        for p in sorted((root / d).glob("*")):
            if p.is_file() and p.suffix in SOURCE_EXT:
                yield p


def check(root=ROOT):
    errors, used = [], set()
    rel = lambda p: p.relative_to(root).as_posix()  # noqa: E731

    # The tables: each sorted, one line per address, each of the disc's addresses once.
    tables = {}
    for table_path in sorted((root / "port" / "regions").glob("*.inc")):
        try:
            table = des_region.load_table(table_path)
        except des_region.RegionError as e:
            return [str(e)]
        tables[table_path] = table
        for other, n in Counter(table.values()).items():
            if n > 1 and other != des_region.NOT_ON_DISC:
                errors.append(f"{table_path.name}: address 0x{other:08X} is on {n} lines")

    def need(us, where):
        used.add(us)
        for table_path, table in tables.items():
            if us not in table:
                errors.append(f"{where}: 0x{us:08X} is not in {table_path.name}: add its address "
                              f"(tools/region/region_table.py find)")

    # The lift's inputs: the hook list, the overrides, the boot patch.
    lift = json.loads((root / "scripts" / "lift_ppu.json").read_text(encoding="utf-8"))
    hooks = root / lift["insn_hooks"]
    for n, line in enumerate(hooks.read_text(encoding="utf-8").splitlines(), 1):
        s = line.partition("#")[0].strip()
        if s:
            need(int(s.partition("=")[0], 0), f"{rel(hooks)}:{n}")
    for item in lift.get("override_funcs", []):
        need(int(item.partition("=")[0], 0), "scripts/lift_ppu.json override_funcs")
    boot = root / "scripts" / "apply_boot_patches.py"
    with open(boot, encoding="utf-8") as f:   # (its numbers only, not its docstrings and comments)
        for tok in tokenize.generate_tokens(f.readline):
            m = _LIT.fullmatch(tok.string) if tok.type == tokenize.NUMBER else None
            if m and in_image(int(m.group(1), 16)) and int(m.group(1), 16) not in NOT_ADDRESSES:
                need(int(m.group(1), 16), f"{rel(boot)}:{tok.start[0]}")

    # The port's sources.
    consts = defaultdict(list)   # name -> [(file, line)] of in-image constants
    texts = {}
    for p in sources(root):
        code = code_only(p.read_text(encoding="utf-8", errors="replace"))
        texts[p] = code
        machinery = p.name in MACHINERY
        for m in _GAME_CALL.finditer(code) if p.name == "des_game_title.h" else ():
            need(int(m.group(1), 16), f"{rel(p)}:{line_of(code, m.start())} (DES_GAME_CALLS)")
        for m in _FUNC.finditer(code):
            v = int(m.group(1), 16)
            if in_image(v):
                need(v, f"{rel(p)}:{line_of(code, m.start())} ({m.group(0)})")
        for m in _LIT.finditer(code):
            v = int(m.group(1), 16)
            if not in_image(v) or v in NOT_ADDRESSES:
                continue
            where = f"{rel(p)}:{line_of(code, m.start())}"
            need(v, where)
            if machinery or wrapped(code, m.start()) or hook_key(code, m.start()):
                continue
            d = _CONST_DEF.search(statement_head(code, m.start()))
            if d:
                consts[d.group(1)].append(where)
                continue
            errors.append(f"{where}: 0x{v:08X} is used as it is: write DES_AT(0x{v:08X}u) (or "
                          f"DES_REGISTER_FUNCTION), so the European disc gets its own address")
    # Each use of a named address goes through DES_AT or DES_REGISTER_FUNCTION too (or is a hook key).
    for name in consts:
        pat = re.compile(r"(?<![\w.])" + re.escape(name) + r"\b")
        for p, code in texts.items():
            if p.name in MACHINERY:
                continue
            for m in pat.finditer(code):
                if _CONST_TYPE.search(code[max(0, m.start() - 80):m.start()]) and \
                        re.match(r"\s*(?:\[[^\]]*\])?\s*=", code[m.end():]):
                    continue   # its definition
                if wrapped(code, m.start()) or hook_key(code, m.start()):
                    continue
                errors.append(f"{rel(p)}:{line_of(code, m.start())}: {name} (an address, {consts[name][0]}) is "
                              f"used as it is: write DES_AT({name})")

    for table_path, table in tables.items():
        for us in sorted(set(table) - used):
            errors.append(f"{table_path.name}: 0x{us:08X} is not used by the port: remove its line")
    errors += key_reads(hooks, texts, rel)
    return errors


_HANDLER = r"\bvoid\s+{name}\s*\(\s*(?:struct\s+)?ppu_context\s*\*\s*\w+\s*,\s*uint32_t\s+(\w+)\s*\)\s*\{{"


def block_at(code, brace):
    """The braced block that starts at `brace`."""
    depth = 0
    for j in range(brace, len(code)):
        if code[j] == "{":
            depth += 1
        elif code[j] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:j + 1]
    return code[brace:]


def key_reads(hooks, texts, rel):
    """A hook handler's key is the US address (des_region.h). A handler, or a helper it hands the key to, that reads
    or writes guest memory at the key, or at an offset from it, works on the US disc only."""
    errors, seen = [], set()
    todo = [line.partition("#")[0].partition("=")[2].strip()
            for line in hooks.read_text(encoding="utf-8").splitlines()]
    while todo:
        name = todo.pop()
        if not name or name in seen:
            continue
        seen.add(name)
        pat = re.compile(_HANDLER.format(name=re.escape(name)))
        for p, code in texts.items():
            for m in pat.finditer(code):
                key, start = m.group(1), m.end() - 1
                body = block_at(code, start)
                k = re.escape(key)
                for u in re.finditer(r"\bvm_\w+\s*\(\s*" + k + r"\b|\b" + k + r"\s*[+-]\s*(?:0[xX]|\d)", body):
                    errors.append(f"{rel(p)}:{line_of(code, start + u.start())}: {name} uses its key {key} as a "
                                  f"guest address: it is the US disc's; read at des_region_addr({key})")
                todo += [c.group(1) for c in re.finditer(r"\b(\w+)\s*\(\s*\w+\s*,\s*" + k + r"\s*\)", body)]
    return errors


# ---- find / verify: both decrypted EBOOTs --------------------------------------------------------------------------

class Elf:
    def __init__(self, path):
        b = Path(path).read_bytes()
        if b[:4] != b"\x7fELF" or b[4] != 2 or b[5] != 2:
            raise SystemExit(f"{path}: not a big-endian 64-bit ELF (the decrypted EBOOT)")
        self.b = b
        phoff, = struct.unpack_from(">Q", b, 0x20)
        phentsize, phnum = struct.unpack_from(">HH", b, 0x36)
        self.segs = []
        for i in range(phnum):
            t, fl, off, va, _pa, fsz, msz, _al = struct.unpack_from(">IIQQQQQQ", b, phoff + i * phentsize)
            if t == 1 and msz:
                self.segs.append((va, va + fsz, va + msz, off, fl & 1))
        entry, = struct.unpack_from(">Q", b, 0x18)
        self.toc = self.u32(int(entry) + 4)   # the entry's descriptor: {function, TOC}
        self.text = next((s[0], s[1]) for s in self.segs if s[4])

    def u32(self, va):
        for lo, fend, _mend, off, _x in self.segs:
            if lo <= va < fend:
                return struct.unpack_from(">I", self.b, off + va - lo)[0]
        return None

    def words(self, lo, hi, center=None):
        """The words from `lo` to `hi`, cut to the file-backed part of the segment that holds `center` (the code,
        when not given)."""
        seg = next((s for s in self.segs if s[0] <= (center if center is not None else self.text[0]) < s[1]), None)
        if seg is None:
            return lo, []
        lo, hi = max(lo, seg[0]), min(hi, seg[1])
        n = max(0, (hi - lo) // 4)
        return lo, list(struct.unpack_from(f">{n}I", self.b, seg[3] + lo - seg[0])) if n else []

    def backed(self, va):
        """Whether `va` has bytes in the file (code or initialized data, not bss)."""
        return any(s[0] <= va < s[1] for s in self.segs)


def masked(w):
    """An instruction with what moves between the discs blanked: branch offsets and address halves."""
    op = w >> 26
    if op == 18:            # b, bl
        return w & 0xFC000003
    if op == 16:            # bc
        return w & 0xFFFF0003
    if op in (14, 15, 24):  # addi, addis (lis), ori: the halves of an address
        return w & 0xFFFF0000
    return w


def masked_data(w):
    """A data word with what moves between the discs blanked: pointers into the program."""
    return -1 if 0x00010000 <= w < 0x02000000 else w


WINDOW_BEFORE, WINDOW_AFTER, REACH = 8, 24, 0x10000


def around(elf, center, reach, mask=masked):
    """The masked words from `reach` words before `center` to `reach` after, None outside its segment's bytes."""
    lo, w = elf.words(center - 4 * reach, center + 4 * reach, center)
    return [None] * ((lo - (center - 4 * reach)) // 4) + [mask(x) for x in w]


def find_code(us, eu, a):
    """The European address of the instruction at US address `a`, with the match's score; None if not unique."""
    return match(us, eu, a, masked, plain=0)


def find_content(us, eu, a):
    """The European address of initialized data at US address `a`, matched by the words around it (pointers
    blanked); None if not unique or too plain to tell (zeros)."""
    return match(us, eu, a, masked_data, plain=8) if us.backed(a) and a >= us.text[1] else None


def match(us, eu, a, mask, plain):
    ulo, u = us.words(a - 4 * WINDOW_BEFORE, a + 4 * WINDOW_AFTER, a)
    if len(u) < 8:
        return None
    u = [mask(w) for w in u]
    if len(set(u)) < plain:   # (mostly zeros: it would match anywhere)
        return None
    elo, e = eu.words(a - REACH, a + REACH + 4 * WINDOW_AFTER, a)
    e = [mask(w) for w in e]
    grams = defaultdict(list)
    for i in range(len(e) - 3):
        grams[(e[i], e[i + 1], e[i + 2], e[i + 3])].append(i)
    votes = Counter()
    for j in range(len(u) - 3):
        for i in grams.get((u[j], u[j + 1], u[j + 2], u[j + 3]), ()):
            votes[i - j] += 1
    if not votes:
        return None
    top = votes.most_common(1)[0][1]
    cands = {o: sum(1 for k in range(len(u)) if 0 <= o + k < len(e) and e[o + k] == u[k])
             for o, n in votes.items() if n >= top - 3}
    best = max(cands.values())
    if best < 0.8 * len(u):
        return None
    cands = [elo + 4 * o + (a - ulo) for o, s in cands.items() if s == best]
    # Code repeated elsewhere (small functions written twice) matches in several places: the instructions further
    # out tell them apart.
    for reach in (128, 512, 2048):
        if len(cands) == 1:
            break
        uw = around(us, a, reach, mask)
        scores = {c: sum(1 for x, y in zip(uw, around(eu, c, reach, mask)) if x is not None and x == y)
                  for c in cands}
        hi = max(scores.values())
        cands = [c for c, s in scores.items() if s == hi]
    if len(cands) != 1:
        return None
    return cands[0], best / len(u)


def _sext16(v):
    return v - 0x10000 if v & 0x8000 else v


def _pair_at(elf, site):
    """The address a lis at `site` and its addi or load/store (within 8 instructions, same base) build, if any."""
    w = elf.u32(site)
    if w is None or w >> 26 != 15 or (w >> 16) & 31:
        return None
    rt, hi = (w >> 21) & 31, w & 0xFFFF
    for k in range(1, 9):
        x = elf.u32(site + 4 * k)
        if x is None:
            return None
        op, ra = x >> 26, (x >> 16) & 31
        if ra == rt and (op == 14 or 32 <= op <= 55 or op in (58, 62)):
            lo = x & 0xFFFC if op in (58, 62) else x & 0xFFFF
            return (k, ((hi << 16) + _sext16(lo)) & 0xFFFFFFFF)
    return None


# Data is often reached through its object's base (a TOC slot or a lis pair holding the start of a structure or a
# vtable): a base up to NEAR below the address counts, at the same distance on the other disc.
NEAR, SITES_PER_ADDRESS = 0x1000, 12
# Two TOC addresses around a data address that moved by the same amount say it moved by that too, when they are
# close: further apart, something between them can have grown and something else shrunk.
BETWEEN_GAP = 0x1000


class SlotMap:
    """The other disc's TOC slot for each US slot. On the European disc every slot keeps its offset; on the
    Japanese and Asian 1.04 discs (TOC 0x18 higher) many moved, so a slot is mapped through code that loads it
    (lwz rX,d(r2)), matched on the other disc. Which kind of pair this is is found from a sample of slots."""

    SAMPLE = 200

    def __init__(self, us, eu, slot_sites):
        self.us, self.eu, self.sites, self.memo = us, eu, slot_sites, {}
        sample = sorted(slot_sites)[::max(1, len(slot_sites) // self.SAMPLE)]
        mapped = [(d, self.by_code(d)) for d in sample]
        mapped = [(d, e) for d, e in mapped if e is not None]
        self.same = bool(mapped) and sum(d == e for d, e in mapped) >= 0.98 * len(mapped)

    def by_code(self, d):
        if d not in self.memo:
            self.memo[d] = None
            for site in self.sites.get(d, ()):
                m = find_code(self.us, self.eu, site)
                x = self.eu.u32(m[0]) if m else None
                if x is not None and x >> 26 == 32 and (x >> 16) & 31 == 2:
                    self.memo[d] = _sext16(x & 0xFFFF)
                    break
        return self.memo[d]

    def __call__(self, d):
        return d if self.same else self.by_code(d)


def find_data(us, eu, a, sites=None, slots=None):
    """The European address of US data address `a`, with its votes and whether any of them named `a` itself (not
    only a base below it): from the TOC slots that hold it or a base below it (at the same offset, or where
    `slots` maps them), and from the code that builds them (lis pairs)."""
    votes, exact = Counter(), set()
    lo = us.toc - 0x8000
    for off in range(0, 0x10000, 4):
        v = us.u32(lo + off)
        if v is not None and 0 <= a - v <= NEAR:
            d2 = slots(off - 0x8000) if slots else off - 0x8000
            e = eu.u32(eu.toc + d2) if d2 is not None else None
            if e is not None:
                votes[e + (a - v)] += 3 if v == a else 1
                if v == a:
                    exact.add(e)
    for site, base in (sites if sites is not None else lis_sites(us, [a])).get(a, []):
        m = find_code(us, eu, site)
        got = _pair_at(eu, m[0]) if m else None
        if got:
            votes[got[1] + (a - base)] += 2 if base == a else 1
            if base == a:
                exact.add(got[1])
    if not votes:
        return None
    (v, n), *rest = votes.most_common(2) + [(None, 0)]
    return (v, n, v in exact) if n >= 2 and n > rest[0][1] else None


# Much of the game's data is reached through an address table: a function loads the table's base from a TOC slot
# (lwz rX,d(r2)), then a word of the table at a fixed offset (lwz rY,k(rX)) holds the data's address (the heap
# classes reach their arenas so). The table keeps its layout on every disc, but the TOC slot that holds its base
# can differ (the Japanese and Asian 1.04 TOCs start 0x18 higher), so the slot is read from the matched code.
TABLE_LOOK = 16   # instructions after the base's load in which the table's word is read


def table_loads(us, addrs):
    """{address: [(site, offset)]}: code that loads the address from the TOC (lwz rX,d(r2) at `site`, offset
    None), or loads a table's base from it and then the table's word at `offset` (lwz rY,offset(rX), within
    TABLE_LOOK instructions) that holds the address. At most SITES_PER_ADDRESS sites each. Also {TOC slot offset:
    [sites]}, up to 4 loads of each slot (SlotMap)."""
    want = set(addrs)
    out = defaultdict(list)
    slot_sites = defaultdict(list)
    lo, w = us.words(*us.text)
    bases = {}
    for i, x in enumerate(w):
        if x >> 26 != 32 or (x >> 16) & 31 != 2:
            continue
        rt, d = (x >> 21) & 31, _sext16(x & 0xFFFF)
        if len(slot_sites[d]) < 4:
            slot_sites[d].append(lo + 4 * i)
        t = bases.get(d)
        if t is None:
            t = bases[d] = us.u32(us.toc + d) or 0
        if t in want and len(out[t]) < SITES_PER_ADDRESS:
            out[t].append((lo + 4 * i, None))
        if not t or not us.backed(t) or t < us.text[1]:
            continue
        for y in w[i + 1:i + 1 + TABLE_LOOK]:
            op = y >> 26
            if op == 32 and (y >> 16) & 31 == rt:
                k = _sext16(y & 0xFFFF)
                v = us.u32(t + k)
                if v in want and len(out[v]) < SITES_PER_ADDRESS:
                    out[v].append((lo + 4 * i, k))
            if (y >> 21) & 31 == rt and op in (14, 15, 32, 34, 40, 58):   # rt loaded again: the base is gone
                break
    return out, slot_sites


def find_table(us, eu, a, loads):
    """The European address of US data address `a` from the code that loads it (table_loads): the load of the TOC
    slot is matched, and the European code's own slot gives the address, or the European table whose word at the
    same offset does. With the number of sites that agree."""
    votes = Counter()
    for site, k in loads.get(a, []):
        m = find_code(us, eu, site)
        x = eu.u32(m[0]) if m else None
        if x is None or x >> 26 != 32 or (x >> 16) & 31 != 2:
            continue
        t2 = eu.u32(eu.toc + _sext16(x & 0xFFFF))
        v = t2 if k is None else eu.u32(t2 + k) if t2 else None
        if v is not None:
            votes[v] += 1
    if not votes:
        return None
    (v, n), *rest = votes.most_common(2) + [(None, 0)]
    return (v, n) if n > rest[0][1] else None


def tocs(elf):
    """The TOC values of the program's function descriptors ({function, TOC} pairs in its data), lowest first: the
    ones that thousands of descriptors use."""
    count = Counter()
    for lo, fend, _mend, off, x in elf.segs:
        if x:
            continue
        n = (fend - lo) // 8
        ws = struct.unpack_from(f">{2 * n}I", elf.b, off)
        for i in range(0, 2 * n, 2):
            if elf.text[0] <= ws[i] < elf.text[1] and not ws[i] & 3:
                count[ws[i + 1]] += 1
    return sorted(t for t, n in count.items() if n >= 1000)


def anchors(us, eu):
    """Sorted (US, European) data address pairs that both discs' TOC slots give: the same slot of the same TOC
    holds the same object's address on each."""
    ut, et = tocs(us), tocs(eu)
    if len(ut) != len(et):
        return []
    pairs = {}
    for a, b in zip(ut, et):
        for off in range(-0x8000, 0x8000, 4):
            v, e = us.u32(a + off), eu.u32(b + off)
            if v is not None and e is not None and us.text[1] <= v < 0x10000000:
                pairs.setdefault(v, e)
    return sorted(pairs.items())


def find_between(pairs, a):
    """The European address of US data address `a` from the closest known pairs around it, when the two agree on
    the shift (nothing between them moved); with the gap they span."""
    keys = [p[0] for p in pairs]
    i = bisect.bisect_right(keys, a)
    if i == 0 or i == len(pairs):
        return None
    (lo_us, lo_eu), (hi_us, hi_eu) = pairs[i - 1], pairs[i]
    if lo_eu - lo_us != hi_eu - hi_us:
        return None
    return a + (lo_eu - lo_us), hi_us - lo_us


def lis_sites(us, addrs):
    """{address: [(site, base)]}: the lis instructions in the US code whose pair builds the address or a base up to
    NEAR below it, the closest first, at most SITES_PER_ADDRESS of them."""
    addrs = sorted(addrs)
    his = {((v + 0x8000) >> 16) & 0xFFFF for a in addrs for v in (a - NEAR, a)}
    out = defaultdict(list)
    lo, words = us.words(*us.text)
    for i, w in enumerate(words):
        if w >> 26 == 15 and not (w >> 16) & 31 and (w & 0xFFFF) in his:
            got = _pair_at(us, lo + 4 * i)
            if not got:
                continue
            base = got[1]
            for a in addrs[bisect.bisect_left(addrs, base):bisect.bisect_right(addrs, base + NEAR)]:
                out[a].append((lo + 4 * i, base))
    return {a: sorted(s, key=lambda sb: a - sb[1])[:SITES_PER_ADDRESS] for a, s in out.items()}


def find(us, eu, addrs):
    """{US address: (European address, how it was found)}, or (None, a hint) when there is no sure answer."""
    out = {}
    data = [a for a in addrs if not (us.text[0] <= a < us.text[1])]
    sites = lis_sites(us, addrs)
    loads, slot_sites = table_loads(us, data) if data else ({}, {})
    slots = SlotMap(us, eu, slot_sites) if data else None
    # The anchors pair TOC slots at the same offset: only where the slots kept their offsets.
    pairs = anchors(us, eu) if data and slots.same else []
    for a in addrs:
        got = None
        if a not in data:
            m = find_code(us, eu, a)
            got = (m[0], f"code, {m[1]:.0%} of the instructions") if m else None
        if got is None:
            # Data: each way that answers must give the same address. Two ways read the other disc itself: the
            # matched code's own TOC slot (find_table) and the matched words around the address (find_content).
            # The other two assume a TOC slot sits at the same offset on both discs (find_data; not so on the
            # Japanese and Asian 1.04 discs) or that nothing between two TOC addresses moved (find_between), so
            # they only answer when neither of the first two does, and a disagreement is noted.
            answers, others = {}, []
            t = find_table(us, eu, a, loads)
            if t:
                answers.setdefault(t[0], []).append(f"the code that loads it, {t[1]} place{'s' * (t[1] > 1)}")
            m = find_content(us, eu, a)
            if m:
                answers.setdefault(m[0], []).append(f"{m[1]:.0%} of the words around it")
            m = find_data(us, eu, a, sites, slots)
            unsure = None
            if m and (slots.same or m[2]):
                others.append((m[0], f"{m[1]} references"))
            elif m:
                # On a disc whose data was reordered (its TOC slots moved too), a base below says little: the
                # object at the base and the address can have moved by different amounts.
                unsure = (m[0], f"{m[1]} references to bases below it, on a disc whose data moved in pieces")
            m = find_between(pairs, a) if a in data else None
            if m and m[1] <= BETWEEN_GAP:
                others.append((m[0], f"between TOC addresses 0x{m[1]:X} apart"))
            if answers:
                for v, how in others:
                    if v in answers:
                        answers[v].append(how)
                    else:
                        for hows in answers.values():
                            hows.append(f"not 0x{v:08X} ({how})")
            else:
                for v, how in others:
                    answers.setdefault(v, []).append(how)
            if len(answers) == 1:
                (v, how), = answers.items()
                got = (v, "data, " + "; ".join(how))
            elif answers:
                got = (None, "the ways disagree: " + ", ".join(f"0x{v:08X} ({'; '.join(h)})"
                                                                for v, h in answers.items()))
            elif m:
                got = (None, f"perhaps 0x{m[0]:08X}: the TOC addresses around it, 0x{m[1]:X} apart, moved alike")
            elif unsure:
                got = (None, f"perhaps 0x{unsure[0]:08X} ({unsure[1]})")
        out[a] = got or (None, "")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    f = sub.add_parser("find")
    f.add_argument("us_elf")
    f.add_argument("eu_elf")
    f.add_argument("addr", nargs="+")
    v = sub.add_parser("verify")
    v.add_argument("us_elf")
    v.add_argument("eu_elf")
    args = ap.parse_args(argv)

    if args.cmd == "check":
        errors = check()
        for e in errors:
            print(e)
        tables = sorted((des_region.ROOT / "port" / "regions").glob("*.inc"))
        print(", ".join(f"{t.name}: {len(des_region.load_table(t))} addresses" for t in tables) + ", "
              + ("all used and every use covered" if not errors else f"{len(errors)} problems"))
        return 1 if errors else 0

    us, eu = Elf(args.us_elf), Elf(args.eu_elf)
    if args.cmd == "find":
        addrs = [int(a, 0) for a in args.addr]
        for a, (v, how) in find(us, eu, addrs).items():
            print(f"DES_REGION_ADDR(0x{a:08X}, 0x{v:08X})  /* {how} */" if v is not None else
                  f"0x{a:08X}: no sure match{'; ' + how if how else ''}. Find it by hand.")
        return 0

    table = des_region.load_table()
    bad, unchecked = [], []
    for a, (v, how) in find(us, eu, sorted(table)).items():
        if v is None:
            unchecked.append(f"  0x{a:08X} (the table: 0x{table[a]:08X}){': ' + how if how else ''}")
        elif v != table[a]:
            bad.append(f"0x{a:08X}: the table has 0x{table[a]:08X}, the EBOOTs say 0x{v:08X} ({how})")
    for b in bad:
        print(b)
    if unchecked:
        print("no sure answer, check by hand:")
        for u in unchecked:
            print(u)
    print(f"{len(table)} lines: {len(table) - len(bad) - len(unchecked)} agree, {len(bad)} differ, "
          f"{len(unchecked)} unchecked")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
