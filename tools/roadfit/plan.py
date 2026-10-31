"""plan.py -- the line a road follows, seen from above: the tangent fit.

From the first cell, the longest run of cells that is a straight; the next
straight starts at its last cell.  Consecutive straights meet at a vertex,
and every vertex is filleted: an arc of the network's radius, its tangent
length at most HALF of each straight beside it, shrunk until both edges of
the road stay inside the corridor (its cells widened by MARGIN).
"""
import math

from knobs import HALF, NET, MARGIN, STAB

ANGLES = [i * 0.25 for i in range(720)]  # degrees, 0 to 180


def line_through(cells, h):
    """A line through every cell's widened square, or None: (theta, d, slack).

    A line with unit normal n and offset d meets the square of centre p and
    half side h iff |n.p - d| <= h(|nx| + |ny|).  For each angle the feasible
    d is an interval; the angle with the most slack wins."""
    best = None
    for a in ANGLES:
        t = math.radians(a)
        nx, ny = -math.sin(t), math.cos(t)
        reach = h * (abs(nx) + abs(ny))
        lo, hi = -1e9, 1e9
        for c, r in cells:
            v = nx * (c + 0.5) + ny * (r + 0.5)
            lo, hi = max(lo, v - reach), min(hi, v + reach)
            if lo > hi:
                break
        if lo <= hi and (best is None or hi - lo > best[2]):
            best = (a, (lo + hi) / 2, hi - lo)
    return best


def risers(run):
    """How many steps the run takes across its main direction.

    A run's cells join edge to edge.  Its main direction is the one it moves
    in most, and a RISER is a move in the other.  A corner is one riser, and
    a staircase is two or more."""
    dx = [abs(run[k + 1][0] - run[k][0]) for k in range(len(run) - 1)]
    dy = [abs(run[k + 1][1] - run[k][1]) for k in range(len(run) - 1)]
    return min(sum(dx), sum(dy))


def rhythm(run):
    """Whether the run's steps keep a straight line's rhythm.

    Its moves split into the main direction and risers across it.  Every
    riser goes the same way, two risers never come together, and the runs
    of main moves between risers differ by at most one.  The first and the
    last run may be shorter, as a line cut anywhere starts and ends part of
    the way into a run."""
    moves = [(run[k + 1][0] - run[k][0], run[k + 1][1] - run[k][1]) for k in range(len(run) - 1)]
    xs = sum(1 for m in moves if m[0]); ys = len(moves) - xs
    main_x = xs >= ys
    rise = [m for m in moves if (m[1] if main_x else m[0])]
    if not rise:
        return all(m == moves[0] for m in moves)
    if any(m != rise[0] for m in rise):
        return False
    if any(m != next(mm for mm in moves if (mm[0] if main_x else mm[1])) for m in moves if (m[0] if main_x else m[1])):
        return False
    runs, cur = [], 0
    for m in moves:
        if (m[1] if main_x else m[0]):
            runs.append(cur); cur = 0
        else:
            cur += 1
    runs.append(cur)
    inner = runs[1:-1]
    if any(r == 0 for r in inner):
        return False
    if inner:
        lo, hi = min(inner), max(inner)
        if hi - lo > 1 or runs[0] > hi or runs[-1] > hi:
            return False
    else:
        if abs(runs[0] - runs[-1]) > 1 and min(runs[0], runs[-1]) > 0:
            return False
    return True


def diagonal(run):
    """Whether a run alternates its two steps every time, over four moves or
    more: a 45° diagonal."""
    moves = [(run[k + 1][0] - run[k][0], run[k + 1][1] - run[k][1]) for k in range(len(run) - 1)]
    return len(moves) >= 4 and all(moves[k] != moves[k + 1] for k in range(len(moves) - 1))


def straight(run, h):
    """A line through every cell's square, when the run keeps a straight
    line's rhythm and steps across never or at least three times.  One step
    across is a corner, and two make a jog: a rhythm repeats before it is a
    line.  A run that alternates its steps is a diagonal however often it
    steps across."""
    if len(run) > 2 and risers(run) in (1, 2) and not diagonal(run):
        return None
    if not rhythm(run):
        return None
    return line_through(run, h)


def straights(chain, h):
    """Greedy longest straights over the chain: a list of (i, j, theta, d).

    From each cell the run grows while it stays a straight.  A run that fails
    may pass again with more cells (a second riser makes a staircase of a
    corner), so the run grows to the longest that passes."""
    out = []
    i = 0
    n = len(chain)
    while i < n - 1:
        j, fit = i + 1, line_through(chain[i:i + 2], h)
        for k in range(i + 2, n):
            f2 = straight(chain[i:k + 1], h)
            if f2:
                j, fit = k, f2
            elif not line_through(chain[i:k + 1], h):
                break
        out.append((i, j, fit[0], fit[1]))
        i = j
    return out


def meet(l1, l2):
    """Where two lines (theta, d) meet, or None when parallel."""
    (a1, d1), (a2, d2) = l1, l2
    t1, t2 = math.radians(a1), math.radians(a2)
    n1 = (-math.sin(t1), math.cos(t1))
    n2 = (-math.sin(t2), math.cos(t2))
    det = n1[0] * n2[1] - n1[1] * n2[0]
    if abs(det) < 1e-9:
        return None
    return ((d1 * n2[1] - n1[1] * d2) / det, (n1[0] * d2 - d1 * n2[0]) / det)


def foot(line, p):
    """The point on a line nearest p."""
    a, d = line
    t = math.radians(a)
    nx, ny = -math.sin(t), math.cos(t)
    k = nx * p[0] + ny * p[1] - d
    return (p[0] - k * nx, p[1] - k * ny)


def polyline(chain, segs):
    """Start, the straights' meeting points, end."""
    c0 = (chain[0][0] + 0.5, chain[0][1] + 0.5)
    c1 = (chain[-1][0] + 0.5, chain[-1][1] + 0.5)
    pts = [foot((segs[0][2], segs[0][3]), c0)]
    for k in range(len(segs) - 1):
        m = meet((segs[k][2], segs[k][3]), (segs[k + 1][2], segs[k + 1][3]))
        if m is None:  # parallel straights: step across at the shared cell
            cc = chain[segs[k][1]]
            m = (cc[0] + 0.5, cc[1] + 0.5)
        pts.append(m)
    pts.append(foot((segs[-1][2], segs[-1][3]), c1))
    return pts


def fillet(pts, R, half, radii=None):
    """The tangent fit: straights and arcs.  Answers a sampled path.

    RADII, when given, holds a radius for each vertex in place of R."""
    path = [pts[0]]
    n = len(pts)
    for i in range(1, n - 1):
        if radii is not None:
            R = radii[i]
        a, b, c = pts[i - 1], pts[i], pts[i + 1]
        u = (a[0] - b[0], a[1] - b[1]); v = (c[0] - b[0], c[1] - b[1])
        lu, lv = math.hypot(*u), math.hypot(*v)
        if lu < 1e-6 or lv < 1e-6:
            continue
        u = (u[0] / lu, u[1] / lu); v = (v[0] / lv, v[1] / lv)
        cosang = max(-1.0, min(1.0, u[0] * v[0] + u[1] * v[1]))
        ang = math.acos(cosang)          # interior angle at the vertex
        turn = math.pi - ang
        if turn < 1e-4:
            path.append(b); continue
        t = R * math.tan(turn / 2)
        tmax = half * min(lu, lv)
        if t > tmax:
            t = tmax
        r = t / math.tan(turn / 2)
        p0 = (b[0] + u[0] * t, b[1] + u[1] * t)
        p1 = (b[0] + v[0] * t, b[1] + v[1] * t)
        bis = (u[0] + v[0], u[1] + v[1]); bl = math.hypot(*bis)
        bis = (bis[0] / bl, bis[1] / bl)
        cen = (b[0] + bis[0] * r / math.sin(ang / 2), b[1] + bis[1] * r / math.sin(ang / 2))
        a0 = math.atan2(p0[1] - cen[1], p0[0] - cen[0]); a1 = math.atan2(p1[1] - cen[1], p1[0] - cen[0])
        da = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
        path.append(p0)
        steps = max(2, int(abs(da) / 0.1))
        for k in range(1, steps):
            aa = a0 + da * k / steps
            path.append((cen[0] + r * math.cos(aa), cen[1] + r * math.sin(aa)))
        path.append(p1)
    path.append(pts[-1])
    return path


def outside(path, cells, margin):
    """How far the path strays outside the widened corridor of its cells."""
    h = 0.5 + margin
    worst = 0.0
    for x, y in path:
        best = min(max(0.0, abs(x - (c + 0.5)) - h, abs(y - (r + 0.5)) - h) for c, r in cells)
        worst = max(worst, best)
    return worst


def offset(path, d):
    """The path moved d to its left, point by point along each point's normal."""
    out = []
    n = len(path)
    for k in range(n):
        a = path[max(0, k - 1)]; b = path[min(n - 1, k + 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        l = math.hypot(tx, ty) or 1.0
        out.append((path[k][0] - ty / l * d, path[k][1] + tx / l * d))
    return out


def ribbon_out(path, chain, width, margin):
    """How far either edge of the road strays outside its corridor."""
    return max(outside(offset(path, width / 2), chain, margin), outside(offset(path, -width / 2), chain, margin))


def fit_road(chain, width, margin, R):
    """The tangent fit, each corner's radius the largest up to R at which both
    edges of the road stay inside the corridor."""
    segs = straights(chain, STAB)
    pts = polyline(chain, segs)
    radii = [R] * len(pts)
    for i in range(1, len(pts) - 1):
        r = R
        while r > 0.05:
            radii[i] = r
            sub = fillet(pts[max(0, i - 1):i + 2], R, HALF, radii[max(0, i - 1):i + 2])
            if ribbon_out(sub, chain, width, margin) < 1e-3:
                break
            r -= 0.05
        else:
            radii[i] = 0.05
    return segs, pts, fillet(pts, R, HALF, radii)


def dense(path, step):
    """The path with a point at least every step along it."""
    out = [path[0]]
    for a, b in zip(path, path[1:]):
        n = max(1, int(math.ceil(math.hypot(b[0] - a[0], b[1] - a[1]) / step)))
        out += [(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(1, n + 1)]
    return out


def fit(net, chain):
    """A road of network net through a chain of cells (the first cell of each
    unit), fitted on the network's grid of units.  Answers the path and its
    two edges in tiles, the straights' angles, and how far an edge strays
    outside the corridor."""
    size, width, radius = NET[net]["unit"], NET[net]["width"], NET[net]["radius"]
    units = [(c // size, r // size) for c, r in chain]
    w, m = width / size, MARGIN / size
    segs, _, path = fit_road(units, w, m, radius / size)
    path = dense(path, 0.1 / size)
    k = float(size)
    scale = lambda ps: [(x * k, y * k) for x, y in ps]
    return {"path": scale(path), "left": scale(offset(path, w / 2)), "right": scale(offset(path, -w / 2)),
            "angles": sorted({round(s[2] % 180, 2) for s in segs}), "out": ribbon_out(path, units, w, m) * k}
