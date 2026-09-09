#!/usr/bin/env python3
"""src/render/mesh/materials.def -> the C constants and the material table.

A material is a number carried in a vertex colour.  The mesh writes it,
the shaders read it, and a script names it.  Those were three hand-written
copies of one list; this makes them one.  Run it after editing the .def:

    python3 tools/gen_materials.py

It writes src/render/generated/materials.h and materials.c, both checked
in, so a plain build needs no Python.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
DEF = os.path.join(ROOT, "src", "render", "mesh", "materials.def")
OUT = os.path.join(ROOT, "src", "render", "generated")

#  Where a script's own materials start.  Far enough above the built-in
#  numbers that a new one here can never collide with a shader's range.
SCRIPT_BASE = 32.0
SCRIPT_MAX = 32


def write(path, text, check):
    """Write it, or under --check say whether it is already what it should
    be: a generated file that is edited or left stale is a list that has
    drifted from its one source again."""
    if check:
        try:
            have = open(path).read()
        except OSError:
            have = None
        if have != text:
            print("%s is not what materials.def says; run tools/gen_materials.py"
                  % os.path.relpath(path, ROOT), file=sys.stderr)
            return False
        return True
    open(path, "w").write(text)
    return True


def flt(v):
    """A C float literal: %g drops the point, and `0f` is not a number."""
    t = "%g" % v
    return t + (".0f" if "." not in t else "f")


def read():
    out = []
    for line in open(DEF):
        line = line.split("#")[0].strip()
        if not line:
            continue
        m = re.match(r"([0-9.]+)\s+([A-Z_0-9]+)\s+([a-z_0-9]+)\s+(.*)$", line)
        if not m:
            print("materials.def: cannot read %r" % line, file=sys.stderr)
            sys.exit(1)
        out.append((float(m.group(1)), m.group(2), m.group(3), m.group(4)))
    return out


def main():
    check = "--check" in sys.argv
    mats = read()
    seen = {}
    for value, name, lua, what in mats:
        if value >= SCRIPT_BASE:
            print("materials.def: %s is at %g, which a script's own materials "
                  "start at" % (name, SCRIPT_BASE), file=sys.stderr)
            return 1
        if value in seen:
            print("materials.def: %s and %s are both %g"
                  % (seen[value], name, value), file=sys.stderr)
            return 1
        seen[value] = name
    os.makedirs(OUT, exist_ok=True)

    wide = max(len(n) for _, n, _, _ in mats)
    h = ['/*  GENERATED from src/render/mesh/materials.def -- do not edit.',
         ' *',
         " *  A material is the number a vertex colour's third component",
         ' *  carries.  The mesh writes it, the shaders read it by range, and',
         ' *  a script names it through arc.mat. */',
         '#ifndef ARC_MATERIALS_H',
         '#define ARC_MATERIALS_H',
         '']
    for value, name, lua, what in mats:
        h.append("#define MAT_%-*s %-6s /* %s */" % (wide, name, flt(value), what))
    h += ['',
          "/*  A script's own materials are numbered from here, one per",
          " *  arc.mat.define, and shaded from their parameters rather than",
          ' *  from a branch of their own. */',
          "#define MAT_SCRIPT_BASE %s" % flt(SCRIPT_BASE),
          "#define MAT_SCRIPT_MAX  %d" % SCRIPT_MAX,
          '',
          '/*  The built-in materials by name, for the script bridge. */',
          'typedef struct',
          '{',
          '    const char *name;',
          '    float       value;',
          '} RMaterial;',
          'extern const RMaterial r_materials[];',
          'extern const int       r_materials_n;',
          '',
          '#endif']
    if not write(os.path.join(OUT, "materials.h"), "\n".join(h) + "\n", check):
        return 1

    c = ['/*  GENERATED from src/render/mesh/materials.def -- do not edit. */',
         '#include "materials.h"',
         '',
         'const RMaterial r_materials[] = {']
    for value, name, lua, what in mats:
        c.append('    {"%s", MAT_%s},' % (lua, name))
    c += ['};',
          'const int r_materials_n = (int)(sizeof r_materials / sizeof r_materials[0]);']
    if not write(os.path.join(OUT, "materials.c"), "\n".join(c) + "\n", check):
        return 1

    print("materials: %d built in, a script's own from %g%s"
          % (len(mats), SCRIPT_BASE, " (checked)" if check else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
