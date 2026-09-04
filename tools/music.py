#!/usr/bin/env python3
"""music.py -- the game's music, read out of its resource fork.

    python3 tools/music.py rsrc/sc2k.rsrc assets/music

The Macintosh SimCity 2000 carries its own music engine, Steve Hales'
SoundMusicSys ("MIDI Synth 3.87" in its MDRV resource): the songs are
MIDI resources, the instruments are INST resources naming an 'snd '
sample each, and a SONG resource per song says which instrument each
MIDI channel plays through a remap table.  All of it is the game's own
data, so it stays where the sprites stay: extracted from your copy,
never shipped.

This writes each MIDI as a .mid and one music.json describing the rest:
the instruments, the samples' rates, loop points and root notes (the
WAVs themselves come from snd.py into assets/sounds), and the songs
with their remaps and their length in seconds.  The struct layouts are
SoundMusicSys's, as documented by its BSD descendant miniBAE.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rezfork  # noqa: E402


def snd_header(data):
    """(rate, loop_start, loop_end, base_note, length) of a format 1/2 'snd '."""
    fmt = struct.unpack(">H", data[:2])[0]
    p = 2
    if fmt == 1:
        n = struct.unpack(">H", data[p:p + 2])[0]
        p += 2 + 6 * n
    else:
        p += 2
    ncmd = struct.unpack(">H", data[p:p + 2])[0]
    p += 2
    off = None
    for _ in range(ncmd):
        cmd, p1, p2 = struct.unpack(">HHI", data[p:p + 8])
        p += 8
        if cmd & 0x7FFF == 0x51:
            off = p2
    ptr, length, rate, ls, le, enc, base = struct.unpack(">IIIIIBB", data[off:off + 22])
    return rate / 65536.0, ls, le, base, length


def vlq(d, p):
    v = 0
    while True:
        c = d[p]
        p += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, p


def midi_seconds(d):
    """The length of a standard MIDI file, through its tempo map."""
    fmt, ntr, div = struct.unpack(">HHH", d[8:14])
    p = 14
    tempos = []          # (tick, microseconds per quarter)
    last = 0
    for _ in range(ntr):
        ln = struct.unpack(">I", d[p + 4:p + 8])[0]
        q, end, tick, st = p + 8, p + 8 + ln, 0, 0
        while q < end:
            dt, q = vlq(d, q)
            tick += dt
            last = max(last, tick)
            s = d[q]
            if s == 0xFF:
                typ = d[q + 1]
                L, q2 = vlq(d, q + 2)
                if typ == 0x51:
                    tempos.append((tick, int.from_bytes(d[q2:q2 + L], "big")))
                q = q2 + L
                continue
            if s in (0xF0, 0xF7):
                L, q2 = vlq(d, q + 1)
                q = q2 + L
                continue
            if s & 0x80:
                st = s
                q += 1
            q += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
        p = end
    tempos.sort()
    secs, tick, us = 0.0, 0, 500000
    for t, u in tempos:
        secs += (t - tick) / div * us / 1e6
        tick, us = t, u
    secs += (last - tick) / div * us / 1e6
    return secs


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    fork, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    by = {t: {r.id: r for r in lst} for t, lst in rezfork.load(fork).items()}
    doc = {"instruments": {}, "samples": {}, "songs": {}}
    for rid, r in sorted(by.get("INST", {}).items()):
        snd, root, pan, f1, f2, smod = struct.unpack(">hhbBBb", r.data[:8])
        doc["instruments"][str(rid)] = {"name": r.name, "snd": snd, "root": root,
                                        "flags1": f1, "flags2": f2, "smod": smod}
        s = by["snd "].get(snd)
        if s is not None and str(snd) not in doc["samples"]:
            rate, ls, le, base, length = snd_header(s.data)
            doc["samples"][str(snd)] = {"file": "sounds/%d-%s.wav" % (snd, s.name),
                                        "rate": round(rate, 3), "loop": [ls, le],
                                        "base": base, "length": length}
    for rid, r in sorted(by.get("SONG", {}).items()):
        (midi, res0, reverb, tempo, stype, pitch, maxfx, maxnotes, mix,
         f1, decay, perc, f2, nremap) = struct.unpack(">hbbHbbbbhBbbBh", r.data[:18])
        remap = {}
        for k in range(nremap):
            a, b = struct.unpack(">hh", r.data[18 + 4 * k:22 + 4 * k])
            remap[str(a)] = b
        m = by["MIDI"].get(midi)
        entry = {"name": r.name, "midi_id": midi, "max_notes": maxnotes,
                 "note_decay": decay, "mix": mix, "flags1": f1, "flags2": f2,
                 "reverb": reverb, "tempo": tempo, "remap": remap}
        if m is not None:
            entry["midi"] = "music/%d-%s.mid" % (midi, m.name.replace(".MID", ""))
            entry["seconds"] = round(midi_seconds(m.data), 2)
        doc["songs"][str(rid)] = entry
    for rid, r in sorted(by.get("MIDI", {}).items()):
        path = os.path.join(out, "%d-%s.mid" % (rid, r.name.replace(".MID", "")))
        with open(path, "wb") as f:
            f.write(r.data)
        print("MIDI %5d %-13s %6d bytes %6.1f s -> %s" % (rid, r.name, len(r.data), midi_seconds(r.data), os.path.relpath(path)))
    with open(os.path.join(out, "music.json"), "w") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
    print("%d instruments, %d samples, %d songs -> %s" % (len(doc["instruments"]), len(doc["samples"]), len(doc["songs"]), os.path.join(out, "music.json")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
