"""Checks that hold against the city data, not against another renderer.

Two earlier tests compared this renderer against another renderer of ours
-- first tools/render.py, then a Python twin -- and called agreement
success.  Both agreed exactly while both were wrong.  A test that compares
two renderers can only find disagreement.

These check the renderer against what is in the city file.  For checks
against the GAME, see tools/render_diff.py (blit list), tools/pixel_diff.py
(pixels) and tools/blit_check.py (the blitter itself).
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))


_XTER_TILE = None


def xter_table():
    """The word table at A5-0x493E that $167CC indexes with 2*XTER.
    Read from the binary's own global image, never hand-typed."""
    global _XTER_TILE
    if _XTER_TILE is None:
        from thinkdata import build, bytesat
        raw = bytesat(build(), -0x493E, 256 * 2)
        _XTER_TILE = [(raw[i] << 8) | raw[i + 1] for i in range(0, len(raw), 2)]
    return _XTER_TILE


def terrain_tile(xter):
    """XTER -> terrain tile id, or 0 for nothing.  The table runs past the
    terrain values into neighbouring data, so a value that is not a tile id
    draws nothing; the 103 shipped cities never exceed XTER 0x45."""
    t = xter_table()[xter]
    return 0 if (t == 0 or t >= 500) else t


def tile_alt(altm, xter):
    """ALTM carries two heights.  Dry land uses bits 0..4; water is drawn at
    the water table in bits 5..9 -- $16862 does lsr.w #5 then andi #$1f on
    the water branch.  Drawing water at the ground height sinks every lake
    into its own bed."""
    return (altm & 0x1F) if xter < 0x10 else ((altm >> 5) & 0x1F)


def draws_here(xzon, xbld, mask):
    """The game's test is (XZON & 0xF0) & g_rotTable[rotation], but it is
    only reached for some XBLD ranges: roads, rail, power lines and trees
    (XBLD < 0x61) never see it and carry no corner bits.  Large buildings
    put corner bits only on their four corners, so a 3x3 has five interior
    tiles with a zero nibble and a 4x4 has twelve -- those must not draw."""
    corners = xzon & 0xF0
    if corners & mask:
        return True
    return corners == 0 and xbld < 0x70


def check(city_path, assets, png_rgb=None):
    """Returns a list of failure strings; empty means every invariant held."""
    import struct

    from sc2 import load as city_load

    have = {int(k) - 1000 for k in
            json.loads((Path(assets) / "tiles32.json").read_text())["frames"]}
    ch, _ = city_load(city_path)
    XB, XT, XZ, BI = ch[b"XBLD"], ch[b"XTER"], ch[b"XZON"], ch[b"XBIT"]
    rot = struct.unpack(">1200i", ch[b"MISC"])[2] & 3
    mask = [0x80, 0x10, 0x20, 0x40][rot]
    bad = []

    # 1. every terrain value resolves to art that exists
    miss = sorted({t for t in map(terrain_tile, set(XT)) if t not in have})
    if miss:
        bad.append("terrain ids with no art: %s" % miss[:8])

    # 2. every building that will be drawn resolves to art that exists
    miss = sorted({b for b in set(XB) if b and b not in have})
    if miss:
        bad.append("XBLD ids with no art: %s" % miss[:8])

    # 3. nothing unzoned is ever discarded.  This is the one that would
    #    have caught the original bug: roads and trees carry corner nibble
    #    0, and the old gate threw every one of them away.
    dropped_unzoned = sum(1 for i in range(len(XB))
                          if XB[i] and XB[i] < 0x70 and (XZ[i] & 0xF0) == 0 and not draws_here(XZ[i], XB[i], mask))
    if dropped_unzoned:
        bad.append("%d unzoned built tiles discarded" % dropped_unzoned)

    # 4. every zone building is drawn exactly once, at whichever tile
    #    carries the corner bit for the current rotation.  A well-formed
    #    building has exactly one tile with each of the four corner bits,
    #    so the count of drawn tiles must be identical for all four
    #    rotations -- and the interior tiles of a 3x3 or 4x4, which carry
    #    no corner bit at all, must never draw.
    per_rot = [sum(1 for i in range(len(XB))
                   if XB[i] >= 0x70 and (XZ[i] & m))
               for m in (0x80, 0x10, 0x20, 0x40)]
    if len(set(per_rot)) != 1:
        bad.append("zone buildings draw %s times depending on rotation; "
                   "each should draw once" % per_rot)
    interior = sum(1 for i in range(len(XB))
                   if XB[i] >= 0x70 and (XZ[i] & 0xF0) == 0
                   and draws_here(XZ[i], XB[i], mask))
    if interior:
        bad.append("%d interior tiles of large buildings were drawn "
                   "(a 3x3 has five, a 4x4 twelve)" % interior)
    drawn_zone = sum(1 for i in range(len(XB))
                     if XB[i] >= 0x70 and draws_here(XZ[i], XB[i], mask))
    if drawn_zone != per_rot[0]:
        bad.append("drew %d zone-building tiles, expected %d"
                   % (drawn_zone, per_rot[0]))

    # 5. open water actually renders as water
    if png_rgb is not None:
        w, h, px = png_rgb
        TW, TH, ALT = 32, 16, 12
        n = 128
        ox, oy = n * TW // 2, 200
        alt = [struct.unpack(">H", ch[b"ALTM"][i * 2:i * 2 + 2])[0] & 0x1F
               for i in range(n * n)]
        tot = blue = 0
        #  Two things this has to respect.  The shore variants (low nibble
        #  1..13) are legitimately teal and green art, so only flat open
        #  water -- low nibble 0 -- is asserted on.  And the water tile is
        #  a dithered pattern, so a single pixel lands dark about a fifth
        #  of the time: average a patch rather than point-sampling.
        #  Sample above the centre row: neighbouring tiles are drawn eight
        #  rows lower and their top vertex covers the centre column.
        probes = ((8, 3), (12, 4), (16, 4), (20, 4), (24, 3),
                  (10, 6), (16, 6), (22, 6))
        for y in range(1, n - 1):
            for x in range(1, n - 1):
                i = y * n + x
                if XB[i] or not (BI[i] & 4):
                    continue
                if XT[i] < 0x10 or (XT[i] & 0x0F) != 0:
                    continue
                if not all(BI[i + d] & 4 and not XB[i + d]
                           for d in (-1, 1, -n, n, -n - 1, -n + 1, n - 1, n + 1)):
                    continue
                #  Art extends upward from its anchor, and tiles with a
                #  larger x+y are drawn lower, so a tall building several
                #  tiles "in front" covers this one.  An arcology rises 219
                #  px, about 13 tile-steps.  Only assert on water nothing
                #  can overdraw, or the check fails on correct renders of
                #  dense cities -- which is what Tokyo was telling us.
                if any(XB[(y + dy) * n + (x + dx)]
                       for dy in range(0, 14) for dx in range(0, 14)
                       if x + dx < n and y + dy < n):
                    continue
                sx = ox + (y - x) * (TW // 2)  # (row - col), $167EE
                sy = oy + (x + y) * (TH // 2) - alt[i] * ALT
                acc = [0, 0, 0]
                cnt = 0
                for dx, dy in probes:
                    px_, py_ = sx + dx, sy + dy
                    if not (0 <= px_ < w and 0 <= py_ < h):
                        continue
                    o = (py_ * w + px_) * 3
                    acc[0] += px[o]
                    acc[1] += px[o + 1]
                    acc[2] += px[o + 2]
                    cnt += 1
                if not cnt:
                    continue
                r, g, b = (v / cnt for v in acc)
                tot += 1
                if b > r + 10 and b > g:
                    blue += 1
        if tot >= 50 and blue * 100 < tot * 95:
            bad.append("flat open water drew blue in only %d of %d tiles"
                       % (blue, tot))
    return bad
