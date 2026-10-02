#!/usr/bin/env python3
"""call_up.py -- every place C calls UP into a script, each one named.

C is not to call into a script except where the design says it may, and
the list below is that design.  A site is a call of `script_rule_object`
or `api_rule_call` in src/render or src/script; each one is allowed by
file and by the rule it names, with the reason it is there.  A site not
on the list fails, and so does a listed site that has gone, so the list
and the tree cannot drift apart.

**This count is not a goal and must never be treated as one.**  See THE
PURPOSE at the top of the house rules.  What matters is whether the look of the city can
be changed from a script.

    tools/call_up.py            # the sites, and whether each is allowed
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TREES = [os.path.join(ROOT, "src", "render"), os.path.join(ROOT, "src", "script")]

#  (file, the rule the site names) -> why it is there.  A rule given as
#  "*" is chosen at run time from a name the script registered.
ALLOWED = {
    ("src/render/net/drive.c", "frame"):
        "the drive: the one door, entered once a turn",
    ("src/script/api_fit.c", "pieces"):
        "arc.fit: a script asking for its own path to be cut",
    ("src/script/api_model.c", "*"):
        "arc.model: a model's builder, called by the name the script gave it",
    ("src/script/script.c", "family"):
        "arc.rules.family: a family's knobs, read when it is declared",
}

#  A call, with the rule it names: a string, or an expression (then "*").
CALL = re.compile(r'\b(script_rule_object|api_rule_call)\s*\(\s*(?:L\s*,\s*|s_L\s*,\s*)?("([a-z_]+)"|[A-Za-z_][A-Za-z_0-9\[\]]*)')
#  The door's own body and the two entry helpers are not call sites.
DEFINITION = re.compile(r"^(int|static int|void)\s+(script_rule_object|api_rule_begin|api_rule_call)\s*\(")


def sites():
    out = []
    for tree in TREES:
        for root, dirs, names in os.walk(tree):
            dirs[:] = [d for d in dirs if d != "generated"]
            for n in sorted(names):
                if not n.endswith(".c"):
                    continue
                p = os.path.join(root, n)
                rel = os.path.relpath(p, ROOT)
                in_door = False
                for i, line in enumerate(open(p, errors="replace"), 1):
                    if DEFINITION.match(line):
                        in_door = True
                    if in_door and line.startswith("}"):
                        in_door = False
                        continue
                    if in_door or line.lstrip().startswith(("*", "/*")):
                        continue
                    for m in CALL.finditer(line):
                        rule = m.group(3) if m.group(3) else "*"
                        out.append((rel, i, m.group(1), rule))
    return out


def main():
    found = sites()
    bad = 0
    seen = set()
    for rel, i, fn, rule in found:
        key = (rel, rule)
        why = ALLOWED.get(key)
        seen.add(key)
        if why:
            print("call up  %-28s %5d  %s(%s)  -- %s" % (rel, i, fn, rule, why))
        else:
            print("call up  %-28s %5d  %s(%s)  -- NOT ALLOWED: C is not to call into a script here"
                  % (rel, i, fn, rule))
            bad += 1
    for key in sorted(set(ALLOWED) - seen):
        print("call up  %s no longer calls %s; take it off the list" % key)
        bad += 1
    if not found:
        print("call up: found no site at all; the scanner is blind")
        return 1
    if bad:
        print("call up: %d site%s not as the list says" % (bad, "" if bad == 1 else "s"))
        return 1
    print("call up: %d sites, every one on the list" % len(found))
    return 0


if __name__ == "__main__":
    sys.exit(main())
