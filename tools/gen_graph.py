"""Recover what each of the sixteen graph series is made of, by probing.

graphHistoryPass ($22330) shifts twelve months of history back one slot
and then writes this month's value into each of sixteen series.  The
sixteen blocks are near-identical -- a counter times ten, plus a share
of two other terms -- and differ only in which counter they read and how
far the shares are divided down.  Reading sixteen copies of the same
code invites a transcription slip, so the terms are measured instead.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

A5 = Emu.A5
NSER = 16
_L = None


def run(accum=None, ebe=None, q=0, arco=0):
    global _L
    if _L is None:
        _L = open(os.path.join(os.path.dirname(__file__), "..", "out",
                               "CODE_2.asm")).read().split("\n")
    e = Emu(_L, build(), A5LOW)
    blocks = {0x1EBA: 0x40, 0x1EBE: 0x40, 0x1EF6: 0x200, 0x1EF2: 0x40,
              0x1EFA: 0x40, 0x2C30: 0x700}
    base = {}
    for off, size in blocks.items():
        p = e.alloc(size + 0x40); e.wr(A5 + off, 4, p); base[off] = p
    #  XGRP is sixteen row pointers of twelve longs each
    rows = []
    for i in range(NSER):
        r = e.alloc(12 * 4 + 16); rows.append(r)
        e.wr(A5 + 0x2BDC + i * 4, 4, r)
    for i, v in enumerate(accum or []):
        e.wr(base[0x1EBA] + i * 4, 4, v & 0xFFFFFFFF)
    for i, v in enumerate(ebe or []):
        e.wr(base[0x1EBE] + i * 4, 4, v & 0xFFFFFFFF)
    e.wr(A5 + 0x2C98, 4, q & 0xFFFFFFFF)
    for idx in (0xFB, 0xFC, 0xFD, 0xFE):
        e.wr(base[0x1EF6] + idx * 2, 2, arco & 0xFFFF)
    e.a[7] = 0x00300000 - 4
    e.wr(e.a[7], 4, 0xDEAD0000)
    err = e.run(0x22330, 0xDEAD0000, limit=4000000, real_calls=True,
                stubs={0x9728, 0x2EDE4})
    if err: raise RuntimeError(err)
    def s32(v): return v - (1 << 32) if v & 0x80000000 else v
    return [s32(e.rd(r, 4)) for r in rows]


if __name__ == "__main__":
    #  which counter feeds each series: give every counter a distinct value
    a = run(accum=[7] + [0] * 7, ebe=[1000 * (i + 1) for i in range(14)])
    print("series value with accum8[0]=7, blk1EBE[k]=1000*(k+1), q=0, arco=0:")
    for i, v in enumerate(a):
        print("   series %2d = %d" % (i, v))
    b = run(accum=[0] * 8, ebe=[0] * 14, q=4096)
    print("\nshare of q ($2C98) = 4096:", b)
    c = run(accum=[0] * 8, ebe=[0] * 14, arco=0x900)
    print("share of the arcology term (census 0x900 in each of 0xFB..0xFE):", c)
