#!/usr/bin/env python3
"""lua_check.py -- the scripts drive the build, and nothing shadows them.

Four things have to hold at once, and each is a different way for the
layer to be wrong:

  * taking a rule AWAY takes the thing away.  A script that clears
    arc.rules.control must leave every junction uncontrolled.  If the C
    still carried a ladder of its own behind the script, control would
    survive, and the two copies would drift apart the moment either
    changed.  This is the one that matters most.
  * a CONSTANT changes the build.  A script that only deepens the
    crossing band must move the counts; a proxy table that swallows the
    write shows up here and nowhere else.
  * a RULE changes the build.  A script that signals every leg must move
    the counts too, or the hooks are dead.
  * the build with that rule still passes its own checks.  A rule may
    change what is drawn; it may not make the mesh unsound.

    tools/lua_check.py [--binary build/arcology] [--city atlanta]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

NONE = "arc.rules.control = nil\n"

CONST = "arc.geo.cross_deep = 0.36\n"

RULES = """
arc.rules.control = function (col, row, arms, busy)
    local c = {0, 0, 0, 0}
    for e = 1, 4 do if arms[e] then c[e] = 2 end end
    return c
end
arc.rules.crossing = function (m)
    if m.control == 0 then return 0 end
    return math.min(m.want, m.straight, 0.35 * m.room)
end
"""


def run(binary, city, script=None, extra=()):
    """A mesh check, with a script or without.  Answers (exit code, the
    lines the checks reported)."""
    path = os.path.join(ROOT, "cities", city + ".sc2")
    cmd = [binary, path, "--mute", "--mesh-check"]
    if script:
        cmd += ["--lua", script]
    cmd += list(extra)
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
    return p.returncode, p.stdout


def counts(out):
    """The lines that say what was built, keyed by their first word."""
    keep = {}
    for line in out.splitlines():
        m = re.match(r"^(crossings|walkways|outlines|overlap|lanes)\b(.*)$", line)
        if m:
            keep.setdefault(m.group(1), []).append(m.group(2))
    return keep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "build", "arcology"))
    ap.add_argument("--city", default="atlanta")
    a = ap.parse_args()
    if not os.path.exists(a.binary):
        print("lua: no binary at", a.binary)
        return 1
    if not os.path.exists(os.path.join(ROOT, "cities", a.city + ".sc2")):
        print("lua: no city", a.city)
        return 0

    bare_rc, bare = run(a.binary, a.city)
    if bare_rc != 0:
        print("lua: the city does not check out without a script")
        return 1

    #  A script named on the line is read AFTER the shipped folder, so
    #  one that clears a rule is the last word on it.
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
        f.write(NONE)
        none = f.name
    try:
        none_rc, none_out = run(a.binary, a.city, none)
    finally:
        os.unlink(none)
    if none_rc != 0:
        print("lua: clearing a rule made the build fail")
        return 1
    line = " ".join(counts(none_out).get("crossings", []))
    m = re.search(r"(\d+) junction mouths; (\d+) uncontrolled", line)
    if not m or m.group(1) != m.group(2):
        print("lua: clearing arc.rules.control left junctions controlled --")
        print("     something behind the script is still deciding:", line.strip())
        return 1

    for what, src, why in (("a constant", CONST, "the write does not reach the pipeline"),
                           ("a rule", RULES, "the hooks are not reached")):
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
            f.write(src)
            path = f.name
        try:
            rc, out = run(a.binary, a.city, path)
        finally:
            os.unlink(path)
        if rc != 0:
            print("lua:", what, "made the mesh unsound")
            return 1
        if counts(out).get("crossings") == counts(bare).get("crossings"):
            print("lua:", what, "changed nothing;", why)
            return 1

    p = subprocess.run([a.binary, os.path.join(ROOT, "cities", a.city + ".sc2"), "--mute",
                        "--lua-eval", "arc.mesh.faults()"],
                       capture_output=True, text=True, cwd=ROOT)
    if p.returncode != 0 or "outlines" not in p.stdout:
        print("lua: --lua-eval answered nothing:", p.stdout.strip()[:200])
        return 1

    print("lua: taking a rule away takes the thing away, a constant and a rule "
          "each change the build and it still checks out, and --lua-eval answers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
