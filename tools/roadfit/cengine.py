"""cengine.py -- the renderer's C in place of the prototype's own code.

`python3 tools/roadfit/run.py --c` draws the same page from the C
(src/network/shape.c): each road's line is the planView the C filed for
the city, read from `arcology CITY --net-check --net-dump`, and every
height goes through the C's own solves by `arcology --shape-profile`.
Both runs go through tools/arcrun.py, so a fault stops the page.
"""
import math
import os
import tempfile

import land as ld
from arcrun import world
from knobs import NET

BINARY = os.path.join(ld.ROOT, "build", "arcology")


def _at(geom, s):
    g = geom[0]
    for q in geom:
        if s >= q["s"]:
            g = q
    ds, c = s - g["s"], g["curvature"]
    if abs(c) < 1e-9:
        return g["x"] + ds * math.cos(g["hdg"]), g["y"] + ds * math.sin(g["hdg"]), g["hdg"]
    a = g["hdg"] + ds * c
    return g["x"] + (math.sin(a) - math.sin(g["hdg"])) / c, g["y"] - (math.cos(a) - math.cos(g["hdg"])) / c, a


def install(city, out_dir):
    """Put the C in place of plan.fit, heights.street and heights.deck."""
    import heights
    import plan
    dump = os.path.join(out_dir, "c_" + os.path.basename(city) + ".txt")
    world(BINARY, [city, "--mute", "--net-check", "--net-dump"], dump=dump)
    cells_of, shapes, cur = {}, {}, None
    #  The prototype's own straights name a road's angles, so both pages
    #  pick and caption the same roads.  Everything drawn is the C's.
    proto_fit = plan.fit
    for line in open(dump):
        f = line.split()
        if not f:
            continue
        if f[0] == "road":
            cells_of[f[1]] = [tuple(map(int, t.split(","))) for t in f[9:]]
        elif f[0] == "sroad":
            cur = shapes[f[1]] = {"length": float(f[3]), "geom": []}
        elif f[0] == "geometry" and cur is not None:
            cur["geom"].append(dict(zip(f[1::2], map(float, f[2::2]))))

    def fit(net, chain):
        """The C's lines of the roads whose cells all lie in the chain, in
        the chain's order and direction, with the prototype's angles."""
        index = {c: i for i, c in enumerate(chain)}
        found = []
        for rid, cells in cells_of.items():
            if rid in shapes and cells and all(c in index for c in cells):
                a, b = index[cells[0]], index[cells[-1]]
                found.append((min(a, b), a > b, rid))
        path = []
        for _, rev, rid in sorted(found):
            sh = shapes[rid]
            n = max(2, int(sh["length"] / 0.05) + 1)
            pts = [_at(sh["geom"], sh["length"] * k / (n - 1)) for k in range(n)]
            if rev:
                pts = [(x, y, h + math.pi) for x, y, h in pts[::-1]]
            path += pts
        w = NET[net]["width"]
        return {"path": [(x, y) for x, y, _ in path],
                "left": [(x - math.sin(h) * w / 2, y + math.cos(h) * w / 2) for x, y, h in path],
                "right": [(x + math.sin(h) * w / 2, y - math.cos(h) * w / 2) for x, y, h in path],
                "angles": proto_fit(net, chain)["angles"], "out": 0.0}

    cache = {}

    def solve(kind, pts):
        key = (kind, tuple((round(x, 6), round(y, 6)) for x, y in pts))
        if key not in cache:
            with tempfile.TemporaryDirectory() as tmp:
                src, out = os.path.join(tmp, "in.txt"), os.path.join(tmp, "out.txt")
                with open(src, "w") as f:
                    f.write(kind + " " + " ".join(f"{x:.6f},{y:.6f}" for x, y in pts) + "\n")
                text = world(BINARY, ["--mute", "--shape-profile", src], dump=out)
            line = next(l for l in text.splitlines() if l.startswith("profile"))
            cache[key] = [list(map(float, t.split(","))) for t in line.split()[1:]]
        return cache[key]

    plan.fit = fit
    heights.street = lambda raw: solve("street", raw)
    heights.deck = lambda floor: solve("deck", floor)
