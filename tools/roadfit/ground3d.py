"""ground3d.py -- the ground beside the streets, in 3D.

    python3 tools/roadfit/ground3d.py [CITY]      # cities/test.arco when none

It writes out/roadfit/ground3d.html: a few places, each with its land as
tiles, its streets, the ground graded to meet them and the walls where a
bank would be too steep (ground.py's rule).  Every place is of the test
world, with the streets the C filed.  A place's frame holds every road
and junction that reaches into it whole, and a place is shown only when
every one of them is drawn.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c_view as cv  # noqa: E402
import ground as gd  # noqa: E402
import land as ld  # noqa: E402
from knobs import LEVEL_H, NET  # noqa: E402

OUT = os.path.join(ld.ROOT, "out", "roadfit")
W = NET["road"]["width"]
STEP = 0.125  # the spacing of the ground's points, in tiles


class Road:
    """A street as samples along it: (x, y, heading, height)."""

    def __init__(self, samples):
        self.p = samples

    def near(self, x, y):
        best = None
        for i, (px, py, h, z) in enumerate(self.p):
            d = math.hypot(x - px, y - py)
            if best is None or d < best[0]:
                best = (d, i)
        return best


def c_road(sh):
    n = max(2, int(sh["length"] / 0.05))
    out = []
    for k in range(n):
        s = sh["length"] * k / (n - 1)
        x, y, h = cv.at(sh, s)
        out.append((x, y, h, cv.height(sh, s)))
    return Road(out)


def side_kind(land, built, road, i, side):
    """'graded' or 'wall' on one side of a road at its sample i."""
    x, y, h, z = road.p[i]
    nx, ny = -math.sin(h) * side, math.cos(h) * side
    fx, fy = x + nx * (W / 2 + gd.GROUND_REACH), y + ny * (W / 2 + gd.GROUND_REACH)
    ex, ey = x + nx * W / 2, y + ny * W / 2
    far, edge = gd.land_at(land, fx, fy), gd.land_at(land, ex, ey)
    steep = abs(far - z) * LEVEL_H / gd.GROUND_REACH > math.tan(math.radians(gd.BANK))
    blocked = (int(math.floor(fx)), int(math.floor(fy))) in built
    return "wall" if (steep or blocked) and abs(edge - z) > 0.02 else "graded"


def inside(poly, x, y):
    """Whether a point lies inside a convex outline."""
    sign = 0
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        c = (bx - ax) * (y - ay) - (by - ay) * (x - ax)
        if abs(c) < 1e-12:
            continue
        if sign == 0:
            sign = 1 if c > 0 else -1
        elif (c > 0) != (sign > 0):
            return False
    return True


def place(name, land, built, roads, box, junctions=(), decks=()):
    """One place: tiles of drawn ground, the roads and the walls, in tiles,
    heights in tiles."""
    c0, r0, c1, r1 = box
    kinds = {}
    tiles = []
    per = int(round(1 / STEP))
    for r in range(r0, r1):
        for c in range(c0, c1):
            grid, mark = [], []
            for j in range(per + 1):
                row, mrow = [], []
                for i in range(per + 1):
                    x, y = c + i * STEP, r + j * STEP
                    g = ld.plane_at(land, c, r, x, y)
                    zz = g
                    under = next((jz for poly, jz in junctions if inside(poly, x, y)), None)
                    if under is not None:
                        #  Under a road or a junction the ground sits a tenth of a
                        #  level below it, so the two never draw at one depth.
                        row.append(round((under - 0.1) * LEVEL_H, 4))
                        mrow.append(0)
                        continue
                    graded = 0
                    for k, road in enumerate(roads):
                        d, n = road.near(x, y)
                        if d > W / 2 + gd.GROUND_REACH:
                            continue
                        px, py, h, z = road.p[n]
                        sd = 1 if (-math.sin(h) * (x - px) + math.cos(h) * (y - py)) >= 0 else -1
                        key = (k, n, sd)
                        if key not in kinds:
                            kinds[key] = side_kind(land, built, road, n, sd)
                        if d <= W / 2:
                            zz = z - 0.1
                        elif kinds[key] == "graded":
                            f = (d - W / 2) / gd.GROUND_REACH
                            f = f * f * (3 - 2 * f)
                            zz = z + (g - z) * f
                            graded = int(abs(zz - g) > 0.01)
                        break
                    row.append(round(zz * LEVEL_H, 4))
                    mrow.append(graded)
                grid.append(row)
                mark.append(mrow)
            k = ld.corners(land, c, r)
            z0 = [round(k[q] * LEVEL_H, 4) for q in ((c, r), (c + 1, r), (c, r + 1), (c + 1, r + 1))]
            tiles.append({"c": c, "r": r, "z": grid, "z0": z0, "g": mark})
    out_roads, walls = [], []
    for k, road in enumerate(roads):
        ribbon = []
        for n, (x, y, h, z) in enumerate(road.p):
            if not (c0 - 1 <= x <= c1 + 1 and r0 - 1 <= y <= r1 + 1):
                continue
            nx, ny = -math.sin(h), math.cos(h)
            ribbon.append([round(x + nx * W / 2, 4), round(y + ny * W / 2, 4), round(x - nx * W / 2, 4),
                           round(y - ny * W / 2, 4), round(z * LEVEL_H, 4)])
        out_roads.append(ribbon)
        #  Each side's walls as runs of samples next to each other.
        for sd in (-1, 1):
            run = []
            for n, (x, y, h, z) in enumerate(road.p):
                shown = c0 - 1 <= x <= c1 + 1 and r0 - 1 <= y <= r1 + 1
                if shown and side_kind(land, built, road, n, sd) == "wall":
                    nx, ny = -math.sin(h), math.cos(h)
                    ex, ey = x + nx * sd * W / 2, y + ny * sd * W / 2
                    run.append([round(ex, 4), round(ey, 4), round(z * LEVEL_H, 4), round(gd.land_at(land, ex, ey) * LEVEL_H, 4)])
                elif run:
                    walls.append(run)
                    run = []
            if run:
                walls.append(run)
    out_decks = []
    for sh in decks:
        rd, wd = c_road(sh), NET["band"]["width"]
        ribbon = [[round(x - math.sin(h) * wd / 2, 4), round(y + math.cos(h) * wd / 2, 4),
                   round(x + math.sin(h) * wd / 2, 4), round(y - math.cos(h) * wd / 2, 4), round(z * LEVEL_H, 4)]
                  for x, y, h, z in rd.p]
        m = sh["map"]
        cols = []
        for k in range(len(m) - 1):
            s0 = (m[k] + m[k + 1]) / 2
            x, y, _ = cv.at(sh, s0)
            cols.append([round(x, 4), round(y, 4), round(cv.height(sh, s0) * LEVEL_H, 4), round(gd.land_at(land, x, y) * LEVEL_H, 4)])
        out_decks.append({"ribbon": ribbon, "cols": cols})
    juncs = [{"poly": [[round(x, 4), round(y, 4)] for x, y in poly], "z": round(jz * LEVEL_H, 4)}
             for poly, jz in junctions]
    return {"name": name, "box": box, "tiles": tiles, "roads": out_roads, "walls": walls, "junctions": juncs, "decks": out_decks}


def main():
    city = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ld.ROOT, "cities", "test.arco")
    land = ld.load(city)
    built = gd.buildings(city)
    ends, roads, shapes, bounds = cv.read(city)
    streets = [(rid, c_road(sh)) for rid, sh in shapes.items()
               if roads.get(rid) and roads[rid]["net"] == "road" and ld.dry(land, roads[rid]["cells"])]

    def junctions_in(box):
        """Each junction's outline, at the mean height of the road ends that
        meet it: the C files no junction height yet.  A street junction is
        answered, and a highway junction kept in deck_junctions."""
        c0, r0, c1, r1 = box
        out = []
        for eid, poly in bounds.items():
            net = ends.get(eid, ("", ""))[1]
            if net not in ("road", "band") or len(poly) < 3:
                continue
            if not any(c0 - 1 <= x <= c1 + 1 and r0 - 1 <= y <= r1 + 1 for x, y in poly):
                continue
            zs = [cv.height(shapes[rid], 0.0 if r["e0"] == eid else shapes[rid]["length"])
                  for rid, r in roads.items() if rid in shapes and eid in (r["e0"], r["e1"]) and r["net"] == net]
            if zs and net == "road":
                out.append((poly, sum(zs) / len(zs)))
            elif zs and eid in cv.HEIGHTS:
                #  A highway junction: its own height at its center, and at
                #  each corner of its outline the height of the deck edge
                #  there, so every arm meets it with no step.
                edges = []
                for rid, r in roads.items():
                    if rid in shapes and eid in (r["e0"], r["e1"]) and r["net"] == net:
                        sh = shapes[rid]
                        at = 0.0 if r["e0"] == eid else sh["length"]
                        x, y, h = cv.at(sh, at)
                        wd = NET["band"]["width"]
                        for sd in (-1, 1):
                            edges.append((x - math.sin(h) * sd * wd / 2, y + math.cos(h) * sd * wd / 2, cv.height(sh, at)))
                corner_z = [min(edges, key=lambda e: math.hypot(e[0] - px, e[1] - py))[2] for px, py in poly]
                deck_junctions.append((poly, cv.HEIGHTS[eid], corner_z))
        return out

    def decks_in(box):
        c0, r0, c1, r1 = box
        return [sh for rid, sh in shapes.items() if roads.get(rid) and roads[rid]["net"] == "band"
                and any(c0 - 1 <= c <= c1 + 1 and r0 - 1 <= r <= r1 + 1 for c, r in roads[rid]["cells"])]

    def near(box):
        c0, r0, c1, r1 = box
        return [rd for _, rd in streets if any(c0 - 1 <= x <= c1 + 1 and r0 - 1 <= y <= r1 + 1 for x, y, _, _ in rd.p)]

    #  Each place: its name, a first frame, and whether its frame keeps to
    #  that first frame rather than growing to hold its roads whole (a
    #  street beside a highway that is not part of the place).
    SPECS = (("a street cut into flat land", (258, 63, 270, 73), False),
             ("climbing streets", (192, 64, 232, 72), False),
             ("a curve on a slope", (302, 79, 307, 93), True),
             ("highway over a valley", (200, 80, 202, 94), False),
             ("highway over a ridge", (216, 80, 218, 94), False),
             ("highway climbing a mountain", (232, 80, 234, 98), False),
             ("highway over a cliff", (248, 80, 250, 92), False),
             ("highway beside a hill", (258, 80, 260, 92), False),
             ("highway ending on a hillside", (276, 80, 288, 88), False))
    NETS = ("road", "band")
    places = []
    for name, box, fixed in SPECS:
        #  The frame grows until every road and junction that reaches into it
        #  lies whole inside it, with a tile to spare.
        while not fixed:
            c0, r0, c1, r1 = box
            cells = [c for r in roads.values() if r["net"] in NETS
                     and any(c0 <= x < c1 and r0 <= y < r1 for x, y in r["cells"]) for c in r["cells"]]
            cells += [c for kind, net, cs in ends.values() if net in NETS
                      and any(c0 <= x < c1 and r0 <= y < r1 for x, y in cs) for c in cs]
            grown = (min([c0] + [x - 1 for x, _ in cells]), min([r0] + [y - 1 for _, y in cells]),
                     max([c1] + [x + 3 for x, _ in cells]), max([r1] + [y + 3 for _, y in cells]))
            if grown == box:
                break
            box = grown
        deck_junctions = []
        p = place(name, land, built, near(box), box, junctions_in(box), [] if fixed else decks_in(box))
        p["deck_junctions"] = [{"poly": [[round(x, 4), round(y, 4)] for x, y in poly], "z": round(z * LEVEL_H, 4),
                                "zs": [round(q * LEVEL_H, 4) for q in cz]} for poly, z, cz in deck_junctions]
        p["fixed"] = fixed
        places.append(p)
    #  A place is shown only when everything in it is drawn: every street
    #  and highway shaped, and nothing of another network or of a ramp in it.
    for p in places:
        if p["fixed"]:
            continue
        c0, r0, c1, r1 = p["box"]
        for rid, r in roads.items():
            if not any(c0 <= c < c1 and r0 <= q < r1 for c, q in r["cells"]):
                continue
            if r["net"] not in NETS or rid not in shapes:
                sys.exit(f"{p['name']}: the {r['net']} road from {r['cells'][0]} is not drawn")
            if not all(c0 <= c < c1 and r0 <= q < r1 for c, q in r["cells"]):
                sys.exit(f"{p['name']}: the road from {r['cells'][0]} runs off the frame")
        for eid, (kind, net, cells) in ends.items():
            if any(c0 <= c < c1 and r0 <= q < r1 for c, q in cells) and (net not in NETS or kind == "ramp"):
                sys.exit(f"{p['name']}: the {kind} of the {net} network at {cells[0]} is not drawn")
            if any(c0 <= c < c1 and r0 <= q < r1 for c, q in cells) and kind != "seam" and eid not in bounds:
                sys.exit(f"{p['name']}: the {kind} at {cells[0]} has no outline")
    #  Every deck, edge to edge and with its caps, stands at least a level
    #  over the land under it, to the end.
    worst = []
    for p in places:
        for dk in p["decks"]:
            for x0, y0, x1, y1, z in dk["ribbon"]:
                for f in (0.0, 0.25, 0.5, 0.75, 1.0):
                    x, y = x0 + (x1 - x0) * f, y0 + (y1 - y0) * f
                    worst.append((z - gd.land_at(land, x, y) * LEVEL_H, p["name"], round(x, 2), round(y, 2)))
        for j in p["deck_junctions"]:
            jz = min([j["z"]] + j["zs"])
            xs, ys = [q[0] for q in j["poly"]], [q[1] for q in j["poly"]]
            for i in range(9):
                for k in range(9):
                    x = min(xs) + (max(xs) - min(xs)) * i / 8
                    y = min(ys) + (max(ys) - min(ys)) * k / 8
                    if inside([tuple(q) for q in j["poly"]], x, y):
                        worst.append((jz - gd.land_at(land, x, y) * LEVEL_H, p["name"] + " (a junction)", round(x, 2), round(y, 2)))
    low = [w for w in worst if w[0] < LEVEL_H - 1e-3]
    if low:
        for w in sorted(low)[:10]:
            print(f"{w[1]}: a deck {w[0] / LEVEL_H:.2f} levels over the land at {w[2]},{w[3]}")
        sys.exit(f"{len(low)} deck samples stand less than a level over the land")
    tpl = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "ground3d.html")).read()
    path = os.path.join(OUT, "ground3d.html")
    open(path, "w").write(tpl.replace("/*DATA*/null", json.dumps(places)))
    print(path, [(p["name"], sum(len(w) for w in p["walls"])) for p in places])


if __name__ == "__main__":
    main()
