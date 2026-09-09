"""Recover the per-ordinance cost formulas by probing $41368.

Each of the twenty ordinances costs (or earns) some fraction of one of
the first three budget departments' amounts, picked out by a jump table.
Reading a twenty-entry jump table off the listing and pairing each entry
with the right arithmetic is precisely the kind of bookkeeping that goes
wrong silently, so the formulas are measured instead: feed the routine
one source at a time and read the ratio back out.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

HERE = os.path.dirname(os.path.abspath(__file__))
A5 = Emu.A5
N_ORD = 20
DEPT_SIZE = 0x70
SRC = ["dept0", "dept1", "dept2", "population", "misc2C98"]

_LISTING = None


def call(idx, dept=(0, 0, 0), population=0, misc2c98=0):
    global _LISTING
    if _LISTING is None:
        _LISTING = open(os.path.join(HERE, "..", "out", "CODE_2.asm")).read().split("\n")
    e = Emu(_LISTING, build(), A5LOW)
    p = e.alloc(0x740)
    e.wr(A5 + 0x2C30, 4, p)
    for d, v in enumerate(dept):
        e.wr(p + d * DEPT_SIZE + 0x60, 4, v & 0xFFFFFFFF)
    e.wr(A5 + 0x1E96, 4, population & 0xFFFFFFFF)
    e.wr(A5 + 0x2C98, 4, misc2c98 & 0xFFFFFFFF)
    e.a[7] = 0x00300000
    e.a[7] -= 2; e.wr(e.a[7], 2, idx)
    e.a[7] -= 4; e.wr(e.a[7], 4, 0xDEAD0000)
    err = e.run(0x41368, 0xDEAD0000, limit=200000, real_calls=True)
    if err: raise RuntimeError(err)
    v = e.d[0] & 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def probe(idx):
    """(source index, numerator, denominator) for one ordinance"""
    P = 720                       # divisible by every denominator in play
    trials = [
        (0, dict(dept=(P, 0, 0))),
        (1, dict(dept=(0, P, 0))),
        (2, dict(dept=(0, 0, P))),
        (3, dict(population=P)),
        (4, dict(misc2c98=P)),
    ]
    hits = [(s, call(idx, **kw)) for s, kw in trials]
    live = [(s, v) for s, v in hits if v != 0]
    if not live:
        return None
    if len(live) > 1:                       # more than one input feeds it
        return ("sum", {SRC[s]: v // P for s, v in live})
    s, v = live[0]
    num, den = v // P, 1
    for d in range(1, 13):                  # recover the exact ratio
        if v * d % P == 0 and abs(v * d) // P <= 12:
            num, den = v * d // P, d
            break
    return (s, num, den)


def emit(w):
    """append the ordinance cost table to the tables.c writer"""
    w('/*  ORDINANCE_COST -- what each of the twenty ordinances does to the')
    w(' *  treasury, as a fraction of one budget department\'s amount.  $41368')
    w(' *  selects the formula through a jump table; the entries here were')
    w(' *  measured by calling the routine one source at a time')
    w(' *  (tools/gen_ordinance.py) rather than read off that table.')
    w(' *')
    w(' *  source 0..2 name a department amount, 3 means -(population +')
    w(' *  MISC A5+0x2C98), and -1 means the ordinance is free.  Negation')
    w(' *  happens before the division, and the division truncates toward')
    w(' *  zero, so plain C division reproduces it. */')
    w('const OrdinanceCost ORDINANCE_COST[%d] = {' % N_ORD)
    for i in range(N_ORD):
        r = probe(i)
        if r is None:
            src, num, den = -1, 0, 1
        elif r[0] == "sum":
            assert set(r[1]) == {"population", "misc2C98"} and set(r[1].values()) == {-1}, r
            src, num, den = 3, -1, 1
        else:
            src, num, den = r
        w('    { %2d, %2d, %d },' % (src, num, den))
    w('};')
    w('')


if __name__ == "__main__":
    for i in range(N_ORD):
        r = probe(i)
        if r is None:
            print("  ordinance %2d: 0" % i)
        elif r[0] == "sum":
            print("  ordinance %2d: %s" % (i, " ".join("%+d*%s" % (v, k) for k, v in r[1].items())))
        else:
            s, num, den = r
            print("  ordinance %2d: %s * %d / %d" % (i, SRC[s], num, den))
