#!/usr/bin/env python3
"""leak_check.py -- what the program left behind when it ended.

`leaks` reports every block a process can no longer reach at exit.  Most
of what it finds on this platform is the window server's: SDL and Metal
open XPC connections whose objects the frameworks never give back, and
those are not ours to fix.  So this counts only the leaks whose own
allocation stack passes through the binary -- the ones we caused.

AddressSanitizer cannot do this job here: LeakSanitizer is unsupported on
Darwin, and ASan's allocator stops `leaks` reading the heap at all.  The
binary under test must be a plain one.

A headless frame is the run: it opens the atlas, loads a city, builds the
mesh, draws once and shuts down -- every subsystem the game has, in the
order the game has them.

    python3 tools/leak_check.py [--binary build/arcology] [--city atlanta]
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

STACK = re.compile(r"^STACK OF (\d+) INSTANCES? OF '([^']*)':$", re.M)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--city", default="atlanta")
    a = ap.parse_args()
    city = os.path.join(ROOT, "cities", a.city + ".sc2")
    if not os.path.exists(a.binary) or not os.path.exists(city):
        print("leaks  nothing to run")
        return 0
    env = dict(os.environ, MallocStackLogging="1")
    p = subprocess.run(
        ["leaks", "--atExit", "--", a.binary, city, "--mute", "--win", "320x200",
         "--shot", os.path.join("/tmp", "leak_check.png")],
        capture_output=True, text=True, env=env, cwd=ROOT)
    out = p.stdout
    total = re.search(r"(\d+) leaks for (\d+) total leaked bytes", out)
    if not total:
        print("leaks  the run gave no report")
        print(p.stderr[-400:])
        return 1
    #  Split the report into one block per distinct allocation stack and
    #  keep the blocks that name the binary: those are ours.
    name = os.path.basename(a.binary)
    ours, blocks = [], STACK.split(out)
    for i in range(1, len(blocks), 3):
        n, what, body = int(blocks[i]), blocks[i + 1], blocks[i + 2]
        #  The binary's own frames look like "N  arcology  0x... symbol".
        if re.search(r"^\s*\d+\s+" + re.escape(name) + r"\s+0x", body, re.M):
            ours.append((n, what))
    mine = sum(n for n, _ in ours)
    print("leaks  %s leaks, %s bytes; %d of them ours"
          % (total.group(1), total.group(2), mine))
    for n, what in ours:
        print("    %d x %s" % (n, what))
    return 1 if mine else 0


if __name__ == "__main__":
    sys.exit(main())
