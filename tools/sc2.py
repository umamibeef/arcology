"""SC2 city file reader (IFF FORM/SCDH + the RLE codec read out of CODE 2)."""
import struct, sys

UNCOMPRESSED = {b"CNAM", b"ALTM"}

def unrle(b):
    out = bytearray(); i = 0
    while i < len(b):
        c = b[i]; i += 1
        if c < 128:                      # literal run of c bytes
            out += b[i:i+c]; i += c
        else:                            # repeat next byte (c-127) times
            if i >= len(b): break
            out += bytes([b[i]]) * (c - 127); i += 1
    return bytes(out)

def load(path):
    b = open(path, "rb").read()
    assert b[:4] == b"FORM", "not an IFF file"
    total = struct.unpack(">I", b[4:8])[0]
    assert b[8:12] == b"SCDH", "not a SimCity 2000 city"
    chunks = {}
    order = []
    o = 12
    while o + 8 <= min(len(b), total + 8):
        t = b[o:o+4]; n = struct.unpack(">I", b[o+4:o+8])[0]
        raw = b[o+8:o+8+n]
        chunks[t] = raw if t in UNCOMPRESSED else unrle(raw)
        order.append((t, n, len(chunks[t])))
        o += 8 + n
    return chunks, order

def unturn(chunks, img=None):
    """Decode a city saved at a map turn into the grid's own terms, in place.

    MISC slot 2 is the turn the file was saved at.  $3AECA turns the map
    and moves XZON as a plain byte, so at turn r corner i of a footprint
    carries ROT_CORNER_4[r*4+i] (A5-0x7DCC).  Each bit goes to the bit
    that corner carries at turn 0.  The turn also moves XBIT as a plain
    byte for ids 0x70 and up, and the original reads their mirror bit
    against the parity of the turn ($178C8, $3391C, $33C64, $322CC), so an
    odd turn flips it.  The turn is then 0.  city_load in src/sim/city.c
    does the same to the city the C simulates.
    """
    from thinkdata import build, words
    misc = bytearray(chunks[b"MISC"])
    turn = struct.unpack(">i", bytes(misc[8:12]))[0] & 3
    misc[8:12] = b"\0\0\0\0"
    chunks[b"MISC"] = bytes(misc)
    if not turn:
        return 0
    rot = words(img if img is not None else build(), -0x7DCC, 16)
    xzon = bytearray(chunks[b"XZON"])
    xbit = bytearray(chunks[b"XBIT"])
    xbld = chunks[b"XBLD"]
    for i in range(len(xzon)):
        z = xzon[i]
        hi = 0
        for k in range(4):
            if z & rot[turn * 4 + k]:
                hi |= rot[k]
        xzon[i] = (z & 0x0F) | hi
        if turn & 1 and i < len(xbld) and i < len(xbit) and xbld[i] >= 0x70:
            xbit[i] ^= 0x02
    chunks[b"XZON"] = bytes(xzon)
    chunks[b"XBIT"] = bytes(xbit)
    return turn


if __name__ == "__main__":
    ch, order = load(sys.argv[1])
    for t, n, m in order:
        print("  %-6s stored=%6d  expanded=%6d" % (t.decode('latin1'), n, m))
