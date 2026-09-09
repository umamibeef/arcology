#!/usr/bin/env python3
"""Where do the GPU frame and the software frame disagree?

    python3 tools/gpu_diff.py gpu.png soft.png [diff.png]

Both files are 8-bit RGB PNGs of the same size, as arcology --check writes
them.  Prints the count of differing pixels, their bounding box, and the
commonest (software, gpu) colour pairs -- which is usually enough to name
the cause -- and writes a picture with the differing pixels in red over a
dimmed copy of the software frame.  Standard library only.
"""
import struct, sys, zlib
from collections import Counter


def read_png(path):
    b = open(path, "rb").read()
    assert b[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w, h, ctype = 8, b"", 0, 0, 0
    while pos < len(b):
        n = struct.unpack(">I", b[pos:pos + 4])[0]
        tag, dat = b[pos + 4:pos + 8], b[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", dat[:10])
            assert depth == 8 and ctype == 2, "want 8-bit RGB"
        elif tag == b"IDAT":
            idat += dat
    raw = zlib.decompress(idat)
    bpp, stride = 3, w * 3
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            up = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + up) & 255
            elif f == 3:
                line[x] = (line[x] + (a + up) // 2) & 255
            elif f == 4:
                p = a + up - c
                pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (up if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    w, h, g = read_png(sys.argv[1])
    w2, h2, s = read_png(sys.argv[2])
    assert (w, h) == (w2, h2), "sizes differ"
    n, pairs = 0, Counter()
    x0, y0, x1, y1 = w, h, -1, -1
    out = []
    for y in range(h):
        gr, sr = g[y], s[y]
        line = bytearray(w * 3)
        for x in range(w):
            k = x * 3
            gp, sp = gr[k:k + 3], sr[k:k + 3]
            if gp != sp:
                n += 1
                pairs[(tuple(sp), tuple(gp))] += 1
                x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
                line[k:k + 3] = b"\xff\x00\x00"
            else:
                line[k:k + 3] = bytes(v // 3 for v in sp)
        out.append(bytes(line))
    print("%d of %d pixels differ (%.4f%%)" % (n, w * h, 100.0 * n / (w * h)))
    if n:
        print("bounding box x %d..%d, y %d..%d" % (x0, x1, y0, y1))
        print("commonest (software -> gpu) pairs:")
        for (sp, gp), c in pairs.most_common(8):
            print("  %-16s -> %-16s %d" % (str(sp), str(gp), c))
    if len(sys.argv) > 3:
        write_png(sys.argv[3], w, h, out)
        print("wrote", sys.argv[3])


if __name__ == "__main__":
    main()
