#!/usr/bin/env python3
"""call_up.py -- how many places the renderer calls UP into Lua.

A RATCHET, and nothing more: it fails when the number RISES, so a new
place where C assembles some state and asks Lua to draw it cannot appear
unnoticed.

**This count is not a goal and must never be treated as one.**  See THE
PURPOSE in CLAUDE.md.  What matters is whether the look of the city can
be changed from a script; a call-up removed that leaves no new number
reachable has bought nothing.  Read the count as "no worse than before",
never as "this far to go".

What is left is ONE site, and it is the drive.  `arc.rules.frame` is
entered once a frame and everything else hangs off it: the build under
`arc.rules.world`, the world that moves under `arc.rules.moving`.

Every path the renderer draws is cut by `arc.rules.pieces`, and every one
of them goes through the CUT QUEUE (mesh/fit.c): a pass that wants a path
cut puts the chain in the queue and reads the pieces back once the drive
has been round.  A junction's connectors, a segment's dead-end caps, the
links across a meet, a thread junction's threads, a spur's descent, its
join and the two legs it falls back on -- all of them queue.  `arc.fit`
asks the same rule from src/script, which is a SCRIPT asking, not the
renderer.

    tools/call_up.py            # the count, and where they are
    tools/call_up.py --list     # every site, file by file
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TREE = os.path.join(ROOT, "src", "render")

#  What the count may not exceed.  It is one, and it is the drive.
CEILING = 1

#  A call from C into a script.  `script_emit_open` opens the window a
#  prop rule draws through, so it is a call up as much as the rule is.
CALL = re.compile(r"\b(script_rule_[a-z_]+|script_stage_[a-z_]+|script_emit_open)\s*\(")

#  The one that is meant to be there.
DRIVE = re.compile(r'script_rule_object\s*\(\s*"frame"')


def sites():
    out = []
    for root, dirs, names in os.walk(TREE):
        dirs[:] = [d for d in dirs if d != "generated"]
        for n in sorted(names):
            if not n.endswith(".c"):
                continue
            p = os.path.join(root, n)
            for i, line in enumerate(open(p, errors="replace"), 1):
                for m in CALL.finditer(line):
                    out.append((os.path.relpath(p, ROOT), i, m.group(1),
                                bool(DRIVE.search(line))))
    return out


def main():
    all_ = sites()
    drive = [s for s in all_ if s[3]]
    if "--list" in sys.argv:
        for f, i, what, is_drive in all_:
            print("%-34s %5d  %s%s" % (f, i, what, "   <- the drive" if is_drive else ""))
        print()
    n = len(all_)
    per = {}
    for f, _, _, _ in all_:
        per[f] = per.get(f, 0) + 1
    if n > CEILING:
        print("call up: %d sites, over the ratchet of %d -- a new one appeared." % (n, CEILING))
        print("         C is not to call into a script except for the drive.")
        for f in sorted(per, key=lambda k: -per[k])[:6]:
            print("         %-34s %d" % (f, per[f]))
        return 1
    if len(drive) != 1:
        print("call up: %d drive sites; there is meant to be exactly one" % len(drive))
        return 1
    left = n - 1
    print("call up: %d site%s, %d of them the drive; %d others (ratchet %d)"
          % (n, "" if n == 1 else "s", len(drive), left, CEILING))
    return 0


if __name__ == "__main__":
    sys.exit(main())
