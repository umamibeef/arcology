"""c_view.py -- draw the shape the renderer's C files (src/network/shape.c).

    python3 tools/roadfit/c_view.py [CITY]      # cities/test.arco when none

It runs `arcology CITY --net-check --net-dump` through tools/arcrun.py,
so a shape that fails its check is a fault and nothing is drawn.  It reads each road's planView
(its geometry pieces), its elevationProfile and each junction's boundary
from the dump, and writes out/roadfit/c.html: the same drawings as
run.py, of what the C filed.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chart  # noqa: E402
import land as ld  # noqa: E402  (it puts tools/ on the path, for arcrun)
from arcrun import world  # noqa: E402
from knobs import NET  # noqa: E402
from run import fig, section  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ld.ROOT, "out", "roadfit")
NETS = {"road": "road", "band": "band"}


#  Each highway junction's height, by its id, as the dump gives it.
HEIGHTS = {}


def read(city):
    """The dump: ends, topology roads, shapes and boundaries."""
    dump = os.path.join(OUT, "c_" + os.path.basename(city) + ".txt")
    world(os.path.join(ld.ROOT, "build", "arcology"), [city, "--mute", "--net-check", "--net-dump"], dump=dump)
    ends, roads, shapes, bounds = {}, {}, {}, {}
    cur = None
    for line in open(dump):
        f = line.split()
        if not f:
            continue
        if f[0] == "end":
            ends[f[1]] = (f[2], f[3], [tuple(map(int, t.split(","))) for t in f[4:]])
        elif f[0] == "road":
            roads[f[1]] = {"net": f[2], "e0": f[6], "e1": f[8], "cells": [tuple(map(int, t.split(","))) for t in f[9:]]}
        elif f[0] == "sroad":
            cur = shapes[f[1]] = {"length": float(f[3]), "geom": [], "elev": [], "map": []}
        elif f[0] == "geometry" and cur is not None:
            v = dict(zip(f[1::2], map(float, f[2::2])))
            cur["geom"].append(v)
        elif f[0] == "elevation" and cur is not None:
            v = dict(zip(f[1::2], map(float, f[2::2])))
            cur["elev"].append(v)
        elif f[0] == "map" and cur is not None:
            cur["map"] = list(map(float, f[1:]))
        elif f[0] == "sjunction":
            k = f.index("boundary")
            bounds[f[1]] = [tuple(map(float, t.split(","))) for t in f[k + 1:]]
            if "height" in f and f[f.index("height") + 1] != "nan":
                HEIGHTS[f[1]] = float(f[f.index("height") + 1])
    return ends, roads, shapes, bounds


def at(sh, s):
    """A point and heading of a shaped road at s."""
    g = sh["geom"][0]
    for q in sh["geom"]:
        if s >= q["s"]:
            g = q
    ds, c = s - g["s"], g["curvature"]
    if abs(c) < 1e-9:
        return g["x"] + ds * math.cos(g["hdg"]), g["y"] + ds * math.sin(g["hdg"]), g["hdg"]
    a = g["hdg"] + ds * c
    return g["x"] + (math.sin(a) - math.sin(g["hdg"])) / c, g["y"] - (math.cos(a) - math.cos(g["hdg"])) / c, a


def edges(sh, w, step=0.05):
    left, right = [], []
    n = max(2, int(sh["length"] / step) + 1)
    for k in range(n):
        x, y, h = at(sh, sh["length"] * k / (n - 1))
        left.append((x - math.sin(h) * w / 2, y + math.cos(h) * w / 2))
        right.append((x + math.sin(h) * w / 2, y - math.cos(h) * w / 2))
    return left, right


def height(sh, s):
    e = sh["elev"][0]
    for q in sh["elev"]:
        if s >= q["s"]:
            e = q
    return e["a"] + e["b"] * (s - e["s"])


def angles(sh):
    return sorted({round(math.degrees(g["hdg"]) % 180, 1) for g in sh["geom"] if abs(g["curvature"]) < 1e-9})


def main():
    city = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ld.ROOT, "cities", "test.arco")
    os.makedirs(OUT, exist_ok=True)
    land = ld.load(city)
    ends, roads, shapes, bounds = read(city)

    def plan_of(rid):
        r, sh = roads[rid], shapes[rid]
        size = NET[r["net"]]["unit"]
        L, R = edges(sh, NET[r["net"]]["width"])
        return r["cells"], size, (L, R, r["cells"])

    #  Roads that meet at a seam are one run: drawn whole, with the outline
    #  of the junction at each end.
    seam_roads = {}
    for rid, r in roads.items():
        for e in (r["e0"], r["e1"]):
            if ends.get(e, ("",))[0] == "seam":
                seam_roads.setdefault(e, []).append(rid)
    runs, seen = [], set()
    for rid in shapes:
        if rid in seen or rid not in roads:
            continue
        run, todo = [], [rid]
        while todo:
            q = todo.pop()
            if q in seen:
                continue
            seen.add(q)
            run.append(q)
            for e in (roads[q]["e0"], roads[q]["e1"]):
                todo += [o for o in seam_roads.get(e, []) if o in shapes]
        runs.append(run)
    kinds = {"shallow": [], "diagonal": [], "corners": []}
    for run in runs:
        r0 = roads[run[0]]
        cells = [c for q in run for c in roads[q]["cells"]]
        if r0["net"] != "road" or len(cells) < 3:
            continue
        a = sorted({x for q in run for x in angles(shapes[q])})
        curved = any(abs(g["curvature"]) > 1e-9 for q in run for g in shapes[q]["geom"])
        kind = ("shallow" if any(x % 45 > 0.5 and x % 45 < 44.5 for x in a) else
                "diagonal" if any(abs(x - 45) < 0.5 or abs(x - 135) < 0.5 for x in a) else
                "corners" if curved else None)
        if kind:
            kinds[kind].append((len(cells), run, a))
    above = []
    for kind, found in kinds.items():
        for _, run, a in sorted(found, key=lambda t: -t[0])[:4]:
            cells, rds, outlines = [], [], []
            for q in run:
                c, _, rd = plan_of(q)
                cells += c
                rds.append(rd)
            for q in run:
                for e in (roads[q]["e0"], roads[q]["e1"]):
                    if e in bounds:
                        cells += ends[e][2]
                        outlines.append(bounds[e])
            first = min(cells)
            above.append(fig(kind, f"{', '.join(f'{x:g}°' for x in a)} · {len(run)} road{'s' if len(run) > 1 else ''} "
                                   f"from cell {first[0]},{first[1]}", chart.plan(kind, cells, 1, rds, outlines=outlines)))

    juncs = []
    by_net = {}
    for eid, poly in bounds.items():
        kind, net, cells = ends[eid]
        arms = [rid for rid, r in roads.items() if eid in (r["e0"], r["e1"]) and rid in shapes and r["net"] == net]
        by_net.setdefault((net, len(arms)), []).append((eid, arms))
    for key in (("road", 3), ("road", 4), ("band", 3), ("band", 4), ("band", 2)):
        for eid, arms in by_net.get(key, [])[:3]:
            kind, net, jcells = ends[eid]
            size = NET[net]["unit"]
            cells, rds = list(jcells), []
            for rid in arms:
                c, _, rd = plan_of(rid)
                cells += c
                rds.append(rd)
            name = ("street " if net == "road" else "highway ") + {2: "two roads", 3: "T", 4: "four-way"}[key[1]]
            juncs.append(fig(name, f"at {jcells[0][0]},{jcells[0][1]}", chart.plan(name, cells, size, rds, bounds[eid])))

    hh = []
    for rid, sh in shapes.items():
        r = roads.get(rid)
        if not r or r["net"] != "band" or not ld.dry(land, r["cells"]):
            continue
        ground = []
        for k, (c, rr) in enumerate(r["cells"]):
            top = max(max(ld.corners(land, c + dc, rr + dr).values()) for dc in (0, 1) for dr in (0, 1))
            ground.append((k, top))
        if len(ground) < 3 or max(g for _, g in ground) == min(g for _, g in ground):
            continue
        m = sh["map"]
        s_of = lambda k: m[k] + (m[k + 1] - m[k]) * 0.5
        step = []
        for k, g in ground:
            step += [(m[k], g), (m[k + 1], g)]
        floor = [(s_of(k), g + 1) for k, g in ground]
        n = max(2, int(sh["length"] / 0.1))
        deck = [(sh["length"] * i / (n - 1), height(sh, sh["length"] * i / (n - 1))) for i in range(n)]
        cols = [(s, height(sh, s), g - 1) for s, g in floor]
        hh.append(fig(f"{len(r['cells'])} units", f"from {r['cells'][0][0]},{r['cells'][0][1]}",
                      chart.profile("highway", step, [(floor, "raw"), (deck, "deckline")], cols, px=80)))

    body = "".join([
        section("Streets from above, filed by the C", "Each road's planView, drawn half a tile wide.", above),
        section("Junctions, filed by the C", "Each junction's boundary, with its arms' planViews.", juncs),
        section("Highway heights, filed by the C",
                "Each dry highway road whose ground varies: its elevationProfile over its units' floor.", hh, True),
    ])
    page = open(os.path.join(HERE, "page.html")).read().replace("<!-- sections -->", body)
    page = page.replace("<h1>Road Fit Prototype</h1>", "<h1>Road Shape in C</h1>").replace(
        "<title>Road Fit Prototype</title>", "<title>Road Shape in C</title>")
    path = os.path.join(OUT, "c.html")
    open(path, "w").write(page)
    print(path)


if __name__ == "__main__":
    main()
