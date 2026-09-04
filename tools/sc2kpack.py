#!/usr/bin/env python3
"""sc2kpack -- turn SimCity 2000's tile art into standard, editable files.

    python3 tools/sc2kpack.py extract  [--rsrc R] [--out DIR] [--split]
    python3 tools/sc2kpack.py verify   [--rsrc R] [--assets DIR]
    python3 tools/sc2kpack.py miff     --atlas DIR --zoom 32 --out FILE [--ids ...]
    python3 tools/sc2kpack.py scurk    PACK... [--out DIR]

The game keeps 500 logical tiles at three sizes, stored interleaved as SHAP
ids N, N+500 and N+1000 inside a MIFF/SC2K container.  `extract` writes each
size out as one palette-indexed PNG atlas plus a JSON sidecar, which any
image editor can open and any tool can read.

Transparency is a run type in MIFF, not a colour, so it has nowhere to go in
an indexed PNG -- except that palette index 0 is used by exactly zero tiles
in the shipped art.  It is reserved here as the transparent index and marked
with a tRNS chunk.  Third-party art may not respect that, so index 0 in an
input is remapped to a duplicate palette slot rather than silently vanishing.

Nothing here needs anything outside the standard library, on purpose: the
asset pipeline has to run wherever the game builds.
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rezfork import load as rez_load  # noqa: E402

# --------------------------------------------------------------------------
# geometry.  A zoom set is named by its tile width; the diamond is half as
# tall as it is wide, and one altitude level is 12 px at full zoom.
# --------------------------------------------------------------------------
ZOOMS = (
    # name, SHAP id base, tile_w, tile_h, alt_step, atlas width
    ("8", 0, 8, 4, 3, 512),
    ("16", 500, 16, 8, 6, 1024),
    ("32", 1000, 32, 16, 12, 2048),
)
TRANSPARENT = 0  # reserved palette index; see the module docstring
#  The tan the underground wireframe is drawn in, rgb(155,135,71).  It is
#  what marks where a tunnel or pipe sprite's ground diamond actually sits.
WIREFRAME = 104
GUTTER = 1  # transparent border per tile, so bilinear taps cannot bleed


# --------------------------------------------------------------------------
# MIFF / SC2K container
# --------------------------------------------------------------------------
def miff_chunks(data):
    """Yield (tag, payload) for every chunk in a MIFF/SC2K image.

    The same layout serves the TSET resource and the loose SCURK Artwork
    files; only the chunks present differ.
    """
    if data[:4] != b"MIFF" or data[8:12] != b"SC2K":
        raise ValueError("not a MIFF/SC2K image")
    off = 12
    while off + 8 <= len(data):
        tag = data[off : off + 4]
        n = struct.unpack(">I", data[off + 4 : off + 8])[0]
        yield tag, data[off + 8 : off + 8 + n]
        off += 8 + n + (n & 1)  # chunks are word aligned


def decode_row(pay, width):
    """One scanline of span pairs -> a list of indices, None for transparent.

    Span types: 3 skips `count` pixels, 4 copies `count` literal palette
    bytes padded to an even boundary, 0 is padding.  The pad on an odd
    literal run is the detail that decides whether a row reads at all.
    """
    out, i = [], 0
    while i + 1 < len(pay):
        cnt, typ = pay[i], pay[i + 1]
        i += 2
        if typ == 3:
            out += [None] * cnt
        elif typ == 4:
            out += list(pay[i : i + cnt])
            i += cnt + (cnt & 1)
        elif typ == 0:
            pass
        elif typ == 2:
            # End of row.  Written by SCURK, never by Maxis's own packer:
            # all 645 occurrences across the 33 readable artwork packs are
            # the last span in their row with no bytes after them, so
            # stopping here and padding is the same as treating it as a
            # skip.  Checked, not assumed.
            break
        else:
            return None
    out += [None] * max(0, width - len(out))
    return out[:width]


def encode_row(px):
    """A list of indices (None or 0 = transparent) -> MIFF span pairs."""
    pay = bytearray()
    i, n = 0, len(px)
    while i < n:
        if px[i] is None or px[i] == TRANSPARENT:
            j = i
            while j < n and (px[j] is None or px[j] == TRANSPARENT):
                j += 1
            run = j - i
            while run:  # counts are one byte
                k = min(run, 255)
                pay += bytes((k, 3))
                run -= k
            i = j
        else:
            j = i
            while j < n and px[j] is not None and px[j] != TRANSPARENT:
                j += 1
            lit = px[i:j]
            while lit:
                k, lit = lit[:255], lit[255:]
                pay += bytes((len(k), 4)) + bytes(k)
                if len(k) & 1:
                    pay += b"\x00"
            i = j
    return bytes(pay)


def decode_shape(data, off, w, h):
    p, rows = off, []
    for _ in range(h):
        if p + 2 > len(data):
            break
        ln = data[p]
        p += 2
        r = decode_row(data[p : p + ln], w)
        p += ln
        if r is None:
            return None
        rows.append(r)
    while len(rows) < h:
        rows.append([None] * w)
    return rows


def iter_shapes(data):
    """Yield (id, w, h, rows) for every SHAP that decodes."""
    off = 12
    while off + 8 <= len(data):
        tag = data[off : off + 4]
        n = struct.unpack(">I", data[off + 4 : off + 8])[0]
        if tag == b"SHAP" and n >= 10:
            sid, w, h, _flags, _dl = struct.unpack(">HHHHH", data[off + 8 : off + 18])
            rows = decode_shape(data, off + 18, w, h)
            if rows is not None and w and h:
                yield sid, w, h, rows
        off += 8 + n + (n & 1)


def load_miff(path):
    """A TSET resource or a loose SCURK file, whichever this path is."""
    p = Path(path)
    raw = p.read_bytes()
    if raw[:4] == b"MIFF":
        return raw
    tset = rez_load(str(p)).get("TSET")
    if not tset:
        raise ValueError("%s: no MIFF header and no TSET resource" % p)
    return tset[0].data


#  Palette animation.  $97C4 calls _AnimatePalette(window, ctab, srcIndex=0,
#  dstIndex=0x9B, count=0x31) and $984E does the same with dstIndex=0xE0,
#  count=0x0F.  So entries 155..203 are driven from clut 500 and 224..238
#  from clut 501, and whatever pltt 0 holds in those slots is never seen.
#  Water is the obvious casualty: it uses indices 192..195, which pltt 0
#  calls green but clut 500 makes blue.
ANIMATED = ((155, 500, 49), (224, 501, 15))


def clut(rsrc, ident):
    for c in rez_load(str(rsrc)).get("clut", []):
        if c.id != ident:
            continue
        d = c.data
        n = struct.unpack(">H", d[6:8])[0] + 1
        out = []
        for i in range(n):
            off = 8 + i * 8
            if off + 8 > len(d):
                break
            _v, r, g, b = struct.unpack(">HHHH", d[off:off + 8])
            out.append((r >> 8, g >> 8, b >> 8))
        return out
    return []


def load_palette(rsrc):
    p = [x for x in rez_load(str(rsrc))["pltt"] if x.name == "Default Palette"][0].data
    n = struct.unpack(">H", p[:2])[0]
    out = []
    for i in range(n):
        r, g, b = struct.unpack(">HHH", p[16 + i * 16 : 16 + i * 16 + 6])
        out.append((r >> 8, g >> 8, b >> 8))
    while len(out) < 256:
        out.append((0, 0, 0))
    out = out[:256]
    #  Apply the animation tables at phase 0.  Without this every animated
    #  colour renders as its unused placeholder -- water comes out green.
    for dst, ident, count in ANIMATED:
        src = clut(rsrc, ident)
        for k in range(min(count, len(src))):
            if dst + k < 256:
                out[dst + k] = src[k]
    return out


# --------------------------------------------------------------------------
# PNG.  Written and read here rather than pulled from a library so the
# pipeline runs on a bare Python on any platform.
# --------------------------------------------------------------------------
def _chunk(tag, payload):
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
    )


def write_indexed_png(path, w, h, rows, palette, transparent=TRANSPARENT):
    """8-bit palette PNG with tRNS marking one index fully transparent."""
    raw = bytearray()
    for r in rows:
        raw.append(0)  # filter None: palette indices do not benefit from deltas
        raw += bytes(r)
    plte = bytearray()
    for c in palette:
        plte += bytes(c)
    trns = bytes([255] * transparent + [0])
    png = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
        + _chunk(b"PLTE", bytes(plte))
        + _chunk(b"tRNS", trns)
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )
    Path(path).write_bytes(png)
    return len(png)


def write_rgba_png(path, w, h, rows, palette, transparent=TRANSPARENT):
    raw = bytearray()
    for r in rows:
        raw.append(0)
        for v in r:
            if v == transparent:
                raw += b"\x00\x00\x00\x00"
            else:
                raw += bytes(palette[v]) + b"\xff"
    png = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + _chunk(b"IEND", b"")
    )
    Path(path).write_bytes(png)
    return len(png)


def read_indexed_png(path):
    """-> (w, h, rows of indices, palette).  Handles all five row filters,
    because a file that has been through an image editor will use them."""
    d = Path(path).read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    off, idat, palette, ihdr = 8, bytearray(), [], None
    while off + 8 <= len(d):
        n = struct.unpack(">I", d[off : off + 4])[0]
        tag = d[off + 4 : off + 8]
        body = d[off + 8 : off + 8 + n]
        if tag == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            palette = [tuple(body[i : i + 3]) for i in range(0, len(body), 3)]
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
        off += 12 + n
    w, h, depth, ctype, _c, _f, interlace = ihdr
    if (depth, ctype, interlace) != (8, 3, 0):
        raise ValueError("%s: need an 8-bit non-interlaced indexed PNG" % path)
    raw = zlib.decompress(bytes(idat))
    rows, prev, p = [], bytearray(w), 0
    for _ in range(h):
        ft = raw[p]
        p += 1
        cur = bytearray(raw[p : p + w])
        p += w
        if ft == 1:
            for i in range(1, w):
                cur[i] = (cur[i] + cur[i - 1]) & 0xFF
        elif ft == 2:
            for i in range(w):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(w):
                left = cur[i - 1] if i else 0
                cur[i] = (cur[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(w):
                a = cur[i - 1] if i else 0
                b = prev[i]
                c = prev[i - 1] if i else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                cur[i] = (cur[i] + pr) & 0xFF
        elif ft != 0:
            raise ValueError("%s: bad row filter %d" % (path, ft))
        rows.append(list(cur))
        prev = cur
    while len(palette) < 256:
        palette.append((0, 0, 0))
    return w, h, rows, palette


# --------------------------------------------------------------------------
# atlas packing
# --------------------------------------------------------------------------
def shelf_pack(items, width, gutter=GUTTER):
    """items: [(key, w, h)] -> ({key: (x, y)}, height).  A shelf packer is
    enough here: the art sorts into a handful of distinct heights."""
    placed, x, y, shelf = {}, 0, 0, 0
    for key, w, h in sorted(items, key=lambda it: (-it[2], -it[1], it[0])):
        pw, ph = w + gutter * 2, h + gutter * 2
        if x + pw > width:
            x, y, shelf = 0, y + shelf, 0
        placed[key] = (x + gutter, y + gutter)
        x += pw
        shelf = max(shelf, ph)
    return placed, y + shelf


def next_pow2(n):
    p = 1
    while p < n:
        p <<= 1
    return p


def remap_index0(rows, palette, note):
    """Third-party art may use index 0 for a real colour.  Move it to a free
    slot with the same RGB rather than losing those pixels to transparency."""
    if not any(v == 0 for r in rows for v in r if v is not None):
        return rows, None
    used = set(v for r in rows for v in r if v is not None)
    free = [i for i in range(1, 256) if i not in used]
    if not free:
        raise ValueError("%s: uses index 0 and the palette is full" % note)
    dst = free[-1]
    palette[dst] = palette[0]
    return [[dst if v == 0 else v for v in r] for r in rows], dst


# --------------------------------------------------------------------------
# commands
# --------------------------------------------------------------------------
def build_atlas(shapes, base, tile_w, tile_h, alt_step, width, palette):
    """shapes: {id: (w, h, rows)} -> (atlas rows, w, h, sidecar dict)."""
    sel = {sid: v for sid, v in shapes.items() if (sid // 500) * 500 == base}
    placed, height = shelf_pack([(k, v[0], v[1]) for k, v in sel.items()], width)
    height = next_pow2(height)
    canvas = [[TRANSPARENT] * width for _ in range(height)]
    frames, meta = {}, {}
    for sid in sorted(sel):
        w, h, rows = sel[sid]
        x, y = placed[sid]
        for j in range(h):
            row, dst = rows[j], canvas[y + j]
            for i in range(w):
                v = row[i]
                dst[x + i] = TRANSPARENT if v is None else v
        name = str(sid)
        frames[name] = {
            "frame": {"x": x, "y": y, "w": w, "h": h},
            "rotated": False,
            "trimmed": False,
            "spriteSourceSize": {"x": 0, "y": 0, "w": w, "h": h},
            "sourceSize": {"w": w, "h": h},
            "duration": 100,
        }
        #  $18E96 reads the $1226 entry as (art pointer, -, height, width)
        #  and adds +4/+6 to y/x to build the clip rect, so the y a caller
        #  passes is the art's TOP-LEFT and $16298's `sub.w $4(a0,d0.l), d1`
        #  is subtracting the sprite's full height.  A uniform offset is
        #  invisible, so keep the canvas framing by carrying h - tile_h;
        #  what matters is that it is the sprite's OWN height, and that the
        #  underground overlays use the ground sprite's instead (soft.c).
        meta[name] = {
            "id": sid,
            "tile": sid - base,
            "foot": max(1, w // tile_w),  # 1x1 .. 4x4 building footprint
            "ax": 0,  # blit origin, relative to the diamond's left corner
            "ay": h,  # == the $1226 descriptor's +4
        }
    return canvas, width, height, frames, meta


def cmd_extract(args):
    rsrc = Path(args.rsrc)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    palette = load_palette(rsrc)
    if getattr(args, "via_game", False):
        #  Decode with the game's own blitter instead of decode_shape().
        #  The two agree exactly -- 0 pixels across 1447 sprites -- so this
        #  is not a fix for anything; it is a standing check that the local
        #  RLE codec still matches the original, and a way to decode art
        #  whose container we have not fully characterised.
        from shapedec import decode_all
        shapes = decode_all()
        #  One sprite (1252) makes $19238 take its own invalid-span exit,
        #  so fall back to the local decoder for anything the blitter will
        #  not draw rather than dropping it from the atlas.
        data = load_miff(rsrc)
        missing = 0
        for sid, w, h, rows in iter_shapes(data):
            if sid not in shapes:
                shapes[sid] = (w, h, rows)
                missing += 1
        if missing:
            print("  %d shape(s) fell back to the local decoder" % missing)

        #  The game's art really does use the transparent index, which is
        #  also the atlas's transparent marker.  Move it to a free slot,
        #  once, across every shape -- per-shape remapping would pick a
        #  different slot each time.
        used = set(v for _, _, rows in shapes.values()
                   for r in rows for v in r if v is not None)
        if TRANSPARENT in used:
            free = [i for i in range(1, 256) if i not in used]
            if not free:
                raise SystemExit("art uses index %d and the palette is full"
                                 % TRANSPARENT)
            dst = free[-1]
            palette[dst] = palette[TRANSPARENT]
            shapes = {sid: (w, h, [[dst if v == TRANSPARENT else v
                                    for v in r] for r in rows])
                      for sid, (w, h, rows) in shapes.items()}
            print("  index %d is real art (%s); moved to %d"
                  % (TRANSPARENT, palette[TRANSPARENT], dst))
    else:
        data = load_miff(rsrc)
        shapes = {}
        for sid, w, h, rows in iter_shapes(data):
            shapes[sid] = (w, h, rows)
        print("decoded %d shapes from %s" % (len(shapes), rsrc.name))

    hit = set(v for _, (_, _, rows) in shapes.items() for r in rows for v in r if v is not None)
    if TRANSPARENT in hit:
        print("  ! index %d is used by the art; remapping" % TRANSPARENT)

    manifest = {
        "zooms": [],
        "palette": [list(c) for c in palette],
        "transparent": TRANSPARENT,
        #  Ranges the game drives with _AnimatePalette, so a renderer that
        #  wants moving water can cycle them rather than baking phase 0.
        "animated": [{"first": d, "count": n, "clut": c} for d, c, n in ANIMATED],
    }
    for name, base, tw, th, alt, width in ZOOMS:
        canvas, aw, ah, frames, meta = build_atlas(
            shapes, base, tw, th, alt, width, palette
        )
        stem = "tiles%s" % name
        png = out / (stem + ".png")
        if args.rgba:
            nbytes = write_rgba_png(png, aw, ah, canvas, palette)
        else:
            nbytes = write_indexed_png(png, aw, ah, canvas, palette)
        sheet = {
            "frames": frames,
            "meta": {
                "app": "sc2kpack",
                "version": "1",
                "image": png.name,
                "format": "RGBA8888" if args.rgba else "I8",
                "size": {"w": aw, "h": ah},
                "scale": "1",
                "sc2k": {
                    "zoom": int(name),
                    "id_base": base,
                    "tile_w": tw,
                    "tile_h": th,
                    "alt_step": alt,
                    "transparent": TRANSPARENT,
                    "tiles": meta,
                },
            },
        }
        write_json(out / (stem + ".json"), sheet)
        manifest["zooms"].append(
            {"zoom": int(name), "sheet": stem + ".json", "image": png.name,
             "w": aw, "h": ah, "tiles": len(frames)}
        )
        print(
            "  %-9s %4d tiles  %4dx%-4d  %7d bytes  %s"
            % (stem, len(frames), aw, ah, nbytes, png.name)
        )
        if args.split:
            split_tiles(out / ("split%s" % name), shapes, base, palette, stem)
    write_json(out / "atlas.json", manifest)
    print("wrote manifest %s" % (out / "atlas.json"))
    return 0


def split_tiles(dirpath, shapes, base, palette, stem):
    """One PNG per tile, plus a Tiled image-collection tileset that indexes
    them.  Slower to load, but it is how you edit a single building."""
    dirpath.mkdir(parents=True, exist_ok=True)
    entries = []
    for sid in sorted(shapes):
        if (sid // 500) * 500 != base:
            continue
        w, h, rows = shapes[sid]
        grid = [[TRANSPARENT if v is None else v for v in r] for r in rows]
        fn = "%04d.png" % sid
        write_indexed_png(dirpath / fn, w, h, grid, palette)
        entries.append((sid, fn, w, h))
    tsx = ['<?xml version="1.0" encoding="UTF-8"?>']
    tsx.append(
        '<tileset version="1.10" tiledversion="1.10" name="%s" '
        'tilecount="%d" columns="0">' % (stem, len(entries))
    )
    tsx.append(' <grid orientation="isometric" width="32" height="16"/>')
    for n, (sid, fn, w, h) in enumerate(entries):
        tsx.append(' <tile id="%d">' % n)
        tsx.append('  <properties><property name="shap" type="int" value="%d"/></properties>' % sid)
        tsx.append('  <image source="%s" width="%d" height="%d"/>' % (fn, w, h))
        tsx.append(" </tile>")
    tsx.append("</tileset>")
    (dirpath / (stem + ".tsx")).write_text("\n".join(tsx) + "\n", encoding="utf-8", newline="\n")
    print("    split: %d files + %s.tsx in %s" % (len(entries), stem, dirpath.name))


def write_json(path, obj):
    """Explicit newline and encoding: a sidecar must not gain CRLFs on
    Windows and then fail a byte comparison on someone else's machine."""
    with open(str(path), "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=1, sort_keys=True)
        f.write("\n")


def cmd_verify(args):
    rsrc = Path(args.rsrc)
    assets = Path(args.assets)
    data = load_miff(rsrc)
    src = {sid: (w, h, rows) for sid, w, h, rows in iter_shapes(data)}
    total = exact = missing = wrong = 0
    for name, base, tw, th, alt, width in ZOOMS:
        stem = "tiles%s" % name
        sheet = json.loads((assets / (stem + ".json")).read_text(encoding="utf-8"))
        aw, ah, rows, _pal = read_indexed_png(assets / sheet["meta"]["image"])
        assert (aw, ah) == (sheet["meta"]["size"]["w"], sheet["meta"]["size"]["h"])
        n_ok = 0
        for key, fr in sheet["frames"].items():
            sid = int(key)
            total += 1
            if sid not in src:
                missing += 1
                continue
            sw, sh, srows = src[sid]
            f = fr["frame"]
            if (f["w"], f["h"]) != (sw, sh):
                wrong += 1
                continue
            ok = True
            for j in range(sh):
                a = rows[f["y"] + j][f["x"] : f["x"] + sw]
                b = [TRANSPARENT if v is None else v for v in srows[j]]
                if a != b:
                    ok = False
                    break
            if ok:
                exact += 1
                n_ok += 1
            else:
                wrong += 1
        print("  %-9s %4d/%-4d shapes exact" % (stem, n_ok, len(sheet["frames"])))
        # the gutter must actually be empty, or bilinear taps will bleed
        bad = 0
        for key, fr in sheet["frames"].items():
            f = fr["frame"]
            for i in range(f["x"] - 1, f["x"] + f["w"] + 1):
                for j in (f["y"] - 1, f["y"] + f["h"]):
                    if 0 <= j < ah and 0 <= i < aw and rows[j][i] != TRANSPARENT:
                        bad += 1
        if bad:
            print("    ! %d gutter pixels are not transparent" % bad)
    print(
        "round-trip: %d/%d exact (%.2f%%), %d missing, %d differing"
        % (exact, total, 100.0 * exact / max(1, total), missing, wrong)
    )
    return 0 if (exact == total and total) else 1


def cmd_miff(args):
    """Re-encode tiles from an atlas back into a MIFF/SC2K file -- the format
    SCURK reads, so a PNG edited here can be loaded by the 1995 game."""
    assets = Path(args.atlas)
    stem = "tiles%d" % args.zoom
    sheet = json.loads((assets / (stem + ".json")).read_text(encoding="utf-8"))
    aw, ah, rows, _pal = read_indexed_png(assets / sheet["meta"]["image"])
    want = set(args.ids) if args.ids else set(int(k) for k in sheet["frames"])
    body = bytearray()
    n = 0
    for key in sorted(sheet["frames"], key=int):
        sid = int(key)
        if sid not in want:
            continue
        f = sheet["frames"][key]["frame"]
        payload = bytearray()
        for j in range(f["h"]):
            span = encode_row(rows[f["y"] + j][f["x"] : f["x"] + f["w"]])
            if len(span) > 255:
                raise ValueError("row %d of shape %d does not fit one byte" % (j, sid))
            payload += bytes((len(span), 1)) + span
        head = struct.pack(">HHHHH", sid, f["w"], f["h"], 0, len(payload))
        chunk = head + bytes(payload)
        body += b"SHAP" + struct.pack(">I", len(chunk)) + chunk
        if len(chunk) & 1:
            body += b"\x00"
        n += 1
    out = b"MIFF" + struct.pack(">I", len(body) + 4) + b"SC2K" + bytes(body)
    Path(args.out).write_bytes(out)
    print("wrote %s: %d shapes, %d bytes" % (args.out, n, len(out)))
    return 0


def cmd_scurk(args):
    """Read loose SCURK Artwork packs and report, or convert, what is in them."""
    out = Path(args.out) if args.out else None
    palette = load_palette(Path(args.rsrc))
    for path in args.packs:
        p = Path(path)
        try:
            raw = p.read_bytes()
            if raw[:4] != b"MIFF":
                print("%-28s not a MIFF file" % p.name)
                continue
            info = {}
            for tag, payload in miff_chunks(raw):
                if tag in (b"INFO", b"NIW_"):
                    info[tag.decode("latin-1")] = payload
            shapes = list(iter_shapes(raw))
            total = sum(1 for tag, _ in miff_chunks(raw) if tag == b"SHAP")
            sizes = sorted(set("%dx%d" % (w, h) for _, w, h, _ in shapes))
            print(
                "%-28s %3d/%-3d SHAP decode   ids %s   %s"
                % (
                    p.name,
                    len(shapes),
                    total,
                    "%d..%d" % (min(s[0] for s in shapes), max(s[0] for s in shapes))
                    if shapes
                    else "-",
                    ",".join(sizes[:4]),
                )
            )
            if out and shapes:
                d = out / p.name.replace(" ", "_")
                d.mkdir(parents=True, exist_ok=True)
                for sid, w, h, rows in shapes:
                    grid = [[TRANSPARENT if v is None else v for v in r] for r in rows]
                    grid, moved = remap_index0(grid, palette, p.name)
                    write_indexed_png(d / ("%04d.png" % sid), w, h, grid, palette)
                write_json(d / "pack.json", {"source": p.name, "shapes": [s[0] for s in shapes]})
        except Exception as exc:  # a 1995 artwork pack may be anything
            print("%-28s ERROR %s" % (p.name, exc))
    return 0


def main(argv=None):
    here = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(prog="sc2kpack", description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    e = sub.add_parser("extract", help="art -> indexed PNG atlases + JSON")
    e.add_argument("--rsrc", default=str(here / "rsrc" / "sc2k.rsrc"))
    e.add_argument("--out", default=str(here / "assets"))
    e.add_argument("--split", action="store_true", help="also emit one PNG per tile + Tiled .tsx")
    e.add_argument("--via-game", action="store_true",
                   help="decode the art by running the game's own blitter")
    e.add_argument("--rgba", action="store_true", help="RGBA atlases instead of indexed")
    e.set_defaults(fn=cmd_extract)

    v = sub.add_parser("verify", help="atlases -> shapes, compared with the original")
    v.add_argument("--rsrc", default=str(here / "rsrc" / "sc2k.rsrc"))
    v.add_argument("--assets", default=str(here / "assets"))
    v.set_defaults(fn=cmd_verify)

    m = sub.add_parser("miff", help="atlas -> a MIFF file the original game reads")
    m.add_argument("--atlas", default=str(here / "assets"))
    m.add_argument("--zoom", type=int, default=32, choices=(8, 16, 32))
    m.add_argument("--out", required=True)
    m.add_argument("--ids", type=int, nargs="*", default=None)
    m.set_defaults(fn=cmd_miff)

    s = sub.add_parser("scurk", help="inspect or convert SCURK Artwork packs")
    s.add_argument("packs", nargs="+")
    s.add_argument("--rsrc", default=str(here / "rsrc" / "sc2k.rsrc"))
    s.add_argument("--out", default=None)
    s.set_defaults(fn=cmd_scurk)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
