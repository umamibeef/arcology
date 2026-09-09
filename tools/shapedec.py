#!/usr/bin/env python3
"""Decode the tileset by running the GAME'S OWN blitter.

`sc2kpack.decode_shape` splits the span stream into scanlines using a
per-row length byte.  That is close enough to read most sprites, but a
strict comparison against $18E96 shows 295 of 498 sprites at the 32 px set
picking up pixels the game leaves transparent -- always in that direction,
never a wrong colour -- so the row splitting drifts on the larger art.

Rather than keep guessing at the container, decode with the authority:
$18E96 needs no toolbox, so point it at a framebuffer, blit each shape on
a sentinel background and read back what it painted.  Whatever the format
really is, this agrees with the game by construction.

    from shapedec import decode_all
    shapes = decode_all()      # {id: (w, h, rows)}, None = transparent
"""
import struct
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from runsim import Sim, A5

BLIT = 0x18E96
#  Two different backgrounds, because no single sentinel is safe: every
#  byte 0..255 is a real palette index, and 255 is black -- so filling with
#  255 and treating the leftovers as transparent punched holes through
#  every dark sprite, the arcologies worst of all.  A pixel is transparent
#  only if it survives BOTH fills unchanged.
FILL_A, FILL_B = 0x00, 0xFF
#  Your own copy of the game.  Nothing here can ship it.
GAME = Path(os.environ.get(
    "SC2K_CITIES",
    Path.home() / "Downloads" / "SimCity 2000\u00ae Collection"))
CITY = GAME / "Cities/Bayview"


def tileset_streams():
    """id -> (w, h, raw span stream) out of the TSET resource."""
    from rezfork import load as rez_load
    rs = rez_load(str(GAME / "SimCity 2000® 1.2/..namedfork/rsrc"))
    data = [e for e in rs["TSET"]][0].data
    out, off = {}, 12
    while off + 8 <= len(data):
        tag = data[off:off + 4]
        n = struct.unpack(">I", data[off + 4:off + 8])[0]
        if tag == b"SHAP" and n >= 10:
            sid, w, h, _f, _dl = struct.unpack(">HHHHH", data[off + 8:off + 18])
            out[sid] = (w, h, data[off + 18: off + 8 + n])
        off += 8 + n + (n & 1)
    return out


def decode_all(verbose=True):
    streams = tileset_streams()
    big = max(max(w, h) for w, h, _ in streams.values())
    W = H = big + 80
    sim = Sim(str(CITY))
    e = sim.e
    desc = e.alloc(0x2EE0)
    for k in range(0, 0x2EE0, 4):
        e.wr(desc + k, 4, 0)
    e.wr(A5 + 0x1226, 4, desc)

    #  Every span stream is placed BEFORE the frame buffer, and the frame
    #  buffer gets a megabyte of slack behind it.  m68kemu's alloc() is an
    #  unchecked bump allocator, so a sprite whose spans reach past the
    #  buffer writes straight into whatever was allocated next -- and with
    #  the old order that was the art of the sprites still to come.  Shape
    #  1252 was the visible casualty: it decodes perfectly on its own and
    #  hit a _Debugger only after 1,251 earlier blits had scribbled on it.
    todo = [sid for sid in sorted(streams)
            if sid * 8 + 8 <= 0x2EE0 and streams[sid][0] and streams[sid][1]]
    for sid in todo:
        w, h, stream = streams[sid]
        art = e.alloc(len(stream) + 16)
        for i, b in enumerate(stream):
            e.wr(art + i, 1, b)
        e.wr(desc + sid * 8 + 0, 4, art)
        e.wr(desc + sid * 8 + 4, 2, h)
        e.wr(desc + sid * 8 + 6, 2, w)

    fb = e.alloc(W * H + 64)
    e.alloc(1 << 20)                  # slack, so an overrun hits nothing
    e.wr(A5 + 0x120C, 4, fb)          # destination base
    e.wr(A5 + 0x1210, 2, W)           # rowBytes
    e.wr(A5 + 0x1212, 2, 0xFFFF)      # clip: top, left, bottom, right
    e.wr(A5 + 0x1214, 2, 0xFFFF)
    e.wr(A5 + 0x1216, 2, H + 1)
    e.wr(A5 + 0x1218, 2, W + 1)

    X = Y = 20
    out, failed = {}, []
    for sid in todo:
        w, h, stream = streams[sid]
        shots, err = [], None
        for fill in (FILL_A, FILL_B):
            for y in range(Y - 1, Y + h + 1):
                for x in range(X - 1, X + w + 1):
                    e.wr(fb + y * W + x, 1, fill)
            #  Ten bytes, not eight: the game's callers push `clr.l` for
            #  the mirror argument, so BOTH $e(a6) and $10(a6) are
            #  parameters and $18F08 tests $10 first.  Leaving it
            #  uninitialised sent one sprite down the mirrored-clipped
            #  path and into a _Debugger trap.
            e.a[7] = (e.a[7] - 10) & 0xFFFFFFFF
            e.wr(e.a[7] + 0, 2, sid)
            e.wr(e.a[7] + 2, 2, X)
            e.wr(e.a[7] + 4, 2, Y)
            e.wr(e.a[7] + 6, 2, 0)
            e.wr(e.a[7] + 8, 2, 0)
            e.a[7] = (e.a[7] - 4) & 0xFFFFFFFF
            e.wr(e.a[7], 4, 0xDEAD0000)
            #  The biggest sprite carries a 16 KB span stream; scale the
            #  instruction budget with it rather than picking one number.
            err = e.run(BLIT, 0xDEAD0000, real_calls=True,
                        limit=4000000 + len(stream) * 800)
            e.a[7] = (e.a[7] + 10) & 0xFFFFFFFF
            if err:
                break
            shots.append([[e.rd(fb + (Y + j) * W + (X + i), 1)
                           for i in range(w)] for j in range(h)])
        if err:
            failed.append((sid, err))
            continue
        a_img, b_img = shots
        out[sid] = (w, h,
                    [[None if (a_img[j][i] == FILL_A and
                               b_img[j][i] == FILL_B) else a_img[j][i]
                      for i in range(w)] for j in range(h)])
    if verbose:
        print("decoded %d shapes through the game's blitter (%d failed)"
              % (len(out), len(failed)))
        if failed:
            for sid, err in failed[:6]:
                print("  shape %d failed: %s" % (sid, err))
    return out


if __name__ == "__main__":
    decode_all()
