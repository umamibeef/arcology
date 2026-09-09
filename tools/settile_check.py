#!/usr/bin/env python3
"""Diff the ported setTile against $4110 over a long call sequence.

Both sides run the same 4000 calls, then XBLD, the per-building census
and the military infra counters are compared.  The counters are
compared as deltas, since the two worlds do not start them alike.
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
N = 4000


def oracle(path):
    s = Sim(path)
    cen0 = [s.e.rd(s.base[0x1EF6] + 2 * i, 2) for i in range(256)]
    inf0 = [s.e.rd(s.base[0x1EFA] + 2 * i, 2) for i in range(16)]
    for k in range(N):
        y, x, b = (k * 37) % 128, (k * 53) % 128, (k * 11) & 0xFF
        s.e.a[7] = 0x00300000
        for v in (b, x, y):
            s.e.a[7] -= 2
            s.e.wr(s.e.a[7], 2, v & 0xFFFF)
        s.e.a[7] -= 4
        s.e.wr(s.e.a[7], 4, 0xDEAD0000)
        _chk(s.e.run(0x4110, 0xDEAD0000, limit=200000, real_calls=True, stubs=set()))
    out = {}
    bld = s.layer("XBLD")
    for i, v in enumerate(bld):
        out[("b", i)] = v
    for i in range(256):
        out[("c", i)] = (s.e.rd(s.base[0x1EF6] + 2 * i, 2) - cen0[i]) & 0xFFFF
    for i in range(16):
        out[("i", i)] = (s.e.rd(s.base[0x1EFA] + 2 * i, 2) - inf0[i]) & 0xFFFF
    return out


def main():
    bad = tot = 0
    for path in sys.argv[1:]:
        want = oracle(path)
        got = {}
        for line in subprocess.run([SIMBIN, "--settile", path],
                                   capture_output=True, text=True).stdout.splitlines():
            f = line.split()
            if f:
                got[(f[0], int(f[1]))] = int(f[2], 16)
        shown = 0
        for k in sorted(want, key=lambda t: (t[0], t[1])):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                if shown < 8:
                    kind = {"b": "XBLD", "c": "census", "i": "infra"}[k[0]]
                    where = f"({k[1]//128},{k[1]%128})" if k[0] == "b" else f"[{k[1]}]"
                    print(f"  {os.path.basename(path)} {kind}{where}: "
                          f"want {want[k]:X} got {got.get(k)}")
                    shown += 1
    print(f"setTile: {tot - bad}/{tot} values exact")
    return 1 if bad else 0


sys.exit(main())
