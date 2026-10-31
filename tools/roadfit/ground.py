"""ground.py -- the drawn ground beside a street, as the street's shape asks.

    python3 tools/roadfit/ground.py [CITY]      # cities/test.arco when none

It reads the streets the renderer's C filed (src/network/shape.c) and
works out the ground beside each one.  The land itself is never changed:
this is only the ground drawn next to a road.

* Under the road, the ground meets the road.
* Beside it, within GROUND_REACH of its edge, the ground is GRADED: it
  blends from the road's height at the edge to the land's own height at
  the end of the reach.
* Where that bank would be steeper than BANK, or a building stands on the
  tile, the ground keeps its own height and a WALL stands at the road's
  edge instead, from the road down or up to the land.

It writes out/roadfit/ground.html: cross-sections through the streets
whose heights part most from their land, and each one from above with
its walls marked.
"""
import math
import os
import struct
import sys
import zipfile
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chart  # noqa: E402
import c_view as cv  # noqa: E402
import land as ld  # noqa: E402
from knobs import LEVEL_H, NET  # noqa: E402
from run import fig, section  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ld.ROOT, "out", "roadfit")

#  How far past a road's edge the ground is graded to meet it, in tiles.
GROUND_REACH = 0.25
#  The steepest a graded bank may be, in degrees.  Steeper is a wall.
BANK = 45.0
#  The ground within this many tiles of a road's ends belongs to the
#  junction there, and is not the street's to grade.
END = 0.5


def buildings(path):
    """Which tiles hold a building: xbld at 0x70 or above."""
    z = zipfile.ZipFile(path)
    w = json.loads(z.read("world.json"))
    side = max(w["bounds"]["x1"], w["bounds"]["y1"])
    cs = w["chunk_size"]
    out = set()
    for ch in w["chunk_list"].split(","):
        cx, cy = map(int, ch.split("_"))
        b = z.read(f"chunks/{ch}/xbld.bin")
        for i in range(cs * cs):
            if b[i] >= 0x70:
                out.add((cx * cs + i % cs, cy * cs + i // cs))
    return out


def land_at(land, x, y):
    side = land[0]
    c, r = min(side - 1, max(0, int(math.floor(x)))), min(side - 1, max(0, int(math.floor(y))))
    return ld.plane_at(land, c, r, x, y)


def side_of(land, built, sh, s, side, w):
    """The ground on one side of a road at s along it: 'graded' or 'wall',
    the road's height, and the land's height at the edge and at the end of
    the reach."""
    x, y, h = cv.at(sh, s)
    nx, ny = -math.sin(h) * side, math.cos(h) * side
    z = cv.height(sh, s)
    ex, ey = x + nx * w / 2, y + ny * w / 2
    fx, fy = x + nx * (w / 2 + GROUND_REACH), y + ny * (w / 2 + GROUND_REACH)
    edge, far = land_at(land, ex, ey), land_at(land, fx, fy)
    steep = abs(far - z) * LEVEL_H / GROUND_REACH > math.tan(math.radians(BANK))
    blocked = (int(math.floor(fx)), int(math.floor(fy))) in built
    kind = "wall" if (steep or blocked) and abs(edge - z) > 0.02 else "graded"
    return kind, z, edge, far, blocked


def cross(land, built, sh, s, w, span=1.5, step=0.02):
    """A cross-section at s: the land, the drawn ground and the walls, over
    t from -span to span across the road."""
    x, y, h = cv.at(sh, s)
    z = cv.height(sh, s)
    nx, ny = -math.sin(h), math.cos(h)
    sides = {sd: side_of(land, built, sh, s, sd, w) for sd in (-1, 1)}
    natural, drawn, walls = [], [], []
    n = int(2 * span / step) + 1
    for k in range(n):
        t = -span + k * step
        g = land_at(land, x + nx * t, y + ny * t)
        natural.append((t, g))
        d = abs(t)
        sd = 1 if t >= 0 else -1
        kind = sides[sd][0]
        if d <= w / 2:
            drawn.append((t, z))
        elif kind == "graded" and d <= w / 2 + GROUND_REACH:
            f = (d - w / 2) / GROUND_REACH
            f = f * f * (3 - 2 * f)
            drawn.append((t, z + (g - z) * f))
        else:
            drawn.append((t, g))
    for sd in (-1, 1):
        kind, zr, edge, _, _ = sides[sd]
        if kind == "wall":
            walls.append((sd * w / 2, max(zr, edge), min(zr, edge)))
    return natural, drawn, walls, sides


def change_map(land, built, roads, shapes, box, w, cell=0.125):
    """From above, over box (c0, r0, c1, r1): how far the drawn ground moves
    from the land at each point, as an SVG.  Blue is cut down, red is
    raised, black is a wall at the road's edge, grey is the road."""
    c0, r0, c1, r1 = box
    samples = {}
    for rid, sh in shapes.items():
        r = roads.get(rid)
        if not r or r["net"] != "road":
            continue
        n = max(2, int(sh["length"] / 0.05))
        for k in range(n):
            s = sh["length"] * k / (n - 1)
            x, y, h = cv.at(sh, s)
            if c0 - 1 <= x <= c1 + 1 and r0 - 1 <= y <= r1 + 1:
                samples.setdefault((int(x), int(y)), []).append((x, y, h, rid, s))
    kinds = {}
    px = 6
    W, H = int((c1 - c0) / cell), int((r1 - r0) / cell)
    o = [f'<svg viewBox="0 0 {W * px} {H * px}" role="img" aria-label="ground change">']
    for j in range(H):
        for i in range(W):
            x, y = c0 + (i + 0.5) * cell, r0 + (j + 0.5) * cell
            best = None
            for dc in (-1, 0, 1):
                for dr in (-1, 0, 1):
                    for q in samples.get((int(x) + dc, int(y) + dr), ()):
                        d = math.hypot(x - q[0], y - q[1])
                        if best is None or d < best[0]:
                            best = (d, q)
            g = land_at(land, x, y)
            colour = None
            if best and best[0] <= w / 2 + GROUND_REACH:
                d, (qx, qy, h, rid, s) = best
                sd = 1 if (-(math.sin(h)) * (x - qx) + math.cos(h) * (y - qy)) >= 0 else -1
                key = (rid, round(s, 3), sd)
                if key not in kinds:
                    kinds[key] = side_of(land, built, shapes[rid], s, sd, w)
                kind, z = kinds[key][0], kinds[key][1]
                if d <= w / 2:
                    colour = "#7d8a96"
                elif kind == "wall":
                    colour = "#111" if d <= w / 2 + 0.07 else None
                else:
                    f = (d - w / 2) / GROUND_REACH
                    f = f * f * (3 - 2 * f)
                    move = (z - g) * (1 - f)
                    if abs(move) > 0.01:
                        a = min(1.0, abs(move) / 0.75)
                        colour = f"rgba({'214,69,65' if move > 0 else '52,120,214'},{0.2 + 0.8 * a:.2f})"
            if colour:
                o.append(f'<rect x="{i * px}" y="{j * px}" width="{px}" height="{px}" fill="{colour}"/>')
    for t in range(int(c1 - c0) + 1):
        o.append(f'<line class="tileline" x1="{t / cell * px}" x2="{t / cell * px}" y1="0" y2="{H * px}"/>')
    for t in range(int(r1 - r0) + 1):
        o.append(f'<line class="tileline" y1="{t / cell * px}" y2="{t / cell * px}" x1="0" x2="{W * px}"/>')
    o.append("</svg>")
    return "\n".join(o)


def main():
    city = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ld.ROOT, "cities", "test.arco")
    os.makedirs(OUT, exist_ok=True)
    land = ld.load(city)
    built = buildings(city)
    ends, roads, shapes, bounds = cv.read(city)
    w = NET["road"]["width"]
    #  How far each street parts from its land at its edges, and how much of
    #  its edge is graded or walled.
    found, total = [], {"graded": 0, "wall": 0, "same": 0}
    for rid, sh in shapes.items():
        r = roads.get(rid)
        if not r or r["net"] != "road" or not ld.dry(land, r["cells"]):
            continue
        worst, at = 0.0, 0.0
        n = max(2, int(sh["length"] / 0.1))
        for k in range(n):
            s = sh["length"] * k / (n - 1)
            #  The ground within END of a road's ends is its junction's.
            if s < END or s > sh["length"] - END:
                continue
            for sd in (-1, 1):
                kind, z, edge, far, _ = side_of(land, built, sh, s, sd, w)
                gap = abs(edge - z)
                total["same" if gap <= 0.02 else kind] += 1
                if gap > worst:
                    worst, at = gap, s
        if worst > 0.02:
            found.append((worst, rid, at))
    found.sort(reverse=True)
    sections = []
    for worst, rid, at in found[:6]:
        sh, r = shapes[rid], roads[rid]
        natural, drawn, walls, sides = cross(land, built, sh, at, w)
        what = ", ".join(f"{'chart right' if sd > 0 else 'chart left'}: {sides[sd][0]}"
                         + (" (a building)" if sides[sd][4] else "") for sd in (1, -1))
        sections.append(fig(f"{r['cells'][0][0]},{r['cells'][0][1]} at s {at:.1f}",
                            f"the road {worst:.2f} levels off its land · {what}",
                            chart.profile("cross-section", drawn, [(natural, "raw"), ([(-w / 2, cv.height(sh, at)),
                                          (w / 2, cv.height(sh, at))], "fit")], walls, px=220)))
    #  The areas round the streets that move the ground most.
    curved = []
    for rid, sh in shapes.items():
        r = roads.get(rid)
        if r and r["net"] == "road" and ld.dry(land, r["cells"]):
            moved = sum(1 for k in range(int(sh["length"] / 0.1) + 1) for sd in (-1, 1)
                        if abs(side_of(land, built, sh, k * 0.1, sd, w)[1] - side_of(land, built, sh, k * 0.1, sd, w)[2]) > 0.02)
            if moved:
                curved.append((moved, rid))
    maps, seen = [], []
    for moved, rid in sorted(curved, reverse=True):
        cells = roads[rid]["cells"]
        c0, r0 = min(c for c, _ in cells) - 2, min(q for _, q in cells) - 2
        if any(abs(c0 - a) < 8 and abs(r0 - b) < 8 for a, b in seen):
            continue
        seen.append((c0, r0))
        box = (c0, r0, max(c for c, _ in cells) + 3, max(q for _, q in cells) + 3)
        maps.append(fig(f"round {cells[0][0]},{cells[0][1]}", f"{moved} edge samples moved", change_map(land, built, roads, shapes, box, w)))
        if len(maps) == 4:
            break
    n = sum(total.values())
    body = "".join([
        section("Cross-sections",
                f"Across the street, away from its ends, where its height parts most from its land. The dashed line "
                f"is the land, and the fill the ground as drawn. The "
                f"ground is graded to the road within {GROUND_REACH:g} tile of its edge, or a wall stands at the "
                f"edge where that bank would be steeper than {BANK:g}° or a building is in the way.", sections, True),
        section("From above",
                "How far the drawn ground moves from the land, round the streets that move it most: red "
                "is raised to meet the road, blue is cut down to it, black is a wall at the road's edge, grey the "
                "road. A tile is drawn as its grid square.", maps),
    ])
    page = open(os.path.join(HERE, "page.html")).read().replace("<!-- sections -->", body)
    page = page.replace("Road Fit Prototype", "Ground Beside the Roads").replace(
        "Roads in the test world and on made strips of land, fitted outside the renderer by <code>tools/roadfit</code>.",
        f"The ground drawn beside each street of the test world, from the streets the C filed. Of {n} samples along the "
        f"street edges, {total['same']} need no change, {total['graded']} are graded and {total['wall']} take a wall. "
        f"The land itself is never changed.")
    k0 = page.index('<div class="key">')
    k1 = page.index("</div>", k0) + 6
    page = page[:k0] + ('<div class="key"><span><i class="r"></i>the land</span><span><i class="g0"></i>the ground as drawn'
                        '</span><span><i></i>the road</span><span><i class="w0"></i>a wall</span></div>') + page[k1:]
    page = page.replace("</style>", ".wall{fill:none;stroke:#c0392b;stroke-width:3}.col{stroke:#8d8d8d;stroke-width:5}"
                                  ".key i.g0{border-top:8px solid #c9bfa8}.key i.w0{border-top:5px solid #8d8d8d}</style>")
    path = os.path.join(OUT, "ground.html")
    open(path, "w").write(page)
    print(path, total)


if __name__ == "__main__":
    main()
