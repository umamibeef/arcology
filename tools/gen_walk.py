"""Recover the road-walk's transition table by executing its dispatch.

   WARNING, and the reason tools/walk_deps.py exists: this probes with a
   value in ONE layer and zero in the others, so it can only represent a
   mode that reads ONE layer.  Mode 13 reads two -- it surfaces at a
   subway station on XBLD == $E9 ($24E42) as well as consulting XUND --
   and the XUND-only probe recorded "keep tunnelling, cost 1" for it,
   which is wrong and was invisible for months.  That case is now
   handled by an explicit test in sim_trip, transcribed from $24E42.

   Run tools/walk_deps.py after any change here.  It sweeps each layer
   per mode and reports any mode whose outcome responds to more than
   one; as of this writing mode 13 is the only one.

$245E8 walks the transport network as a state machine: it holds a
transport mode in d6, steps onto a tile, and a fourteen-case switch at
$247EC decides the new mode, what the step cost, and whether the walk
moved or arrived.  Fourteen cases of overlapping range tests is a lot of
bookkeeping to read correctly, so instead the switch is run directly --
once for every (mode, tile) pair -- and the answers written down.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

A5 = Emu.A5
DISPATCH = 0x247EC
JOIN = 0x250E2
N_MODE = 14
HERE = os.path.dirname(os.path.abspath(__file__))
_L = None


def probe(mode, val, layer=0x21C2, zone=1, zone_at=0):
    """Run the switch once, with `val` placed in the given map layer."""
    global _L
    if _L is None:
        _L = open(os.path.join(HERE, "..", "out", "CODE_2.asm")).read().split("\n")
    e = Emu(_L, build(), A5LOW)
    bufs = {}
    #  ALTM, XBIT, XBLD, XZON, XTER, XUND -- every layer an arm might
    #  read.  Allocating only three of them is how the ALTM test in
    #  modes 2 and 6 stayed invisible.
    #  ALTM is a WORD layer: two bytes per cell, so its rows are 256
    #  bytes apart.  Allocating it at a byte stride, and writing a byte
    #  into it, cannot reach the bits modes 2 and 6 test --
    #  (ALTM >> 10) & 0x1F at $24B3E.
    WORDL = {0x1FC2}
    for off in (0x1FC2, 0x1BBA, 0x21C2, 0x23C2, 0x25C2, 0x27C2):
        w = 2 if off in WORDL else 1
        b = e.alloc(128 * 128 * w + 64); bufs[off] = b
        for r in range(128):
            e.wr(A5 + off + r * 4, 4, b + r * 128 * w)
    ty, tx = 40, 40
    lw = 2 if layer == 0x1FC2 else 1
    e.wr(bufs[layer] + (ty * 128 + tx) * lw, lw, val)
    e.wr(bufs[0x23C2] + ty * 128 + tx, 1, zone_at)
    frame = 0x00280000
    e.a[6] = frame
    e.wr(frame - 2, 2, ty)                          # -$2(a6) row
    e.wr(frame - 4, 2, tx)                          # -$4(a6) col
    e.wr(frame - 6, 2, ty)                          # -$6(a6) current row
    e.wr(frame - 8, 2, tx)                          # -$8(a6) current col
    e.wr(frame + 0x0C, 2, zone)                     # the caller's zone
    e.wr(frame + 0x0E, 2, 1)
    e.wr(frame + 0x10, 2, 100)
    e.a[7] = 0x00300000
    e.d[6] = mode
    e.d[5] = 0
    e.d[4] = 0
    e.d[7] = 0
    err = e.run(DISPATCH, JOIN, limit=20000, real_calls=True)
    if err: raise RuntimeError("mode %d val %02X: %s" % (mode, val, err))
    def s16(v): return v - 0x10000 if v & 0x8000 else v
    return (s16(e.d[6] & 0xFFFF), s16(e.d[5] & 0xFFFF),
            e.d[4] & 0xFF, e.d[7] & 0xFF)


def table(layer=0x21C2):
    out = {}
    for m in range(N_MODE):
        for t in range(256):
            out[(m, t)] = probe(m, t, layer)
    return out


def _emit_one(w, name, t, comment):
    w(comment)
    w('const WalkStep %s[%d][256] = {' % (name, N_MODE))
    for m in range(N_MODE):
        w('    { /* mode %d */' % m)
        for i in range(0, 256, 8):
            row = []
            for tt in range(i, i + 8):
                nm, cost, moved, arr = t[(m, tt)]
                row.append('{%3d, %d, %d, %d},' % (nm, cost, 1 if moved else 0, 1 if arr else 0))
            w('        ' + ' '.join(row))
        w('    },')
    w('};')
    w('')


def emit(w):
    w('/*  The road walk in $245E8 is a state machine: it carries a transport')
    w(' *  mode, steps onto a tile, and a fourteen-case switch at $247EC picks')
    w(' *  the new mode, the step cost, and whether the walk moved or arrived.')
    w(' *  Fourteen cases of overlapping range tests is a great deal of')
    w(' *  bookkeeping to read correctly, so the switch was executed once for')
    w(' *  every (mode, tile) pair instead (tools/gen_walk.py).  Two tables,')
    w(' *  because some modes read the surface and the subway modes read the')
    w(' *  underground layer.')
    w(' *')
    w(' *  Arrival at a compatible zone is decided by ZONE_ATTRACTS and depends')
    w(' *  on the zone the trip started in, so it is not in these tables. */')
    _emit_one(w, 'WALK_STEP_BLD', table(0x21C2),
              '/* stepping onto XBLD */')
    _emit_one(w, 'WALK_STEP_UND', table(0x27C2),
              '/* stepping onto XUND -- how the subway modes 11 and 13 move */')


if __name__ == "__main__":
    t = table()
    for m in range(N_MODE):
        moves = {}
        for tt in range(256):
            nm, cost, moved, arr = t[(m, tt)]
            if moved or arr:
                moves.setdefault((nm, cost, moved, arr), []).append(tt)
        print("mode %2d:" % m)
        for k, v in sorted(moves.items()):
            def runs(x):
                o = []
                for q in x:
                    if o and o[-1][1] == q - 1: o[-1][1] = q
                    else: o.append([q, q])
                return ",".join("%02X" % a if a == b else "%02X-%02X" % (a, b) for a, b in o)
            print("    -> mode %3d cost +%d moved=%d arrived=%d : %s" % (k[0], k[1], k[2] and 1, k[3] and 1, runs(v)))
