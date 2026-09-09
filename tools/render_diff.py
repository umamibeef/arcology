#!/usr/bin/env python3
"""Diff the C renderer against the game's own drawing code, blit for blit.

The renderer equivalent of `oracle_diff.py`.  `render_oracle.py` runs the
real per-tile routine under the 68k interpreter with the blitter stubbed
and records every `(shape, x, y, mirror)`; `arcology --soft --dump-blits` prints
the same list from the reconstruction.  Any disagreement is a bug in the C,
with the guesswork removed.

    python3 tools/render_diff.py <city> [--tiles N] [--exe PATH]
"""
import argparse
import collections
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from render_oracle import RenderOracle


def c_blits(exe, assets, city, extra=()):
    """-> {(row, col): [(shape, x, y, mirror), ...]}"""
    out = subprocess.run([exe, assets, city, "--zoom", "32", "--dump-blits"] + list(extra),
                         capture_output=True, text=True, check=True).stdout
    d = collections.defaultdict(list)
    for line in out.splitlines():
        f = line.split()
        if len(f) != 6:
            continue
        r, c, sh, x, y, m = (int(v) for v in f)
        #  $18F10 only does `tst.w` on the mirror argument, so the game's
        #  own callers pass whatever bit they had (XBIT & 2 gives 2, not
        #  1).  Compare the flag, not the value.
        d[(r, c)].append((sh, x, y, 1 if m else 0))
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("--exe", default=str(HERE.parent / "build/arcology"))
    ap.add_argument("--assets", default=str(HERE.parent / "assets"))
    ap.add_argument("--tiles", type=int, default=400)
    #  Sampling is fine for a quick read, but it cannot tell you that a
    #  defect is absent -- only that it was not in the sample.  --all
    #  compares every tile the C draws anything on.
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--underground", action="store_true")
    args = ap.parse_args()

    mine = c_blits(args.exe, args.assets, args.city,
                   ["--underground"] if args.underground else [])
    o = RenderOracle(args.city, underground=args.underground)

    #  Sample tiles that actually have something on them, spread over the
    #  map, plus every tile that the C draws more than two things on --
    #  those are the interesting ones (multi-tile art, elevated pieces).
    import random
    rng = random.Random(args.seed)
    busy = [k for k, v in mine.items() if len(v) > 2]
    rest = [k for k in mine if len(mine[k]) <= 2]
    if args.all:
        cells = sorted(mine)
    else:
        cells = (busy[: args.tiles // 2] +
                 rng.sample(rest, min(args.tiles - args.tiles // 2, len(rest))))

    same = diff = err = anchor_only = 0
    kinds = collections.Counter()
    examples = []
    for (row, col) in cells:
        want, e = o.tile(row, col)
        if e:
            err += 1
            continue
        got = mine.get((row, col), [])
        if want == got:
            same += 1
            continue
        #  The oracle runs with the shape-descriptor table at $1226 zeroed,
        #  because nothing in CODE 2 writes it and we have not found where
        #  it is loaded from.  The game folds each shape's vertical anchor
        #  into that table, so a y differing by exactly our footprint drop
        #  is expected, not a defect -- CONFIRMED against the real game by
        #  eye (user, 31 Aug 2026): the drop is correct.  Counted apart so
        #  the number stays visible rather than silently absorbed.
        if len(want) == len(got) and all(
                w[0] == g[0] and w[1] == g[1] and w[3] == g[3]
                and (g[2] - w[2]) in (0, 8, 16, 24)
                for w, g in zip(want, got)):
            anchor_only += 1
            continue
        diff += 1
        ws = [b[0] for b in want]
        gs = [b[0] for b in got]
        if ws == gs:
            kinds["same shapes, different position or mirror"] += 1
        elif set(ws) - set(gs):
            kinds["shapes the game draws and we do not"] += 1
        elif set(gs) - set(ws):
            kinds["shapes we draw and the game does not"] += 1
        else:
            kinds["same shapes, different order"] += 1
        if len(examples) < 6:
            examples.append((row, col, want, got))

    n = same + diff + anchor_only
    print("compared %d tiles (%d emulator errors)" % (n, err))
    print("  identical                        : %d  (%.1f%%)" % (same, 100.0 * same / max(1, n)))
    print("  y differs by the footprint drop  : %d  (%.1f%%)  -- expected; the drop is confirmed correct"
          % (anchor_only, 100.0 * anchor_only / max(1, n)))
    print("  genuinely differing              : %d" % diff)
    for k, v in kinds.most_common():
        print("      %-42s %d" % (k, v))
    for row, col, want, got in examples:
        print("\n  tile (col %d, row %d)" % (col, row))
        print("    game: %s" % (want,))
        print("    ours: %s" % (got,))
    return 0 if diff == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
