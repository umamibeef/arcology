"""net.py -- the topology as the drawing sees it: roads, chains and junctions.

A road's CHAIN is its cells with the junction cell it meets at each end.  A
seam is a chunk's border, which the drawing never sees, so the roads that
meet at one are joined into one road.  A junction's surface is the outline
round its roads' mouths, where each road's centre line leaves its squares.
"""
import math

import land as ld
import plan
from knobs import NET


def chain(ends, net, e0, e1, cells):
    size = NET[net]["unit"]
    def touch(eid, cell):
        #  A seam or the map's edge has no cell of its own: the road runs on
        #  to the cut, and nothing is added.
        if eid not in ends or ends[eid][0] in ("seam", "map-edge"):
            return None
        for c in ends[eid][2]:
            if abs(c[0] - cell[0]) + abs(c[1] - cell[1]) == size:
                return c
        return None
    a = touch(e0, cells[0]) if cells else None
    b = touch(e1, cells[-1]) if cells else None
    return ([a] if a else []) + cells + ([b] if b else [])


def inside(p, squares, size):
    return any(c <= p[0] <= c + size and r <= p[1] <= r + size for c, r in squares)


def hull(points):
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0:
            lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cross(up[-2], up[-1], p) <= 0:
            up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]


def junction(ends, roads, eid):
    """A junction with its arms: each arm cut at its mouth, and the surface."""
    kind, net, jcells = ends[eid]
    size = NET[net]["unit"]
    arms, mouths = [], []
    for rid, rnet, e0, e1, cells in roads:
        if eid not in (e0, e1) or rnet != net or not cells:
            continue
        ch = chain(ends, rnet, e0, e1, cells)
        f = plan.fit(rnet, ch)
        path, L, R = f["path"], f["left"], f["right"]
        if e1 == eid:  # walk from the junction's side
            path, L, R = path[::-1], R[::-1], L[::-1]
        m = 0
        while m < len(path) - 1 and inside(path[m], jcells, size):
            m += 1
        mouths += [L[m], R[m]]
        near = cells[::-1] if e1 == eid else cells  # the arm's cells from the junction outward
        arms.append({"cells": near, "left": L[m:], "right": R[m:], "angles": f["angles"], "out": f["out"]})
    return {"id": eid, "kind": kind, "net": net, "cells": jcells, "size": size, "arms": arms, "surface": hull(mouths)}


def pick(ends, roads):
    """Junctions to show: street T and cross junctions, and highway junctions."""
    out, seen = [], set()
    def arms_of(eid):
        return sum(1 for _, n, e0, e1, c in roads if eid in (e0, e1) and c and n == ends[eid][1])
    for want_net, want_arms, count in (("road", 3, 3), ("road", 4, 3), ("band", 3, 2), ("band", 4, 2), ("band", 2, 2)):
        for eid, (kind, net, cells) in ends.items():
            if net == want_net and kind == "node" and eid not in seen and arms_of(eid) == want_arms:
                out.append(eid); seen.add(eid)
                if sum(1 for e in out if ends[e][1] == want_net and arms_of(e) == want_arms) >= count:
                    break
    return out


def join_seams(ends, roads):
    """Join the roads that meet at a seam into one road: a seam is a chunk's
    border, which the drawing never sees.  A road whose two ends are one seam
    is a closed ring, opened again in the middle of its longest straight."""
    roads = [list(r) for r in roads]
    changed = True
    while changed:
        changed = False
        for i, a in enumerate(roads):
            for end_a in (2, 3):
                eid = a[end_a]
                if ends.get(eid, ("",))[0] != "seam":
                    continue
                if a[2] == a[3]:  # a ring on its own seam
                    continue
                for j, b in enumerate(roads):
                    if j == i or b[1] != a[1] or eid not in (b[2], b[3]):
                        continue
                    ca = a[4] if end_a == 3 else a[4][::-1]           # a, ending at the seam
                    other_a = a[2] if end_a == 3 else a[3]
                    cb = b[4] if b[2] == eid else b[4][::-1]           # b, starting at the seam
                    other_b = b[3] if b[2] == eid else b[2]
                    roads[i] = [a[0], a[1], other_a, other_b, ca + cb]
                    del roads[j]
                    changed = True
                    break
                if changed:
                    break
            if changed:
                break
    out = []
    for r in roads:
        if r[2] == r[3] and ends.get(r[2], ("",))[0] == "seam":
            cells = r[4]
            n = len(cells)
            # open the ring halfway along its longest straight
            def straight_at(k):
                a, b, c = cells[k - 1], cells[k], cells[(k + 1) % n]
                return (b[0] - a[0], b[1] - a[1]) == (c[0] - b[0], c[1] - b[1])
            best, k = -1, 0
            for start in range(n):
                if straight_at(start) and not straight_at((start - 1) % n):
                    length = 0
                    while length < n and straight_at((start + length) % n):
                        length += 1
                    if length > best:
                        best, k = length, (start + length // 2) % n
            cells = cells[k:] + cells[:k] + [cells[k]]
            r = [r[0], r[1], "ring", "ring", cells]
        out.append(tuple(r))
    return out


def unit_ground(land, net, cells):
    """The highest ground under each unit of a road, as (s, height): s runs
    along the road from its first unit's centre, plus half a unit."""
    size = NET[net]["unit"]
    side = land[0]
    out, s, prev = [], size / 2, None
    for c, r in cells:
        if prev:
            s += math.hypot(c - prev[0], r - prev[1])
        top = max(max(ld.corners(land, c + dc, r + dr).values())
                  for dc in range(size) for dr in range(size) if c + dc < side and r + dr < side)
        out.append([s, top])
        prev = (c, r)
    return out
