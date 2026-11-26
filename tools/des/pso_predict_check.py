#!/usr/bin/env python3
"""How much of what the game asked for in play the predicted pipelines cover (docs/97).

    des_host --pso-predict <game folder> <EBOOT.elf> pred_sdr.tsv
    des_host --pso-predict <game folder> <EBOOT.elf> pred_hdr.tsv hdr
    python tools/des/pso_predict_check.py pred_sdr.tsv pred_hdr.tsv -- <seen or index .tsv> [...]

The files after -- are pipeline index rows recorded in play (a cache's pso_index_seen.tsv, or
port/data/pso_index/*.tsv). Prints the share of their rows the predicted files hold, and the rows
they miss, with the fields that tell them apart from the nearest predicted row with the same two
programs. Standard library only.
"""
import collections
import sys

FIELDS = "vp vstart vfetch fp exp32 cube d2c clamp blend nrt rtfmt cmask cull depth hdrv".split()


def rows(path):
    out = set()
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                v = [int(x, 16) for x in line.split("\t")]
            except ValueError:
                continue
            if len(v) in (14, 15):
                out.add(tuple(v + [0] * (15 - len(v))))
    return out


def main(argv):
    if "--" not in argv or argv.index("--") == 0 or argv.index("--") == len(argv) - 1:
        print(__doc__.strip())
        return 2
    cut = argv.index("--")
    pred = set().union(*(rows(p) for p in argv[:cut]))
    seen = set().union(*(rows(p) for p in argv[cut + 1:]))
    if not seen:
        print("no recorded rows")
        return 1
    miss = sorted(seen - pred)
    print("predicted %d rows; recorded %d; covered %d (%.1f%%)" % (
        len(pred), len(seen), len(seen) - len(miss), 100.0 * (len(seen) - len(miss)) / len(seen)))
    by_pair = collections.defaultdict(list)
    for r in pred:
        by_pair[(r[0], r[3])].append(r)
    why = collections.Counter()
    for r in miss:
        near = by_pair.get((r[0], r[3]))
        if not near:
            why["no predicted row has these two programs"] += 1
            continue
        best = min(near, key=lambda p: sum(a != b for a, b in zip(p, r)))
        why["differs in " + " ".join(FIELDS[i] for i in range(15) if best[i] != r[i])] += 1
    for k, n in why.most_common():
        print("  %5d  %s" % (n, k))
    for r in miss[:20]:
        print("  missed: " + "\t".join("%x" % v for v in r))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
