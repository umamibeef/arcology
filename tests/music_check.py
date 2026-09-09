#!/usr/bin/env python3
"""music_check.py -- the music player against arithmetic it cannot fake.

    python3 tests/music_check.py build/arcology

Two checks, both through `arcology --song`, the headless render:
  1. a flute played at MIDI 72 must sound exactly an octave above the
     same flute at 60 -- the pitch rule (root key, reference rate) in one
     number, read off the rendered audio's spectrum;
  2. the game's own theme, SONG 10018, renders to its MIDI's length and
     is not silent.
Needs assets/music, i.e. tools/import_assets.py has run.
"""
import cmath
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.insert(0, (v & 0x7F) | 0x80)
        v >>= 7
    return bytes(out)


def one_note_midi(path, channel, note):
    st_on, st_off = 0x90 | channel, 0x80 | channel
    ev = vlq(0) + bytes([st_on, note, 100]) + vlq(960) + bytes([st_off, note, 0]) + vlq(0) + b"\xff\x2f\x00"
    trk = b"MTrk" + struct.pack(">I", len(ev)) + ev
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + trk)


def read_wav(path):
    w = wave.open(path)
    n, r = w.getnframes(), w.getframerate()
    s = struct.unpack("<%dh" % n, w.readframes(n))
    w.close()
    return r, s


def fundamental(rate, seg):
    """The lowest spectral peak within a quarter of the strongest."""
    n = len(seg)
    win = [seg[i] * (0.5 - 0.5 * math.cos(2 * math.pi * i / n)) for i in range(n)]
    mags = []
    for k in range(1, n // 8):
        w = -2j * math.pi * k / n
        acc = sum(win[i] * cmath.exp(w * i) for i in range(0, n, 2))
        mags.append((k * rate / n, abs(acc)))
    top = max(v for f, v in mags)
    return next(f for f, v in mags if v > 0.25 * top)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "build/arcology"
    tmp = tempfile.mkdtemp()
    ok = True

    def render(song, out):
        r = subprocess.run([exe, "--song", song, out], capture_output=True, text=True)
        return r.returncode == 0 and os.path.exists(out)

    # 1. the octave
    freqs = []
    for note in (60, 72):
        mid, wav = os.path.join(tmp, "n%d.mid" % note), os.path.join(tmp, "n%d.wav" % note)
        one_note_midi(mid, 13, note)  # channel 13 is the flute under the default remap
        if not render(mid, wav):
            print("render failed for note", note)
            return 1
        rate, s = read_wav(wav)
        freqs.append(fundamental(rate, s[int(0.05 * rate):int(0.05 * rate) + 4096]))
    ratio = freqs[1] / freqs[0]
    print("octave: flute at 60 -> %.1f Hz, at 72 -> %.1f Hz, ratio %.3f" % (freqs[0], freqs[1], ratio))
    ok &= abs(ratio - 2.0) < 0.03

    # 2. the theme
    wav = os.path.join(tmp, "theme.wav")
    if not render("10018", wav):
        print("the theme did not render (no assets/music?)")
        return 1
    rate, s = read_wav(wav)
    secs = len(s) / rate
    rms = math.sqrt(sum(x * x for x in s[::16]) / (len(s) // 16)) / 32768
    print("theme: %.1f s, rms %.3f" % (secs, rms))
    ok &= 100.0 <= secs <= 120.0 and rms > 0.02
    print("music check:", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
