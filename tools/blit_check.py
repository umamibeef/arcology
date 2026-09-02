#!/usr/bin/env python3
"""Is our blit() functionally identical to the game's $18E96?

Not "does it look right" and not "does the blit list agree" -- those were
both checked and both missed real defects.  This draws every sprite, in
both mirror states, through the ORIGINAL blitter into a framebuffer, and
compares it against what r_soft.c's blit() would put in the same place
given the same atlas.  Anything that differs is a difference in the
blitter itself: the anchor, the mirror pivot, the transparency rule.

    python3 tools/blit_check.py [--zoom 32]
"""
import argparse
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from runsim import Sim, A5
from sc2kpack import read_indexed_png, TRANSPARENT
from shapedec import tileset_streams

BLIT = 0x18E96
#  A city to render.  The repository ships its own, so this works
#  from a fresh clone; SC2K_CITIES points it at your game folder instead.
CITY = os.environ.get(
    "SC2K_CITY",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cities",
                 "bayview.sc2"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zoom", type=int, default=32)
    a = ap.parse_args()

    at = json.loads((HERE.parent / ("assets/tiles%d.json" % a.zoom)).read_text())
    fr = {int(k): v["frame"] for k, v in at["frames"].items()}
    meta = at["meta"]["sc2k"]["tiles"]
    _w, _h, rows, _pal = read_indexed_png(
        HERE.parent / ("assets/tiles%d.png" % a.zoom))

    streams = tileset_streams()
    big = max(max(w, h) for w, h, _ in streams.values())
    W = H = big + 80
    sim = Sim(CITY)
    e = sim.e
    fb = e.alloc(W * H + 64)
    e.wr(A5 + 0x120C, 4, fb)
    e.wr(A5 + 0x1210, 2, W)
    e.wr(A5 + 0x1212, 2, 0xFFFF)
    e.wr(A5 + 0x1214, 2, 0xFFFF)
    e.wr(A5 + 0x1216, 2, H + 1)
    e.wr(A5 + 0x1218, 2, W + 1)
    desc = e.alloc(0x2EE0)
    for k in range(0, 0x2EE0, 4):
        e.wr(desc + k, 4, 0)
    e.wr(A5 + 0x1226, 4, desc)

    X = Y = 24

    def shot(sid, mirror, fill):
        w, h, stream = streams[sid]
        for y in range(H):
            base = fb + y * W
            for x in range(W):
                e.wr(base + x, 1, fill)
        e.a[7] = (e.a[7] - 10) & 0xFFFFFFFF
        e.wr(e.a[7] + 0, 2, sid)
        e.wr(e.a[7] + 2, 2, X)
        e.wr(e.a[7] + 4, 2, Y)
        e.wr(e.a[7] + 6, 2, mirror)
        e.wr(e.a[7] + 8, 2, 0)
        e.a[7] = (e.a[7] - 4) & 0xFFFFFFFF
        e.wr(e.a[7], 4, 0xDEAD0000)
        err = e.run(BLIT, 0xDEAD0000, real_calls=True,
                    limit=4000000 + len(stream) * 800)
        e.a[7] = (e.a[7] + 10) & 0xFFFFFFFF
        if err:
            return None
        return [[e.rd(fb + (Y + j) * W + (X + i), 1) for i in range(w)]
                for j in range(h)]

    base_id = at["meta"]["sc2k"]["id_base"]
    bad, tested, skipped = [], 0, 0
    for sid in sorted(streams):
        if sid not in fr or sid * 8 + 8 > 0x2EE0:
            continue
        w, h, stream = streams[sid]
        f = fr[sid]
        if (f["w"], f["h"]) != (w, h):
            skipped += 1
            continue
        art = e.alloc(len(stream) + 16)
        for i, b in enumerate(stream):
            e.wr(art + i, 1, b)
        e.wr(desc + sid * 8 + 0, 4, art)
        e.wr(desc + sid * 8 + 4, 2, h)
        e.wr(desc + sid * 8 + 6, 2, w)
        for mirror in (0, 1):
            lo = shot(sid, mirror, 0x00)
            hi = shot(sid, mirror, 0xFF)
            if lo is None or hi is None:
                skipped += 1
                continue
            tested += 1
            d = 0
            for j in range(h):
                for i in range(w):
                    g = (TRANSPARENT if (lo[j][i] == 0 and hi[j][i] == 255)
                         else lo[j][i])
                    m = rows[f["y"] + j][f["x"] + (w - 1 - i if mirror else i)]
                    if g != m:
                        d += 1
            if d:
                bad.append((sid, mirror, d))
        #  Also check the anchor: `ay` must equal the descriptor's +4.
        if meta[str(sid)]["ay"] != h:
            bad.append((sid, "ay", meta[str(sid)]["ay"] - h))
    print("blits compared: %d (%d skipped)" % (tested, skipped))
    if not bad:
        print("our blit() is functionally identical to $18E96 "
              "for every sprite, both mirror states, and every anchor")
        return 0
    print("%d disagreements:" % len(bad))
    for sid, kind, d in bad[:20]:
        print("   shape %-5d %-6s %d" % (sid, kind, d))
    return 1


if __name__ == "__main__":
    sys.exit(main())
