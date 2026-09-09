#!/usr/bin/env python3
"""Diff the ported $5FAA against the original's own code.

The first building of every distinct id is knocked down in the same
order on both sides, then XBLD, XZON, XBIT, XTXT and ALTM are compared,
along with how many numbers each side drew.  Only drawing is stubbed.
"""
import subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
DRAW = {0x9728, 0x3F636, 0x392E, 0x15A54, 0x18E96, 0x18D40, 0x18E62,
        0x1DEA, 0x30FE, 0x155CA}


def oracle(path):
    s = Sim(path)
    log = []
    s.e.rng_log = log
    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    bld = s.layer("XBLD")
    seen = set()
    for y in range(3, 124):
        for x in range(3, 124):
            b = bld[y * 128 + x]
            if b < 6 or b in seen:
                continue
            seen.add(b)
            s.e.a[7] = 0x00300000
            #  a byte argument sits in the HIGH half of its word, so a
            #  flag pushed as 0x00FF reads as zero at $c(a6).
            for v in (0x0000, x, y):
                s.e.a[7] -= 2
                s.e.wr(s.e.a[7], 2, v & 0xFFFF)
            s.e.a[7] -= 4
            s.e.wr(s.e.a[7], 4, 0xDEAD0000)
            r = s.e.run(0x5FAA, 0xDEAD0000, limit=200000000,
                        real_calls=True, stubs=DRAW)
            if r is not None:
                raise RuntimeError("oracle stopped early: %s" % r)
            bld = s.layer("XBLD")
    out = {}
    xb, xz, xt = s.layer("XBLD"), s.layer("XZON"), s.layer("XTXT")
    xi, al = s.layer("XBIT"), s.layer("ALTM")
    for i in range(16384):
        out[("d", i)] = (xb[i], xz[i], xi[i], xt[i],
                         int.from_bytes(al[2 * i:2 * i + 2], "big"))
    out[("n", 0)] = len(log)
    return out


def main():
    bad = tot = 0
    for path in sys.argv[1:]:
        want = oracle(path)
        got = {}
        for line in subprocess.run([SIMBIN, "--demolish", path],
                                   capture_output=True, text=True).stdout.splitlines():
            f = line.split()
            if f and f[0] == "d":
                got[("d", int(f[1]))] = tuple(int(v, 16) for v in f[2:])
            elif f and f[0] == "n":
                got[("n", 0)] = int(f[2])
        print(f"  {os.path.basename(path)}: draws oracle {want[('n',0)]} "
              f"mine {got.get(('n',0))}")
        shown = 0
        for k in sorted(want, key=lambda t: (t[0], t[1])):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                if shown < 8 and k[0] == "d":
                    print(f"    ({k[1]//128},{k[1]%128}): want {want[k]} got {got.get(k)}")
                    shown += 1
    print(f"demolishAndPlace: {tot - bad}/{tot} values exact")
    return 1 if bad else 0


sys.exit(main())
