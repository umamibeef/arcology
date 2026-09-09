"""The SCEN resource -- the part of a scenario that is NOT in the data fork.

Every one of the 18 shipped "cities" is really a scenario: Finder type
SCEN, creator SCDH, with a resource fork carrying

    SCEN 128   48 bytes, the rules
    TMPL 128   the ResEdit template that DESCRIBES those 48 bytes
    TEXT 128   the briefing, TEXT 129 the win text
    PICT 128   the title picture

The TMPL is the authority for the layout and is quoted below verbatim;
nothing here was guessed.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rezfork

#  The layout the CODE reads, from the goal checker at $0221A8.
#
#  This is NOT what TMPL 128 says.  The template calls +$1E a single
#  DLNG "Pollution Limit"; the code reads +$1E and +$20 as two WORDS and
#  compares them against MISC[18] and MISC[19], which are life
#  expectancy and education.  Everything after shifts by four bytes.
#
#  Every field from +$1E on is ZERO in all 18 shipped scenarios, so the
#  two readings agree on every file that exists and the data cannot
#  settle it.  The code is what the game does, so the code wins.
FIELDS = [
    ("disaster_type", "h"), ("disaster_x", "B"), ("disaster_y", "B"),
    ("time_limit_months", "h"),
    ("population_goal", "i"), ("residential_goal", "i"),
    ("commercial_goal", "i"), ("industrial_goal", "i"),
    ("cash_goal", "i"), ("land_value_goal", "i"),
    ("life_expectancy_goal", "h"), ("education_goal", "h"),
    ("pollution_limit", "i"), ("crime_limit", "i"), ("traffic_limit", "i"),
    ("build_item_one", "B"), ("build_item_two", "B"),
    ("build_tiles_one", "h"), ("build_tiles_two", "h"),
]
FMT = ">hBBh6ihh3iBBhh"   # exactly $34 = 52 bytes, what the loader copies

#  Names taken from the scenarios' own TEXT 128 briefings, not guessed.
#  My first pass at this table was wrong on nine of eleven entries --
#  the numbers are ground truth from SCEN, the meanings are not, and the
#  briefing is the only thing in the file that states them.
DISASTER = {
    0:  "none",            # Dullsville, Flint -- goal-only scenarios
    6:  "earthquake",      # San Francisco 1906
    8:  "UFO / monster",   # Atlanta "UFO Invasion", Hollywood "strange monster"
    9:  "nuclear",         # Barcelona "nuclear device", Manhattan "meltdown"
    10: "meltdown",        # Silicon Valley "new energy source... malfunctioned"
    11: "volcano",         # Portland "a previously unknown volcano erupts"
    12: "firestorm",       # Malibu, Oakland Hills, Paris -- all fires
    13: "riot",            # Washington "lawyers running amuck"
    14: "flood",           # Davenport "the mighty Mississippi"
    16: "hurricane",       # Charleston, Homestead, Tokyo -- all say hurricane
    #  15: Chicago.  Its briefing names no disaster at all (the goal is
    #  industry with low pollution), so the meaning is UNKNOWN rather
    #  than inferred.
}

def load(city_path):
    """Returns the scenario dict, or None for a plain city."""
    rp = os.path.join(city_path, "..namedfork", "rsrc")
    if not os.path.exists(rp): return None
    try: r = rezfork.parse(open(rp, "rb").read())
    except Exception: return None
    if "SCEN" not in r: return None
    #  The code's layout needs exactly $34 = 52 bytes, which is what the
    #  loader BlockMoves.  Only Barcelona's resource is that long.  The
    #  other seventeen are 48 bytes, so the game reads their two build
    #  requirements from past the end of the resource.  Pad with zeros,
    #  which is what those fields hold in practice.
    d = r["SCEN"][0].data[:52]
    if len(d) < 6: return None
    d = d + b"\0" * (52 - len(d))
    names = [n for n, t in FIELDS if t != "x"]
    v = dict(zip(names, struct.unpack(FMT, d)))
    v["disaster_name"] = DISASTER.get(v["disaster_type"], "?")
    for tid, key in ((128, "briefing"), (129, "win_text")):
        for t in r.get("TEXT", []):
            if t.id == tid:
                v[key] = t.data.decode("mac_roman", "replace")
    return v

if __name__ == "__main__":
    import glob
    for p in sorted(glob.glob(sys.argv[1] + "/*")):
        if not os.path.isfile(p): continue
        v = load(p)
        if not v: continue
        goals = ", ".join("%s=%d" % (k.replace("_goal", ""), v[k])
                          for k in ("population_goal", "residential_goal",
                                    "commercial_goal", "industrial_goal",
                                    "cash_goal", "land_value_goal",
                                    "life_expectancy_goal", "education_goal")
                          if v.get(k))
        print("%-17s %-11s at (%3d,%3d)  %3d months   %s"
              % (os.path.basename(p), v["disaster_name"],
                 v["disaster_x"], v["disaster_y"], v["time_limit_months"], goals))
