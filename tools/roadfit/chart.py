"""chart.py -- the two drawings: a road from above, and a profile along it.

Both are at true proportions.  From above, a tile is a square.  In a
profile, a level is LEVEL_H of a tile tall, so a slope looks as steep as it
is.  The tile grid is drawn under both.
"""
import html
import math

from knobs import LEVEL_H


def _pts(ps, f):
    return " ".join(f"{f(x, y)[0]:.1f},{f(x, y)[1]:.1f}" for x, y in ps)


def plan(name, cells, size, roads, junction=None, px=28, outlines=()):
    """A road or a junction from above.  cells: the first cell of each unit;
    roads: [(left, right, centre cells)] in tiles; junction: an outline, and
    outlines: more of them."""
    c0 = min(c for c, _ in cells) - 1
    r0 = min(r for _, r in cells) - 1
    c1 = max(c for c, _ in cells) + size + 1
    r1 = max(r for _, r in cells) + size + 1
    f = lambda x, y: ((x - c0) * px, (y - r0) * px)
    o = [f'<svg viewBox="0 0 {(c1 - c0) * px} {(r1 - r0) * px}" role="img" aria-label="{html.escape(name)}">']
    for c, r in cells:
        x, y = f(c, r)
        o.append(f'<rect class="cell" x="{x:.1f}" y="{y:.1f}" width="{size * px}" height="{size * px}"/>')
    for poly in ([junction] if junction else []) + list(outlines):
        o.append(f'<polygon class="junc" points="{_pts(poly, f)}"/>')
    for left, right, chain in roads:
        o.append(f'<polygon class="road" points="{_pts(list(left) + list(right)[::-1], f)}"/>')
        mid = [(c + size / 2, r + size / 2) for c, r in chain]
        o.append(f'<polyline class="raw" points="{_pts(mid, f)}"/>')
    o.append("</svg>")
    return "\n".join(o)


def profile(name, ground, lines, cols=None, ramps=(), marks=(), px=40):
    """A profile along a road.  ground: [(s, h)] filled; lines: [(points,
    class)]; cols: [(s, top, bottom)] the deck's columns; ramps: [((s0, h0),
    (s1, h1))]; marks: [(s0, s1, h)] tiles marked on the ground.  s in tiles,
    heights in levels, px pixels to a tile."""
    L0 = min(x for x, _ in ground)
    L = max(x for x, _ in ground)
    ys = [h for _, h in ground] + [h for pts, _ in lines for _, h in pts]
    lo, hi = math.floor(min(ys)) - 1, math.ceil(max(ys)) + 1
    left, top = 30, 8
    W = left + (L - L0) * px + 10
    H = top + (hi - lo) * LEVEL_H * px + 26
    fx = lambda x: left + (x - L0) * px
    fy = lambda h: top + (hi - h) * LEVEL_H * px
    o = [f'<svg width="{W:.0f}" height="{H:.0f}" viewBox="0 0 {W:.0f} {H:.0f}" role="img" '
         f'aria-label="{html.escape(name)}">']
    for t in range(math.ceil(L0), int(L) + 1):
        o.append(f'<line class="{"unitline" if t % 2 == 0 else "tileline"}" x1="{fx(t):.1f}" x2="{fx(t):.1f}" '
                 f'y1="{fy(hi):.1f}" y2="{fy(lo):.1f}"/>')
        if t % 2 == 0:
            o.append(f'<text class="lab" x="{fx(t) - 4:.1f}" y="{H - 8:.0f}">{t}</text>')
    for lv in range(lo, hi + 1):
        o.append(f'<line class="grid" x1="{left}" x2="{W - 10:.0f}" y1="{fy(lv):.1f}" y2="{fy(lv):.1f}"/>'
                 f'<text class="lab" x="4" y="{fy(lv) + 4:.1f}">{lv}</text>')
    f = lambda x, h: (fx(x), fy(h))
    o.append(f'<polygon class="ground" points="{_pts([(L0, lo)] + list(ground) + [(L, lo)], f)}"/>')
    for x, t, b in cols or []:
        o.append(f'<line class="col" x1="{fx(x):.1f}" x2="{fx(x):.1f}" y1="{fy(t):.1f}" y2="{fy(b):.1f}"/>')
    for x0, x1, h in marks:
        o.append(f'<line class="mark" x1="{fx(x0) + 2:.1f}" x2="{fx(x1) - 2:.1f}" y1="{fy(h):.1f}" y2="{fy(h):.1f}"/>')
    for pts, cls in lines:
        o.append(f'<polyline class="{cls}" points="{_pts(pts, f)}"/>')
    for (x0, h0), (x1, h1) in ramps:
        o.append(f'<line class="ramp" x1="{fx(x0):.1f}" y1="{fy(h0):.1f}" x2="{fx(x1):.1f}" y2="{fy(h1):.1f}"/>')
        o.append(f'<circle class="pin" cx="{fx(x1):.1f}" cy="{fy(h1):.1f}" r="4"/>')
    o.append("</svg>")
    return "\n".join(o)
