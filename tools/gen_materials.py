#!/usr/bin/env python3
"""src/render/mesh/materials.def -> the C constants and the material table.

A material is a number carried in a vertex colour.  The mesh writes it,
the shaders read it, and a report names it.  Three readers need one list,
and this writes each of them from the one .def.  Run it after editing the .def:

    python3 tools/gen_materials.py

It writes src/render/generated/materials.h and materials.c and
src/render/gpu/shaders/materials.glsl, all checked in, so a plain build
needs no Python.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
DEF = os.path.join(ROOT, "src", "render", "mesh", "materials.def")
OUT = os.path.join(ROOT, "src", "render", "generated")

#  Where the materials named at run time start.  Far enough above the built-in
#  numbers that a new one here can never collide with a shader's range.
RUNTIME_BASE = 32.0
RUNTIME_MAX = 32
#  The most tones the layers of a cut are dealt from (STRATA in
#  src/render/gpu/frame.c).
STRATA_MAX = 8


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


def glsl_flt(v):
    """A GLSL float literal.  A material is a float everywhere it is read,
    and `0` beside a float in a macro is a type the compiler may refuse."""
    t = "%g" % v
    return t + (".0" if "." not in t else "")


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
    for value, name, label, what in mats:
        if value >= RUNTIME_BASE:
            print("materials.def: %s is at %g, which the run-time materials "
                  "start at" % (name, RUNTIME_BASE), file=sys.stderr)
            return 1
        if value in seen:
            print("materials.def: %s and %s are both %g"
                  % (seen[value], name, value), file=sys.stderr)
            return 1
        seen[value] = name
    os.makedirs(OUT, exist_ok=True)

    wide = max(len(n) for _, n, _, _ in mats)
    h = ['/*  GENERATED from src/render/mesh/materials.def.  Do not edit.',
         ' *',
         " *  A material is the number a vertex colour's third component",
         ' *  carries.  The mesh writes it, the shaders read it by range, and',
         ' *  a report names it by the name in r_materials. */',
         '#ifndef ARC_MATERIALS_H',
         '#define ARC_MATERIALS_H',
         '']
    for value, name, label, what in mats:
        h.append("#define MAT_%-*s %-6s /* %s */" % (wide, name, flt(value), what))
    h += ['',
          '/*  A material is a NUMBER, and a variant is shaded from the whole',
          ' *  number beside it.  So a test on a material is a test on the half',
          ' *  step around it.  MAT_IS names one material with its variants.',
          ' *  MAT_IN names a run of them, from the first to the last.',
          ' *  MAT_ABOVE, MAT_FROM and MAT_UPTO each name everything on one',
          ' *  side of one. */',
          '#define MAT_IS(m, M)    ((m) > (M) -0.5f && (m) < (M) + 0.5f)',
          '#define MAT_IN(m, A, B) ((m) > (A) -0.5f && (m) < (B) + 0.5f)',
          '#define MAT_ABOVE(m, M) ((m) > (M) + 0.5f)',
          '#define MAT_FROM(m, M)  ((m) > (M) -0.5f)',
          '#define MAT_UPTO(m, M)  ((m) < (M) + 0.5f)',
          '',
          "/*  The materials named at run time are numbered from here, one",
          " *  per material_define.  Each is shaded from its parameters",
          ' *  rather than from a branch of its own. */',
          "#define MAT_RUNTIME_BASE %s" % flt(RUNTIME_BASE),
          "#define MAT_RUNTIME_MAX  %d" % RUNTIME_MAX,
          '',
          "/*  The most tones the layers of a cut are dealt from (STRATA",
          ' *  in gpu/frame.c). */',
          "#define MAT_STRATA_MAX  %d" % STRATA_MAX,
          '',
          '/*  How many built-in materials there are, for a table with a',
          ' *  slot for each. */',
          "#define MAT_COUNT %d" % len(mats),
          '',
          '/*  The built-in materials by name, for a report to print. */',
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

    g = ['//  GENERATED from src/render/mesh/materials.def.  Do not edit.',
         '//',
         "//  The shader reads a material by RANGE, so it needs the same numbers",
         '//  and the same membership tests the C does.  gen_shaders.py splices',
         '//  this file in where a shader asks for it.',
         '']
    for value, name, label, what in mats:
        g.append("#define MAT_%-*s %-6s // %s" % (wide, name, glsl_flt(value), what))
    g += ['',
          '//  A material is a NUMBER, and a variant is shaded from the whole',
          '//  number beside it.  So a test on a material is a test on the half',
          '//  step around it.',
          '#define MAT_IS(m, M)    ((m) > (M) - 0.5 && (m) < (M) + 0.5)',
          '#define MAT_IN(m, A, B) ((m) > (A) - 0.5 && (m) < (B) + 0.5)',
          '#define MAT_ABOVE(m, M) ((m) > (M) + 0.5)',
          '#define MAT_FROM(m, M)  ((m) > (M) - 0.5)',
          '#define MAT_UPTO(m, M)  ((m) < (M) + 0.5)',
          '',
          "//  The materials named at run time are numbered from here.",
          "#define MAT_RUNTIME_BASE %s" % glsl_flt(RUNTIME_BASE),
          "#define MAT_RUNTIME_MAX  %d" % RUNTIME_MAX,
          '',
          "//  The most tones the layers of a cut are dealt from.",
          "#define MAT_STRATA_MAX  %d" % STRATA_MAX]
    if not write(os.path.join(ROOT, "src", "render", "gpu", "shaders", "materials.glsl"),
                 "\n".join(g) + "\n", check):
        return 1

    c = ['/*  GENERATED from src/render/mesh/materials.def.  Do not edit. */',
         '#include "materials.h"',
         '',
         'const RMaterial r_materials[] = {']
    for value, name, label, what in mats:
        c.append('    {"%s", MAT_%s},' % (label, name))
    c += ['};',
          'const int r_materials_n = (int)(sizeof r_materials / sizeof r_materials[0]);']
    if not write(os.path.join(OUT, "materials.c"), "\n".join(c) + "\n", check):
        return 1

    print("materials: %d built in, the run-time ones from %g%s"
          % (len(mats), RUNTIME_BASE, " (checked)" if check else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
