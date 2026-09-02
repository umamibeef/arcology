#!/usr/bin/env python3
"""snd.py -- the game's 'snd ' resources as WAV files.

    python3 tools/snd.py rsrc/sc2k.rsrc assets/sounds

A format 1 resource lists its data formats and commands; a format 2 has a
reference count instead.  Both end in a bufferCmd (0x8051) whose second
parameter is the offset of a sampled sound header: pointer, length,
rate (16.16 fixed), loop start, loop end, encoding, base frequency, then
the samples, 8-bit unsigned mono.  The game's 500..527 are its effects;
10000.. are the music's instruments, kept too.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rezfork  # noqa: E402


def decode(data):
    fmt = struct.unpack(">H", data[:2])[0]
    p = 2
    if fmt == 1:
        n = struct.unpack(">H", data[p:p + 2])[0]
        p += 2 + 6 * n
    elif fmt == 2:
        p += 2                                      # refCount
    else:
        raise ValueError("format %d" % fmt)
    ncmd = struct.unpack(">H", data[p:p + 2])[0]
    p += 2
    off = None
    for _ in range(ncmd):
        cmd, p1, p2 = struct.unpack(">HHI", data[p:p + 8])
        p += 8
        if cmd & 0x7FFF == 0x51:                    # bufferCmd
            off = p2
    if off is None:
        raise ValueError("no bufferCmd")
    ptr, length, rate, ls, le, enc, base = struct.unpack(">IIIIIBB", data[off:off + 22])
    hz = rate / 65536.0
    if enc == 0:                                    # standard header
        samples = data[off + 22:off + 22 + length]
        return hz, samples
    if enc == 0xFF:                                 # extended header
        nch = length
        frames = struct.unpack(">I", data[off + 22:off + 26])[0]
        bits = struct.unpack(">H", data[off + 48:off + 50])[0]
        start = off + 64
        if bits == 8 and nch == 1:
            return hz, data[start:start + frames]
        raise ValueError("extended %d-bit x%d" % (bits, nch))
    raise ValueError("encoding %d" % enc)


def write_wav(path, hz, samples):
    with open(path, "wb") as f:
        n = len(samples)
        f.write(b"RIFF" + struct.pack("<I", 36 + n) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, int(round(hz)), int(round(hz)), 1, 8))
        f.write(b"data" + struct.pack("<I", n) + samples)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    res = rezfork.load(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for s in sorted(res.get("snd ", []), key=lambda x: x.id):
        try:
            hz, samples = decode(s.data)
        except Exception as e:  # noqa: BLE001
            print("snd %5d %-14r FAILED: %s" % (s.id, s.name, e))
            continue
        name = "".join(ch if ch.isalnum() else "_" for ch in (s.name or "snd"))
        path = os.path.join(out, "%d-%s.wav" % (s.id, name))
        write_wav(path, hz, samples)
        print("snd %5d %-14r %6d samples at %5.0f Hz -> %s" % (s.id, s.name, len(samples), hz, os.path.basename(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
