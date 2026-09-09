#!/usr/bin/env python3
"""The renderer's oracle: run the game's own drawing code and write down
every shape it blits.

This is the same trick `oracle_diff.py` uses for the simulation, and it
works even better here.  The renderer's output is pixels, which would mean
emulating QuickDraw -- but every pixel it draws goes through one routine,
`$18E96(shape, x, y, mirror)`.  Stub that out and watch the call sites and
the oracle becomes a *list of blits*, which is exactly the thing a
reconstruction gets wrong: which shape, at what offset, in what order.

    python3 tools/render_oracle.py <city> [row] [col]

The per-tile renderers, picked by zoom at $15490:
    $183F2  8 px      $17978  16 px      $16B74  32 px
each delegating to $0184DC / $017A6C / $016FF8 for one tile.  We drive the
32 px one directly, one tile at a time.
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m68kemu import Emu
from runsim import Sim, A5

TILE = 0x016FF8  # the 32 px per-tile renderer
VIEWT = 0x0167CC # the data-view per-tile renderer ($160CA drives it)
#  The surface has one per zoom; the underground and data-view renderers
#  are zoom-generic.  Only 32 px had ever been driven from here.
TILE16, TILE8 = 0x017A6C, 0x0184DC
UGND = 0x0161DC  # the underground per-tile renderer ($15FAC drives it)
BLIT = 0x18E96   # drawShape(shape, x, y, mirror)
BLIT2 = 0x19004  # the variant the traffic cars use
#  $FABA is NOT just the sign drawer: it looks the tile's thing up via
#  $399D8 and calls $A032 to draw it.  Stubbing it silently removed every
#  aircraft, boat and train from the oracle's output -- and made the
#  renderer look wrong for drawing them.  Let it run.
SIGN = 0xFABA

#  Globals the renderer reads that the simulation harness never sets up.
G_ZOOM = 0x2C26
G_SCROLL_X, G_SCROLL_Y = 0x2C3C, 0x2C3E
G_CLIP_TOP, G_CLIP_BOT = 0x122C, 0x1230
G_CLIP_LEFT, G_CLIP_RIGHT = 0x122E, 0x1232
G_CORNER = 0x122A          # g_rotTable[rotation]
G_VIEW = 0x2C34
G_SHAPEDESC = 0x1226       # 1500 x 8 descriptor table
ROT_TABLE = -0x7DD4


class RenderOracle:
    def __init__(self, city, zoom=2, view=0, underground=False):
        self.zoom = zoom
        self.sim = Sim(city)
        e = self.sim.e
        self.e = e

        #  The shape-descriptor table, filled for real.  $18E96 reads it
        #  as (art pointer, -, height, width) and adds +4/+6 to y/x to
        #  build its clip rect, so the two fields the CALLERS use are just
        #  the sprite's size: $16298 subtracts +4 to get the top edge and
        #  $A364 subtracts +6/2 to centre a sprite on its tile.
        #
        #  This used to be zeroed, on the grounds that the real contents
        #  were unknown.  That was the harness's worst bug: with the table
        #  zeroed the game skips both subtractions, so the oracle reported
        #  a multi-tile building 16 px high and an aircraft 32 px left of
        #  where the game really puts them -- differences that came from
        #  this file, not from the renderer.  The whole "y differs by the
        #  footprint drop, expected" category was that artefact.
        #
        #  The blitter is stubbed here, so +0 can stay null; only the size
        #  fields matter.  render_pixels.py fills the pointers too.
        from render_pixels import tileset_shapes
        desc = e.alloc(0x2EE0)
        for k in range(0, 0x2EE0, 4):
            e.wr(desc + k, 4, 0)
        for sid, (w, h, _stream) in tileset_shapes().items():
            if sid * 8 + 8 <= 0x2EE0:
                e.wr(desc + sid * 8 + 4, 2, h)
                e.wr(desc + sid * 8 + 6, 2, w)
        e.wr(A5 + G_SHAPEDESC, 4, desc)

        e.wr(A5 + G_ZOOM, 2, zoom)
        e.wr(A5 + G_VIEW, 2, view)
        e.wr(A5 + G_SCROLL_X, 2, 0)
        e.wr(A5 + G_SCROLL_Y, 2, 0)
        #  Clip generously: we want every blit recorded, not culled.
        e.wr(A5 + G_CLIP_TOP, 2, 0x8000)      # signed -32768
        e.wr(A5 + G_CLIP_BOT, 2, 0x7FFF)
        e.wr(A5 + G_CLIP_LEFT, 2, 0x8000)
        e.wr(A5 + G_CLIP_RIGHT, 2, 0x7FFF)

        rot = e.rd(A5 + 0x2C24, 2) & 3
        e.wr(A5 + G_CORNER, 2, e.rd(A5 + ROT_TABLE + rot * 2, 2))
        self.rotation = rot

        #  Display flags, left alone on purpose: the four view flags are initialised
        #  data and the A5 image already holds the application's shipped
        #  defaults (all 1).  Nothing in CODE_2 writes them.
        e.wr(A5 - 0x7DE4, 1, 1 if underground else 0)  # g_cityMode
        self.entry = UGND if underground else (
            VIEWT if view else (TILE, TILE16, TILE8)[2 - zoom])

        self.sites = self._blit_sites()

    def _blit_sites(self):
        """Every `jsr $18E96` / `$19004` address, so we can watch them."""
        sites = {}
        for pc, (mn, ops) in self.e.code.items():
            if not mn.startswith('jsr'):
                continue
            m = re.fullmatch(r'\$([0-9a-f]+)\.l', ops.strip())
            if not m:
                continue
            tgt = int(m.group(1), 16)
            if tgt in (BLIT, BLIT2):
                sites[pc] = tgt
        return sites

    def tile(self, row, col, limit=400000):
        """-> (list of (shape, x, y, mirror), error or None)"""
        e = self.e
        out = []

        def rec(emu, pc):
            sp = emu.a[7]
            #  $18F10 only does `tst.w` on the mirror argument, so record
            #  the flag rather than the bit the caller happened to pass
            #  ($16722 pushes XBIT & 2, which is 2).
            rec_t = (emu.rd(sp, 2), _s16(emu.rd(sp + 2, 2)),
                     _s16(emu.rd(sp + 4, 2)),
                     1 if emu.rd(sp + 6, 2) else 0)
            #  Which blitter: $18E96 writes every pixel, $19004 writes only
            #  onto index 0x91.  A blit list that does not say which one is
            #  blind to the difference between a car under a power line and
            #  a car over it.
            if self.with_pc:
                rec_t += (self.sites.get(pc, 0),)
            out.append(rec_t)

        watch = {pc: rec for pc in self.sites}
        if not hasattr(self, "with_pc"):
            self.with_pc = False
        #  Pascal order: the first argument ends up highest, so the last
        #  one pushed sits at a6+8.  The two entry points differ:
        #    $016FF8 takes x, y, row, col -- the caller has already
        #      projected the tile ($8=x, $a=y, $c=row, $e=col)
        #    $0161DC takes row, col only and projects them itself
        #      ($8=row, $a=col, using $2C3C/$2C3E for the scroll)
        if self.entry in (UGND, VIEWT):
            args = [col, row]          # col pushed first, row ends at a6+8
        else:
            #  Project at the CALLER's zoom, not always 32 px: tile_w is
            #  8<<zoom and tile_h is 4<<zoom, so the halves are 4<<zoom
            #  and 2<<zoom.  Passing the 32 px numbers at zoom 16 or 8
            #  put every recorded blit at twice its real offset.
            args = [col, row,
                    (row + col) * (2 << self.zoom),
                    (row - col) * (4 << self.zoom)]
        n = len(args) * 2
        e.a[7] = (e.a[7] - n) & 0xFFFFFFFF
        for k, v in enumerate(reversed(args)):
            e.wr(e.a[7] + k * 2, 2, v & 0xFFFF)
        e.a[7] = (e.a[7] - 4) & 0xFFFFFFFF
        e.wr(e.a[7], 4, 0xDEAD0000)
        err = e.run(self.entry, 0xDEAD0000, limit=limit, real_calls=True,
                    stubs={BLIT, BLIT2}, watch=watch)
        e.a[7] = (e.a[7] + n) & 0xFFFFFFFF
        return out, err


def _s16(v):
    return v - 0x10000 if v & 0x8000 else v


if __name__ == "__main__":
    city = sys.argv[1]
    import argparse
    ug = "--underground" in sys.argv
    if ug:
        sys.argv.remove("--underground")
        city = sys.argv[1]
    o = RenderOracle(city, underground=ug)
    print("rotation %d, corner mask %#x" % (o.rotation, o.e.rd(A5 + G_CORNER, 2)))
    if len(sys.argv) > 3:
        cells = [(int(sys.argv[2]), int(sys.argv[3]))]
    else:
        cells = [(r, c) for r in range(16, 20) for c in range(64, 70)]
    for row, col in cells:
        blits, err = o.tile(row, col)
        print("  tile (col %3d,row %3d): %s%s" %
              (col, row, blits if blits else "nothing",
               "   ERR %s" % err if err else ""))
