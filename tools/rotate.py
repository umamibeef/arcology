"""The map rotation, as $3AECA performs it.

Rotating the view in SimCity 2000 does not change how the map is drawn --
it rewrites the map.  `$3AECA` turns every layer through 90 degrees in
place and then bumps `g_rotation`, so the arrays a save file holds are
already in whichever of the four orientations the city was left in.

The geometry, read off the four stores in the loop at $3AF7E and the ring
bounds at $3B34E..$3B35E:

    new[y][x] = old[N-1-x][y]

Three of the layers also pass every byte through a translation table,
because their ids encode a direction: a road running north-south has a
different id from the same road running east-west.

    XBLD  A5-0xEE2      XTER  A5-0xDE2      XUND  A5-0xD9C

ALTM, XZON, XTXT and XBIT move as plain bytes.  XZON is the interesting
one: its high nibble is a per-corner mask, and rotating the array does
*not* rotate those bits, which is exactly why the game carries a
rotation-indexed ROT_CORNER_MASK to pick the right corner afterwards.

Checked by running it: turning all 103 shipped cities four times returns
every one of the fifteen map layers to its original bytes.  XTHG returns
1,101 of its 1,227 live records; the other 126 hold a field outside the
range the game's own masking round-trips -- a type 4 with a heading of 255,
or a type 11 whose +8 exceeds 7 -- and for those the original's own rotate
is not 4-periodic either.  That is a property of the saved data, not of
this transcription.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from thinkdata import build, bytesat

TABLE_OFF = {"XBLD": -0xEE2, "XTER": -0xDE2, "XUND": -0xD9C}
FULL = ("ALTM", "XBLD", "XZON", "XTER", "XUND", "XTXT", "XBIT")
HALF = ("XTRF", "XPLT", "XVAL", "XCRM")
QUARTER = ("XPLC", "XFIR", "XPOP", "XROG")

_img = None


def tables():
    """name -> 256-byte rotation remap, straight out of the A5 image."""
    global _img
    if _img is None:
        _img = build()
    return {k: bytes(bytesat(_img, off, 256)) for k, off in TABLE_OFF.items()}


def rotate_layer(buf, n, table=None, stride=1):
    """One 90 degree turn of an n x n layer.  `stride` is 2 for ALTM,
    which is words rather than bytes."""
    out = bytearray(len(buf))
    for y in range(n):
        for x in range(n):
            sy, sx = n - 1 - x, y
            s = (sy * n + sx) * stride
            d = (y * n + x) * stride
            if table is not None:
                out[d] = table[buf[s]]
            else:
                out[d : d + stride] = buf[s : s + stride]
    return bytes(out)


def rotate_thing(rec):
    """One XTHG record, 12 bytes.

    Field +3 is y and +4 is x -- settled by checking which reading puts
    things on real infrastructure (98.5% vs 93.6% across the 103 shipped
    cities).  The pair rotates like every other coordinate in the game,
    new = (127 - y, x).

    $3B96E switches on the type byte at +0 and has three arms.  Types 10..13
    carry a four-direction heading, everything else an eight-direction one,
    which is presumably ground vehicles against everything that flies or
    floats.
    """
    r = bytearray(rec)
    t = r[0]
    if t == 0:
        return bytes(r)

    #  the position pair, common to all three arms  ($3B9CE / $3BA60 / $3BB24)
    r[3], r[4] = r[4], (127 - r[3]) & 0xFF

    if 10 <= t <= 13:  # $3BA54
        r[1] = (r[1] - 1) & 3  # four-direction heading
        r[8] = (r[8] - 2) & 7
        return bytes(r)

    r[1] = (r[1] - 2) & 7  # eight-direction heading
    r[8], r[9] = r[9], (127 - r[8]) & 0xFF
    if t == 1:  # $3B9A8 alone also turns the nibble at +2
        r[2] = (((((r[2] >> 4) - 2) & 7) << 4) | (r[2] & 0x0F)) & 0xFF
    return bytes(r)


def rotate_city(chunks):
    """A dict of chunk name -> bytes, turned 90 degrees. Returns a new dict."""
    tbl = tables()
    out = dict(chunks)
    for name in FULL:
        key = name.encode()
        if key not in chunks:
            continue
        stride = 2 if name == "ALTM" else 1
        out[key] = rotate_layer(chunks[key], 128, tbl.get(name), stride)
    for name in HALF:
        key = name.encode()
        if key in chunks:
            out[key] = rotate_layer(chunks[key], 64)
    for name in QUARTER:
        key = name.encode()
        if key in chunks:
            out[key] = rotate_layer(chunks[key], 32)
    if b"XTHG" in chunks:
        t = chunks[b"XTHG"]
        out[b"XTHG"] = b"".join(rotate_thing(t[k : k + 12]) for k in range(0, len(t) - 11, 12))
    return out
