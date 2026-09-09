#!/usr/bin/env python3
"""Build docs/appendix-routines.rst: every address the documents cite,
with a name where one is known.

The names come from three places, in order of trust: the banner comments
in sim/*.c, the hand-curated table below for routines the prose names
only in passing, and finally nothing -- an address with no name is still
listed, because the appendix is meant to be the index of everything
cited, not only of what has been named.
"""
import re, glob, os, collections

HERE = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(HERE, "..", "docs")

#  Curated.  These are named in the prose but not in a form any regex can
#  lift, so they are written down once here.
CURATED = {
    "21EDE": ("simTick", "the 25-entry jump table; one phase per in-game day"),
    "263C8": ("budgetPass", "phase 0: the sixteen department records"),
    "20FC4": ("powerGridReset", "phase 1: the power flood fill"),
    "210A2": ("powerFlood", "spreads power through conductive tiles"),
    "2156E": ("waterGrid", "phase 20: the water flood fill"),
    "2182E": ("waterFlood", "spreads water through the pipe network"),
    "2317E": ("cityScanPass", "phase 2: pollution, land value, crime, density"),
    "231CE": ("pollutionRaw", "the raw pollution accumulation"),
    "233E8": ("pollutionBlur", "the blur that spreads it"),
    "23960": ("landValue", "land value from distance, terrain and pollution"),
    "23EE4": ("density", "population density into XPOP"),
    "23FAE": ("crime", "crime from density, land value and police cover"),
    "23D10": ("population", "XPOP from the building census"),
    "3170E": ("growthScan", "one of the sixteen growth slices"),
    "31B30": ("autoBuild", "the automatic placements at the end of a slice"),
    "3258A": ("pickBuilding", "chooses a building id within a group"),
    "31DDA": ("mapPopulation", "the population a tier implies"),
    "34D04": ("economyPass", "phase 21: national cycle, industry, migration"),
    "24232": ("trafficTotal", "phase 19: decays and totals XTRF"),
    "245E8": ("tripGenerate", "starts a trip from a zone tile"),
    "247EC": ("walkStep", "one step of a trip along the road network"),
    "2530E": ("tripAccept", "does this trip arrive somewhere useful"),
    "20F30": ("rngMod", "the game's own LFSR, tap polynomial 0x1BF5"),
    "20EE6": ("libRand", "THINK C's rand(), a third generator"),
    "293EC": ("rleEncode", "the save file's run-length codec"),
    "2D834": ("allocCityMaps", "allocates every tile layer"),
    "09E0A": ("stepThings", "the moving-object driver, once a frame"),
    "16B74": ("sweep32", "the surface sweep at 32 px a tile"),
    "16FF8": ("tile32", "one tile at 32 px"),
    "161DC": ("sweepUnder", "the underground sweep"),
    "17978": ("sweep16", "the surface sweep at 16 px"),
    "18E96": ("blit", "the plain blitter: writes every pixel it is given"),
    "19004": ("blitTraffic", "stencils traffic onto road surface only"),
    "19B76": ("blitShadow", "darkens the pixels it lands on"),
    "1226":  ("shapeDescriptors", "A5 pointer: eight bytes a shape"),
    "120C":  ("frameBuffer", "A5 pointer: the 8-bit destination"),
    "9750":  ("paletteStep", "the palette animation, every 12 ticks"),
    "17192": ("drawZone", "the zone tint under a bare zoned tile"),
    "5FAA":  ("demolishAndPlace", "clears a footprint and puts something there"),
    "3A000": ("burnTile", "what a fire, a flood or a quake leaves behind"),
    "763A":  ("footprintOrigin", "which tile of a multi-tile building this is"),
    "4110":  ("setTile", "writes one tile and its flags"),
}

#  Addresses of every instruction in CODE 2, so a citation that is really
#  a bit mask ($FC1F) or a constant can be told apart from a routine.
def code_addresses():
    lst = open(os.path.join(HERE, "..", "out", "CODE_2.ann.asm"), encoding="utf8").read()
    return set(int(x, 16) for x in re.findall(r"^([0-9A-F]{6}):", lst, re.M)), lst


def listing_symbols(lst):
    """the names the annotated listing already carries, resolved to the
    address in each `jsr <name>.l` instruction's own bytes"""
    out = {}
    for hexb, name in re.findall(
            r"^[0-9A-F]{6}:\s+4eb9([0-9a-f]{8})\s+jsr\s+([a-zA-Z_][A-Za-z0-9_]{2,})\.l",
            lst, re.M):
        out.setdefault("%05X" % int(hexb, 16), name)
    return out


def collect(lst):
    names = {}
    def put(a, n, d):
        a = a.upper().lstrip("$")
        if len(a) < 4 or a in names:
            return
        #  banner comments end with */ and may carry stray markup
        d = re.sub(r"\*+/?\s*$", "", d).strip().rstrip(".").replace("*", "")
        names[a] = (n.strip(), d)
    for p in sorted(glob.glob(os.path.join(HERE, "..", "src", "sim", "*.c"))):
        txt = open(p, encoding="utf8").read()
        for a, n, d in re.findall(
                r"\$([0-9A-Fa-f]{4,5})\s+([a-zA-Z][A-Za-z0-9_]{2,})\s*--\s*([^\n]*)", txt):
            put(a, n, d)
    for a, n in listing_symbols(lst).items():
        put(a, n, "named in the annotated listing")
    for a, (n, d) in CURATED.items():
        names.pop(a.upper().lstrip("$"), None)      # curated wins
        put(a, n, d)
    return names

def enclosing(addr, funcs, names):
    """Most cited addresses are not routine entry points -- they are a
    line inside one, quoted because that is where a rule lives.  Saying
    which routine contains it is far more use than `not yet named`."""
    import bisect
    i = bisect.bisect_right(funcs, addr) - 1
    if i < 0:
        return None
    home = "%05X" % funcs[i]
    if home == addr_key(addr) or home not in names:
        return None
    return home


def addr_key(a):
    return "%05X" % a


def main():
    code, lst = code_addresses()
    names = collect(lst)
    funcs = sorted(int(x, 16) for x in
                   open(os.path.join(HERE, "..", "out", "funcs.txt")))
    cited = collections.Counter()
    where = collections.defaultdict(set)
    for p in sorted(glob.glob(os.path.join(DOCS, "*.rst"))):
        stem = os.path.basename(p)[:-4]
        if stem.startswith("appendix"):
            continue
        txt = open(p, encoding="utf8").read()
        for a in re.findall(r"\$([0-9A-Fa-f]{4,6})\b", txt):
            a = a.upper()
            #  a citation that is not an instruction address is a bit mask
            #  or a constant, not a routine, and has no place here
            if int(a, 16) not in code:
                continue
            cited[a] += 1
            where[a].add(stem)

    rows = sorted(a for a in (set(cited) | set(names)) if int(a, 16) in code)
    out = [".. _appendix-routines:", "",
           "=" * 19, "Appendix: addresses", "=" * 19, "",
           ".. rubric:: Every address the documents cite, with a name where "
           "one is known", "",
           "Addresses are offsets into the CODE 2 resource of SimCity 2000 "
           "1.2; an ``A5-`` or ``A5+`` prefix elsewhere means a global rather "
           "than code. Every ``$xxxxx`` in these documents links here.", ""]
    named = 0
    for a in rows:
        n, d = names.get(a, ("", ""))
        if n:
            named += 1
        pages = ", ".join(":doc:`%s`" % w for w in sorted(where.get(a, ())))
        #  an explicit target before each entry, so :ref: can reach it.  A
        #  definition list keeps 263 of these readable where a table with
        #  per-row targets does not parse at all.
        out += [".. _rt-%s:" % a, ""]
        term = "``$%s``" % a
        if n:
            term += " -- ``%s``" % n
        out.append(term)
        if n:
            body = d or "--"
        else:
            home = enclosing(int(a, 16), funcs, names)
            import bisect as _b
            _i = _b.bisect_right(funcs, int(a, 16)) - 1
            home_any = ("%05X" % funcs[_i]) if _i >= 0 and funcs[_i] != int(a, 16) else None
            if home and home in names:
                body = ("a line inside :ref:`%s <rt-%s>`, which begins at "
                        "``$%s``" % (names[home][0], home, home))
            elif home_any is not None:
                body = ("a line inside the routine that begins at ``$%s``"
                        % home_any)
            else:
                body = "*quoted by the documents; not a routine entry point*"
        out.append("   " + body)
        if pages:
            out.append("")
            out.append("   Discussed in %s." % pages)
        out.append("")
    out += [".. note::", "",
            "   %d of the %d addresses cited have a name. The rest are listed "
            "so that every reference in these documents resolves; naming them "
            "is ongoing work." % (named, len(rows)), ""]
    with open(os.path.join(DOCS, "appendix-routines.rst"), "w", encoding="utf8") as fh:
        fh.write("\n".join(out) + "\n")
    print("appendix: %d addresses, %d named" % (len(rows), named))

if __name__ == "__main__":
    main()
