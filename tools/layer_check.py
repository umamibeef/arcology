#!/usr/bin/env python3
"""layer_check -- nothing in src/sim is global by accident.

    python3 tools/layer_check.py

sim.c was one 8,438-line file.  Splitting it meant taking `static` off
every routine that turned out to be called from another part, and the
danger in that is silent: drop a `static` you did not have to drop and
the name becomes global, linking against anything in the program that
happens to share it.  Nothing would say so.  These are names like
`thing`, `bearing`, `stencil` and `wander` -- exactly the ones a
renderer or a tool would also want.

So the rule is: a function in src/sim is either `static`, or it is
declared in a header.  sim.h is the simulation's face to the rest of
the program; sim_int.h is what its own files ask of each other.  A
definition in neither is a leak, and the fix is almost always to put
`static` back.

The declaration must be in a header, not merely somewhere: an `extern`
written at the top of the .c that calls it would satisfy the compiler
and defeat the point, which is that every meet is written down in
one place a reader can go and count.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SIM = ROOT / "src" / "sim"
#  The mode entry points.  src/app/main.c declares all five together as
#  its dispatch table, which is the one place a reader would go to count
#  them -- a header would only scatter them.
ALLOWED = {"main", "arc_dev_main", "testcity_main", "atlas_main",
           "soft_main", "game_main"}

DEFN = re.compile(
    r'^(?P<qual>(?:[A-Za-z_]\w*[ \t]+)+\*?\s*)(?P<name>[A-Za-z_]\w*)\('
    r'(?![^;{]*\);)[^;{]*?\)\s*(?:/\*[^\n]*)?\n?\{', re.M)
KEYWORD = {"if", "for", "while", "switch", "return", "sizeof", "else", "do"}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def declared():
    """Every function name any src/sim header declares.

    Split on the semicolon rather than matching a whole declaration in
    one go: a parameter list written as `[^;{]*` crosses newlines, so a
    single match starting at a macro two lines above swallowed the
    declarations that followed and reported them missing."""
    out = set()
    for h in SIM.glob("*.h"):
        for chunk in strip_comments(h.read_text()).split(";"):
            m = re.search(r'([A-Za-z_]\w*)\s*\([^()]*\)\s*$', chunk)
            if m:
                out.add(m.group(1))
    return out


def main():
    known = declared()
    bad = []
    for c in sorted(SIM.glob("*.c")):
        text = strip_comments(c.read_text())
        for m in DEFN.finditer(text):
            qual, name = m.group("qual"), m.group("name")
            if name in KEYWORD or qual.split()[0] in KEYWORD:
                continue
            if "static" in qual.split() or name in known or name in ALLOWED:
                continue
            line = text[:m.start()].count("\n") + 1
            bad.append("%s:%d: %s() is global but no src/sim header declares it"
                       % (c.relative_to(ROOT), line, name))
    if bad:
        print("src/sim leaks names into the whole program:")
        for b in bad:
            print("   ", b)
        print("\nMake it static, or declare it in sim.h (the simulation's face to\n"
              "the program) or sim_int.h (what its own files ask of each other).")
        return 1
    print("layer_check: every function in src/sim is either static or declared in "
          "a header; sim_int.h carries %d of the meets"
          % sum(1 for _ in re.finditer(r'\b[A-Za-z_]\w*\s*\([^;{]*\)\s*;',
                                       strip_comments((SIM / "sim_int.h").read_text()))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
