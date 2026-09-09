#!/usr/bin/env python3
"""Diff arcology --soft's pixels against the GAME'S OWN renderer.

    python3 tools/pixel_diff.py <city> --crop x,y,w,h [--underground]

render_pixels.py drives the original per-tile renderer with its real
blitter into a framebuffer; this renders the same rectangle with the C
reconstruction and compares pixel for pixel.  Unlike render_diff.py this
sees the per-shape anchor, the draw order and the palette -- everything
that stubbing $18E96 used to hide.
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import struct
import zlib

from sc2kpack import write_indexed_png, TRANSPARENT
from render_pixels import render


def read_rgb_png(path):
    """8-bit truecolour PNG -> (w, h, [[(r,g,b), ...], ...]).

    arcology --soft's default output is exactly that; --indexed is avoided here
    because lodepng picks whatever bit depth fits and a small crop comes
    back as 4-bit, which is a nuisance to compare against.
    """
    d = Path(path).read_bytes()
    pos, idat = 8, b""
    w = h = 0
    while pos < len(d):
        ln = struct.unpack(">I", d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        pay = d[pos + 8:pos + 8 + ln]
        if typ == b"IHDR":
            w, h, _bd, ct, _c, _f, _i = struct.unpack(">IIBBBBB", pay[:13])
            if ct != 2:
                raise ValueError("%s: expected truecolour" % path)
        elif typ == b"IDAT":
            idat += pay
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp, stride = 3, w * 3
    out, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if (pa <= pb and pa <= pc)
                                      else (b if pb <= pc else c))) & 255
        out.append([tuple(line[k:k + 3]) for k in range(0, stride, 3)])
        prev = line
    return w, h, out

EXE = HERE.parent / "build/arcology"
ASSETS = HERE.parent / "assets"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("--crop", required=True)
    ap.add_argument("--underground", action="store_true")
    ap.add_argument("--view", type=int, default=0)
    ap.add_argument("--out", default=None)
    #  Both renderers are asked for a bigger rectangle than we compare.
    #  Sprites straddling the boundary go down $18E96's clipped path, and
    #  the tile culling in render_pixels has to stop somewhere, so the
    #  outermost band disagrees for reasons that have nothing to do with
    #  the reconstruction: the differing pixel count scales with the
    #  crop's PERIMETER, not its area.  Compare the interior.
    ap.add_argument("--margin", type=int, default=48)
    a = ap.parse_args()
    cx, cy, cw, ch = (int(v) for v in a.crop.split(","))
    m = max(0, a.margin)
    ox, oy, ow, oh = cx - m, cy - m, cw + 2 * m, ch + 2 * m

    tmp = Path(a.out or "/tmp/pixdiff")
    tmp.mkdir(parents=True, exist_ok=True)

    #  The reconstruction.
    cmd = [str(EXE), "--soft", str(ASSETS), a.city, str(tmp / "mine.png"),
           "--zoom", "32", "--crop", "%d,%d,%d,%d" % (ox, oy, ow, oh)]
    if a.underground:
        cmd.append("--underground")
    if a.view:
        cmd += ["--view", str(a.view)]
    subprocess.run(cmd, capture_output=True, check=True)
    _w, _h, mine = read_rgb_png(tmp / "mine.png")
    import json
    pal = [tuple(c) for c in
           json.loads((HERE.parent / "assets/atlas.json").read_text())["palette"]]

    #  The original.  Fill the framebuffer with a colour the art never
    #  uses so "the game painted nothing here" is distinguishable.
    SENTINEL = 255
    game = render(a.city, (ox, oy, ow, oh), a.underground, a.view,
                  bg=SENTINEL)

    same = diff = unpainted = 0
    marks = [[0] * cw for _ in range(ch)]
    for y in range(m, m + ch):
        for x in range(m, m + cw):
            g = game[y][x]
            if g == SENTINEL:
                unpainted += 1
                continue
            if pal[g] == mine[y][x]:
                same += 1
                marks[y - m][x - m] = g
            else:
                diff += 1
                marks[y - m][x - m] = 0
    tot = same + diff
    print("painted pixels compared: %d" % tot)
    print("  identical : %d  (%.2f%%)" % (same, 100.0 * same / max(tot, 1)))
    print("  differing : %d  (%.2f%%)" % (diff, 100.0 * diff / max(tot, 1)))
    print("  background (game painted nothing): %d" % unpainted)
    write_indexed_png(tmp / "game.png", cw, ch,
                      [[0 if v == SENTINEL else v for v in r[m:m + cw]]
                       for r in game[m:m + ch]], pal, TRANSPARENT)
    write_indexed_png(tmp / "diff.png", cw, ch, marks, pal, TRANSPARENT)
    print("wrote %s/{mine,game,diff}.png" % tmp)


if __name__ == "__main__":
    main()
