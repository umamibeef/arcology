"""land.py -- a city's land and road network, as the road fit reads them.

The land is ALTM's level and XTER's slope code per tile, read from a .arco
or a .sc2.  The network is the renderer's own topology, from
`arcology CITY --net-check --net-dump`, run through tools/arcrun.py: each end with its kind and cells,
and each road with its two ends and its cells in order.
"""
import json
import os
import struct
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

#  The corners each slope code lifts, bit 0 NW, 1 SW, 2 SE, 3 NE
#  (src/render/mesh/ground_bytes.c).
LIFTS = [0, 9, 3, 6, 12, 11, 7, 14, 13, 1, 2, 4, 8, 5]


def load(path):
    """The land: (side, level[row][col], lift mask[row][col], water[row][col])."""
    if path.endswith(".arco"):
        z = zipfile.ZipFile(path)
        w = json.loads(z.read("world.json"))
        side = max(w["bounds"]["x1"], w["bounds"]["y1"])
        cs = w["chunk_size"]
        alt = [[0] * side for _ in range(side)]
        ter = [[0] * side for _ in range(side)]
        for ch in w["chunk_list"].split(","):
            cx, cy = map(int, ch.split("_"))
            a = z.read(f"chunks/{ch}/altm.bin")
            t = z.read(f"chunks/{ch}/xter.bin")
            for i in range(cs * cs):
                x, y = cx * cs + i % cs, cy * cs + i // cs
                if x < side and y < side:
                    alt[y][x] = struct.unpack_from("<H", a, 2 * i)[0] & 0x1F
                    ter[y][x] = t[i]
    else:
        import sc2
        ch, _ = sc2.load(path)
        side = 128
        alt = [[struct.unpack_from(">H", ch[b"ALTM"], 2 * (r * side + c))[0] & 0x1F for c in range(side)] for r in range(side)]
        ter = [[ch[b"XTER"][r * side + c] for c in range(side)] for r in range(side)]
    lift = [[0] * side for _ in range(side)]
    water = [[False] * side for _ in range(side)]
    for r in range(side):
        for c in range(side):
            x = ter[r][c]
            water[r][c] = x >= 0x10
            code = x % 16 if x < 0x40 else 0
            lift[r][c] = LIFTS[code] if code <= 13 else 0
    return side, alt, lift, water


def corners(land, c, r):
    """A tile's four corner heights, keyed by grid point.  The lift bits name
    NW (c, r), SW (c, r+1), SE (c+1, r+1) and NE (c+1, r): under that mapping
    every road tile meets its neighbour at their shared edge."""
    _, alt, lift, _ = land
    a, m = alt[r][c], lift[r][c]
    nw, sw, se, ne = (a + ((m >> k) & 1) for k in range(4))
    return {(c, r): nw, (c, r + 1): sw, (c + 1, r + 1): se, (c + 1, r): ne}


def plane_at(land, c, r, x, y):
    """The height of tile (c, r)'s own slope plane at point (x, y) in it."""
    k = corners(land, c, r)
    u, v = x - c, y - r
    return ((1 - u) * (1 - v) * k[(c, r)] + u * (1 - v) * k[(c + 1, r)] +
            (1 - u) * v * k[(c, r + 1)] + u * v * k[(c + 1, r + 1)])


def network(city, out_dir):
    """The topology: (ends, roads).  ends maps an id to (kind, network, cells),
    and roads is a list of (id, network, end0, end1, cells)."""
    dump = os.path.join(out_dir, "net_" + os.path.basename(city) + ".txt")
    if not os.path.exists(dump):
        from arcrun import world
        world(os.path.join(ROOT, "build", "arcology"), [city, "--mute", "--net-check", "--net-dump"], dump=dump)
    ends, roads = {}, []
    for line in open(dump):
        f = line.split()
        if line.startswith("end "):
            ends[f[1]] = (f[2], f[3], [tuple(map(int, t.split(","))) for t in f[4:]])
        elif line.startswith("road "):
            roads.append((f[1], f[2], f[6], f[8], [tuple(map(int, t.split(","))) for t in f[9:]]))
    return ends, roads


def street_raw(land, cells, step=0.25):
    """A street's raw profile: each cell's own slope plane under its centre
    line, from its entry edge through its middle to its exit edge, as
    (s, height) samples step tiles apart."""
    pts, s, n = [], 0.0, len(cells)
    for i, (c, r) in enumerate(cells):
        cx, cy = c + 0.5, r + 0.5
        pc, pr = cells[i - 1] if i > 0 else (2 * c - cells[i + 1][0], 2 * r - cells[i + 1][1])
        nc, nr = cells[i + 1] if i < n - 1 else (2 * c - cells[i - 1][0], 2 * r - cells[i - 1][1])
        ex, ey = cx + (pc - c) * 0.5, cy + (pr - r) * 0.5
        xx, xy = cx + (nc - c) * 0.5, cy + (nr - r) * 0.5
        t = 0.0
        while t < 1.0 - 1e-9:
            if t < 0.5:
                u = t / 0.5
                x, y = ex + (cx - ex) * u, ey + (cy - ey) * u
            else:
                u = (t - 0.5) / 0.5
                x, y = cx + (xx - cx) * u, cy + (xy - cy) * u
            pts.append([s + t, plane_at(land, c, r, x, y)])
            t += step
        s += 1.0
    c, r = cells[-1]
    pts.append([s, plane_at(land, c, r, xx, xy)])
    return pts


def dry(land, cells):
    """Whether no cell of the road is water.  A bridge's reading is not
    written, so a road over water is left out."""
    side, _, _, water = land
    return all(0 <= c < side and 0 <= r < side and not water[r][c] for c, r in cells)
