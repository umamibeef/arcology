#!/usr/bin/env python3
"""Diff the ported thing-stepper against $09E0A, the original's own.

$09E0A runs from the main loop, not the simulation tick: it walks the
forty XTHG records once and moves each according to its type.  Only
drawing is stubbed.

usage: things_check.py <city> [<city>...] [--passes N]
"""
import subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
#  every one of these was profiled and only draws
DRAW = {0x9728, 0x3F636, 0x392E, 0x15A54, 0x18E96, 0x18D40, 0x18E62, 0x1DEA,
        0x30FE, 0x155CA, 0x36EA6, 0x36CB2, 0x158D0, 0xA3E4, 0x15408, 0x370A4}
#  the per-type counters the driver clears and the steppers rebuild
COUNTERS = {0x12E0: "plane", 0x12E2: "heli", 0x12E4: "c12E4", 0x12E6: "c12E6",
            0x12E8: "c12E8", 0x12EA: "monster", 0x12EC: "road",
            0x12EE: "tornado"}


def oracle(path, passes):
    s = Sim(path)
    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    for _ in range(passes):
        s.run(0x9E0A, stubs=DRAW, limit=200000000)
    out = {}
    thg = s.block(0x2BCA, 40 * 12)
    for i in range(40):
        out[("t", i)] = thg[i * 12:(i + 1) * 12].hex()
    for i, b in enumerate(s.layer("XTXT")):
        if b:
            out[("x", i)] = b
    for off, name in COUNTERS.items():
        out[("c", off)] = s.e.rd(A5 + off, 2)
    return out


def main():
    args = [a for a in sys.argv[1:] if a != "--passes"]
    passes = 1
    if "--passes" in sys.argv:
        passes = int(args.pop())
    bad = tot = 0
    for path in args:
        want = oracle(path, passes)
        got = {}
        r = subprocess.run([SIMBIN, "--things", path, str(passes)],
                           capture_output=True, text=True)
        for line in r.stdout.splitlines():
            f = line.split()
            if not f:
                continue
            if f[0] == "t":
                got[("t", int(f[1]))] = f[2]
            elif f[0] == "x":
                got[("x", int(f[1]))] = int(f[2], 16)
            elif f[0] == "c":
                got[("c", int(f[1], 16))] = int(f[2])
        shown = 0
        for k in sorted(want, key=lambda t: (t[0], t[1])):
            tot += 1
            if want[k] != got.get(k):
                bad += 1
                if shown < 10:
                    lbl = {"t": "thing", "x": "XTXT", "c": "counter"}[k[0]]
                    where = (f"[{k[1]}]" if k[0] != "x"
                             else f"({k[1]//128},{k[1]%128})")
                    print(f"  {os.path.basename(path)} {lbl}{where}: "
                          f"want {want[k]} got {got.get(k)}")
                    shown += 1
    print(f"stepThings: {tot - bad}/{tot} values exact")
    return 1 if bad else 0


sys.exit(main())
