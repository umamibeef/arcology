#!/usr/bin/env python3
"""pict.py -- decode the game's QuickDraw PICT resources to PNG.

The interface art -- the tool palette, the status bar, the demand bars,
the newspaper mastheads, the budget panels, the growth icons -- is PICT
resources in the application's resource fork, all version 2 pictures.
This walks the opcodes and composites every PackBitsRect / PackBitsRgn
(indexed, with its own colour table) and DirectBitsRect / DirectBitsRgn
(16- or 32-bit) onto a canvas the size of the picture's frame.  Drawing
opcodes other than pixel maps (lines, rects, text) are skipped: the
game's pictures are pixel maps, and a picture that draws anything else is
reported so it can be looked at.

    python3 tools/pict.py rsrc/sc2k.rsrc out/pict          # all of them
    python3 tools/pict.py rsrc/sc2k.rsrc out/pict 500 505  # some
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rezfork  # noqa: E402
from gpu_diff import write_png  # noqa: E402


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack(">H", self.d[self.p:self.p + 2])[0]
        self.p += 2
        return v

    def s16(self):
        v = struct.unpack(">h", self.d[self.p:self.p + 2])[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack(">I", self.d[self.p:self.p + 4])[0]
        self.p += 4
        return v

    def rect(self):
        t, l, b, r = struct.unpack(">hhhh", self.d[self.p:self.p + 8])
        self.p += 8
        return t, l, b, r

    def skip(self, n):
        self.p += n

    def align(self):
        if self.p & 1:
            self.p += 1


def unpack_bits(src, out_len):
    """PackBits: one row of bytes."""
    out = bytearray()
    i = 0
    n = len(src)
    while i < n and len(out) < out_len:
        c = src[i]
        i += 1
        if c < 128:
            out += src[i:i + c + 1]
            i += c + 1
        elif c > 128:
            out += bytes([src[i]]) * (257 - c)
            i += 1
    return bytes(out[:out_len])


def unpack_words(src, out_words):
    """PackBits over 16-bit words (packType 3)."""
    out = []
    i = 0
    n = len(src)
    while i < n and len(out) < out_words:
        c = src[i]
        i += 1
        if c < 128:
            k = c + 1
            out += list(struct.unpack(">%dH" % k, src[i:i + 2 * k]))
            i += 2 * k
        elif c > 128:
            w = struct.unpack(">H", src[i:i + 2])[0]
            i += 2
            out += [w] * (257 - c)
    return out[:out_words]


def read_pixmap(r, has_base):
    if has_base:
        r.u32()
    row_bytes = r.u16()
    is_pixmap = bool(row_bytes & 0x8000)
    row_bytes &= 0x3FFF
    bounds = r.rect()
    pm = {"row_bytes": row_bytes, "bounds": bounds, "pixmap": is_pixmap,
          "pixel_size": 1, "cmp_count": 1, "pack_type": 0}
    if is_pixmap:
        r.u16()                      # pmVersion
        pm["pack_type"] = r.u16()
        r.u32()                      # packSize
        r.u32()                      # hRes
        r.u32()                      # vRes
        r.u16()                      # pixelType
        pm["pixel_size"] = r.u16()
        pm["cmp_count"] = r.u16()
        r.u16()                      # cmpSize
        r.u32()                      # planeBytes
        r.u32()                      # pmTable
        r.u32()                      # pmReserved
    return pm


def read_clut(r):
    r.u32()                          # ctSeed
    r.u16()                          # ctFlags
    n = r.u16() + 1
    table = {}
    for k in range(n):
        v = r.u16()
        red, green, blue = r.u16(), r.u16(), r.u16()
        table[k] = (red >> 8, green >> 8, blue >> 8)
        table.setdefault(("val", v), (red >> 8, green >> 8, blue >> 8))
    return table


def read_region(r):
    size = r.u16()
    r.skip(size - 2)


def read_rows(r, pm, height):
    """The packed rows of a pixel map, as unpacked byte rows."""
    rb = pm["row_bytes"]
    rows = []
    for _ in range(height):
        if rb < 8:
            rows.append(r.d[r.p:r.p + rb])
            r.p += rb
            continue
        n = r.u16() if rb > 250 else r.u8()
        chunk = r.d[r.p:r.p + n]
        r.p += n
        if pm["pack_type"] == 1:
            rows.append(chunk)
        elif pm["pack_type"] == 3:
            rows.append(unpack_words(chunk, rb // 2))
        else:
            rows.append(unpack_bits(chunk, rb))
    return rows


def blit_indexed(canvas, fw, fh, frame, pm, clut, src_rect, dst_rect, rows):
    t, l, b, rr = pm["bounds"]
    w = rr - l
    ps = pm["pixel_size"]
    ft, fl = frame[0], frame[1]
    dt, dl = dst_rect[0] - ft, dst_rect[1] - fl
    st, sl = src_rect[0] - t, src_rect[1] - l
    sh = src_rect[2] - src_rect[0]
    sw = src_rect[3] - src_rect[1]
    for y in range(sh):
        sy = st + y
        if sy < 0 or sy >= len(rows):
            continue
        row = rows[sy]
        for x in range(sw):
            sx = sl + x
            if sx < 0 or sx >= w:
                continue
            if ps == 8:
                idx = row[sx] if sx < len(row) else 0
            elif ps == 4:
                byte = row[sx // 2] if sx // 2 < len(row) else 0
                idx = (byte >> 4) if (sx & 1) == 0 else (byte & 15)
            elif ps == 2:
                byte = row[sx // 4] if sx // 4 < len(row) else 0
                idx = (byte >> (6 - 2 * (sx & 3))) & 3
            else:
                byte = row[sx // 8] if sx // 8 < len(row) else 0
                idx = (byte >> (7 - (sx & 7))) & 1
                if not pm["pixmap"]:
                    # a bitmap: 1 is black
                    rgb = (0, 0, 0) if idx else (255, 255, 255)
                    put(canvas, fw, fh, dl + x, dt + y, rgb)
                    continue
            rgb = clut.get(idx, (255, 0, 255))
            put(canvas, fw, fh, dl + x, dt + y, rgb)


def blit_direct(canvas, fw, fh, frame, pm, src_rect, dst_rect, rows):
    t, l, b, rr = pm["bounds"]
    w = rr - l
    ps = pm["pixel_size"]
    ft, fl = frame[0], frame[1]
    dt, dl = dst_rect[0] - ft, dst_rect[1] - fl
    st, sl = src_rect[0] - t, src_rect[1] - l
    sh = src_rect[2] - src_rect[0]
    sw = src_rect[3] - src_rect[1]
    for y in range(sh):
        sy = st + y
        if sy < 0 or sy >= len(rows):
            continue
        row = rows[sy]
        for x in range(sw):
            sx = sl + x
            if sx < 0 or sx >= w:
                continue
            if ps == 16:
                v = row[sx] if sx < len(row) else 0
                rgb = (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31,
                       (v & 31) * 255 // 31)
            else:
                # 32-bit, packed by component planes: cmp_count planes of w
                cc = pm["cmp_count"]
                base = (cc - 3) * w
                rgb = (row[base + sx], row[base + w + sx], row[base + 2 * w + sx])
            put(canvas, fw, fh, dl + x, dt + y, rgb)


def put(canvas, fw, fh, x, y, rgb):
    if 0 <= x < fw and 0 <= y < fh:
        canvas[y][x] = rgb


# Reserved opcode data lengths, by range (Inside Macintosh: Imaging).
def reserved_len(op, r):
    if 0x0100 <= op <= 0x01FF:
        return 2
    if 0x0200 <= op <= 0x02FF:
        return 4
    if 0x0300 <= op <= 0x0BFF:
        return 2 * ((op >> 8) & 0xFF)
    if 0x0C00 <= op <= 0x0CFF:
        return 24
    if 0x0D00 <= op <= 0x7EFF:
        return 2 * ((op >> 8) & 0xFF)
    if 0x7F00 <= op <= 0x7FFF:
        return 254
    if 0x8000 <= op <= 0x80FF:
        return 0
    if 0x8100 <= op <= 0xFFFF:
        return r.u32()
    return None


FIXED = {
    0x0000: 0, 0x0003: 2, 0x0004: 2, 0x0005: 2, 0x0006: 4, 0x0007: 4,
    0x0008: 2, 0x0009: 8, 0x000A: 8, 0x000B: 4, 0x000C: 4, 0x000D: 2,
    0x000E: 4, 0x000F: 4, 0x0010: 8, 0x0011: 2, 0x0015: 2, 0x0016: 2,
    0x0017: 0, 0x0018: 0, 0x0019: 0, 0x001A: 6, 0x001B: 6, 0x001C: 0,
    0x001D: 6, 0x001E: 0, 0x001F: 6, 0x0020: 8, 0x0021: 4, 0x0022: 6,
    0x0023: 2, 0x0024: 0, 0x0025: 0, 0x0026: 0, 0x0027: 0,
    0x002B: 0, 0x002C: 0, 0x002D: 0, 0x002E: 0, 0x002F: 0,
    0x0030: 8, 0x0031: 8, 0x0032: 8, 0x0033: 8, 0x0034: 8,
    0x0038: 0, 0x0039: 0, 0x003A: 0, 0x003B: 0, 0x003C: 0,
    0x0040: 8, 0x0041: 8, 0x0042: 8, 0x0043: 8, 0x0044: 8,
    0x0048: 0, 0x0049: 0, 0x004A: 0, 0x004B: 0, 0x004C: 0,
    0x0050: 8, 0x0051: 8, 0x0052: 8, 0x0053: 8, 0x0054: 8,
    0x0058: 0, 0x0059: 0, 0x005A: 0, 0x005B: 0, 0x005C: 0,
    0x0060: 12, 0x0061: 12, 0x0062: 12, 0x0063: 12, 0x0064: 12,
    0x0068: 4, 0x0069: 4, 0x006A: 4, 0x006B: 4, 0x006C: 4,
    0x00A0: 2, 0x00FF: 0, 0x0C00: 24,
}


def decode(data, report):
    r = Reader(data)
    r.u16()                          # picture size (low word)
    frame = r.rect()
    fh, fw = frame[2] - frame[0], frame[3] - frame[1]
    canvas = [[(255, 255, 255)] * fw for _ in range(fh)]
    n_maps = 0
    while r.p < len(r.d):
        r.align()
        if r.p + 2 > len(r.d):
            break
        op = r.u16()
        if op == 0x00FF:
            break
        if op == 0x0001:             # clip
            read_region(r)
        elif op == 0x00A1:           # long comment
            r.u16()
            n = r.u16()
            r.skip(n)
        elif op in (0x0028,):        # LongText
            r.skip(4)
            n = r.u8()
            r.skip(n)
            report.append("text")
        elif op in (0x0029, 0x002A):  # DHText, DVText
            r.skip(1)
            n = r.u8()
            r.skip(n)
            report.append("text")
        elif op == 0x002B:            # DHDVText
            r.skip(2)
            n = r.u8()
            r.skip(n)
            report.append("text")
        elif op in (0x0070, 0x0071, 0x0072, 0x0073, 0x0074):  # polygons
            n = r.u16()
            r.skip(n - 2)
            report.append("poly")
        elif op in (0x0080, 0x0081, 0x0082, 0x0083, 0x0084):  # regions
            read_region(r)
            report.append("rgn")
        elif op in (0x0090, 0x0098, 0x0091, 0x0099):
            pm = read_pixmap(r, False)
            clut = read_clut(r) if pm["pixmap"] else {}
            src = r.rect()
            dst = r.rect()
            r.u16()                  # mode
            if op in (0x0091, 0x0099):
                read_region(r)
            h = pm["bounds"][2] - pm["bounds"][0]
            rows = read_rows(r, pm, h)
            blit_indexed(canvas, fw, fh, frame, pm, clut, src, dst, rows)
            n_maps += 1
        elif op in (0x009A, 0x009B):
            pm = read_pixmap(r, True)
            src = r.rect()
            dst = r.rect()
            r.u16()
            if op == 0x009B:
                read_region(r)
            h = pm["bounds"][2] - pm["bounds"][0]
            rows = read_rows(r, pm, h)
            blit_direct(canvas, fw, fh, frame, pm, src, dst, rows)
            n_maps += 1
        elif op in FIXED:
            n = FIXED[op]
            if 0x0030 <= op <= 0x0064 or 0x0020 <= op <= 0x0023:
                report.append("draw %04x" % op)
            r.skip(n)
        else:
            n = reserved_len(op, r)
            if n is None:
                report.append("unknown %04x at %d" % (op, r.p - 2))
                break
            r.skip(n)
    return fw, fh, canvas, n_maps


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    res = rezfork.load(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    want = set(int(a) for a in sys.argv[3:])
    for p in sorted(res["PICT"], key=lambda x: x.id):
        if want and p.id not in want:
            continue
        report = []
        try:
            fw, fh, canvas, n_maps = decode(p.data, report)
        except Exception as e:  # noqa: BLE001
            print("PICT %5d: FAILED %s" % (p.id, e))
            continue
        rows = [bytes(sum((list(px) for px in row), [])) for row in canvas]
        path = os.path.join(out, "pict-%d.png" % p.id)
        write_png(path, fw, fh, rows)
        print("PICT %5d: %dx%d, %d pixel map(s)%s" %
              (p.id, fw, fh, n_maps, ("; " + ", ".join(sorted(set(report)))) if report else ""))
    return 0


if __name__ == "__main__" and not (len(sys.argv) > 1 and sys.argv[1] == "--atlas"):
    sys.exit(main())


# ---- the interface atlas ------------------------------------------------
#  python3 tools/pict.py --atlas rsrc/sc2k.rsrc assets
#  writes assets/ui.png (RGB) and assets/ui.json ({id: [x, y, w, h]}):
#  the pictures the interface draws, shelf-packed.
UI_PICTS = ([500, 522, 503, 505, 506, 507, 513, 514, 519, 128, 129, 131, 132] +
            list(range(300, 312)) + list(range(700, 713)) +
            list(range(200, 206)) + list(range(600, 608)) +
            list(range(10000, 10008)))


def atlas(rsrc, out_dir):
    import json
    res = rezfork.load(rsrc)
    by_id = {p.id: p for p in res["PICT"]}
    imgs = []
    for i in UI_PICTS:
        fw, fh, canvas, _ = decode(by_id[i].data, [])
        imgs.append((i, fw, fh, canvas))
        if i in (500, 522):
            # the pressed look: the Mac inverts the button; 100000 + id
            inv = [[(255 - r, 255 - g, 255 - b) for (r, g, b) in row] for row in canvas]
            imgs.append((100000 + i, fw, fh, inv))
    imgs.sort(key=lambda e: -e[2])           # tallest first
    W = 512
    x = y = shelf = 0
    places = {}
    for i, fw, fh, canvas in imgs:
        if x + fw > W:
            x = 0
            y += shelf
            shelf = 0
        places[i] = (x, y, fw, fh)
        x += fw
        shelf = max(shelf, fh)
    H = y + shelf
    rows = [bytearray(b"\x00" * (W * 3)) for _ in range(H)]
    for i, fw, fh, canvas in imgs:
        px, py, _, _ = places[i]
        for yy in range(fh):
            row = rows[py + yy]
            for xx in range(fw):
                r, g, b = canvas[yy][xx]
                row[(px + xx) * 3:(px + xx) * 3 + 3] = bytes((r, g, b))
    os.makedirs(out_dir, exist_ok=True)
    write_png(os.path.join(out_dir, "ui.png"), W, H, [bytes(r) for r in rows])
    with open(os.path.join(out_dir, "ui.json"), "w") as f:
        json.dump({str(k): list(v) for k, v in sorted(places.items())}, f, indent=1)
    print("ui atlas %dx%d, %d pictures" % (W, H, len(places)))


if __name__ == "__main__" and len(sys.argv) > 1 and sys.argv[1] == "--atlas":
    atlas(sys.argv[2], sys.argv[3])
