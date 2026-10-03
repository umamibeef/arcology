#!/usr/bin/env python3
"""Diff the player's tools against the original's own code.

Two modes, and both compare every full-resolution layer of every tile,
the funds, the census, the military tallies, the transit count and the
first moving-object record.

  --call ADDR CITY...   one ported tile routine, called at a spread of
                        tiles in turn.  The oracle calls the original
                        routine at the same tiles in the same order.
  --tool CITY...        whole gestures.  The C drives a SimGesture, and
                        the oracle runs the tool's own mouse loop with a
                        scripted mouse: $8E60, which turns the mouse into
                        a tile, answers the gesture's tiles one per call,
                        and _StillDown holds the button until the last.

The dice are seeded once, on both sides, before the first call.  Only
the interface is stubbed: drawing, sound, the title bar and the
newspaper.

    python3 tools/tool_check.py --call 8064 cities/atlanta.sc2
    python3 tools/tool_check.py --tool cities/*.sc2
"""
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from runsim import Sim, A5

SIMBIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "..", "build", "arcology")

#  The interface, and nothing else.  $15A54 and $155CA redraw, $A3E4
#  draws the moving things, $1522A is the title bar, $154A fetches a
#  string, $2EDE4 prints the newspaper, $1DEA shows it.  $30D6, $30EE
#  and $3106 start, stop and ask after a repeating sound, $3F636 plays
#  one.  The rest are the QuickDraw side of $5FAA's collapse, as in
#  raise_check.py.
STUBS = {a: None for a in (
    0x9728, 0x15A54, 0x18E96, 0x18D40, 0x18E62, 0x1DEA, 0x3F636, 0x392E,
    0x30FE, 0x155CA, 0x15408, 0x370A4, 0x3D566, 0xA3E4, 0x1522A, 0x154A,
    0x2EDE4, 0x30EE, 0x30D6, 0x3106)}

KEY, UNDERGROUND = 1, 2          # GESTURE_ in sim.h
LAYERS = ("ALTM", "XBIT", "XZON", "XTER", "XBLD", "XUND", "XTXT")

#  The tool's loop the oracle runs for a gesture, by tool.
TOOL_LOOP = {0: 0x7D44}


def call(s, addr, args, stubs):
    """Call a routine with word arguments, as the original pushes them.
    The dice are NOT seeded here: a run is one stream of draws."""
    s.e.a[7] = Emu.SP_INIT
    for v in reversed(args):
        s.e.a[7] -= 2
        s.e.wr(s.e.a[7], 2, v & 0xFFFF)
    s.e.a[7] -= 4
    s.e.wr(s.e.a[7], 4, 0xDEAD0000)
    r = s.e.run(addr, 0xDEAD0000, limit=40000000, real_calls=True,
                stubs=stubs)
    if r is not None:
        #  A run that stopped early is not the original's answer.
        raise RuntimeError("$%05X stopped early: %s" % (addr, r))


def oracle_setup(path):
    s = Sim(path)
    s.e.wr(A5 + 0x1E26, 4, 25000)       # the funds, as the C sets them
    s.e.wr(A5 + 0x11DC, 2, 1)           # the LFSR
    s.e.tb_seed = 1                     # the Toolbox _Random
    s.e.wr(A5 - 0x8000, 1, 0)           # the sound is off
    #  A saved city is in city mode, never the terrain editor's, as
    #  city_load sets it.  The A5 image holds 0, and $128DE treats trees
    #  otherwise in the editor.
    s.e.wr(A5 - 0x7DE6, 2, 1)
    return s


def oracle_state(s):
    lay = {n: s.layer(n) for n in LAYERS}
    out = {}
    for i in range(16384):
        alt = int.from_bytes(lay["ALTM"][2 * i:2 * i + 2], "big")
        out[("a", i)] = (alt,) + tuple(lay[n][i] for n in LAYERS[1:])
    out[("m", 0)] = (s.glob(0x1E26) & 0xFFFFFFFF,)
    cen = s.block(0x1EF6, 0x200)
    for i in range(256):
        out[("c", i)] = (int.from_bytes(cen[2 * i:2 * i + 2], "big"),)
    inf = s.block(0x1EFA, 0x20)
    for i in range(16):
        out[("i", i)] = (int.from_bytes(inf[2 * i:2 * i + 2], "big"),)
    out[("x", 0)] = (s.glob(0x1EFE, 2),)
    out[("t", 0)] = tuple(s.block(0x2BCA, 12))
    return out


def c_state(argv):
    r = subprocess.run([SIMBIN] + argv, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("arcology %s: %s" % (argv[0], r.stderr.strip()))
    out, ev = {}, []
    for line in r.stdout.splitlines():
        f = line.split()
        if not f:
            continue
        if f[0] in ("a", "m", "c", "i", "x", "t"):
            out[(f[0], int(f[1]))] = tuple(int(v, 16) for v in f[2:])
        elif f[0] == "e":
            ev.append(tuple(int(v) for v in f[1:]))
    return out, ev


#  What each fitter is about: the layer its network is in, and the range
#  of values that is the network.
FITTER = {
    0x1F632: ("XBLD", 0x1D, 0x2C),
    0x1FB60: ("XBLD", 0x2C, 0x3B),
    0x1F2FE: ("XBLD", 0x0E, 0x1D),
    0x1FFB8: ("XUND", 0x10, 0x1F),
    0x20352: ("XUND", 0x01, 0x10),
}


#  The shapes a disturbed tile is given, so that the water fitter meets
#  each kind of XTER it tells apart: flat land, a slope, under water, a
#  shore, open water, a channel, and the two shapes it leaves alone.
WATER_SHAPES = (0x00, 0x06, 0x00, 0x13, 0x24, 0x30, 0x3E, 0x43, 0x2E,
                0x0B, 0x2A, 0x3D)


def water_tiles(s):
    """The water fitter's tiles.  Every fourth water tile loses its water
    and takes a shape from WATER_SHAPES, and some others keep their
    water but take a shape that does not fit.  Then the fitter runs at
    every tile that is still water.  Then it runs where there is no water:
    half the tiles that lost theirs, a third of the land beside the water,
    and a spread over the whole map, so it floods them."""
    bit = s.layer("XBIT")
    wet = [i for i in range(16384) if bit[i] & 0x04]
    dry = wet[1::4]
    odd = wet[2::4][::2]
    gone = set(dry)
    near = [i for i in range(16384) if not bit[i] & 0x04 and any(
        0 <= i // 128 + dy < 128 and 0 <= i % 128 + dx < 128 and
        bit[i + 128 * dy + dx] & 0x04
        for dy, dx in ((0, -1), (1, 0), (0, 1), (-1, 0)))]
    out = []
    for n, i in enumerate(dry):
        out.append(divmod(i, 128) + ("XBIT", bit[i] & ~0x04))
        out.append(divmod(i, 128) + ("XTER", WATER_SHAPES[n % 12]))
    for n, i in enumerate(odd):
        out.append(divmod(i, 128) + ("XTER", WATER_SHAPES[5 * n % 12]))
    out += [divmod(i, 128) for i in wet if i not in gone]
    out += [divmod(i, 128) for i in dry[::2] + near[::3]]
    out += [((k * 29) % 128, (k * 41) % 128) for k in range(60)]
    return out


def tiles_for(path, addr):
    """What --call does, in order: a tile is (y, x) to call the routine
    at, or (y, x, layer, value) to write first.  The bulldozer and the
    others get a spread over the whole map.  A fitter's network loses
    every fourth tile, so the rest meet dead ends, corners and gaps, and
    then the fitter runs at every tile still in the network.  The water
    fitter's tiles are water_tiles."""
    s = Sim(path)
    if addr == 0x122E0:
        return water_tiles(s)
    if addr not in FITTER:
        return [((k * 29) % 128, (k * 41) % 128) for k in range(300)]
    name, lo, hi = FITTER[addr]
    lay = s.layer(name)
    net = [i for i in range(16384) if lo <= lay[i] < hi]
    gone = set(net[1::4])
    return ([divmod(i, 128) + (name, 0) for i in sorted(gone)] +
            [divmod(i, 128) for i in net if i not in gone])


#  The letter --call writes a layer by.  XBLD and XBIT share their
#  second letter, so it is a table.
SET_LETTER = {"XBLD": "B", "XUND": "U", "XTER": "T", "XBIT": "I"}


def tile_arg(t):
    if len(t) == 4:
        return "%d,%d=%s%02X" % (t[0], t[1], SET_LETTER[t[2]], t[3])
    return "%d,%d" % t


def gestures_for(path, n=40):
    """Gestures a hand could make: a start, then up to seven moves of one
    or two tiles in any direction, never staying on the same tile."""
    rnd = random.Random(os.path.basename(path))
    out = []
    for k in range(n):
        y, x = (k * 29 + 7) % 128, (k * 41 + 3) % 128
        tiles = [(y, x)]
        for _ in range(rnd.randrange(8)):
            while True:
                ny = min(127, max(0, y + rnd.randint(-2, 2)))
                nx = min(127, max(0, x + rnd.randint(-2, 2)))
                if (ny, nx) != (y, x):
                    break
            y, x = ny, nx
            tiles.append((y, x))
        sub = 2 if k % 5 == 4 else 0
        flags = (KEY if k % 7 == 3 else 0) | (UNDERGROUND if k % 9 == 5 else 0)
        out.append((0, sub, flags, tiles))
    return out


def oracle_gesture(s, tool, sub, flags, tiles):
    s.e.wr(A5 + 0x1F88 + 2 * tool, 2, sub)
    s.e.wr(A5 - 0x7DE4, 1, 0xFF if flags & UNDERGROUND else 0)
    rest = tiles[1:]
    seen = [0]

    def pick(e):
        k = seen[0]
        seen[0] += 1
        y, x = rest[k] if k < len(rest) else tiles[-1]
        return (x << 8) | y

    s.e.still_down = lambda e: seen[0] <= len(rest)
    stubs = dict(STUBS)
    stubs[0x8E60] = pick
    stubs[0x810] = 0xFF if flags & KEY else 0
    call(s, TOOL_LOOP[tool], tiles[0], stubs)
    s.e.still_down = None


def compare(name, want, got):
    bad, shown = 0, 0
    tally = {}
    for k in sorted(want):
        if k[0] == "t" and k not in got:
            continue                    # the city has no XTHG chunk
        if want[k] != got.get(k):
            bad += 1
            if k[0] == "a":
                g = got.get(k) or (None,) * len(want[k])
                for j, n in enumerate(LAYERS):
                    if want[k][j] != g[j]:
                        tally[n] = tally.get(n, 0) + 1
            else:
                n = {"m": "funds", "c": "census", "i": "infra", "x": "transit",
                     "t": "thing"}[k[0]]
                tally[n] = tally.get(n, 0) + 1
            if shown < 10:
                shown += 1
                if k[0] == "a":
                    w, g = want[k], got.get(k) or ()
                    diff = ", ".join("%s %X->%X" % (LAYERS[j], w[j], g[j])
                                     for j in range(len(w))
                                     if j < len(g) and w[j] != g[j])
                    print("  %s (%d,%d): %s  [want %s]" % (
                        name, k[1] // 128, k[1] % 128, diff,
                        " ".join("%X" % v for v in w)))
                else:
                    print("  %s %s %d: want %s got %s" % (
                        name, k[0], k[1], want[k], got.get(k)))
    if tally:
        print("  %s differs in: %s" % (name, ", ".join(
            "%s %d" % kv for kv in sorted(tally.items()))))
    return bad, len(want)


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("--call", "--tool"):
        print(__doc__)
        return 2
    bad = tot = 0
    if args[0] == "--call":
        addr = int(args[1], 16)
        stubs = dict(STUBS)
        stubs[0x810] = 0
        for path in args[2:]:
            tiles = tiles_for(path, addr)
            if not any(len(t) == 2 for t in tiles):
                print("%s: nothing to call $%X at" % (os.path.basename(path), addr))
                continue
            s = oracle_setup(path)
            s.e.wr(A5 + 0x1F88, 2, 0)          # the bulldozer demolishes
            s.e.wr(A5 - 0x7DE4, 1, 0)          # on the surface
            for t in tiles:
                if len(t) == 4:
                    s.e.mem[s.base[t[2]] + t[0] * 128 + t[1]] = t[3]
                else:
                    call(s, addr, t, stubs)
            got, _ = c_state(["--call", path, "%X" % addr] +
                             [tile_arg(t) for t in tiles])
            b, t = compare(os.path.basename(path), oracle_state(s), got)
            print("%s: $%X at %d tiles, %d/%d values exact" % (
                os.path.basename(path), addr, len(tiles), t - b, t))
            bad += b
            tot += t
    else:
        for path in args[1:]:
            gs = gestures_for(path)
            s = oracle_setup(path)
            for tool, sub, flags, tiles in gs:
                oracle_gesture(s, tool, sub, flags, tiles)
            got, _ = c_state(["--tool", path] + [
                "%d,%d,%d:%s" % (tool, sub, flags,
                                 "/".join("%d,%d" % t for t in tiles))
                for tool, sub, flags, tiles in gs])
            b, t = compare(os.path.basename(path), oracle_state(s), got)
            print("%s: %d gestures, %d/%d values exact" % (
                os.path.basename(path), len(gs), t - b, t))
            bad += b
            tot += t
    print("tool_check: %d/%d values exact" % (tot - bad, tot))
    return 1 if bad else 0


sys.exit(main())
