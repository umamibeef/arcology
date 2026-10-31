#!/usr/bin/env python3
"""The witness of invariant 2: a window lays what a whole build lays.

The city is built whole once, and then as each window named, alone.  For
every chunk a window lays, the faces of the window's build are held to the
faces the whole build files under that chunk.  A face is filed under the
chunk of its first corner, as the mesh files it.  A face is compared as
the tile dump prints it: what drew it, its slot and its three corners,
bit for bit.  A window that lays one face more, one face less or one face
moved fails, and the chunks where it does are printed with what drew the
faces.

    tools/window_witness.py BINARY CITY C0,R0,C1,R1 [C0,R0,C1,R1 ...]

A window is named in tiles, as --build-window names it, and lays every
chunk those tiles lie in.  The whole build and each window run through
arcrun.world, so a build that fails is a fault and never a match.
"""
import collections
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from arcrun import world, Fault  # noqa: E402

CHUNK = 32


def faces(text):
    """The faces of a tile dump, by the chunk of each one's first corner."""
    by = collections.defaultdict(collections.Counter)
    for line in text.splitlines():
        if not line.startswith("face "):
            continue
        head, _, rest = line.partition(" slot ")
        corner = rest.split()[1]
        x, y = struct.unpack("<ff", bytes.fromhex(corner[:16]))
        by[(int(x) // CHUNK, int(y) // CHUNK)][line] += 1
    return by


def drawn_by(line):
    return " ".join(line.partition(" slot ")[0].split()[2:5])


def build(binary, city, window):
    args = [city, "--mute", "--tile-dump", "all"]
    if window:
        args += ["--build-window", window]
    with tempfile.TemporaryDirectory() as d:
        return faces(world(binary, args, dump=os.path.join(d, "dump.txt")))


def chunks_of(window):
    c0, r0, c1, r1 = (int(v) for v in window.split(","))
    return [(c, r) for r in range(min(r0, r1) // CHUNK, max(r0, r1) // CHUNK + 1)
            for c in range(min(c0, c1) // CHUNK, max(c0, c1) // CHUNK + 1)]


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    binary, city, windows = argv[1], argv[2], argv[3:]
    try:
        whole = build(binary, city, None)
        bad = 0
        for window in windows:
            part = build(binary, city, window)
            for ch in chunks_of(window):
                want, got = whole.get(ch, collections.Counter()), part.get(ch, collections.Counter())
                lost, extra = want - got, got - want
                n_lost, n_extra = sum(lost.values()), sum(extra.values())
                state = "same" if not (n_lost or n_extra) else "DIFFERS"
                print("window %-17s chunk %2d,%2d  %7d faces  %s%s" % (
                    window, ch[0], ch[1], sum(want.values()), state,
                    "" if state == "same" else ": %d faces the whole build lays are missing, %d are extra" % (n_lost, n_extra)))
                if n_lost or n_extra:
                    bad += 1
                    for what, label in ((lost, "missing"), (extra, "extra")):
                        by = collections.Counter()
                        for line, n in what.items():
                            by[drawn_by(line)] += n
                        for name, n in by.most_common(6):
                            print("    %-8s %6d  %s" % (label, n, name))
    except Fault as e:
        print("FAULT: %s" % e)
        return 1
    print("window witness: %d of the chunks laid differ from the whole build" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
