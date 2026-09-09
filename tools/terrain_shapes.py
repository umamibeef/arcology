#!/usr/bin/env python3
"""The fourteen slope shapes as geometry, read from the sprites.

XTER's low nibble is a corner mask (A5-0x4DEE, inverted here), and a lifted
corner is one altitude step up.  That gives four corner heights per code;
what it does not give is how the game's artists cut the non-planar tiles
into faces, and that decides the silhouette.  This script builds each code
as faces -- the top, split along one diagonal or the other, and the two
walls under the near edges -- projects it exactly as soft.c does, and
compares the union of the projected faces against the sprite's opaque
mask.  The diagonal that reproduces the sprite is the one the mesh uses.

    python3 tools/terrain_shapes.py            # print the table, write assets/terrain-shapes.json

The projection.  A flat tile is a diamond tw wide and th+1 rows tall with
a one-row belt at its widest, i.e. the hexagon
    (tw/2, 0)  (0, th/2)  (0, th/2+1)  (tw/2, th+1)  (tw, th/2+1)  (tw, th/2)
and a lifted corner moves its vertex up by alt_step.  A pixel belongs to a
face when its centre is inside the projected polygon.
"""
import json, sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from sc2kpack import read_indexed_png

ROOT = HERE.parent
ASSETS = ROOT / "assets"

SLOPE_CODE = [0, 9, 10, 2, 11, 13, 3, 6, 12, 1, 13, 5, 4, 8, 7, 0x32]   # A5-0x4DEE
MASKS = defaultdict(list)
for _m, _c in enumerate(SLOPE_CODE):
    MASKS[_c].append(_m)
CORNER = ("NW", "SW", "SE", "NE")            # bit 0..3


def levels(mask):
    return {n: (mask >> i) & 1 for i, n in enumerate(CORNER)}


def vertices(tw, th, alt, lv):
    """Screen-space corner positions, in the sprite's frame with the flat
    diamond's top row at y=0.  The side corners are a one-row belt."""
    return {
        "NW": [(tw / 2, 0 - lv["NW"] * alt)],
        "NE": [(0, th / 2 - lv["NE"] * alt), (0, th / 2 + 1 - lv["NE"] * alt)],
        "SW": [(tw, th / 2 - lv["SW"] * alt), (tw, th / 2 + 1 - lv["SW"] * alt)],
        "SE": [(tw / 2, th + 1 - lv["SE"] * alt)],
    }


def base_vertices(tw, th):
    return vertices(tw, th, 0, {n: 0 for n in CORNER})


def inside(poly, px, py):
    """Even-odd test of a point against a polygon; points on the boundary
    count as inside."""
    n, hit = len(poly), False
    for i in range(n):
        x0, y0 = poly[i]; x1, y1 = poly[(i + 1) % n]
        if (y0 > py) != (y1 > py):
            xi = x0 + (py - y0) * (x1 - x0) / (y1 - y0)
            if px < xi:
                hit = not hit
            elif abs(px - xi) < 1e-9:
                return True
        elif y0 == py == y1 and min(x0, x1) - 1e-9 <= px <= max(x0, x1) + 1e-9:
            return True
    return hit


def faces(tw, th, alt, lv, diagonal):
    """The polygons of one tile: the top cut along `diagonal` ("NW-SE" or
    "NE-SW") and the walls under the two near edges (NE-SE and SE-SW),
    each dropping to the base."""
    v = vertices(tw, th, alt, lv)
    b = base_vertices(tw, th)
    top = []
    if diagonal == "NW-SE":
        top.append(v["NW"] + v["NE"] + v["SE"])                 # west triangle
        top.append(v["NW"] + v["SE"] + list(reversed(v["SW"]))) # east triangle
    else:
        top.append(v["NE"] + v["SE"] + list(reversed(v["SW"])))  # near triangle
        top.append(v["NW"] + v["NE"] + list(reversed(v["SW"])))  # far triangle
    walls = [
        v["NE"] + v["SE"] + list(reversed(b["SE"])) + list(reversed(b["NE"])),   # left-near edge down to base
        v["SE"] + list(reversed(v["SW"])) + b["SW"] + list(reversed(b["SE"])),   # right-near edge down to base
    ]
    return top, walls


def raster(polys, w, h, dy):
    """Pixels (x, y) of the sprite frame whose centre lies in any polygon.
    dy shifts the polygons down so the flat diamond's top row is at dy."""
    out = set()
    for y in range(h):
        for x in range(w):
            px, py = x + 0.5, y + 0.5 - dy
            for p in polys:
                if inside(p, px, py):
                    out.add((x, y)); break
    return out


def sprite_mask(sheet, rows, tid, transparent):
    f = sheet[str(tid)]["frame"]; x0, y0, w, h = f["x"], f["y"], f["w"], f["h"]
    return w, h, {(x, y) for y in range(h) for x in range(w) if rows[y0 + y][x0 + x] != transparent}


def main():
    atlas = json.loads((ASSETS / "atlas.json").read_text())
    table = {"corner_bits": list(CORNER), "codes": {}, "zooms": {}}
    for z in atlas["zooms"]:
        sheet_j = json.loads((ASSETS / z["sheet"]).read_text())
        meta = sheet_j["meta"]["sc2k"]; sheet = sheet_j["frames"]
        tw, th, alt, base = meta.get("tile_w", meta["zoom"]), meta.get("tile_h", meta["zoom"] // 2), meta.get("alt_step", 12), meta.get("id_base", 0)
        transparent = meta.get("transparent", 0)
        W, H, rows, pal = read_indexed_png(ASSETS / z["image"])
        zr = {"tile_w": tw, "tile_h": th, "alt_step": alt, "codes": {}}
        print(f"\n== {meta['zoom']} px: tile {tw}x{th}, {alt} px per level ==")
        print(f"{'code':>4} {'sprite':>6} {'size':>7}  {'NW SW SE NE':<12} {'NW-SE cut':>10} {'NE-SW cut':>10}  chosen")
        for code in range(14):
            tid = base + 256 + code
            w, h, mask = sprite_mask(sheet, rows, tid, transparent)
            dy = h - (th + 1)                      # the flat diamond's top row inside this sprite
            best = None
            for m in MASKS[code]:
                lv = levels(m)
                for diag in ("NW-SE", "NE-SW"):
                    top, walls = faces(tw, th, alt, lv, diag)
                    got = raster(top + walls, w, h, dy)
                    diff = len(got ^ mask)
                    if best is None or diff < best[0]:
                        best = (diff, m, diag, len(got - mask), len(mask - got))
            results = {}
            for m in MASKS[code]:
                lv = levels(m)
                for diag in ("NW-SE", "NE-SW"):
                    top, walls = faces(tw, th, alt, lv, diag)
                    results[(m, diag)] = len(raster(top + walls, w, h, dy) ^ mask)
            diff, m, diag, extra, missing = best
            lv = levels(m)
            row = " ".join(f"{lv[n]:>2}" for n in CORNER)
            per = {d: min(results[(mm, d)] for mm in MASKS[code]) for d in ("NW-SE", "NE-SW")}
            chosen = diag if per["NW-SE"] != per["NE-SW"] else "either"
            print(f"{code:>4} {tid:>6} {w:>3}x{h:<3}  {row:<12} {per['NW-SE']:>10} {per['NE-SW']:>10}  {chosen}{'' if diff == 0 else f'   (best still differs by {diff}: +{extra} -{missing})'}")
            zr["codes"][code] = {"sprite": tid, "w": w, "h": h, "diff": {d: per[d] for d in per}, "chosen": chosen, "residual": diff}
            table["codes"].setdefault(code, {"masks": MASKS[code], "levels": [levels(mm) for mm in MASKS[code]]})
        table["zooms"][str(meta["zoom"])] = zr
    out = ASSETS / "terrain-shapes.json"
    out.write_text(json.dumps(table, indent=1))
    print(f"\nwrote {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
