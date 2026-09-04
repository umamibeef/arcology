#!/usr/bin/env python3
"""profile.py -- draw a corridor's grade, side on.

    SC2K_PROF_DUMP=1 build/arcology assets <city> --mesh-check --roads3d \
        | python3 tools/profile.py out.png [min-climb-in-levels]

One panel per segment, tallest climb first: the ground under the band in
brown, the height the band was given in yellow.  Where yellow runs above
brown the road is on fill and carries an embankment; where it runs below,
it is in a cut and the terrain is levelled to it behind retaining walls.
This is how the grade smoothing is read (the user, 3 September 2026: "I
want to see how the smoothing of grade is happening").
"""
import sys, zlib, struct


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
    def __init__(self, w, h, bg=(22, 24, 26)):
        self.w, self.h = w, h
        self.px = [bytearray(bg * w) for _ in range(h)]

    def dot(self, x, y, c):
        xi, yi = int(round(x)), int(round(y))
        if 0 <= xi < self.w and 0 <= yi < self.h:
            self.px[yi][xi * 3 : xi * 3 + 3] = bytes(c)

    def line(self, x0, y0, x1, y1, c, wide=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 2
        for i in range(n + 1):
            t = i / n
            for d in range(-(wide // 2), wide // 2 + 1):
                self.dot(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + d, c)

    def vline(self, x, y0, y1, c):
        if y1 < y0:
            y0, y1 = y1, y0
        y = y0
        while y <= y1:
            self.dot(x, y, c)
            y += 1


def main():
    out = sys.argv[1]
    floor = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
    segs, cur = [], None
    for line in sys.stdin:
        if line.startswith("PROF"):
            cur = {"head": line.split(), "pts": []}
            segs.append(cur)
        elif cur is not None and line.startswith("  "):
            f = line.split()
            try:
                if len(f) >= 3:
                    cur["pts"].append(tuple(float(v) for v in f[:3]))
            except ValueError:
                pass  # another dump's line, not a station
    segs = [s for s in segs if len(s["pts"]) > 3]
    for s in segs:
        zs = [p[2] for p in s["pts"]]
        gs = [p[1] for p in s["pts"]]
        s["climb"] = max(zs) - min(zs)
        s["fill"] = max(z - g for g, z in zip(gs, zs))
        s["cut"] = max(g - z for g, z in zip(gs, zs))
    segs = [s for s in segs if s["climb"] >= floor]
    segs.sort(key=lambda s: -s["climb"])
    segs = segs[:6]
    if not segs:
        print("no segment climbs %.1f levels" % floor, file=sys.stderr)
        return 1
    pw, ph, pad = 900, 150, 26
    cv = Canvas(pw + 2 * pad, len(segs) * (ph + pad) + pad)
    for i, s in enumerate(segs):
        y0 = pad + i * (ph + pad)
        L = s["pts"][-1][0] or 1.0
        lo = min(min(p[1] for p in s["pts"]), min(p[2] for p in s["pts"])) - 0.2
        hi = max(max(p[1] for p in s["pts"]), max(p[2] for p in s["pts"])) + 0.2
        rng = (hi - lo) or 1.0

        def X(sv):
            return pad + sv / L * pw

        def Y(z):
            return y0 + ph - (z - lo) / rng * ph

        for k in range(int(L) + 1):  # a tick every tile
            cv.vline(X(k), y0 + ph, y0 + ph + 3, (70, 74, 78))
        for k in range(int(lo) + 1, int(hi) + 1):  # a line every level
            cv.line(pad, Y(k), pad + pw, Y(k), (44, 48, 52))
        for k in range(len(s["pts"]) - 1):
            a, b = s["pts"][k], s["pts"][k + 1]
            cv.line(X(a[0]), Y(a[1]), X(b[0]), Y(b[1]), (150, 105, 60), 2)  # the ground
            cv.line(X(a[0]), Y(a[2]), X(b[0]), Y(b[2]), (240, 215, 80), 2)  # the band
        print(
            "segment %d: %.1f tiles, climb %.2f levels, max fill %.2f, max cut %.2f"
            % (i, L, s["climb"], s["fill"], s["cut"])
        )
    write_png(out, cv.w, cv.h, cv.px)
    print("%s  %dx%d  %d panels" % (out, cv.w, cv.h, len(segs)))
    return 0


sys.exit(main())
