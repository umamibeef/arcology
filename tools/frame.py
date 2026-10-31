#!/usr/bin/env python3
"""A shot CENTRED ON A POINT of the world, at any height.

    python3 tools/frame.py CITY X,Y[,Z] [--zoom F] [--plan] [--outline]
                           [--angle DEG] [--pitch DEG] [--out FILE]
    python3 tools/frame.py CITY --collide PATTERN [N] [...]

`--centre` puts a TILE in the middle of the window, and a thing built
above that tile is drawn above it: eight pixels a level at zoom 1, so
a way lifted 2.7 levels over its ground at --zoom 10 lands 216 px
above the tile it stands on, and a shot centred on the tile shows grass
where the way is.  This aims the camera below the point by its height
over the ground, along the screen's own vertical (north-west in map
terms, one tile of column and row a 160 px at zoom 10), and checks the
aim with --pick: the tile under the window centre must be the one the
height was measured against.  In the map view (--plan) nothing stands
above its tile and the point is put under the centre directly.

`--collide PATTERN [N]` takes the point from the Nth line (1 by
default) of the city's collision listing that matches PATTERN, so a
conflict the mesh check names can be framed without typing its
coordinates:

    python3 tools/frame.py atlanta --collide "power line at"
    python3 tools/frame.py tokyo --collide "power line at" 3 --plan --outline
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BIN = os.path.join(ROOT, "build", "arcology")
PX_PER_LEVEL = 8.0   # the game's projection: a level is 8 px when a tile is 32
PX_PER_TILE = 16.0   # a tile's step down the screen when both column and row grow by one


def city_file(city):
    """A city by its name in cities/, or a .sc2 or .arco by its path."""
    if city.endswith(".sc2") or city.endswith(".arco"):
        return os.path.abspath(city)
    for ext in (".sc2", ".arco"):
        p = os.path.join(ROOT, "cities", city + ext)
        if os.path.exists(p):
            return p
    return os.path.join(ROOT, "cities", city + ".sc2")


def run(args):
    return subprocess.run([BIN] + args, capture_output=True, text=True, errors="replace",
                          cwd=ROOT).stdout


def collide_point(city, pattern, nth):
    out = run([city_file(city), "--mute", "--mesh-check",
               "--run", "1", "--dump-to", "-"])
    hits = [ln for ln in out.splitlines() if ln.startswith("collide   ") and pattern in ln]
    if len(hits) < nth:
        sys.exit("frame: %d collision lines match %r, not %d" % (len(hits), pattern, nth))
    ln = hits[nth - 1]
    m = re.search(r", at ([-0-9.]+),([-0-9.]+),([-0-9.]+)\s*$", ln)
    if not m:
        sys.exit("frame: no point on the line: " + ln)
    print("frame: " + ln.strip())
    return float(m.group(1)), float(m.group(2)), float(m.group(3))


def ground_level(city, col, row, zoomf, win):
    """The level of the ground at a tile, from the query tool."""
    out = run([city_file(city), "--mute", "--win", win,
               "--centre", "%d,%d" % (col, row), "--zoom", str(zoomf),
               "--pick", "%d,%d" % (int(win.split("x")[0]) // 2, int(win.split("x")[1]) // 2)])
    m = re.search(r"Mesh: .*?at (-?[0-9.]+)", out)
    return float(m.group(1)) if m else None


def pick(city, col, row, px, py, zoomf, win, extra):
    out = run([city_file(city), "--mute", "--win", win,
               "--centre", "%d,%d" % (col, row), "--zoom", str(zoomf),
               "--pick", "%d,%d" % (px, py)] + extra)
    m = re.search(r"pick\s+window [^:]*: column (-?\d+) row (-?\d+)", out)
    return (int(m.group(1)), int(m.group(2))) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("city")
    ap.add_argument("point", nargs="?", help="X,Y or X,Y,Z in tiles and levels")
    ap.add_argument("--collide", nargs="+", metavar=("PATTERN", "N"))
    ap.add_argument("--zoom", type=float, default=12.0)
    ap.add_argument("--plan", action="store_true")
    ap.add_argument("--outline", action="store_true")
    ap.add_argument("--angle", type=float)
    ap.add_argument("--pitch", type=float)
    ap.add_argument("--win", default="1280x800")
    ap.add_argument("--out", default=None)
    ap.add_argument("--edit", default=None, help="C,R[;C,R...]: the tiles bulldozed before the shot")
    ap.add_argument("--no-air", action="store_true", help="the frame with no air to see through (arcology --disable air)")
    a = ap.parse_args()

    if a.collide:
        nth = int(a.collide[1]) if len(a.collide) > 1 else 1
        x, y, z = collide_point(a.city, a.collide[0], nth)
    elif a.point:
        parts = [float(v) for v in a.point.split(",")]
        x, y = parts[0], parts[1]
        z = parts[2] if len(parts) > 2 else None
    else:
        sys.exit("frame: a point or --collide is needed")

    extra = []
    if a.plan:
        extra.append("--plan")
    if a.outline:
        extra.append("--outline")
    if a.angle is not None:
        extra += ["--angle", str(a.angle)]
    if a.pitch is not None:
        extra += ["--pitch", str(a.pitch)]
    if a.edit:
        extra += ["--edit", a.edit]
    if a.no_air:
        extra += ["--disable", "air"]
    W, H = [int(v) for v in a.win.split("x")]
    col, row = int(x), int(y)
    out = a.out or os.path.join(ROOT, "out", "frame_%s_%d_%d.png" % (a.city, col, row))
    os.makedirs(os.path.dirname(out), exist_ok=True)

    aim_c, aim_r = col, row
    if not a.plan and z is not None:
        ground = ground_level(a.city, col, row, a.zoom, a.win)
        if ground is None:
            print("frame: no ground level read at %d,%d; aiming at the tile" % (col, row))
        else:
            above = z - ground
            shift = above * PX_PER_LEVEL / PX_PER_TILE   # tiles of column and row
            aim_c = int(round(col - shift))
            aim_r = int(round(row - shift))
            print("frame: point %.2f,%.2f stands %.2f levels over ground %.1f: aiming at %d,%d"
                  % (x, y, above, ground, aim_c, aim_r))
            # the check: the target tile must lie under the window centre
            # moved down by the height's pixels
            py = H // 2 + int(round(above * PX_PER_LEVEL * a.zoom))
            got = pick(a.city, aim_c, aim_r, W // 2, min(H - 1, py), a.zoom, a.win, extra)
            print("frame: under the centre %d px down the tile is %s (wanted %d,%d)"
                  % (py - H // 2, got, col, row))
    run([city_file(a.city), "--mute", "--win", a.win,
         "--centre", "%d,%d" % (aim_c, aim_r), "--zoom", str(a.zoom), "--shot", out] + extra)
    print("frame: " + out)


if __name__ == "__main__":
    main()
