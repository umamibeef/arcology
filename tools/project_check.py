#!/usr/bin/env python3
"""project_check -- nothing may restate the projection's constants.

    python3 tools/project_check.py

The tile-to-pixel projection lives in src/render/project.h, which the C
callers include.  The shaders cannot include a C header, so they take the
origin and the scales as uniforms computed from it -- but they still
spell out the sine and cosine of the game's own 30 degree pitch, and so
did two places in C until this check went looking.

If those drift the query box stops landing where the pointer is: picking
uses one projection and drawing another.  Nothing else would notice.

What is checked:

  * the cosine, 0.8660254, is distinctive enough that ANY occurrence
    outside project.h is drift, in C or in GLSL;
  * the sine is 0.5, which is also every halving and midpoint in the
    codebase, so it counts only where it stands against a pitch: divided
    into, subtracted from, or multiplying sinf/cosf or a shader's sp/cp.
    A first version of this check flagged every 0.5 and produced two
    hundred false positives -- a check nobody can run is not a check;
  * each constant is counted separately, so coverage of one cannot fall
    to zero while the other keeps the test alive.

What it still cannot see: whether a shader reads the right uniform out of
proj[] -- swapping proj.z and proj.w moves the picture and no grep here
would know.  That needs the pixel comparison, which ctest runs separately.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "src" / "render" / "project.h"
SHADERS = ROOT / "src" / "render" / "shaders"
CODE = ROOT / "src"


def constants():
    text = HEADER.read_text()
    out = {}
    for name in ("ARC_PITCH_SIN0", "ARC_PITCH_COS0", "ARC_SPRITE_SKEW"):
        m = re.search(r"#define\s+%s\s+([0-9.]+)f?" % name, text)
        if not m:
            sys.exit("project_check: %s is not defined in %s" % (name, HEADER))
        out[name] = float(m.group(1))
    return out


#  A pitch, however it is spelled.  The identifiers must not match inside
#  a longer word: `disp - 0.5f` is not a pitch.
PITCH = r"(?<![A-Za-z0-9_])(?:sp|cp|sinf\([^()]*\)|cosf\([^()]*\))"


def patterns(consts):
    """Built FROM the header's values, not from a copy of them.  The first
    version of this check parsed the constants and then matched hard-coded
    literals, so changing project.h and leaving the shaders alone -- the
    exact drift it exists to catch -- passed."""
    def num(v):
        #  0.5 and .5 and 0.50 are the same number to a compiler.
        return r"0?\.%s0*f?" % re.escape(("%g" % v).split(".")[1])
    sin_n, cos_n = num(consts["ARC_PITCH_SIN0"]), num(consts["ARC_PITCH_COS0"])
    skew_n = num(consts["ARC_SPRITE_SKEW"])
    return (re.compile(r"(?:%s\s*[-/]\s*%s)|(?:%s\s*[-/]\s*%s)"
                       % (PITCH, sin_n, sin_n, PITCH)),
            re.compile(cos_n),
            re.compile(skew_n))


def strip_comments(text):
    """Block and line comments out, so a line is judged on its code.  The
    first version skipped any line starting with `*` to skip continuation
    lines of a block comment -- which is also how every out-parameter in
    camera.c starts (`*ysc = ...`), so the file this check exists to guard
    was the one place it could not see."""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def offenders(text, is_shader, sin_use, cos_use, skew_use):
    """Lines that restate one of the shared constants, with which one."""
    for n, line in enumerate(strip_comments(text).splitlines(), 1):
        if cos_use.search(line):
            yield n, "ARC_PITCH_COS0", line.strip(), is_shader
        if sin_use.search(line):
            yield n, "ARC_PITCH_SIN0", line.strip(), is_shader
        if skew_use.search(line):
            yield n, "ARC_SPRITE_SKEW", line.strip(), is_shader


def main():
    consts = constants()
    seen = {k: 0 for k in consts}
    bad = []

    files = [p for p in SHADERS.glob("*.vert")] + [p for p in SHADERS.glob("*.frag")]
    files += [p for p in CODE.rglob("*.c") if "vendor" not in p.parts]
    files += [p for p in CODE.rglob("*.cpp") if "vendor" not in p.parts]
    files += [p for p in CODE.rglob("*.h") if "vendor" not in p.parts and p != HEADER]

    sin_use, cos_use, skew_use = patterns(consts)
    for path in files:
        for n, name, line, is_shader in offenders(path.read_text(errors="ignore"),
                                                  path.suffix in (".vert", ".frag"),
                                                  sin_use, cos_use, skew_use):
            seen[name] += 1
            if not is_shader:
                bad.append("%s:%d: %s restates %s"
                           % (path.relative_to(ROOT), n, line[:64], name))
    missing = [k for k, v in seen.items() if v == 0]
    if missing:
        print("project_check: %s appears nowhere -- has the projection moved without "
              "this check?" % ", ".join(missing))
        return 1
    if bad:
        print("the projection's constants are restated outside project.h:")
        for b in bad:
            print("   ", b)
        print("\nUse ARC_PITCH_SIN0 / ARC_PITCH_COS0, or the helpers in project.h.")
        return 1
    print("project_check: %s; no C outside project.h restates either"
          % ", ".join("%s in %d shader lines" % (k, v) for k, v in sorted(seen.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
