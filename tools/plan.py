#!/usr/bin/env python3
"""plan.py -- inspect a city from above.

    tools/plan.py <city> <out.png> [col row cols rows]

Runs the renderer with its plan and path dumps and draws the world flat:
the ground shaded by height, the water, what each tile carries, and over
that the corridors, their gates and the centreline fitted through them.
A plan view is how this is worked on and argued about (the user, 3
September 2026: "you can probably do all this in 2D for previews eh? ...
much easier for you to see too", then "can you provide a debug view that
allows me to inspect a city?").

    tools/plan.py Bayview out.png                 the whole map
    tools/plan.py Bayview out.png 60 100 24 24    a window on it
    tools/plan.py - out.png                       a dump on stdin

    the ground        green, darker low, lighter high
    water             blue
    a road tile       warm grey        a rail tile      brown-grey
    a highway tile    pale blue        a building       dark slate
    a gate            a blue tick across the corridor
    the centreline    yellow, the band's edges grey -- red outside the corridor
    a corner          red where it sweeps, white where it has no room
    a red band edge   the road over a building, a network or water
    a junction        orange: the outline its arms cut out
"""
import os
import subprocess
import struct
import sys
import zlib

GAME = os.path.expanduser("~/Downloads/SimCity 2000® Collection")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    d = zlib.compress(raw, 6)

    def ck(t, c):
        x = t + c
        return struct.pack(">I", len(c)) + x + struct.pack(">I", zlib.crc32(x) & 0xFFFFFFFF)

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + ck(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + ck(b"IDAT", d)
        + ck(b"IEND", b"")
    )


class Canvas:
    def __init__(self, w, h, bg=(18, 20, 22)):
        self.w, self.h = w, h
        self.px = [bytearray(bg * w) for _ in range(h)]

    def dot(self, x, y, c):
        xi, yi = int(round(x)), int(round(y))
        if 0 <= xi < self.w and 0 <= yi < self.h:
            self.px[yi][xi * 3 : xi * 3 + 3] = bytes(c)

    def line(self, x0, y0, x1, y1, c, wide=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 2
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            for dx in range(-(wide // 2), wide // 2 + 1):
                for dy in range(-(wide // 2), wide // 2 + 1):
                    self.dot(x + dx, y + dy, c)

    def fill(self, x0, y0, x1, y1, c):
        b = bytes(c)
        for y in range(int(y0), int(y1)):
            if 0 <= y < self.h:
                row = self.px[y]
                for x in range(int(x0), int(x1)):
                    if 0 <= x < self.w:
                        row[x * 3 : x * 3 + 3] = b


def run(city):
    path = os.path.join(GAME, "Cities", city)
    if not os.path.isfile(path):
        path = os.path.join(GAME, city)
    if not os.path.isfile(path):
        sys.exit("no city at %s" % path)
    env = dict(os.environ, SC2K_PLAN_DUMP="1", SC2K_PATH_DUMP="1", SC2K_JUNC_DUMP="1")
    out = subprocess.run(
        [os.path.join(ROOT, "build", "arcology"), "assets", path, "--mesh-check", "--roads3d"],
        capture_output=True,
        text=True,
        env=env,
    )
    return out.stdout


def parse(text):
    d = {"field": [], "xbld": [], "xter": [], "segs": [], "juncs": []}
    it = iter(text.split("\n"))
    cur = None
    for line in it:
        if line.startswith("FIELD "):
            n = int(line.split()[1])
            d["field"] = [[float(v) for v in next(it).split()] for _ in range(n)]
        elif line.startswith("XBLD "):
            n = int(line.split()[1])
            d["xbld"] = [[int(v, 16) for v in next(it).split()] for _ in range(n)]
        elif line.startswith("XTER "):
            n = int(line.split()[1])
            d["xter"] = [[int(v, 16) for v in next(it).split()] for _ in range(n)]
        elif line.startswith("JPOLY"):
            t = line.split()
            d["juncs"].append(
                {"col": int(t[1]), "row": int(t[2]),
                 "pts": [tuple(float(v) for v in p.split(",")) for p in t[3:]]})
        elif line.startswith("PATH"):
            cur = {"hw": float(line.split("hw=")[1]), "tiles": [], "gates": [], "pts": [], "rad": []}
            d["segs"].append(cur)
        elif cur is None:
            continue
        elif line.startswith("TILES"):
            cur["tiles"] = [tuple(int(v) for v in t.split(",")) for t in line.split()[1:]]
        elif line.startswith("GATES"):
            cur["gates"] = [tuple(float(v) for v in g.split(",")) for g in line.split()[1:]]
        elif line.startswith("PTS"):
            cur["pts"] = [tuple(float(v) for v in p.split(",")) for p in line.split()[1:]]
        elif line.startswith("RAD"):
            cur["rad"] = [float(v) for v in line.split()[1:]]
    return d


def fillet(pts, rad, steps=12):
    """The polyline with each corner replaced by its arc, as
    fillet_r builds it: a line to the tangent point, the arc, and on."""
    import math
    if len(pts) < 3:
        return pts
    out = [pts[0]]
    cur = pts[0]
    for i in range(1, len(pts) - 1):
        r = rad[i] if i < len(rad) else 0.0
        ax, ay = pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]
        bx, by = pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]
        la = math.hypot(ax, ay) or 1.0
        lb = math.hypot(bx, by) or 1.0
        ax, ay, bx, by = ax / la, ay / la, bx / lb, by / lb
        dot = max(-1.0, min(1.0, ax * bx + ay * by))
        if r <= 0.001 or dot > 0.9999:
            out.append(pts[i]); cur = pts[i]; continue
        theta = math.acos(dot)
        cross = ax * by - ay * bx
        lim = 0.5 * min(la, lb)
        d = r * math.tan(0.5 * theta)
        if d > lim:
            d = lim
            r = d / math.tan(0.5 * theta)
        t1 = (pts[i][0] - ax * d, pts[i][1] - ay * d)
        nx, ny = (-ay, ax) if cross > 0 else (ay, -ax)
        cen = (t1[0] + nx * r, t1[1] + ny * r)
        a0 = math.atan2(t1[1] - cen[1], t1[0] - cen[0])
        sweep = theta if cross > 0 else -theta
        out.append(t1)
        for k in range(1, steps + 1):
            a = a0 + sweep * k / steps
            out.append((cen[0] + r * math.cos(a), cen[1] + r * math.sin(a)))
        cur = out[-1]
    out.append(pts[-1])
    return out


def tile_colour(b, t, shade):
    """What a tile carries, tinted by how high it stands."""
    if t >= 0x10:
        base = (46, 96, 156)  # water
    elif 0x1D <= b <= 0x2B or 0x43 <= b <= 0x46:
        base = (150, 148, 142)  # a road
    elif 0x2C <= b <= 0x3A or b in (0x47, 0x48):
        base = (152, 126, 106)  # a rail
    elif 0x49 <= b <= 0x68:
        base = (140, 166, 192)  # a highway
    elif b >= 0x70:
        base = (86, 90, 98)  # a building
    elif 0x0E <= b <= 0x1C:
        base = (122, 122, 130)  # a power line
    elif b:
        base = (92, 126, 80)  # trees and the rest
    else:
        base = (104, 138, 88)  # open ground
    return tuple(min(255, int(v * shade)) for v in base)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    city, out = sys.argv[1], sys.argv[2]
    text = sys.stdin.read() if city == "-" else run(city)
    d = parse(text)
    if not d["field"]:
        sys.exit("no plan dump in the output: is build/arcology current?")
    n = len(d["xbld"])
    if len(sys.argv) >= 7:
        c0, r0, nc, nr = (int(v) for v in sys.argv[3:7])
    else:
        c0, r0, nc, nr = 0, 0, n, n
    scale = max(3, min(40, 1400 // max(nc, nr)))
    cv = Canvas(nc * scale, nr * scale)
    zs = [z for row in d["field"] for z in row]
    lo, hi = min(zs), max(zs)
    rng = (hi - lo) or 1.0

    def X(x):
        return (x - c0) * scale

    def Y(y):
        return (y - r0) * scale

    for r in range(r0, min(r0 + nr, n)):
        for c in range(c0, min(c0 + nc, n)):
            z = 0.25 * (
                d["field"][r][c] + d["field"][r][c + 1] + d["field"][r + 1][c] + d["field"][r + 1][c + 1]
            )
            shade = 0.55 + 0.75 * (z - lo) / rng
            cv.fill(X(c), Y(r), X(c + 1), Y(r + 1), tile_colour(d["xbld"][r][c], d["xter"][r][c], shade))
    if scale >= 8:
        for i in range(nc + 1):
            cv.line(X(c0 + i), 0, X(c0 + i), nr * scale, (0, 0, 0))
        for i in range(nr + 1):
            cv.line(0, Y(r0 + i), nc * scale, Y(r0 + i), (0, 0, 0))
    out_n = [0]
    segs = [s for s in d["segs"] if any(c0 <= t[0] < c0 + nc and r0 <= t[1] < r0 + nr for t in s["tiles"])]
    for s in segs:
        if scale >= 8:
            for g in s["gates"]:
                cv.line(X(g[0]), Y(g[1]), X(g[2]), Y(g[3]), (90, 150, 220), 2)
        hw, pts = s["hw"], s["pts"]
        tiles = set(s["tiles"])
        # The path as it is actually built: straight runs joined by the
        # arc each corner was given (RAD).  Drawing the raw polyline
        # instead showed sharp vertices the road does not have.
        pts = fillet(pts, s.get("rad", []))
        for i in range(len(pts) - 1):
            ax, ay, bx, by = pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]
            dx, dy = bx - ax, by - ay
            ln = (dx * dx + dy * dy) ** 0.5 or 1.0
            px, py = -dy / ln * hw, dx / ln * hw
            if scale >= 8:
                for sgn in (-1, 1):
                    # red where the band's edge has left the corridor: the
                    # road has to fit inside the tiles the segment owns
                    steps = max(2, int(ln * 8))
                    for k in range(steps):
                        t0, t1 = k / steps, (k + 1) / steps
                        e0 = (ax + dx * t0 + px * sgn, ay + dy * t0 + py * sgn)
                        e1 = (ax + dx * t1 + px * sgn, ay + dy * t1 + py * sgn)
                        # A violation is an overhang onto something that
                        # matters -- a building, another network, water --
                        # not onto the bare ground beside the corridor.
                        def occupied(e):
                            c2, r2 = int(e[0] // 1), int(e[1] // 1)
                            if (c2, r2) in tiles:
                                return False
                            if not (0 <= c2 < len(d["xbld"]) and 0 <= r2 < len(d["xbld"])):
                                return True
                            return d["xbld"][r2][c2] > 0x0D or d["xter"][r2][c2] >= 0x10
                        bad = any(occupied(e) for e in (e0, e1))
                        if bad:
                            out_n[0] += 1
                        cv.line(X(e0[0]), Y(e0[1]), X(e1[0]), Y(e1[1]),
                                (235, 60, 60) if bad else (208, 208, 208), 2 if bad else 1)
            cv.line(X(ax), Y(ay), X(bx), Y(by), (245, 220, 70), 2 if scale >= 8 else 1)
        if scale >= 8:
            for i, p in enumerate(pts):
                col = (235, 80, 70) if i < len(s["rad"]) and s["rad"][i] > 0.001 else (245, 245, 245)
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        cv.dot(X(p[0]) + dx, Y(p[1]) + dy, col)
    for j in d["juncs"]:
        if not (c0 <= j["col"] < c0 + nc and r0 <= j["row"] < r0 + nr):
            continue
        p = j["pts"]
        for i in range(len(p)):
            a, b = p[i], p[(i + 1) % len(p)]
            cv.line(X(a[0]), Y(a[1]), X(b[0]), Y(b[1]), (255, 120, 40), 2 if scale >= 8 else 1)
    write_png(out, cv.w, cv.h, cv.px)
    print(
        "%s  %dx%d  tiles %d,%d + %dx%d  %d segments  heights %.1f to %.1f  "
        "%d band samples over something occupied"
        % (out, cv.w, cv.h, c0, r0, nc, nr, len(segs), lo, hi, out_n[0])
    )


main()
