#!/usr/bin/env python3
"""scheme.py -- read a Kaleidoscope 2.x scheme into a theme pack.

A scheme is a resource fork (here usually AppleDouble, as an unarchiver
writes it).  What it holds, as the scheme's own ResEdit templates say:

  cicn  the elements as colour icons: 'Menu Bar', 'Menu Item', 'Active
        Document Window', 'Pull Down Menu Background', scroll bars...
  cinf  per element, by the same id: corner size and side thickness for
        stretching the icon, whether the sides tile, and which pixel of
        the icon gives the background colour, the text colour and the
        embossing colour
  wnd#  per window kind: the parts of the frame as rectangles inside the
        icon (close box, zoom box, title tile...) and which parts make
        each side
  ppat  patterns (desktop, menus), clut  header gradients, Colr  flags

This decodes the colour icons to RGBA (the mask is the alpha), packs the
interface's elements into theme.png, and writes theme.json: for every
element its rectangle in the atlas, its cinf, and the colours read from
the pixels the cinf names; and the wnd# part lists.

    python3 tools/scheme.py scheme.rsrc assets/themes/classic7
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rezfork  # noqa: E402
from gpu_diff import write_png  # noqa: E402


def read_fork(path):
    d = open(path, "rb").read()
    if d[:4] == b"\x00\x05\x16\x07":          # AppleDouble
        n = struct.unpack(">H", d[24:26])[0]
        for k in range(n):
            eid, off, ln = struct.unpack(">III", d[26 + 12 * k:38 + 12 * k])
            if eid == 2:
                return d[off:off + ln]
    return d


def decode_cicn(data):
    """A colour icon: PixMap, mask BitMap, icon BitMap, then the mask
    bits, the icon bits, the colour table and the pixel rows.  Returns
    (w, h, rows of (r, g, b, a))."""
    p = 4                                          # pixMap.baseAddr
    row_bytes = struct.unpack(">H", data[p:p + 2])[0] & 0x3FFF
    t, l, b, r = struct.unpack(">hhhh", data[p + 2:p + 10])
    pixel_size = struct.unpack(">H", data[p + 32:p + 34])[0]
    p += 46
    p += 4                                         # mask.baseAddr
    mask_rb = struct.unpack(">H", data[p:p + 2])[0]
    mt, ml, mb, mr = struct.unpack(">hhhh", data[p + 2:p + 10])
    p += 10
    p += 4                                         # bmap.baseAddr
    bmap_rb = struct.unpack(">H", data[p:p + 2])[0]
    bt, bl, bb, br = struct.unpack(">hhhh", data[p + 2:p + 10])
    p += 10
    p += 4                                         # iconData handle
    w, h = r - l, b - t
    mask = data[p:p + mask_rb * (mb - mt)]
    p += mask_rb * (mb - mt)
    p += bmap_rb * (bb - bt)                       # the 1-bit icon, unused
    # colour table
    p += 4                                         # ctSeed
    p += 2                                         # ctFlags
    n = struct.unpack(">H", data[p:p + 2])[0] + 1
    p += 2
    clut = {}
    for k in range(n):
        v, cr, cg, cb = struct.unpack(">HHHH", data[p:p + 8])
        clut[v] = (cr >> 8, cg >> 8, cb >> 8)
        p += 8
    rows = []
    for y in range(h):
        row = data[p + y * row_bytes:p + (y + 1) * row_bytes]
        mrow = mask[y * mask_rb:(y + 1) * mask_rb] if mask else b""
        out = []
        for x in range(w):
            if pixel_size == 8:
                idx = row[x] if x < len(row) else 0
            elif pixel_size == 4:
                byte = row[x // 2] if x // 2 < len(row) else 0
                idx = (byte >> 4) if (x & 1) == 0 else (byte & 15)
            elif pixel_size == 2:
                byte = row[x // 4] if x // 4 < len(row) else 0
                idx = (byte >> (6 - 2 * (x & 3))) & 3
            else:
                byte = row[x // 8] if x // 8 < len(row) else 0
                idx = (byte >> (7 - (x & 7))) & 1
            rgb = clut.get(idx, (255, 0, 255))
            if mrow:
                mbyte = mrow[x // 8] if x // 8 < len(mrow) else 0
                a = 255 if (mbyte >> (7 - (x & 7))) & 1 else 0
            else:
                a = 255
            out.append((rgb[0], rgb[1], rgb[2], a))
        rows.append(out)
    return w, h, rows


def decode_ppat(data):
    """A colour pattern as RGBA rows: its pixel map's bits through its
    colour table.  Only the 1-, 2-, 4- and 8-bit indexed maps a scheme
    uses."""
    _, pmap, pdata = struct.unpack(">HII", data[:10])
    rb = struct.unpack(">H", data[pmap + 4:pmap + 6])[0] & 0x3FFF
    top, left, bot, right = struct.unpack(">hhhh", data[pmap + 6:pmap + 14])
    psize = struct.unpack(">H", data[pmap + 32:pmap + 34])[0]
    ctab = struct.unpack(">I", data[pmap + 42:pmap + 46])[0]
    n = struct.unpack(">H", data[ctab + 6:ctab + 8])[0] + 1
    colours = {}
    for k in range(n):
        v, r, g, b = struct.unpack(">HHHH", data[ctab + 8 + 8 * k:ctab + 16 + 8 * k])
        colours[v] = (r >> 8, g >> 8, b >> 8, 255)
    w, h = right - left, bot - top
    rows = []
    for y in range(h):
        line = data[pdata + y * rb:pdata + (y + 1) * rb]
        row = []
        for x in range(w):
            bit = x * psize
            v = (line[bit // 8] >> (8 - psize - bit % 8)) & ((1 << psize) - 1)
            row.append(colours.get(v, (0, 0, 0, 255)))
        rows.append(row)
    return w, h, rows


#  The patterns a scheme fills its scroll bar tracks with, by their ids.
PATTERNS = {-8278: "vscroll_pattern", -8277: "vscroll_pattern_pressed",
            -8286: "hscroll_pattern", -8285: "hscroll_pattern_pressed"}


def decode_cinf(data):
    f = struct.unpack(">bbbbhhhhhhh", data[:18])
    return {"corner": f[0], "side": f[1], "tile_sides": f[2], "anchor": f[3],
            "pattern": f[4], "bg_pixel": [f[6], f[5]], "text_pixel": [f[8], f[7]],
            "emboss_pixel": [f[10], f[9]]}


def decode_wnd(data):
    """ZCNT lists: rectangles (part, rect), then top, bottom, left, right
    sides as (part, border)."""
    p = 0
    n = struct.unpack(">h", data[p:p + 2])[0] + 1
    p += 2
    rects = []
    for _ in range(n):
        part = struct.unpack(">h", data[p:p + 2])[0]
        t, l, b, r = struct.unpack(">hhhh", data[p + 2:p + 10])
        rects.append({"part": part, "rect": [l, t, r - l, b - t]})
        p += 10
    sides = {}
    for name in ("top", "bottom", "left", "right"):
        n = struct.unpack(">h", data[p:p + 2])[0] + 1
        p += 2
        lst = []
        for _ in range(n):
            part, border = struct.unpack(">hh", data[p:p + 4])
            lst.append({"part": part, "border": border})
            p += 4
        sides[name] = lst
    return {"rects": rects, "sides": sides}


#  The elements the interface draws, by cicn id (the scheme's names vary
#  in case and spacing; the ids are the contract).
ELEMENTS = {
    -14335: "window_active", -14336: "window_inactive",
    -14333: "grow_active", -14334: "grow_inactive",
    -14330: "window_down_states",
    -14327: "modal_active", -14328: "modal_inactive",
    -14323: "movable_modal_active", -14324: "movable_modal_inactive",
    -14303: "utility_active", -14304: "utility_inactive",
    -14298: "utility_down_states",
    -12240: "menu_bar", -12239: "menu_item", -12238: "menu_bar_selected",
    -12237: "menu_background", -12236: "menu_item_selected",
    -12235: "menu_divider", -12228: "menu_border", -12227: "menu_shadow",
    -8287: "hscroll_empty", -8286: "hscroll", -8279: "vscroll_empty",
    -8278: "vscroll", -10208: "vthumb", -10206: "hthumb",
    -8285: "hscroll_pressed", -8277: "vscroll_pressed", -8288: "hscroll_disabled",
    -8280: "vscroll_disabled", -10207: "vthumb_pressed", -10205: "hthumb_pressed",
    -10131: "slider_track", -10130: "slider_track_pressed", -10132: "slider_track_inactive",
    -10129: "slider_thumb", -10224: "progress_track", -10223: "progress_fill",
    -10047: "little_arrows", -10046: "little_arrow_up_pressed", -10045: "little_arrow_down_pressed",
    -9791: "placard", -9790: "placard_pressed", -9550: "list_color", -9549: "list_sort_color",
    -9977: "tab_pane", -9980: "tab_front", -9983: "tab_rear", -9982: "tab_rear_pressed",
    -8206: "popup_pressed", -8203: "popup_arrow_pressed",
    -8207: "popup", -8204: "popup_arrow", 0: "stop_icon", 1: "note_icon",
    2: "alert_icon", 3000: "triangle_right", 3001: "triangle_down",
}
WINDOWS = {-14336: "document", -14328: "dialog", -14324: "movable_dialog",
           -14304: "utility", -14296: "side_utility"}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    res = rezfork.parse(read_fork(sys.argv[1]))
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    cicn = {c.id: c for c in res.get("cicn", [])}
    cinf = {c.id: c for c in res.get("cinf", [])}
    wnd = {w.id: w for w in res.get("wnd#", [])}
    imgs = []
    theme = {"source": os.path.basename(sys.argv[1]), "elements": {}, "windows": {}}
    for cid, name in ELEMENTS.items():
        if cid not in cicn:
            continue
        w, h, rows = decode_cicn(cicn[cid].data)
        imgs.append((name, cid, w, h, rows))
    ppat = {p.id: p for p in res.get("ppat", [])}
    for pid, name in PATTERNS.items():
        if pid in ppat:
            w, h, rows = decode_ppat(ppat[pid].data)
            imgs.append((name, pid, w, h, rows))
            cicn.setdefault(pid, type("Pat", (), {"name": name})())
    imgs.sort(key=lambda e: -e[3])
    W = 512
    x = y = shelf = 0
    places = {}
    for name, cid, w, h, rows in imgs:
        if x + w > W:
            x, y, shelf = 0, y + shelf, 0
        places[name] = (x, y, w, h)
        x += w
        shelf = max(shelf, h)
    H = max(y + shelf, 1)
    png = [bytearray(b"\x00" * (W * 4)) for _ in range(H)]
    for name, cid, w, h, rows in imgs:
        px, py, _, _ = places[name]
        for yy in range(h):
            for xx in range(w):
                r, g, b, a = rows[yy][xx]
                png[py + yy][(px + xx) * 4:(px + xx) * 4 + 4] = bytes((r, g, b, a))
        entry = {"id": cid, "name": cicn[cid].name, "rect": list(places[name])}
        if cid in cinf:
            ci = decode_cinf(cinf[cid].data)
            entry["cinf"] = ci

            def pix(xy):
                xx, yy = xy
                if 0 <= xx < w and 0 <= yy < h:
                    return list(rows[yy][xx][:3])
                return None
            entry["bg_colour"] = pix(ci["bg_pixel"])
            entry["text_colour"] = pix(ci["text_pixel"])
            entry["emboss_colour"] = pix(ci["emboss_pixel"])
        theme["elements"][name] = entry
    for wid, name in WINDOWS.items():
        if wid in wnd:
            theme["windows"][name] = decode_wnd(wnd[wid].data)
    write_png_rgba(os.path.join(out, "theme.png"), W, H, [bytes(r) for r in png])
    with open(os.path.join(out, "theme.json"), "w") as f:
        json.dump(theme, f, indent=1)
    #  The same, flat, for the game: one element or part per line.
    #    element NAME x y w h corner side bg_r bg_g bg_b text_r text_g text_b
    #    part WINDOW PARTNO x y w h
    with open(os.path.join(out, "theme.txt"), "w") as f:
        f.write("# theme pack from %s\n" % theme["source"])
        for name, e in sorted(theme["elements"].items()):
            ci = e.get("cinf") or {}
            bg = e.get("bg_colour") or [-1, -1, -1]
            tx = e.get("text_colour") or [-1, -1, -1]
            f.write("element %s %d %d %d %d %d %d %d %d %d %d %d %d\n" %
                    (name, e["rect"][0], e["rect"][1], e["rect"][2], e["rect"][3],
                     ci.get("corner", 0), ci.get("side", 0), bg[0], bg[1], bg[2],
                     tx[0], tx[1], tx[2]))
        for wname, wd in sorted(theme["windows"].items()):
            for r in wd["rects"]:
                f.write("part %s %d %d %d %d %d\n" %
                        (wname, r["part"], r["rect"][0], r["rect"][1], r["rect"][2], r["rect"][3]))
    print("theme %s: %d elements, %d windows, atlas %dx%d" %
          (out, len(theme["elements"]), len(theme["windows"]), W, H))
    return 0


def write_png_rgba(path, w, h, rows):
    import struct as st
    import zlib
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(t, d):
        return st.pack(">I", len(d)) + t + d + st.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", st.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    sys.exit(main())
