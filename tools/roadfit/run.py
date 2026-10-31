"""run.py -- fit every road of a city and of the made strips, and draw the page.

    python3 tools/roadfit/run.py [CITY]      # cities/test.arco when none
    python3 tools/roadfit/run.py --c [CITY]  # the same page from the C (cengine.py)

It writes out/roadfit/index.html, or out/roadfit/c.html from the C.  Every road goes through the same three
calls: plan.fit for its line from above, heights.street or heights.deck for
its profile.  The numbers they are tuned by are knobs.py.  The topology is
read once from the renderer and kept in out/roadfit; delete that file to
read it again.
"""
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chart  # noqa: E402
import heights  # noqa: E402
import land as ld  # noqa: E402
import net  # noqa: E402
import plan  # noqa: E402
import strips  # noqa: E402
from knobs import NET  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ld.ROOT, "out", "roadfit")
#  Pixels per tile in a profile: the made strips are 40 tiles long and
#  scroll sideways, a test world road is a dozen.
STRIP_PX = 44
TEST_PX = 80


def fig(title, caption, svg):
    return (f'<figure><div class="pic">{svg}</div><figcaption><b>{html.escape(title)}</b> '
            f'<span>{html.escape(caption)}</span></figcaption></figure>')


def section(title, text, figs, wide=False):
    cls = "stack" if wide else "grid"
    return f'<section><h2>{title}</h2>\n<p class="muted">{text}</p>\n<div class="{cls}">{"".join(figs)}</div></section>\n'


def angles(a):
    return ", ".join(f"{x:g}°" for x in a)


def deck_lines(floor, deck, ground_at):
    """The floor, the deck and its columns, one at each unit centre, from the
    deck down to ground_at(s)."""
    return [(floor, "raw"), (deck, "deckline")], [(x, strips.at(deck, x), ground_at(x)) for x, _ in floor]


def streets_from_above(ends, roads):
    kinds = {"shallow": [], "diagonal": [], "corners": []}
    for _, nt, e0, e1, cells in roads:
        if nt != "road" or len(cells) < 3:
            continue
        ch = net.chain(ends, nt, e0, e1, cells)
        f = plan.fit(nt, ch)
        a = f["angles"]
        kind = ("shallow" if any(x % 45 for x in a) else "diagonal" if any(x % 90 for x in a)
                else "corners" if len(a) > 1 else None)
        if kind:
            kinds[kind].append((len(ch), ch, f))
    figs = []
    for kind, found in kinds.items():
        for _, ch, f in sorted(found, key=lambda t: -t[0])[:4]:
            figs.append(fig(kind, f"{angles(f['angles'])} · from cell {ch[0][0]},{ch[0][1]}",
                            chart.plan(kind, ch, 1, [(f["left"], f["right"], ch)])))
    return figs


def junctions(ends, roads):
    figs = []
    for eid in net.pick(ends, roads):
        j = net.junction(ends, roads, eid)
        size = j["size"]
        name = ("street " if j["net"] == "road" else "highway ") + {2: "two roads", 3: "T", 4: "four-way"}.get(len(j["arms"]), "")
        cells = list(j["cells"]) + [c for a in j["arms"] for c in a["cells"]]
        arms = [(a["left"], a["right"], a["cells"]) for a in j["arms"]]
        c = min(j["cells"])
        figs.append(fig(name, f"arms {' · '.join(angles(a['angles']) for a in j['arms'])} · at {c[0]},{c[1]}",
                        chart.plan(name, cells, size, arms, j["surface"])))
    return figs


def highways(ends, roads):
    figs = []
    for _, nt, e0, e1, cells in roads:
        if nt != "band" or len(cells) < 3:
            continue
        ch = net.chain(ends, nt, e0, e1, cells)
        f = plan.fit(nt, ch)
        if len(f["angles"]) < 2:
            continue
        figs.append(fig("highway", f"{angles(f['angles'])} · from {ch[0][0]},{ch[0][1]}",
                        chart.plan("highway", ch, NET[nt]["unit"], [(f["left"], f["right"], ch)])))
    return figs


def made_strips():
    both, ramped = [], []
    for name, levels in strips.LANDS.items():
        land = strips.land(levels)
        n = len(levels) - 1
        raw = ld.street_raw(land, [(k, 2) for k in range(n)])
        street = heights.street(raw)
        floor = heights.floor_of(net.unit_ground(land, "band", [(k, 0) for k in range(0, n, 2)]))
        deck = heights.deck(floor)
        lines, cols = deck_lines(floor, deck, lambda x: strips.at(raw, x))
        both.append(fig(name, f"{n} tiles", chart.profile(name, raw, [(raw, "raw"), (street, "fit")] + lines[1:],
                                                          cols, px=STRIP_PX)))
        found = strips.ramps(levels, deck)
        tiles = sorted({int(min(a[0], b[0])) for a, b in found})
        marks = [(k, k + 1, levels[k]) for k in tiles]
        cap = (f"{len(tiles)} {'tile takes' if len(tiles) == 1 else 'tiles take'} a ramp, marked green; "
               "the one nearest the middle is drawn" if found
               else "no tile takes a ramp: the deck is nowhere one level above a flat tile")
        mid = sorted(found, key=lambda rp: abs(rp[1][0] - n / 2))[:1]
        ramped.append(fig(name, cap, chart.profile(name, raw, lines, cols, mid, marks, px=STRIP_PX)))
    return both, ramped


def highway_heights(ends, roads, land):
    figs = []
    for _, nt, e0, e1, cells in roads:
        if nt != "band" or len(cells) < 3 or not ld.dry(land, cells):
            continue
        #  The deck runs over the junction units at its ends too, so the road
        #  lies over its floor right to its end.
        ch = net.chain(ends, nt, e0, e1, cells)
        size = NET[nt]["unit"]
        ground = net.unit_ground(land, nt, ch)
        if ch[0] != cells[0]:
            ground = [[x - size, g] for x, g in ground]
        if max(g for _, g in ground) == min(g for _, g in ground):
            continue
        floor = heights.floor_of(ground)
        deck = heights.deck(floor)
        lines, cols = deck_lines(floor, deck, lambda x: min(ground, key=lambda e: abs(e[0] - x))[1])
        step = []
        for x, g in ground:
            step += [(x - 1, g), (x + 1, g)]
        figs.append(fig(f"{len(cells)} units", f"from {cells[0][0]},{cells[0][1]}",
                        chart.profile("highway", step, lines, cols, px=TEST_PX)))
    return figs


def streets_along(roads, land):
    found = []
    for _, nt, _, _, cells in roads:
        if nt != "road" or len(cells) < 4 or not ld.dry(land, cells):
            continue
        raw = ld.street_raw(land, cells)
        rise = max(y for _, y in raw) - min(y for _, y in raw)
        if rise > 0:
            found.append((rise, len(cells), cells, raw))
    figs = []
    for _, n, cells, raw in sorted(found, key=lambda t: (-t[0], -t[1]))[:6]:
        figs.append(fig(f"{n} cells", f"from cell {cells[0][0]},{cells[0][1]}",
                        chart.profile("street", raw, [(raw, "raw"), (heights.street(raw), "fit")], px=TEST_PX)))
    return figs


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    use_c = "--c" in sys.argv[1:]
    city = args[0] if args else os.path.join(ld.ROOT, "cities", "test.arco")
    os.makedirs(OUT, exist_ok=True)
    if use_c:
        import cengine
        cengine.install(city, OUT)
    land = ld.load(city)
    ends, roads = ld.network(city, OUT)
    roads = net.join_seams(ends, roads)
    both, ramped = made_strips()
    body = "".join([
        section("Streets from above",
                "A run of cells is a straight when one line passes through all their squares and it steps sideways "
                "never or at least three times. Each corner takes the largest arc, up to 1.5 tiles, that keeps both "
                "edges inside the cells widened by a quarter tile.", streets_from_above(ends, roads)),
        section("Junctions",
                "Each road keeps its angle into the junction and is cut where its centre line leaves the junction's "
                "cells. The surface is the outline round the mouths.", junctions(ends, roads)),
        section("Highways from above",
                "The same fit on a grid of 2×2 units, 1.5 tiles wide, corners up to 4 tiles. Straight highways are left out.", highways(ends, roads)),
        section("A street and a highway over varying land",
                "The street stays within a level of its tiles, never above or below the heights near it, and comes "
                "level into its ends. The deck stands a level above the highest ground under each unit, and holds "
                "a level over a dip under 8 tiles wide and a level deep.", both, True),
        section("Ramps",
                "A ramp is one flat tile beside the highway, climbing one level to the deck. It is legal only where "
                "the deck is one level above that tile.", ramped, True),
        section("Highway heights in the test world",
                "Every dry highway whose ground varies. The dashed line is the deck's floor.",
                highway_heights(ends, roads, land), True),
        section("Streets in the test world",
                "The six streets that climb the most. The dashed line is their tiles' own heights.",
                streets_along(roads, land), True),
    ])
    page = open(os.path.join(HERE, "page.html")).read().replace("<!-- sections -->", body)
    if use_c:
        page = page.replace("Road Fit Prototype", "Road Fit in C").replace(
            "fitted outside the renderer by <code>tools/roadfit</code>",
            "drawn from the renderer's C: each line as src/network/shape.c filed it, and each height through its "
            "solves.  The roads are picked and captioned as on the prototype's page")
    path = os.path.join(OUT, "c.html" if use_c else "index.html")
    open(path, "w").write(page)
    print(path)


if __name__ == "__main__":
    main()
