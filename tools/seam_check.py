#!/usr/bin/env python3
"""seam_check -- the renderer may read the simulation, never write it.

    python3 tools/seam_check.py

city.h says it "deliberately does NOT include the simulation's sc2k.h",
and adapt.h calls itself "the one file that includes both sides": the
adapter copies what the renderer reads into an RCity, and the renderer
cannot reach the City it came from.

That is the design boundary of this codebase and it was enforced by
nothing.  It broke twice in one day -- a save generator under src/render
that included sc2k.h to author a City, and then app_int.h, which included
adapt.h and so handed every field of City to camera.c, paths.c and
prefs.c while claiming in its own comment that it did not.

The second one is why this follows includes instead of grepping each file
in isolation: a direct include is the easy case, and it was not the one
that happened.  Every simulation header counts, not just sc2k.h, and the
allow-list is keyed on path so a file cannot be exempted by being named
after one that is.

It also looks for the simulation's own names, because no include is
needed to break the seam: an `extern int sim_tick(struct City *);` in a
renderer file links perfectly well and mentions no header at all.

What this does NOT prove is the headline "reads, never writes".  app.c is
on the allow-list and does write -- it runs the clock, it demolishes
tiles.  What is enforced is narrower and worth stating plainly: nothing
outside adapt.c, adapt.h and app.c can name the simulation at all.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
SIM = SRC / "sim"
#  The files that may see both sides, by path from src/.
ALLOWED = {"render/adapt.h", "render/adapt.c", "app/app.c"}
SUFFIX = (".c", ".h", ".cpp", ".cc", ".hpp", ".mm")
INCLUDE = re.compile(r'^\s*#\s*include\s*["<]([^">]+)[">]')


def sim_headers():
    return {p.name for p in SIM.glob("*.h")}


def sim_symbols():
    """Every function the simulation's headers declare, by name.  These are
    what a renderer file would have to name to reach across without an
    include."""
    out = set()
    for h in SIM.glob("*.h"):
        for m in re.finditer(r"^\s*(?:extern\s+)?[A-Za-z_][\w \t*]*?\b([a-z_][a-z0-9_]{4,})\s*\(",
                             h.read_text(errors="ignore"), re.M):
            out.add(m.group(1))
    #  Names the renderer legitimately has of its own.
    return out - {"city_load", "city_free", "city_save"}


def includes_of(path):
    out = []
    try:
        text = path.read_text(errors="ignore")
    except OSError:
        return out
    for n, line in enumerate(text.splitlines(), 1):
        m = INCLUDE.match(line)
        if m:
            out.append((n, pathlib.PurePosixPath(m.group(1)).name))
    return out


def main():
    sims = sim_headers()
    #  Every header an include could resolve to, by name, so the route can
    #  be followed.  Generated headers count: they are on every renderer
    #  translation unit's include path, and a generated one that pulled in
    #  a simulation header would be a hole nothing else would see.
    byname = {}
    roots = [SRC] + [d for d in (ROOT / "build" / "generated",
                                 ROOT / "build" / "src") if d.is_dir()]
    for root in roots:
        for p in root.rglob("*"):
            if p.suffix in (".h", ".hpp") and p.is_file():
                byname.setdefault(p.name, p)

    def reaches(name, seen):
        """Does header `name` reach a simulation header, and by what route?"""
        if name in sims:
            return [name]
        if name in seen or name not in byname:
            return None
        seen.add(name)
        for _, inc in includes_of(byname[name]):
            route = reaches(inc, seen)
            if route:
                return [name] + route
        return None

    syms = sim_symbols()
    bad = []
    for path in sorted(SRC.rglob("*")):
        if not path.is_file() or path.suffix not in SUFFIX:
            continue
        rel = path.relative_to(SRC).as_posix()
        if rel.startswith(("sim/", "vendor/")) or rel in ALLOWED:
            continue
        for n, inc in includes_of(path):
            route = reaches(inc, set())
            if route:
                how = " -> ".join(route)
                bad.append("%s:%d: #include \"%s\"%s" % (rel, n, inc, "" if len(route) == 1 else "   (%s)" % how))
        #  And the simulation's names, include or no include.
        for n, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
            if line.lstrip().startswith(("*", "//", "/*")):
                continue
            for m in re.finditer(r"\b([a-z_][a-z0-9_]{4,})\s*\(", line):
                if m.group(1) in syms:
                    bad.append("%s:%d: names the simulation's %s()" % (rel, n, m.group(1)))
    if bad:
        print("the renderer's side of the seam can see the simulation:")
        for b in bad:
            print("   ", b)
        print("\nOnly %s may.  Code that authors a City belongs in src/sim."
              % ", ".join(sorted(ALLOWED)))
        return 1
    print("seam_check: %s see the simulation, as intended; %d other files name "
          "nothing of it, through any header they include or by declaring it "
          "themselves" %
          (", ".join(sorted(ALLOWED)),
           sum(1 for p in SRC.rglob("*")
               if p.is_file() and p.suffix in SUFFIX
               and not p.relative_to(SRC).as_posix().startswith(("sim/", "vendor/")))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
