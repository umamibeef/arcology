#!/usr/bin/env python3
"""Scan a whole city for pixel differences and say WHERE they are.

pixel_diff.py answers "how close is this rectangle".  This answers the
question that actually matters: which tiles are wrong.  It renders the
entire map once with the original renderer (render_pixels) and once with
the reconstruction, diffs them, clusters the differing pixels, maps each
cluster back to the tiles that could have painted it, and prints the blit
lists for those tiles side by side.

    python3 tools/pixel_scan.py <city> [--underground] [--top 12]

Note the margin: sprites straddling the edge of the rendered area take
$18E96's clipped path and the tile culling has to stop somewhere, so the
outermost band disagrees for reasons that are not the renderer's.  The
count of such pixels scales with the perimeter, not the area, which is how
it was spotted.  The scan therefore ignores a band around the edge.
"""
import argparse
import collections
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from pixel_diff import read_rgb_png
from render_pixels import render, OX, OY, TW, TH
import json

EXE = HERE.parent / "build/arcology"
ASSETS = HERE.parent / "assets"
BAND = 64


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("--underground", action="store_true")
    ap.add_argument("--view", type=int, default=0)
    ap.add_argument("--crop", default=None,
                    help="x,y,w,h; default is the whole canvas")
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--tmp", default="/tmp/pixscan")
    a = ap.parse_args()

    tmp = Path(a.tmp)
    tmp.mkdir(parents=True, exist_ok=True)
    if a.crop:
        cx, cy, cw, ch = (int(v) for v in a.crop.split(","))
    else:
        cx, cy, cw, ch = 0, 0, 128 * TW + TW * 4, 128 * TH + 420 * TH // 16

    cmd = [str(EXE), "--soft", str(ASSETS), a.city, str(tmp / "mine.png"),
           "--zoom", "32", "--crop", "%d,%d,%d,%d" % (cx, cy, cw, ch)]
    if a.underground:
        cmd.append("--underground")
    if a.view:
        cmd += ["--view", str(a.view)]
    subprocess.run(cmd, capture_output=True, check=True)
    _w, _h, mine = read_rgb_png(tmp / "mine.png")
    pal = [tuple(c) for c in
           json.loads((HERE.parent / "assets/atlas.json").read_text())["palette"]]

    SENTINEL = 255
    game = render(a.city, (cx, cy, cw, ch), a.underground, a.view,
                  bg=SENTINEL)

    pts = []
    same = 0
    for y in range(BAND, ch - BAND):
        row_g, row_m = game[y], mine[y]
        for x in range(BAND, cw - BAND):
            g = row_g[x]
            if g == SENTINEL:
                continue
            if pal[g] == row_m[x]:
                same += 1
            else:
                pts.append((x, y))
    tot = same + len(pts)
    print("painted pixels compared: %d" % tot)
    print("  identical : %d  (%.3f%%)" % (same, 100.0 * same / max(tot, 1)))
    print("  differing : %d  (%.3f%%)" % (len(pts), 100.0 * len(pts) / max(tot, 1)))
    if not pts:
        return

    #  Attribute each differing pixel to the tile whose flat projection is
    #  nearest.  Altitude shifts a tile up, so this is approximate -- it is
    #  a pointer to a neighbourhood, not a claim about one tile.
    cells = collections.Counter()
    for x, y in pts:
        gx, gy = x + cx - OX, y + cy - OY
        s = gy * 2 // TH          # row + col
        d = gx // (TW // 2)       # row - col
        row, col = (s + d) // 2, (s - d) // 2
        cells[(row // 4 * 4, col // 4 * 4)] += 1
    print("\nworst neighbourhoods (row, col) -> differing pixels:")
    for (r, c), n in cells.most_common(a.top):
        print("   row %3d col %3d   %5d px" % (r, c, n))


if __name__ == "__main__":
    main()
