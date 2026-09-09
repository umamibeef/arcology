#!/usr/bin/env python3
"""Diff the ported $8758 / $896C pair against the original's own code.

Three hundred tiles are tested and raised in the same order on both
sides, then ALTM, XTER, XBIT, XZON, XBLD and the funds are compared.
The raise now runs $12C04 for real, so this also exercises $128DE nine
times per raise.  Only $5FAA is stubbed.
"""
import subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5


def _chk(r):
    """An oracle run that stopped early -- out of steps, or on a trap the
    interpreter does not implement -- did not produce the original's
    answer.  Treating one as ground truth invents a result."""
    if r is not None:
        raise RuntimeError("oracle stopped early: %s" % r)
    return r

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
#  Only drawing is stubbed.  $128DE calls $5FAA with the collapse
#  animation asked for, so the QuickDraw side has to be shut off.
STUBS = {0x9728, 0x15A54, 0x18E96, 0x18D40, 0x18E62, 0x1DEA, 0x3F636,
         0x392E, 0x30FE, 0x155CA, 0x15408, 0x370A4, 0x3D566, 0xA3E4}


def call(s, addr, args):
    s.e.a[7] = 0x00300000
    for v in reversed(args):
        s.e.a[7] -= 2
        s.e.wr(s.e.a[7], 2, v & 0xFFFF)
    s.e.a[7] -= 4
    s.e.wr(s.e.a[7], 4, 0xDEAD0000)
    _chk(s.e.run(addr, 0xDEAD0000, limit=40000000, real_calls=True, stubs=STUBS))
    return s.e.d[0] & 0xFF


def oracle(path):
    s = Sim(path)
    s.e.wr(A5 + 0x61E, 4, 25)
    s.e.wr(A5 + 0x1E26, 4, 25000)
    for k in range(300):
        y, x = (k * 29) % 128, (k * 41) % 128
        if call(s, 0x8758, (y, x)):
            call(s, 0x896C, (y, x))
    alt, bit, zon = s.layer("ALTM"), s.layer("XBIT"), s.layer("XZON")
    ter, bld = s.layer("XTER"), s.layer("XBLD")
    out = {}
    for i in range(16384):
        out[("a", i)] = (int.from_bytes(alt[2 * i:2 * i + 2], "big"), bit[i],
                         zon[i], ter[i], bld[i])
    out[("m", 0)] = (s.e.rd(A5 + 0x1E26, 4),)
    return out


def main():
    bad = tot = 0
    for path in sys.argv[1:]:
        want = oracle(path)
        got = {}
        for line in subprocess.run([SIMBIN, "--raise", path],
                                   capture_output=True, text=True).stdout.splitlines():
            f = line.split()
            if f and f[0] == "a":
                got[("a", int(f[1]))] = tuple(int(v, 16) for v in f[2:])
            elif f and f[0] == "m":
                got[("m", 0)] = (int(f[2], 16),)
        shown = 0
        for k in sorted(want, key=lambda t: (t[0], t[1])):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                if shown < 8:
                    where = f"({k[1]//128},{k[1]%128})" if k[0] == "a" else "funds"
                    print(f"  {os.path.basename(path)} {where}: want {want[k]} got {got.get(k)}")
                    shown += 1
    print(f"raise: {tot - bad}/{tot} values exact")
    return 1 if bad else 0


sys.exit(main())
