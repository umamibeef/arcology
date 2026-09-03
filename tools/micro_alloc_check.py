#!/usr/bin/env python3
"""Diff sim_alloc_micro against $EEAE, the XMIC allocator.

Placing a special building has to give it a record, and the marker the
caller writes into XTXT has to be the one the year-end pass at $101AC
will search for.  Get either wrong and the building is simulated by
nobody, which is invisible until a plant fails to age.

Both sides are handed the same city and asked to place one building of
every id from 0xC6 to 0xFF, in order, so the slot allocation, the
eviction when the table fills, and the initial contents of each record
are all exercised in one pass.

usage: micro_alloc_check.py [<city>...]
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clock_check as CC          # noqa: E402  (its STUBS list)
from runsim import Sim, A5        # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BIN = os.path.join(ROOT, "build", "arcology")


def oracle(path):
    """$EEAE for every id, and the record each one produced."""
    s = Sim(path)
    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    xmic = s.e.rd(A5 + 0x2BC6, 4)
    out = []
    for b in range(0xC6, 0x100):
        #  Pascal order: the LAST word pushed is $8(a6), so y goes last.
        s.e.a[7] = s.e.SP_INIT
        for v in (b, 3, 3):
            s.e.a[7] -= 2
            s.e.wr(s.e.a[7], 2, v & 0xFFFF)
        s.e.a[7] -= 4
        s.e.wr(s.e.a[7], 4, 0xDEAD0000)
        r = s.e.run(0xEEAE, 0xDEAD0000, limit=40000000, real_calls=True,
                    stubs=CC.STUBS)
        if r is not None:
            raise RuntimeError("$EEAE stopped early: %s" % r)
        m = s.e.d[0] & 0xFFFF
        if m & 0x8000:
            m -= 0x10000
        rec = ""
        if m:
            slot = m - 0x33
            if 0 <= slot < 150:
                rec = bytes(s.e.mem[xmic + slot * 8:xmic + slot * 8 + 8]).hex()
        out.append((b, m, rec))
    return out


def main():
    args = sys.argv[1:]
    if not args:
        d = os.path.join(ROOT, "cities")
        args = [os.path.join(d, n) for n in
                ("bayview.sc2", "hawaii.sc2", "1898.sc2", "biggestc.sc2")]
    bad = tot = 0
    for path in args:
        if not os.path.isfile(path):
            continue
        name = os.path.basename(path)[:13]
        want = oracle(path)
        got = {}
        r = subprocess.run([BIN, "--allocmicro", path], check=True,
                           capture_output=True, text=True)
        for line in r.stdout.splitlines():
            f = line.split()
            got[int(f[0], 16)] = (int(f[1]), f[2] if len(f) > 2 else "")
        shown = 0
        for b, m, rec in want:
            tot += 1
            g = got.get(b)
            if g is None or g[0] != m or g[1] != rec:
                bad += 1
                if shown < 6:
                    shown += 1
                    print("  %-13s $%02X  want marker %-4d %s"
                          % (name, b, m, rec))
                    print("  %-13s      got  marker %-4d %s"
                          % ("", g[0] if g else -1, g[1] if g else "-"))
    print("allocMicro: %d / %d placements exact" % (tot - bad, tot))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
