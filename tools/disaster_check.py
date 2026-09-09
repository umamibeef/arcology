#!/usr/bin/env python3
"""Compare a ported disaster against the original's own code.

Both sides start from the same city, the same point and the same two
seeds, then the tiles each one marked are compared.  A disaster writes
$FC (water) or $FD/$FE (fire) into XTXT.

usage: disaster_check.py <kind> <city> [<city>...]
       kind is riot, storm or flood
"""
import subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

ENTRY = {"riot": 0x37D34, "storm": 0x37C66, "flood": 0x37940,
         "chem": 0x37888,
         "poll": 0x37FB6, "crash": 0x38186, "fire": 0x38290,
         "tornado": 0x38766, "monster": 0x38574, "micro": 0x38B6C,
         "volcano": 0x37DD6, "quake": 0x383D4,
         "melt": 0x38916, "hurricane": 0x3755A}
#  Only drawing is stubbed now that $5FAA and $3A000 are ported.  A stub
#  is skipped, so D0 keeps whatever the code before the call left in it;
#  where the caller TESTS the result, the stub has to be given the answer
#  the real routine would return.
#
#  $30FE is the one that matters.  It hit-tests a point against the
#  visible window, and the volcano at $37F6A tests it to decide whether
#  to roll for which of two sounds to play -- a roll that comes from the
#  same generator every tile choice comes from, so getting it wrong
#  shifts the whole eruption.  The game scrolls the view to a disaster
#  before running it, so the erupting tile IS on screen and the answer is
#  "yes".  Left to a stale register it answered from the water bit that
#  $37F30 happened to leave in D0, which is not an answer at all.
STUBS = {a: None for a in
         (0x9728, 0x3F636, 0x392E, 0x15A54, 0x15408, 0x370A4,
          0x18E96, 0x18D40, 0x18E62, 0x1DEA, 0x155CA, 0x3D566, 0xA3E4)}
STUBS[0x30FE] = 1
SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
POINTS = [(64, 64), (32, 96), (100, 20), (10, 10), (120, 120)]


def oracle(path, kind, h, v):
    s = Sim(path)
    s.e.wr(A5 + 0x11DC, 2, 1)          # LFSR seed
    s.e.tb_seed = 1                    # Toolbox _Random seed
    s.e.wr(A5 + 0x13A4, 2, h & 0xFFFF)
    s.e.wr(A5 + 0x13A2, 2, v & 0xFFFF)
    s.e.wr(A5 - 0x7982, 2, h & 0xFFFF)   # view centre, row
    s.e.wr(A5 - 0x7980, 2, v & 0xFFFF)   # view centre, column
    #  the flood's ring search is expensive when no shoreline is near, so
    #  give every disaster room.  A truncated oracle run is not the
    #  original's answer, and Sim.run now raises rather than return one.
    s.run(ENTRY[kind], stubs=STUBS, limit=400000000)
    t = s.layer("XTXT")
    st = {("x", i // 128, i % 128): b for i, b in enumerate(t) if b}
    #  the volcano moves the ground, so compare the terrain layers too
    alt = s.layer("ALTM")
    for n in ("XTER", "XBIT", "XZON", "XBLD"):
        for i, b in enumerate(s.layer(n)):
            st[(n, i // 128, i % 128)] = b
    for i in range(16384):
        st[("ALTM", i // 128, i % 128)] = int.from_bytes(alt[2 * i:2 * i + 2], "big")
    st[("funds", 0, 0)] = s.e.rd(A5 + 0x1E26, 4)
    thg = s.block(0x2BCA, 40 * 12)
    for k in range(1, 40):
        st[("t", k, 0)] = thg[k * 12:(k + 1) * 12].hex()
    return st


def ported(path, kind, h, v):
    out = subprocess.run([SIMBIN, "--riot", path, str(h), str(v), kind],
                         capture_output=True, text=True).stdout
    got = {}
    for line in out.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] == "x":
            got[("x", int(f[1]), int(f[2]))] = int(f[3], 16)
        elif f[0] in ("XTER", "XBIT", "XZON", "XBLD", "ALTM"):
            got[(f[0], int(f[1]), int(f[2]))] = int(f[3], 16)
        elif f[0] == "funds":
            got[("funds", 0, 0)] = int(f[3], 16)
        elif f[0] == "t":
            got[("t", int(f[1]), 0)] = "".join(f[2:]).lower()
    return got


def main():
    kind, cities = sys.argv[1], sys.argv[2:]
    runs = bad = 0
    for path in cities:
        for h, v in POINTS:
            runs += 1
            want, got = oracle(path, kind, h, v), ported(path, kind, h, v)
            if want == got:
                continue
            bad += 1
            miss = sorted(set(want) - set(got))
            extra = sorted(set(got) - set(want))
            wrong = sorted(k for k in set(want) & set(got) if want[k] != got[k])
            print(f"{os.path.basename(path)} @{h},{v}: "
                  f"want {len(want)} got {len(got)}  "
                  f"missing {len(miss)} extra {len(extra)} wrong {len(wrong)}")
            for k in (miss[:4] + extra[:4] + wrong[:4]):
                print(f"    {k}  want {want.get(k, '--')}  got {got.get(k, '--')}")
    print(f"{kind}: {runs - bad}/{runs} runs exact")
    return 1 if bad else 0


sys.exit(main())
