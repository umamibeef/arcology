"""Extract the police/fire coverage kernel by running it, not by reading it.

`$24232` is a hand-unrolled diamond: about sixty calls to `$241B2`, with
the strength stepped down between rings by a chain of integer divisions.
Transcribing sixty call sites by eye is exactly the kind of reading error
this project keeps paying for, so instead run the routine under the
interpreter, watch every call, and write down the offsets it actually
visits and the strength it actually carries.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

HERE = os.path.dirname(os.path.abspath(__file__))
A5 = Emu.A5
CY = CX = 100          # far outside the 32x32 plane: the clip is applied
                       # inside coveragePoint, so every offset still shows up


def trace(strength):
    listing = open(os.path.join(HERE, "..", "out", "CODE_2.asm")).read().split("\n")
    e = Emu(listing, build(), A5LOW)
    plane = e.alloc(0x200)
    calls = []

    def watch(em, pc):
        sp = em.a[7]
        y = em.rd(sp + 8, 2); x = em.rd(sp + 10, 2); amt = em.rd(sp + 12, 2)
        def sw(v): return v - 0x10000 if v & 0x8000 else v
        calls.append((sw(y) - CY, sw(x) - CX, sw(amt)))

    # push the arguments the way the caller does: strength, x, y, plane
    e.a[7] = 0x00300000
    for v, sz in ((strength & 0xFFFF, 2), (CX, 2), (CY, 2)):
        e.a[7] -= 2; e.wr(e.a[7], 2, v)
    e.a[7] -= 4; e.wr(e.a[7], 4, plane)
    e.a[7] -= 4; e.wr(e.a[7], 4, 0xDEAD0000)      # return address sentinel
    err = e.run(0x24232, 0xDEAD0000, limit=2000000, real_calls=True,
                watch={0x241B2: watch})
    return err, calls


def rings(calls):
    """group consecutive calls that share a strength"""
    out = []
    for dy, dx, amt in calls:
        if not out or out[-1][0] != amt:
            out.append((amt, []))
        out[-1][1].append((dy, dx))
    return out


def emit(w):
    """append the coverage kernel to the tables.c writer"""
    err, calls = trace(1000)
    if err: raise RuntimeError(err)
    rs = rings(calls)
    w('/*  COVERAGE_KERNEL -- the diamond that a police or fire station')
    w(' *  stamps into its 32x32 coverage layer.  $24232 spells it out as')
    w(' *  about forty separate calls to $241B2 with the strength stepped')
    w(' *  down between rings, so it was recovered by running the routine')
    w(' *  under the interpreter and writing down the offsets it actually')
    w(' *  visits (tools/gen_coverage.py) rather than by reading them.')
    w(' *')
    w(' *  The ring strengths are, from the same trace,')
    w(' *      r0 = s          r1 = r0*4/5     r2 = r1*3/4')
    w(' *      r3 = r2*2/3     r4 = r3/2')
    w(' *  each step done in 16 bits and truncated toward zero.  That was')
    w(' *  checked against the interpreter for 595 strengths including')
    w(' *  negatives and values that overflow the 16-bit intermediate. */')
    w('const CoverageCell COVERAGE_KERNEL[%d] = {' % len(calls))
    for i, (amt, offs) in enumerate(rs):
        for dy, dx in offs:
            w('    { %2d, %2d, %d },' % (dy, dx, i))
    w('};')
    w('const int COVERAGE_KERNEL_LEN = %d;' % len(calls))
    w('')


if __name__ == "__main__":
    err, calls = trace(1000)
    print("run: %s   %d calls to coveragePoint" % (err or "completed", len(calls)))
    rs = rings(calls)
    print("%d rings" % len(rs))
    for i, (amt, offs) in enumerate(rs):
        print("  ring %d  strength %5d  %2d cells  %s"
              % (i, amt, len(offs), " ".join("(%+d,%+d)" % o for o in offs)))
