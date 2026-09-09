#!/usr/bin/env python3
"""Diff the whole 25-phase clock against $21EDE, tick for tick.

Every other checker in this project drives ONE routine.  That leaves
anything travelling between phases untested, and it hid two real bugs:
the water-treatment flag at A5+0x2CA0, which waterGrid sets and the
pollution stage reads, and the power ordinance boosting the capacity it
should not have.  Both were invisible because the layers were right and
only the scalars were wrong.

This runs sim_tick and $21EDE the same number of times from the same
city and compares every layer and every scalar.

usage: clock_check.py [--ticks N] <city> [<city>...]
"""
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import A5, Sim, at_tick

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")

#  XTXT belongs here as much as any of the others: it carries the sign
#  index and, on a special building's tile, the XMIC record that owns it
#  ($33 + slot).  Leaving it out hid the microsim allocator entirely --
#  a stub that returns 0 where the original returns a marker changes
#  XTXT and NOTHING ELSE, so every other layer went on matching.
LAYERS = ("ALTM", "XBLD", "XZON", "XTER", "XUND", "XBIT", "XTXT", "XTRF",
          "XPLT", "XVAL", "XCRM", "XPLC", "XFIR", "XPOP", "XROG")

#  interface the clock reaches that the model deliberately leaves out
#  The clock's last two phases are pure interface: $222C4 is the
#  redraw and the graph windows, $222F8 the rest of the drawing.  Their
#  call lists are stubbed at the top rather than leaf by leaf, so the
#  reason for each entry stays readable.  Stub ROUTINES, never traps --
#  the interpreter refuses an unknown trap on purpose, because guessing
#  a Pascal stack effect corrupts the frame silently.
STUBS = {
    0x9728: 0,   # idlePump, the event pump between phases
    0x15A54: 0,
    0x36EA6: 0,
    0x15408: 0,  # phase 23, $222C4
    0x3C8E6: 0,
    0x3CD10: 0,
    0x3CF4E: 0,
    0x021FA: 0,  # phase 24, $222F8
    0x3E03E: 0,
    0x3D79E: 0,
    #  Four keyboard predicates of the same shape: _GetKeys, test one
    #  bit, answer 0xFF or 0 in D0.  Nothing is held during an
    #  automated run, so all four answer no.
    0x00810: 0,
    0x00832: 0,
    0x00850: 0,
    0x0086E: 0,
    #  PICT loading and drawing.
    0x3D566: 0,
    0x0154A: 0,
    #  $370A4 sets the cursor, 33 callers.
    0x370A4: 0,
    #  $3C480 fetches a menu, 11 callers.
    0x3C480: 0,
    #  $23EE draws, 5 callers.
    0x023EE: 0,
    #  $3C3B0 reads the menu bar, 4 callers.  Only reached on runs
    #  long enough for the interface to want redrawing.
    0x3C3B0: 0,
}

#  name -> (A5 offset, width in bytes, signed)
SCALARS = {
    "date": (0x1E1E, 4, 1), "funds": (0x1E26, 4, 1), "bonds": (0x1E2A, 4, 1),
    "month": (0x1E32, 2, 1), "years": (0x1E38, 4, 1),
    "land_value_tot": (0x1E76, 4, 1), "crime_tot": (0x1E7A, 4, 1),
    "traffic_tot": (0x1E7E, 4, 1), "pollution_tot": (0x1E82, 4, 1),
    "power_pct": (0x1E86, 4, 1), "water_pct": (0x1E8A, 4, 1),
    "power_capacity": (0x11D6, 4, 1), "water_capacity": (0x11D2, 4, 1),
    "population": (0x1E96, 4, 1), "pop_increase": (0x1E9A, 4, 1),
    "pop_decrease": (0x1E9E, 4, 1), "developed": (0x11D0, 2, 1),
    "ordinances": (0x1E6E, 4, 1), "unemployment": (0x2C82, 4, 1),
    "treatment": (0x2CA0, 2, 1),
    #  the weather walk drives wind and solar output, so a drift here
    #  shows up as capacity rather than as anything named weather
    "temperature": (0x1F00, 1, 0), "weather1": (0x1F01, 1, 0),
    "weather2": (0x1F02, 1, 0), "weather_state": (0x1F03, 1, 0),
    #  what the disaster trigger at $310B0 chose, and where
    "disaster_kind": (0x13A0, 2, 1), "disaster_v": (0x13A2, 2, 1),
    "disaster_h": (0x13A4, 2, 1),
    #  A 68k (d16,An) displacement is SIGNED, so the listing's
    #  $867e(a5) is A5-0x7982, not A5+0x867E.  Reading the positive
    #  offset lands on an unrelated word and reports a difference
    #  that is not there.
    "centre_y": (-0x7982, 2, 1), "centre_x": (-0x7980, 2, 1),
    "age_w65": (0x1EAA, 4, 1), "age_w90": (0x1EA6, 4, 1),
    "nat_index": (0x1EB2, 4, 1), "nat_index2": (0x1EB6, 4, 1),
    "nat_mood": (0x1EAE, 2, 1),
}
for _i in range(3):
    SCALARS["rci_demand%d" % _i] = (0x1E3C + _i * 2, 2, 1)

#  pointer-backed blocks: name template -> (A5 pointer offset, count)
BLOCKS = [("accum8_%d", 0x1EBA, 8), ("rci_pop%d", 0x1EBE, 3),
          #  The age pyramid: twenty brackets of heads, education and
          #  life expectancy, interleaved three longs to a bracket at
          #  A5+0x1EDE.  It was missing here for the same reason XTXT
          #  was missing from the layers -- nothing reads it back as a
          #  headline number, so a drift of one or two people stayed
          #  invisible until it changed a branch in the emigration loop
          #  sixty ticks later.  Compare it directly.
          #  THREE separate arrays of twenty, not one interleaved block:
          #  A5+0x1EDE, 0x1EE2 and 0x1EE6 are three pointers.  (In MISC
          #  they ARE interleaved, three longs to a bracket from
          #  MISC[31], which is what economy.c's HEADS/EDUQ/LIFE index.)
          ("heads%d", 0x1EDE, 20),
          ("eduq%d", 0x1EE2, 20),
          ("life%d", 0x1EE6, 20)]
#  A5+0x1EFA -- sixteen WORD counters, one per id $DD..$EC.  The
#  military and airport ladders read them, so a drift here picks the
#  wrong building rather than showing up as a number.
WBLOCKS = [("infra%d", 0x1EFA, 16),
            #  The eleven industry levels, A5+0x1EEA.  The national
            #  economy carries them from month to month and the labour
            #  market branches on whether each is zero, so a drift here
            #  changes how many dice the pass throws rather than showing
            #  up as a number anyone reads.
            ("ind_level%d", 0x1EEA, 11)]


def sgn(v, w):
    bits = w * 8
    return v - (1 << bits) if v >= (1 << (bits - 1)) else v


#  The six LFSR entry points and THINK C's rand(), watched at their rts
#  so d0 holds the masked result.  The letters match rng.c's logdraw so
#  the two logs line up; the emulator appends ("t", v) for every Toolbox
#  _Random into the same list, keeping one ordered stream.
RTS = {0x20F4A: "0", 0x20F62: "1", 0x20F7A: "3",
       0x20F92: "f", 0x20FAA: "6", 0x20FC2: "7", 0x20F2E: "L"}


def oracle(path, ticks):
    #  at_tick keeps a checkpoint of the machine AND the draws that got
    #  it there, so the second run at a given tick costs a tenth of a
    #  second instead of two minutes.  Seeding both generators is its
    #  job now -- the C harness calls rng_seed(1, 1), and without the
    #  same seed the two streams part company on the very first draw,
    #  which looks exactly like a model bug.
    s, log = at_tick(path, ticks, stubs=STUBS)
    #  A restored machine has no live log -- the draws happened when the
    #  checkpoint was built.  Hand the saved one back so the dice
    #  comparison below reads it exactly as it did before.
    s.e.rng_log = list(log)
    sc = {}
    for name, (off, w, is_signed) in SCALARS.items():
        v = s.e.rd(A5 + off, w)
        sc[name] = sgn(v, w) if is_signed else v
    for tmpl, off, n in BLOCKS:
        p = s.e.rd(A5 + off, 4)
        for i in range(n):
            sc[tmpl % i] = sgn(s.e.rd(p + i * 4, 4), 4)
    for tmpl, off, n in WBLOCKS:
        p = s.e.rd(A5 + off, 4)
        for i in range(n):
            sc[tmpl % i] = sgn(s.e.rd(p + i * 2, 2), 2)
    #  the budget block at A5+0x2C30.  The records are 0x70 apart --
    #  $224C0 reads department 10 at +0x4C0 -- and `amount` sits at
    #  +0x60 within one, `funding` at +0x64, `accrued` at +0x68.
    dp = s.e.rd(A5 + 0x2C30, 4)
    for i in range(16):
        for nm, off in (("amount", 0x60), ("funding", 0x64), ("accrued", 0x68)):
            sc["dept%d_%s" % (i, nm)] = sgn(s.e.rd(dp + i * 0x70 + off, 4), 4)

    for i in range(16):
        p = s.e.rd(A5 + 0x2BDC + i * 4, 4)
        sc["gmax%d" % i] = sgn(s.e.rd(s.e.rd(A5 + 0x2C1C, 4) + i * 4, 4), 4)
        for k in range(52):
            sc["g%d_%d" % (i, k)] = sgn(s.e.rd(p + k * 4, 4), 4)
    return s, sc


def main():
    args = sys.argv[1:]
    ticks = 25
    if args and args[0] == "--ticks":
        ticks = int(args[1])
        args = args[2:]

    bad_l = bad_s = tot_l = tot_s = 0
    dice_bad = dice_tot = 0
    shown = 0
    for path in args:
        if not os.path.isfile(path):
            continue
        try:
            s, want = oracle(path, ticks)
        except RuntimeError as e:
            #  usually one more piece of interface the clock reaches.
            #  Report it and keep going rather than losing the run.
            print("  %-13s stopped: %s" % (os.path.basename(path)[:13], e))
            continue
        if s is None:
            print("  %-14s the interpreter stopped" % os.path.basename(path))
            continue
        md = tempfile.mkdtemp()
        subprocess.run([SIMBIN, "--clock", path, str(ticks), md], check=True,
                       stdout=subprocess.DEVNULL)

        for n in LAYERS:
            a, b = s.layer(n), open(os.path.join(md, n), "rb").read()
            if n == "ALTM":
                #  two bytes a cell.  The interpreter keeps them big
                #  endian, the C dump writes host order, so line them up
                #  rather than reporting every cell as a difference.
                b = b"".join(b[i + 1:i + 2] + b[i:i + 1]
                             for i in range(0, len(b), 2))
            k = min(len(a), len(b))
            live = sum(1 for i in range(k) if a[i] or b[i])
            ok = sum(1 for i in range(k) if (a[i] or b[i]) and a[i] == b[i])
            tot_l += live
            bad_l += live - ok
            if ok != live and shown < 24:
                shown += 1
                print("  %-13s %-5s %d of %d live cells differ"
                      % (os.path.basename(path)[:13], n, live - ok, live))

        #  the random stream, which has to match draw for draw
        dpath = os.path.join(md, "dice")
        if os.path.exists(dpath):
            #  'S' is a C-side marker, not a draw
            mine = [(f[0], int(f[1]))
                    for f in (l.split() for l in open(dpath))
                    if len(f) == 2 and f[0] != "S"]
            theirs = [(k, v) for k, v, *_ in (s.e.rng_log or [])]
            n = min(len(mine), len(theirs))
            first = next((i for i in range(n) if mine[i] != theirs[i]), None)
            if len(mine) != len(theirs) or first is not None:
                print("  %-13s dice: %d drawn, %d expected%s"
                      % (os.path.basename(path)[:13], len(mine), len(theirs),
                         "" if first is None
                         else ", first differs at %d (%s vs %s)"
                              % (first, mine[first], theirs[first])))
            dice_bad += (len(mine) != len(theirs)) or (first is not None)
            dice_tot += 1

        got = {}
        for line in open(os.path.join(md, "scalars")):
            f = line.split()
            if len(f) == 2:
                got[f[0]] = int(f[1])
        for name in sorted(want):
            tot_s += 1
            if want[name] != got.get(name):
                bad_s += 1
                if shown < 24:
                    shown += 1
                    print("  %-13s %-16s want %-12s got %s"
                          % (os.path.basename(path)[:13], name,
                             want[name], got.get(name)))

    print("clock, %d ticks: layers %d/%d live cells, scalars %d/%d exact, "
          "dice %d/%d cities identical"
          % (ticks, tot_l - bad_l, tot_l, tot_s - bad_s, tot_s,
             dice_tot - dice_bad, dice_tot))
    return 1 if (bad_l or bad_s or dice_bad) else 0


if __name__ == "__main__":
    sys.exit(main())
