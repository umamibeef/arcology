#!/usr/bin/env python3
"""The pixel oracle: render a city with the GAME'S OWN renderer.

`render_oracle.py` stubs `$18E96` and records a list of blits.  That is
blind to one whole stage: the game and the reconstruction both apply a
per-shape anchor before drawing, and stubbing the blitter cancels it on
both sides.  Every join-alignment defect lived in that blind spot.

`$18E96` turns out to need no toolbox at all -- it is a pure software
blitter.  Its descriptor table at $1226(a5) is a POINTER table:

    +0  pointer to the shape's span stream
    +4  height    added to y to make the clip rect ($18EC4)
    +6  width     added to x                       ($18ECE)

and $19238 paints into $120C(a5) with $1210(a5) as rowBytes, 8 bits per
pixel.  So we can hand it a framebuffer and get the original renderer's
actual pixels.

    python3 tools/render_pixels.py <city> out.png [--crop x,y,w,h]
                                   [--underground] [--view N]

Coordinates match arcology --soft's canvas exactly (origin 2048,200 at zoom 32),
so `--crop` takes the same numbers and the two images can be diffed.
"""
import argparse
import struct
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from runsim import Sim, A5
from sc2kpack import write_indexed_png, TRANSPARENT

TILE, UGND, VIEWT = 0x016FF8, 0x0161DC, 0x0167CC
#  The surface has a SEPARATE renderer per zoom, picked at $15490; the
#  underground and data-view renderers are zoom-generic (they build
#  2<<zoom, 3<<zoom and 4<<zoom in their own prologues), so those two
#  drivers serve all three sizes.  Only zoom 32 had ever been checked.
TILE16, TILE8 = 0x017A6C, 0x0184DC
#  The whole-map renderer.  Driving the per-tile function ourselves means
#  imposing OUR sweep order on the original, which makes an ordering bug
#  invisible by construction -- and it skips the clip-rect adjustment
#  $16B7C..$16B88 makes on entry.  Running this instead removes both
#  assumptions: the game walks its own map.
MAP32, MAP16, MAP8 = 0x016B74, 0x017978, 0x0183F2
#  The underground view has its own whole-map driver.  Running $16B74 for
#  it renders the SURFACE, which then gets compared against an underground
#  reconstruction and reports a nonsense 16% -- a harness bug that looks
#  exactly like a renderer defect.
MAPUG = 0x015FAC
#  And the data views are a third renderer again: $160CA drives $167CC,
#  which is where the $168C8 overlay jump table lives.  $16FF8 ignores
#  $2C34 entirely, so driving it for a view compares a tinted picture
#  against an untinted one and reports 8%.
MAPVIEW = 0x0160CA
G_DST, G_ROWB = 0x120C, 0x1210
G_TOP, G_LEFT, G_BOT, G_RIGHT = 0x1212, 0x1214, 0x1216, 0x1218
G_DESC = 0x1226
G_ZOOM, G_VIEW = 0x2C26, 0x2C34
G_SCROLL_X, G_SCROLL_Y = 0x2C3C, 0x2C3E
G_CLIP_T, G_CLIP_B = 0x122C, 0x1230
G_CLIP_L, G_CLIP_R = 0x122E, 0x1232
G_CORNER, ROT_TABLE = 0x122A, -0x7DD4
GAME = Path(os.environ.get(
    "SC2K_CITIES",
    Path.home() / "Downloads" / "SimCity 2000\u00ae Collection"))
OX, OY, TW, TH = 2048, 200, 32, 16


from tset import shapes as tileset_shapes


def render(city, crop, underground=False, view=0, bg=0, zoneflag=None,
           whole=False, zoom=2):
    cx, cy, cw, ch = crop
    #  arcology --soft puts its canvas at ox = 128 * tile_w / 2, oy = 200 * tile_h
    #  / 16 (soft.c $645/$650); match it so both sides land on the same
    #  coordinates at every zoom, not just 32.
    tw, th = 8 << zoom, 4 << zoom
    ox, oy = 128 * tw // 2, 200 * th // 16
    sim = Sim(city)
    e = sim.e

    #  Art and descriptor FIRST, frame buffer last, with a wide guard after
    #  it.  The emulator heap is a bump allocator with no bounds check, so
    #  anything the blitter writes past the buffer's end lands in whatever
    #  was allocated next -- and that used to be the sprite streams it then
    #  reads back, which surfaces much later as an invalid span and a
    #  _Debugger trap in the middle of a render.  This way a stray write
    #  hits dead space instead of the art.
    shapes = tileset_shapes()
    desc = e.alloc(0x2EE0)
    for k in range(0, 0x2EE0, 4):
        e.wr(desc + k, 4, 0)
    for sid, (w, h, stream) in shapes.items():
        if sid * 8 + 8 > 0x2EE0 or not w or not h:
            continue
        art = e.alloc(len(stream) + 16)
        for i, byte in enumerate(stream):
            e.wr(art + i, 1, byte)
        e.wr(desc + sid * 8 + 0, 4, art)
        e.wr(desc + sid * 8 + 4, 2, h)
        e.wr(desc + sid * 8 + 6, 2, w)
    e.wr(A5 + G_DESC, 4, desc)

    fb = e.alloc(cw * ch + 64)
    e.alloc(1 << 20)                      # guard
    word = (bg << 24) | (bg << 16) | (bg << 8) | bg
    for k in range(0, cw * ch, 4):
        e.wr(fb + k, 4, word)
    e.wr(A5 + G_DST, 4, fb)
    e.wr(A5 + G_ROWB, 2, cw)
    #  $18E96 culls on y+h <= top, x+w <= left, y >= bottom, x >= right.
    e.wr(A5 + G_TOP, 2, 0xFFFF)
    e.wr(A5 + G_LEFT, 2, 0xFFFF)
    e.wr(A5 + G_BOT, 2, ch + 1)
    e.wr(A5 + G_RIGHT, 2, cw + 1)


    e.wr(A5 + G_ZOOM, 2, zoom)
    e.wr(A5 + G_VIEW, 2, view)
    e.wr(A5 + G_SCROLL_X, 2, (ox - cx) & 0xFFFF)
    e.wr(A5 + G_SCROLL_Y, 2, (oy - cy) & 0xFFFF)
    e.wr(A5 + G_CLIP_T, 2, 0x8000)
    e.wr(A5 + G_CLIP_B, 2, 0x7FFF)
    e.wr(A5 + G_CLIP_L, 2, 0x8000)
    e.wr(A5 + G_CLIP_R, 2, 0x7FFF)
    rot = e.rd(A5 + 0x2C24, 2) & 3
    e.wr(A5 + G_CORNER, 2, e.rd(A5 + ROT_TABLE + rot * 2, 2))
    #  The four view flags -- $7DE0 buildings-over-zone-tint ($1779A,
    #  $164E4), $7DE1 signs ($FB32), $7DE2 and $7DE3 -- are NOT written
    #  anywhere in CODE_2; the menu code that toggles them lives in
    #  another segment.  They are initialised data, and the A5 image the
    #  harness loads already carries the application's own shipped
    #  defaults: all four are 1.  So the harness leaves them alone and
    #  reads the game's values instead of choosing them, which is one
    #  fewer assumption in the oracle.  `zoneflag` overrides only when a
    #  caller deliberately wants the other state.
    if zoneflag is not None:
        e.wr(A5 - 0x7DE0, 1, zoneflag)
    #  $7DE4 is the one the harness really is choosing: it selects the
    #  underground view, which is the mode being rendered.
    e.wr(A5 - 0x7DE4, 1, 1 if underground else 0)
    entry = UGND if underground else (
        VIEWT if view else (TILE, TILE16, TILE8)[2 - zoom])

    if whole:
        #  $16B7C..$16B88 ADJUST the clip bounds on entry (-0x80 on the
        #  left, +0xDC on the bottom, -0x20 on the top).  Handing it
        #  0x8000/0x7FFF makes those adjustments wrap, the test rejects
        #  every tile and the render comes back blank -- which is exactly
        #  what happened.  Give it finite bounds with room to move.
        e.wr(A5 + G_CLIP_T, 2, (-8000) & 0xFFFF)
        e.wr(A5 + G_CLIP_B, 2, 8000)
        e.wr(A5 + G_CLIP_L, 2, (-8000) & 0xFFFF)
        e.wr(A5 + G_CLIP_R, 2, 8000)
        #  $16B74 loops every tile itself, in two passes: diagonals from
        #  row 0 for d7 = 0..127 ($16B8E), then from col 127 ($16BF6).
        e.a[7] = (e.a[7] - 4) & 0xFFFFFFFF
        e.wr(e.a[7], 4, 0xDEAD0000)
        drv = MAPUG if underground else (
            MAPVIEW if view else (MAP32, MAP16, MAP8)[2 - zoom])
        err = e.run(drv, 0xDEAD0000, limit=400000000, real_calls=True)
        print("whole-map render: %s" % (err or "ok"))
        rows = [[e.rd(fb + j * cw + i, 1) for i in range(cw)]
                for j in range(ch)]
        return rows

    errs = drawn = 0
    bad = []
    for s in range(2 * 128):
        for row in range(128):
            col = s - row
            if not (0 <= col < 128):
                continue
            x = (row - col) * (tw // 2) + ox - cx
            y = (row + col) * (th // 2) + oy - cy
            #  Skip only tiles that cannot touch the crop.  The tallest
            #  art (an arcology) rises 219 px above its tile and a 4x4
            #  footprint is 128 px wide, so be generous -- culling too
            #  eagerly leaves holes along the crop edges that look exactly
            #  like renderer defects.
            if x < -320 or x > cw + 320 or y < -400 or y > ch + 200:
                continue
            args = ([col, row] if entry in (UGND, VIEWT)
                    else [col, row, y, x])
            n = len(args) * 2
            e.a[7] = (e.a[7] - n) & 0xFFFFFFFF
            for k, v in enumerate(reversed(args)):
                e.wr(e.a[7] + k * 2, 2, v & 0xFFFF)
            e.a[7] = (e.a[7] - 4) & 0xFFFFFFFF
            e.wr(e.a[7], 4, 0xDEAD0000)
            err = e.run(entry, 0xDEAD0000, limit=600000, real_calls=True)
            e.a[7] = (e.a[7] + n) & 0xFFFFFFFF
            drawn += 1
            if err:
                errs += 1
                bad.append((row, col, err))
    #  Blank out anything a failed tile could have painted.  A tile that
    #  aborted mid-render (an unimplemented QuickDraw text trap on the sign
    #  path, say) leaves a hole that is indistinguishable from a renderer
    #  defect, and it is the harness's fault, not the reconstruction's.
    rows = [[e.rd(fb + j * cw + i, 1) for i in range(cw)] for j in range(ch)]
    for row, col, _err in bad:
        bx = (row - col) * (tw // 2) + ox - cx
        by = (row + col) * (th // 2) + oy - cy
        for y in range(by - 260, by + 60):
            if not (0 <= y < ch):
                continue
            for x in range(bx - 80, bx + 144):
                if 0 <= x < cw:
                    rows[y][x] = bg
    print("tiles run %d, emulator errors %d" % (drawn, errs))
    for row, col, err in bad[:8]:
        print("   tile row %d col %d: %s" % (row, col, err))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("out")
    ap.add_argument("--crop", default="0,0,320,320")
    ap.add_argument("--underground", action="store_true")
    ap.add_argument("--view", type=int, default=0)
    ap.add_argument("--bg", type=int, default=0)
    ap.add_argument("--zoom", type=int, default=32, choices=(8, 16, 32))
    ap.add_argument("--whole", action="store_true",
                    help="let $16B74 walk the map instead of driving tiles")
    a = ap.parse_args()
    crop = tuple(int(v) for v in a.crop.split(","))
    rows = render(a.city, crop, a.underground, a.view, a.bg,
                  whole=a.whole, zoom={8: 0, 16: 1, 32: 2}[a.zoom])
    import json
    pal = json.loads((HERE.parent / "assets/atlas.json").read_text())["palette"]
    pal = [tuple(c) for c in pal]
    write_indexed_png(a.out, crop[2], crop[3], rows, pal, TRANSPARENT)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
