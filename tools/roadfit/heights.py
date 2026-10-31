"""heights.py -- the one height solver, and what a street and a deck hand it.

Every road's height along it comes from profile(): the smoothest profile
between a floor and a ceiling, its ends fixed, drawn gently toward a target,
solved directly along the road's band of equations.
A street and a highway's deck differ only in what they hand it.
"""
import math

from knobs import CLEARANCE, DECK_PULL, EXTRA, FLAT, LEVEL_H, MAX_GRADE, REACH, SMOOTH, STREET_HOLD, STREET_PULL


def rows(n, level_ends, target, pull):
    """The stationary condition of the profile as rows of a band five wide:
    row i holds its coefficients on h[i-2] .. h[i+2] and its right side.  A
    row near a level end takes its missing neighbour as its mirror inside,
    so the profile comes level there.  A row near a free end bends less."""
    lv0, lv1 = level_ends
    out = []
    for i in range(1, n - 1):
        c = {}
        if 2 <= i <= n - 3 or (i < 2 and lv0) or (i > n - 3 and lv1):
            for j, w in ((i - 2, 1.0), (i - 1, -4.0), (i, 6.0 + pull), (i + 1, -4.0), (i + 2, 1.0)):
                j = 1 if j < 0 else n - 2 if j > n - 1 else j
                c[j] = c.get(j, 0.0) + w
        else:
            c = {i - 1: -1.0, i: 2.0 + pull, i + 1: -1.0}
        out.append((i, c, pull * target[i]))
    return out


def band_solve(n, eqs):
    """Solve n equations of a band five wide, each (coefficients by index,
    right side), by elimination without pivoting: one pass down, one up."""
    A = [[0.0] * 5 for _ in range(n)]
    b = [0.0] * n
    for i, (c, r) in enumerate(eqs):
        for j, w in c.items():
            A[i][j - i + 2] = w
        b[i] = r
    for i in range(n):
        p = A[i][2]
        for k in range(1, 3):
            if i + k >= n or A[i + k][2 - k] == 0.0:
                continue
            m = A[i + k][2 - k] / p
            for d in range(3):
                if 2 - k + d <= 4 and 2 + d <= 4:
                    A[i + k][2 - k + d] -= m * A[i][2 + d]
            b[i + k] -= m * b[i]
    h = [0.0] * n
    for i in range(n - 1, -1, -1):
        h[i] = (b[i] - sum(A[i][2 + d] * h[i + d] for d in (1, 2) if i + d < n)) / A[i][2]
    return h


def profile(xs, lo, hi, h0, h1, level_ends, target, pull):
    """Minimise the bending (the squared second differences) plus pull times
    the squared distance from target, over samples xs, with each height in
    [lo, hi] and the ends at h0 and h1.  level_ends is a pair of flags: an
    end that is level comes level into it, and one that is not is free in
    grade.

    An active set: the band is solved with every height held at a bound
    fixed there, a height that crosses a bound joins the set, and one whose
    own row would pull it back inside leaves it, until nothing changes."""
    n = len(xs)
    R = rows(n, level_ends, target, pull)
    held = {}
    for _ in range(4 * n):
        eqs = [({0: 1.0}, h0)]
        for i, c, r in R:
            eqs.append(({i: 1.0}, held[i]) if i in held else (c, r))
        eqs.append(({n - 1: 1.0}, h1))
        h = band_solve(n, eqs)
        out = {i: (lo[i] if h[i] < lo[i] else hi[i]) for i, _, _ in R
               if i not in held and (h[i] < lo[i] - 1e-12 or h[i] > hi[i] + 1e-12)}
        if out:
            held.update(out)
            continue
        back = []
        for i, c, r in R:
            if i in held:
                v = (r - sum(w * h[j] for j, w in c.items() if j != i)) / c[i]
                if (held[i] == lo[i] and v > lo[i] + 1e-12) or (held[i] == hi[i] and v < hi[i] - 1e-12):
                    back.append(i)
        if not back:
            break
        for i in back:
            del held[i]
    return [[xs[k], h[k]] for k in range(n)]


def street(raw):
    """A street's profile from its raw (s, height) samples: within STREET_HOLD
    of them, never above the highest within REACH nor below the lowest, level
    into its two junctions, drawn toward its own heights."""
    xs = [x for x, _ in raw]
    ys = [y for _, y in raw]
    near = lambda k: [ys[m] for m in range(len(ys)) if abs(xs[m] - xs[k]) <= REACH]
    lo = [max(ys[k] - STREET_HOLD, min(near(k))) for k in range(len(ys))]
    hi = [min(ys[k] + STREET_HOLD, max(near(k))) for k in range(len(ys))]
    return profile(xs, lo, hi, ys[0], ys[-1], (True, True), ys, STREET_PULL)


def floor_of(ground):
    """A deck's floor from the ground under each unit, (s, highest ground):
    CLEARANCE above it."""
    return [[x, g + CLEARANCE] for x, g in ground]


def deck(floor, step=0.5):
    """A highway's deck over its floor (s, height per unit centre), from the
    first unit's centre to the last.

    It stands one level above the highest ground under each unit, over the
    whole of the unit, the agreed height.  It never climbs steeper than
    MAX_GRADE: where the floor steps up, the floor it keeps to rises toward
    the step at that grade, and wherever the deck itself climbs faster, it
    is lifted.  So it starts climbing early and never cuts into the ground.  It HOLDS FLATS: it aims at the highest floor
    within FLAT, never more than EXTRA above the floor there, so it crosses
    a short, shallow dip level and follows a longer or deeper one.  Its
    ceiling is SMOOTH above that aim, so it eases onto a flat.  Its ends are
    free in grade, each at its own floor."""
    a, b = floor[0][0], floor[-1][0]
    n = max(3, int(round((b - a) / step)) + 1)
    xs = [a + (b - a) * k / (n - 1) for k in range(n)]
    half = (floor[1][0] - floor[0][0]) / 2 if len(floor) > 1 else 1.0
    unit = [max(y for x0, y in floor if abs(x0 - x) <= half + 1e-9) for x in xs]
    g = math.tan(math.radians(MAX_GRADE)) / LEVEL_H
    lo = [max(unit[j] - g * abs(xs[j] - xs[k]) for j in range(n)) for k in range(n)]
    aim = [min(max(lo[m] for m in range(n) if abs(xs[m] - xs[k]) <= FLAT), lo[k] + EXTRA) for k in range(n)]
    #  The deck itself never climbs steeper than the grade: where it does,
    #  the lower of the two samples is raised to meet it, and the deck is
    #  solved again.  A deck is only ever lifted, never lowered.
    for _ in range(4 * n):
        hi = [max(t + SMOOTH, l) for t, l in zip(aim, lo)]
        d = profile(xs, lo, hi, lo[0], lo[-1], (False, False), aim, DECK_PULL)
        raised = False
        for k in range(n - 1):
            rise, room = d[k + 1][1] - d[k][1], g * (xs[k + 1] - xs[k])
            low = k if rise > 0 else k + 1
            need = max(d[k][1], d[k + 1][1]) - room
            if abs(rise) > room + 1e-4 and need > lo[low] + 1e-6:
                lo[low] = need
                raised = True
        if not raised:
            break
    return d
