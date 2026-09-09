#!/usr/bin/env python3
"""Side-by-side of one spot: ours, the original's, and the difference.

    python3 tools/pixel_sbs.py <city> --crop x,y,w,h [--scale 4] out.png

Left is arcology --soft, middle is the game's own renderer driven through its own
blitter, right marks every differing pixel in magenta.  Same margin trick
as pixel_diff: both sides are rendered wider than the box that is shown,
because sprites straddling the edge take the clipped path.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from pixel_diff import read_rgb_png
from render_pixels import render
from sc2kpack import write_indexed_png, TRANSPARENT
from gif import write_gif_anim

EXE = HERE.parent / "build/arcology"
ASSETS = HERE.parent / "assets"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("out")
    ap.add_argument("--crop", required=True)
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--margin", type=int, default=48)
    ap.add_argument("--underground", action="store_true")
    ap.add_argument("--view", type=int, default=0)
    #  Drive $16B74 over the whole map rather than calling the per-tile
    #  routine in an order of my choosing: the sweep is then the game's,
    #  so nothing about tile order is being assumed by the measurement.
    ap.add_argument("--whole", action="store_true")
    #  >1 emits an animated GIF instead of a PNG.  The sheet's pixels do
    #  not move, so only the colour table is rebuilt per frame -- and it
    #  is rebuilt by arcology --soft, so the permutation has one implementation.
    ap.add_argument("--frames", type=int, default=1)
    ap.add_argument("--delay", type=int, default=20)
    a = ap.parse_args()
    cx, cy, cw, ch = (int(v) for v in a.crop.split(","))
    m = a.margin
    ox, oy, ow, oh = cx - m, cy - m, cw + 2 * m, ch + 2 * m

    import tempfile
    tmp = Path(tempfile.mkdtemp(prefix="pixsbs-"))
    tmp.mkdir(exist_ok=True)
    cmd = [str(EXE), "--soft", str(ASSETS), a.city, str(tmp / "mine.png"), "--zoom", "32",
           "--crop", "%d,%d,%d,%d" % (ox, oy, ow, oh)]
    if a.underground:
        cmd.append("--underground")
    if a.view:
        cmd += ["--view", str(a.view)]
    subprocess.run(cmd, capture_output=True, check=True)
    _w, _h, mine = read_rgb_png(tmp / "mine.png")
    pal = [tuple(c) for c in
           json.loads((HERE.parent / "assets/atlas.json").read_text())["palette"]]
    idx = {}
    for i, c in enumerate(pal):
        idx.setdefault(c, i)
    #  The sentinel has to be an index the game never paints, and 255 is
    #  not one: it is black, and every dark window in a skyscraper is a
    #  real 255.  Using it blanked those pixels in the middle panel AND
    #  excluded them from the comparison.  Index 0 is the one the game
    #  leaves alone -- 406 pixels of a 900x700 window, all off-map.
    SENT = 0
    game = render(a.city, (ox, oy, ow, oh), a.underground, a.view, bg=SENT,
                  whole=a.whole)

    #  A magenta that the game's palette does not contain, so the marks
    #  cannot be mistaken for art.
    MARK = 254
    pal = list(pal)
    pal[MARK] = (255, 0, 255)

    S, GAP = a.scale, 6
    Wpx = (cw * 3 + GAP * 2) * S
    sheet = [[0] * Wpx for _ in range(ch * S)]
    ndiff = 0
    for y in range(ch):
        for x in range(cw):
            gm = game[y + m][x + m]
            mi = idx.get(mine[y + m][x + m], 0)
            gi = 0 if gm == SENT else gm
            same = (gm == SENT) or (pal[gm] == mine[y + m][x + m])
            if not same:
                ndiff += 1
            for dy in range(S):
                for dx in range(S):
                    Y = y * S + dy
                    sheet[Y][x * S + dx] = mi
                    sheet[Y][(cw + GAP) * S + x * S + dx] = gi
                    sheet[Y][(cw + GAP) * 2 * S + x * S + dx] = (
                        MARK if not same else mi)
    if a.frames > 1:
        #  One phase per frame, read back from arcology --soft so atlas_animate
        #  stays the only implementation of the permutation.  The magenta
        #  sits at 254, outside every animated run, so it never moves.
        pals = []
        for k in range(a.frames):
            f = tmp / ("p%d.png" % k)
            subprocess.run([str(EXE), "--soft", str(ASSETS), a.city, str(f), "--zoom",
                            "32", "--indexed", "--phase", str(k), "--crop",
                            "%d,%d,32,32" % (cx, cy)],
                           capture_output=True, check=True)
            from sc2kpack import read_indexed_png
            q = [tuple(c) for c in read_indexed_png(f)[3]]
            while len(q) < 256:
                q.append((0, 0, 0))
            q[MARK] = (255, 0, 255)
            pals.append(q)
        runs = [(x["first"], x["count"])
                for x in json.loads((HERE.parent / "assets/atlas.json")
                                    .read_text())["animated"]]
        n = write_gif_anim(a.out, Wpx, ch * S, sheet, pals, runs,
                           delay_cs=a.delay)
        print("%d differing pixels; wrote %s (ours | original | diff), "
              "%dx%d %.1f KB" % (ndiff, a.out, Wpx, ch * S, n / 1024))
    else:
        write_indexed_png(a.out, Wpx, ch * S, sheet, pal, TRANSPARENT)
        print("%d differing pixels; wrote %s (ours | original | diff)"
              % (ndiff, a.out))


if __name__ == "__main__":
    main()
