#!/usr/bin/env python3
"""A minimal animated-GIF writer for palette-indexed frames.

The renderer already works in palette indices and SC2K's animation is a
palette rotation, so GIF is the natural container: one global colour table,
one frame per phase, and the frames differ only in the table.  Written here
rather than pulled in because the project has no image dependencies and is
not about to acquire one for 80 lines of LZW.
"""
import struct


def _lzw(data, min_code_size):
    clear, eoi = 1 << min_code_size, (1 << min_code_size) + 1
    table = {bytes([i]): i for i in range(1 << min_code_size)}
    nxt, width = eoi + 1, min_code_size + 1
    out, cur, nbits = bytearray(), 0, 0

    def emit(code):
        nonlocal cur, nbits
        cur |= code << nbits
        nbits += width
        while nbits >= 8:
            out.append(cur & 0xFF)
            cur >>= 8
            nbits -= 8

    emit(clear)
    buf = b""
    for ch in data:
        nb = buf + bytes([ch])
        if nb in table:
            buf = nb
            continue
        emit(table[buf])
        table[nb] = nxt
        nxt += 1
        if nxt > (1 << width) and width < 12:
            width += 1
        elif nxt > 4095:
            emit(clear)
            table = {bytes([i]): i for i in range(1 << min_code_size)}
            nxt, width = eoi + 1, min_code_size + 1
        buf = bytes([ch])
    if buf:
        emit(table[buf])
    emit(eoi)
    if nbits:
        out.append(cur & 0xFF)
    return bytes(out)


def _blocks(data):
    out = bytearray()
    for i in range(0, len(data), 255):
        chunk = data[i:i + 255]
        out.append(len(chunk))
        out += chunk
    out.append(0)
    return bytes(out)


def write_gif_anim(path, w, h, rows, palettes, animated, delay_cs=20,
                   loop=0, transparent=0):
    """One image, many palettes -- the way SC2K animates.

    Every frame shares the same pixels; only the colour table moves.  So
    frame 1 carries the picture and every later frame carries ONLY the
    pixels whose index lies in an animated run, with the rest marked
    transparent and disposal left as "do not dispose".  Those frames are
    almost entirely one repeated value, so they cost a few KB instead of
    re-encoding the whole map -- which is what makes animating a
    4224x2468 city affordable at all.

    `animated` is [(first, count), ...]; `transparent` must be an index no
    animated run contains.
    """
    live = set()
    for first, count in animated:
        live.update(range(first, first + count))
    assert transparent not in live

    g = bytearray(b"GIF89a")
    g += struct.pack("<HHBBB", w, h, 0xF7, 0, 0)
    for i in range(256):
        c = palettes[0][i] if i < len(palettes[0]) else (0, 0, 0)
        g += bytes(c[:3])
    g += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01" + struct.pack("<H", loop) + b"\x00"

    full = bytearray()
    for r in rows:
        full += bytes(r)
    delta = bytearray(bytes([transparent]) * (w * h))
    for y, r in enumerate(rows):
        base = y * w
        for x, v in enumerate(r):
            if v in live:
                delta[base + x] = v
    delta = bytes(delta)

    for n, pal in enumerate(palettes):
        #  disposal 1 (leave in place) so the transparent frames show what
        #  the first frame painted.
        flags = 0x05 if n else 0x04           # bit0 = transparency on
        g += b"\x21\xF9\x04" + bytes([flags]) + struct.pack("<H", delay_cs)
        g += bytes([transparent]) + b"\x00"
        g += b"\x2C" + struct.pack("<HHHHB", 0, 0, w, h, 0x87)
        for i in range(256):
            c = pal[i] if i < len(pal) else (0, 0, 0)
            g += bytes(c[:3])
        g += bytes([8]) + _blocks(_lzw(bytes(full) if n == 0 else delta, 8))
    g += b"\x3B"
    open(path, "wb").write(bytes(g))
    return len(g)


def write_gif(path, w, h, frames, delay_cs=8, loop=0):
    """frames: [(rows, palette)] -- rows are lists of 0..255 indices."""
    g = bytearray(b"GIF89a")
    g += struct.pack("<HHBBB", w, h, 0xF7, 0, 0)  # 256-entry global table
    pal0 = frames[0][1]
    for i in range(256):
        c = pal0[i] if i < len(pal0) else (0, 0, 0)
        g += bytes(c[:3])
    g += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01" + struct.pack("<H", loop) + b"\x00"
    for rows, pal in frames:
        #  A local colour table per frame: the whole animation IS the table
        #  turning, so this is where the movement lives.
        g += b"\x21\xF9\x04\x00" + struct.pack("<H", delay_cs) + b"\x00\x00"
        g += b"\x2C" + struct.pack("<HHHHB", 0, 0, w, h, 0x87)
        for i in range(256):
            c = pal[i] if i < len(pal) else (0, 0, 0)
            g += bytes(c[:3])
        flat = bytearray()
        for r in rows:
            flat += bytes(r)
        g += bytes([8]) + _blocks(_lzw(bytes(flat), 8))
    g += b"\x3B"
    open(path, "wb").write(bytes(g))
    return len(g)
