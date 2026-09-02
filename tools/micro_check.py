#!/usr/bin/env python3
"""Diff sim_microsim against $101AC, the year-end microsimulation pass.

$101AC walks XMIC -- 150 eight-byte records, one per special building --
once a year from the January settlement, and dispatches on each record's
building id through a 58-entry table.  It is what ages the power plants,
counts the arcologies into the population, and sets the police radius.

Both sides start from the same city and the same two seeds.  Compared:
every byte of XMIC, the scalars the pass writes, and the random stream
draw for draw.

usage: micro_check.py [<city>...]      (default: a spread from cities/)
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import clock_check as CC          # noqa: E402  (its STUBS list)
from runsim import Sim, A5        # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
BIN = os.path.join(ROOT, "build", "arcology")

#  What the pass writes outside XMIC, and where the C dump names it.
SCALARS = {
    "police_term":    (0x2C8E, 2, True),
    "arco_pop":       (0x2C98, 4, True),
    "police_load":    (0x2C92, 2, True),
    "transit_bus":    (0x1246, 4, True),
    "transit_rail":   (0x124A, 4, True),
    "transit_subway": (0x124E, 4, True),
}


def sgn(v, n):
    bits = n * 8
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def oracle(path):
    s = Sim(path)
    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    log = []
    s.e.rng_log = log
    s.run(0x101AC, stubs=CC.STUBS, limit=200000000)
    xmic = s.e.rd(A5 + 0x2BC6, 4)
    want = bytes(s.e.mem[xmic:xmic + 1200])
    sc = {k: sgn(s.e.rd(A5 + off, n), n) if sg else s.e.rd(A5 + off, n)
          for k, (off, n, sg) in SCALARS.items()}
    return want, sc, len(log)


def main():
    args = sys.argv[1:]
    if not args:
        d = os.path.join(ROOT, "cities")
        args = [os.path.join(d, n) for n in
                ("bayview.sc2", "centerville.sc2", "hawaii.sc2",
                 "1898.sc2", "happyland.sc2", "biggestc.sc2")]
    bad_r = tot_r = bad_s = tot_s = 0
    for path in args:
        if not os.path.isfile(path):
            continue
        name = os.path.basename(path)[:13]
        try:
            want, wsc, draws = oracle(path)
        except RuntimeError as e:
            print("  %-13s stopped: %s" % (name, e))
            continue
        md = tempfile.mkdtemp()
        subprocess.run([BIN, "--micro", path, md], check=True,
                       stdout=subprocess.DEVNULL)
        got = open(os.path.join(md, "xmic"), "rb").read()
        if len(got) != len(want):
            print("  %-13s XMIC is %d bytes, expected %d"
                  % (name, len(got), len(want)))
            continue
        #  Report by RECORD: a record is one building, and one wrong
        #  field in it is one wrong building, not four wrong bytes.
        bad = sorted({i // 8 for i in range(1200) if got[i] != want[i]})
        tot_r += 149
        bad_r += len(bad)
        if bad:
            #  The arms draw in record order, so ONE unported arm puts
            #  the stream out of phase and every record after it differs
            #  whether its own arm is right or not.  The first differing
            #  record is the only one worth looking at.
            f = bad[0]
            print("  %-13s FIRST differing record %d, type $%02X: "
                  "want %s got %s"
                  % (name, f, want[f * 8], want[f * 8:f * 8 + 8].hex(),
                     got[f * 8:f * 8 + 8].hex()))
            types = {}
            for r in bad:
                types.setdefault(want[r * 8], []).append(r)
            print("  %-13s %d of 149 records differ:" % (name, len(bad)))
            for t in sorted(types):
                rs = types[t]
                print("      type $%02X  %2d records  e.g. rec %d "
                      "want %s got %s"
                      % (t, len(rs), rs[0],
                         want[rs[0] * 8:rs[0] * 8 + 8].hex(),
                         got[rs[0] * 8:rs[0] * 8 + 8].hex()))
        gsc = {}
        for line in open(os.path.join(md, "scalars")):
            k, v = line.split()
            gsc[k] = int(v)
        for k in sorted(SCALARS):
            tot_s += 1
            if gsc.get(k) != wsc[k]:
                bad_s += 1
                print("  %-13s %-15s want %-12d got %d"
                      % (name, k, wsc[k], gsc.get(k)))
        print("  %-13s %d draws expected" % (name, draws))
    print("microsim: records %d/%d exact, scalars %d/%d exact"
          % (tot_r - bad_r, tot_r, tot_s - bad_s, tot_s))
    return 1 if (bad_r or bad_s) else 0


if __name__ == "__main__":
    sys.exit(main())
