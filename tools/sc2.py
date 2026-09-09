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

if __name__ == "__main__":
    ch, order = load(sys.argv[1])
    for t, n, m in order:
        print("  %-6s stored=%6d  expanded=%6d" % (t.decode('latin1'), n, m))
