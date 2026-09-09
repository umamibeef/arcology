"""Which map layers does each walk mode actually READ?

gen_walk.py probes the $247EC dispatch with a value in ONE layer and
zero in the others.  That silently loses any transition that depends on
a second layer -- mode 13 surfaces at a subway station on XBLD == $E9,
which the XUND-only probe could never see, and the generated table
recorded "keep tunnelling, cost 1" instead.

This asks the question directly: for each mode, sweep each layer on its
own and see whether the outcome varies.  A mode that responds to more
than one layer CANNOT be represented by a [mode][one-layer] table, and
the generator has no business emitting one for it.
"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_walk

LAYERS = {0x1FC2: "ALTM", 0x1BBA: "XBIT", 0x21C2: "XBLD",
          0x23C2: "XZON", 0x25C2: "XTER", 0x27C2: "XUND"}

if __name__ == "__main__":
    print("mode | layers whose value changes the outcome")
    bad = []
    for m in range(gen_walk.N_MODE):
        responds = []
        for off, name in sorted(LAYERS.items()):
            seen = set()
            #  ALTM is a word: sweep the high bits too, or the
            #  tunnel field at bits 10..14 is never set.
            vals = range(0, 65536, 137) if off == 0x1FC2 else range(256)
            for v in vals:
                try: seen.add(gen_walk.probe(m, v, layer=off))
                except Exception: pass
            if len(seen) > 1: responds.append("%s(%d outcomes)" % (name, len(seen)))
        print("%4d | %s" % (m, ", ".join(responds) if responds else "none"))
        if len(responds) > 1: bad.append(m)
    print()
    if bad:
        print("MULTI-LAYER modes, not representable as [mode][layer]: %s" % bad)
    else:
        print("every mode depends on at most one layer")
