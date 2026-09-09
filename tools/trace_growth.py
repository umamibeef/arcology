"""Trace what the growth pass actually does, tile by tile.

$3170E is far too big to transcribe in one go and check at the end, so
this makes it checkable in pieces: run the original under the
interpreter and record every tile it visits, every random number it
draws and every tile it modifies, in order.  The C emits the same
trace.  Diffing the two puts the first disagreement on a named tile
instead of leaving a percentage to interpret.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

TILE_BODY = 0x31788        # top of the per-tile body: d5 = y, d4 = x
TILE_MODIFY = 0x4110       # tileModify(y, x, bld)
RNG = {0x20F30: "mod", 0x20F4C: "and1", 0x20F64: "and3",
       0x20F7C: "and15", 0x20F94: "and63", 0x20FAC: "and127"}


def trace(path, y0, x0, lfsr=1, tb=1, limit=40000000):
    s = Sim(path)
    out = []

    def at_tile(e, pc):
        out.append("TILE %d %d" % (e.d[5] & 0xFFFF, e.d[4] & 0xFFFF))

    def at_modify(e, pc):
        sp = e.a[7]
        out.append("SET %d %d %d" % (e.rd(sp + 4, 2), e.rd(sp + 6, 2), e.rd(sp + 8, 2)))

    def at_rng(kind):
        def f(e, pc):
            out.append("RNG %s" % kind)
        return f

    w = {TILE_BODY: at_tile, TILE_MODIFY: at_modify}
    #  The six generator variants all end in an rts with the result in
    #  d0, so watching the rts catches the value after the mask.
    RTS = {0x20F4A: "mod", 0x20F62: "and1", 0x20F7A: "and3",
           0x20F92: "and15", 0x20FAA: "and63", 0x20FC2: "and127"}

    def at_rts(kind):
        def f(e, pc):
            out.append("RNG %s %d" % (kind, e.d[0] & 0xFFFF))
        return f

    for a, k in RTS.items():
        w[a] = at_rts(k)
    s2 = s
    s2.e.wr(A5 + 0x11DC, 2, lfsr)
    s2.e.tb_seed = tb
    s2.e.a[7] = 0x00300000
    for v in reversed((y0, x0)):
        s2.e.a[7] -= 2; s2.e.wr(s2.e.a[7], 2, v)
    s2.e.a[7] -= 4; s2.e.wr(s2.e.a[7], 4, 0xDEAD0000)
    s2.e.rng_log = []
    err = s2.e.run(0x3170E, 0xDEAD0000, limit=limit, real_calls=True,
                   stubs={0x9728}, watch=w)
    return err, out, s2


if __name__ == "__main__":
    path, y0, x0 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    err, ev, s = trace(path, y0, x0)
    sys.stderr.write("run: %s   %d events\n" % (err or "completed", len(ev)))
    print("\n".join(ev))
