#!/usr/bin/env python3
"""Diff sim_graph_pass against $22330 under the interpreter.

$22330 is the graph history: sixteen series of 52 longs, on three time
bases.  The block lives behind sixteen pointers at A5+0x2BDC, so runsim
has to build it before the routine runs -- see _graph there and the note
on unset oracle state in the project brief.

Both sides are seeded from the city's own XGRP chunk and then run one
month.  All 832 samples and all 16 scales are compared.

A month and year can be forced, which is how the half-yearly and
five-yearly bands get exercised without needing a city saved in the
right month:

    graph_check.py <city> [<city>...]           the saved date
    graph_check.py --month 6 <city> ...         July, the half-year band
    graph_check.py --month 0 --years 10 <city>  January of a fifth year
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")
NS, NSAMP = 16, 52
NAMES = ["city_size", "residents", "commerce", "industry",
         "traffic", "pollution", "value", "crime",
         "power", "water", "health", "education",
         "unemployment", "nat_gnp", "nat_pop", "fed_rate"]


def oracle(path, month, years):
    s = Sim(path)
    if month is not None:
        s.e.wr(A5 + 0x1E32, 2, month)
    if years is not None:
        s.e.wr(A5 + 0x1E38, 4, years)
    #  power and water are their own clock phases ($220DA and $20FC4),
    #  NOT part of the scan.  Series 8 and 9 are 100 minus what they
    #  leave behind, so they have to run or those two read as 100.
    s.run(0x20FC4, stubs={0x9728, 0x15A54}, limit=60000000)
    s.run(0x2156E, stubs={0x9728, 0x15A54}, limit=60000000)
    #  then the scan, so the totals the four averages divide are right
    s.run(0x2317E, stubs={0x9728, 0x15A54, 0x36EA6}, limit=200000000)
    if month is not None:            # the scan can move the calendar on
        s.e.wr(A5 + 0x1E32, 2, month)
    if years is not None:
        s.e.wr(A5 + 0x1E38, 4, years)
    s.run(0x22330, stubs={0x9728, 0x15A54}, limit=60000000)

    out = {}
    for i in range(NS):
        p = s.e.rd(A5 + 0x2BDC + i * 4, 4)
        for k in range(NSAMP):
            out[("g", i, k)] = s.e.rd(p + k * 4, 4)
    scale = s.e.rd(A5 + 0x2C1C, 4)
    for i in range(NS):
        out[("max", i)] = s.e.rd(scale + i * 4, 4)
    return out


def model(path, month, years):
    argv = [SIMBIN, "--graph", path]
    if month is not None:
        argv.append(str(month))
        argv.append(str(years if years is not None else 0))
    r = subprocess.run(argv, capture_output=True, text=True)
    out = {}
    for line in r.stdout.split("\n"):
        f = line.split()
        if len(f) == 4 and f[0] == "g":
            out[("g", int(f[1]), int(f[2]))] = int(f[3]) & 0xFFFFFFFF
        elif len(f) == 3 and f[0] == "max":
            out[("max", int(f[1]))] = int(f[2]) & 0xFFFFFFFF
    return out


def main():
    args = sys.argv[1:]
    month = years = None
    while args and args[0].startswith("--"):
        if args[0] == "--month":
            month = int(args[1])
        elif args[0] == "--years":
            years = int(args[1])
        else:
            print("unknown option", args[0])
            return 2
        args = args[2:]

    bad = tot = 0
    shown = 0
    for path in args:
        if not os.path.isfile(path):
            continue          # the shipped cities has directories in it too
        try:
            want = oracle(path, month, years)
        except Exception as e:
            print("  %-14s skipped: %s" % (os.path.basename(path), e))
            continue
        got = model(path, month, years)
        for key in sorted(want, key=str):
            tot += 1
            if want[key] != got.get(key):
                bad += 1
                if shown < 20:
                    shown += 1
                    if key[0] == "g":
                        where = "%s[%d]" % (NAMES[key[1]], key[2])
                    else:
                        where = "%s.max" % NAMES[key[1]]
                    print("  %-14s %-18s want %-12s got %s"
                          % (os.path.basename(path), where,
                             want[key], got.get(key)))
    tag = "graphHistoryPass"
    if month is not None:
        tag += " (month %d, years %s)" % (month, years)
    print("%s: %d/%d values exact" % (tag, tot - bad, tot))
    return 1 if bad else 0


sys.exit(main())
