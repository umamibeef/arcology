"""Recover the budget pass's tile-type -> department map by running it.

`$263C8` sorts every infrastructure tile type into a maintenance
department with a nest of overlapping range tests -- and the ranges do
overlap, so a tile can be charged to two departments.  Reading fifteen
nested compares off the listing is error-prone; running the routine once
per tile type with a census of exactly one tile is not.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

HERE = os.path.dirname(os.path.abspath(__file__))
A5 = Emu.A5
N_DEPT = 16
DEPT_SIZE = 0x70

# everything $263C8 dereferences
BLOCKS = {0x2C30: 0x700, 0x1EF6: 0x200}
STUBS = {0x1522A, 0x101AC, 0x1DEA, 0x140A, 0x41368, 0x2EDE4, 0x9728}

_LISTING = None


def amounts(census):
    """run the budget pass over one census and return the 16 dept amounts"""
    global _LISTING
    if _LISTING is None:
        _LISTING = open(os.path.join(HERE, "..", "out", "CODE_2.asm")).read().split("\n")
    e = Emu(_LISTING, build(), A5LOW)
    base = {}
    for off, size in BLOCKS.items():
        p = e.alloc(size + 0x40)
        e.wr(A5 + off, 4, p)
        base[off] = p
    for i, v in enumerate(census):
        e.wr(base[0x1EF6] + i * 2, 2, v & 0xFFFF)
    e.wr(A5 + 0x2C7A, 1, 0)      # not the year-end reconciliation
    e.wr(A5 + 0x1E32, 2, 0)      # month 0
    e.wr(A5 + 0x1E6E, 4, 0)      # no ordinances, so $41368 is never called
    e.wr(A5 + 0x1E2A, 4, 0)      # no bonds
    e.wr(A5 + 0x1EFE, 2, 0)
    err = e.run(0x263C8, -1, limit=4000000, real_calls=True, stubs=STUBS)
    if err: raise RuntimeError(err)
    out = []
    for d in range(N_DEPT):
        v = e.rd(base[0x2C30] + d * DEPT_SIZE + 0x60, 4)
        out.append(v - (1 << 32) if v & 0x80000000 else v)
    return out


def membership(probe=1, lo=0, hi=0x100):
    """for each tile type, the departments it is charged to and by how much"""
    zero = [0] * 256
    baseline = amounts(zero)
    out = {}
    for t in range(lo, hi):
        cs = list(zero); cs[t] = probe
        got = amounts(cs)
        hit = [d for d in range(N_DEPT) if got[d] != baseline[d]]
        if hit:
            out[t] = {d: got[d] - baseline[d] for d in hit}
    return out


def masks():
    """the dept bitmask for every tile id 0x00..0x6F

    Recovered by running the budget pass at $263C8 once per tile type with
    a census holding a single tile, because the fifteen nested range tests
    there overlap: bridges and crossings are charged to two departments at
    once, which is easy to miss by eye.
    """
    m = membership(probe=1, lo=0, hi=0x70)
    out = []
    for t in range(0x70):
        mask = 0
        for d in m.get(t, {}):
            mask |= 1 << d
        out.append(mask)
    return out


if __name__ == "__main__":
    m = membership(probe=144)
    print("%d tile types are charged to a department" % len(m))
    for t in sorted(m):
        print("  0x%02X -> %s" % (t, ", ".join("dept%d x%d" % (d, v) for d, v in sorted(m[t].items()))))
