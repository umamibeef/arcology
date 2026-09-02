#!/usr/bin/env python3
"""Diff the ported $128DE against the original's own code.

Two thousand tiles are put back in order on both sides, then ALTM,
XTER, XBIT, XZON, XBLD and XUND are compared.  $5FAA is stubbed on the
original's side because the demolition half is not reconstructed yet;
everything else is checked exactly.
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
         0x392E, 0x30FE, 0x155CA, 0x15408, 0x370A4, 0x3D566, 0xA3E4, 0x23EE}
N = 2000
MODE = os.environ.get("MODE", "")
ENTRY = 0x12C04 if MODE == "nb" else 0x128DE


def oracle(path):
    s = Sim(path)
    for k in range(N):
        y, x = (k * 31) % 128, (k * 17) % 128
        s.e.a[7] = s.e.SP_INIT
        for v in (x, y):
            s.e.a[7] -= 2
            s.e.wr(s.e.a[7], 2, v & 0xFFFF)
        s.e.a[7] -= 4
        s.e.wr(s.e.a[7], 4, 0xDEAD0000)
        _chk(s.e.run(ENTRY, 0xDEAD0000, limit=40000000, real_calls=True, stubs=STUBS))
    alt = s.layer("ALTM")
    ter, bit = s.layer("XTER"), s.layer("XBIT")
    zon, bld = s.layer("XZON"), s.layer("XBLD")
    und = s.layer("XUND")
    out = {}
    for i in range(16384):
        out[("t", i)] = (int.from_bytes(alt[2 * i:2 * i + 2], "big"),
                         ter[i], bit[i], zon[i], bld[i])
        if und[i]:
            out[("u", i)] = (und[i],)
    return out


def main():
    bad = tot = 0
    for path in sys.argv[1:]:
        want = oracle(path)
        got = {}
        for line in subprocess.run([SIMBIN, "--terrain", path] + ([MODE] if MODE else []),
                                   capture_output=True, text=True).stdout.splitlines():
            f = line.split()
            if not f:
                continue
            if f[0] == "t":
                got[("t", int(f[1]))] = tuple(int(v, 16) for v in f[2:])
            elif f[0] == "u":
                got[("u", int(f[1]))] = (int(f[2], 16),)
        shown = 0
        for k in sorted(want, key=lambda t: (t[0], t[1])):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                if shown < 8:
                    print(f"  {os.path.basename(path)} {k[0]}({k[1]//128},{k[1]%128}): "
                          f"want {want[k]} got {got.get(k)}")
                    shown += 1
        for k in got:
            if k not in want:
                bad += 1
                if shown < 8:
                    print(f"  {os.path.basename(path)} extra {k}: {got[k]}")
                    shown += 1
    print(f"{'fixNeighbourhood' if MODE else 'fixTerrain'}: {tot - bad}/{tot} values exact")
    return 1 if bad else 0


sys.exit(main())
