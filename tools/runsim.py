"""Run a real simulation routine from the binary against a real city.

This is the oracle.  It builds the A5 world the game expects -- map
layers, row-pointer tables, globals restored from MISC -- and then
executes the original's own 68k code under the interpreter.  Whatever
comes back is what the game does, with no transcription in the way.
"""
import sys, os, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW
from sc2 import load as city_load

A5 = Emu.A5
HERE = os.path.dirname(os.path.abspath(__file__))

#   A5 offset of the row-pointer table, rows, columns, bytes per cell
LAYERS = {
    "ALTM": (0x1FC2, 128, 128, 2), "XBLD": (0x21C2, 128, 128, 1),
    "XZON": (0x23C2, 128, 128, 1), "XTER": (0x25C2, 128, 128, 1),
    "XUND": (0x27C2, 128, 128, 1), "XTXT": (0x29C2, 128, 128, 1),
    "XBIT": (0x1BBA, 128, 128, 1),
    "XTRF": (0x15BA,  64,  64, 1), "XPLT": (0x16BA,  64,  64, 1),
    "XVAL": (0x17BA,  64,  64, 1), "XCRM": (0x18BA,  64,  64, 1),
    "XPLC": (0x19BA,  32,  32, 1), "XFIR": (0x1A3A,  32,  32, 1),
    "XPOP": (0x1ABA,  32,  32, 1), "XROG": (0x1B3A,  32,  32, 1),
}
# pointer globals the passes dereference, and how big the game makes them
BLOCKS = {0x11B8: 4800, 0x2C30: 0x700, 0x1EF6: 0x200, 0x1EBA: 0x20,
          0x1EFA: 0x20, 0x13B2: 0x800, 0x1EBE: 0xC, 0x1EC2: 0x80,
          0x1EC6: 8, 0x1ECA: 0x10, 0x1ECE: 0x10, 0x1ED2: 0x10,
          0x1ED6: 0x48, 0x1EDA: 0x24, 0x1EDE: 0x50, 0x1EE2: 0x50,
          #  A5+0x2BDC..0x2C18 are NOT separate blocks: see _graph.
          0x2BCA: 480,   # XTHG, forty 12-byte moving-object records
          0x2BC6: 1200,  # XMIC, the microsim records
          0x1EE6: 0x50, 0x1EEA: 0x16, 0x1EEE: 0x16, 0x1EF2: 0x2C}

_LISTING = None
_A5IMAGE = None

class Sim:
    def __init__(self, city_path):
        global _LISTING
        if _LISTING is None:
            _LISTING = open(os.path.join(HERE, "..", "out", "CODE_2.asm")).read().split("\n")
        listing = _LISTING
        global _A5IMAGE
        if _A5IMAGE is None:
            _A5IMAGE = build()
        self.e = Emu(listing, _A5IMAGE, A5LOW)
        self.ch, _ = city_load(city_path)
        self.misc = struct.unpack(">1200i", self.ch[b"MISC"])
        self.base = {}
        self._blocks()
        self._layers()
        self._raw()
        self._globals()
        self._shapes()
        self._graph()

    def _shapes(self):
        """the shape descriptor at A5+0x1226, eight bytes an id.

        Only the height at +4 matters to the simulation -- $C2DA divides
        it by three and crashes an aeroplane into anything taller than it
        is flying.  Left unallocated, that read goes through a NULL
        pointer to whatever sits at address 0.
        """
        from tset import shapes
        desc = self.e.alloc(0x2EE0)
        for k in range(0, 0x2EE0, 4):
            self.e.wr(desc + k, 4, 0)
        for sid, (w, h, _stream) in shapes().items():
            if sid * 8 + 8 > 0x2EE0:
                continue
            self.e.wr(desc + sid * 8 + 4, 2, h)
            self.e.wr(desc + sid * 8 + 6, 2, w)
        self.e.wr(A5 + 0x1226, 4, desc)
        self.base[0x1226] = desc

    def _graph(self):
        """XGRP, the graph history at A5+0x2BDC.

        $2D52E allocates ONE 0xD00 block and $2D54A slices it into
        sixteen series of 0x34 longs, storing the sixteen pointers at
        A5+0x2BDC through 0x2C18.  They are not sixteen separate
        allocations, and A5+0x2BEC..0x2BF8 -- the four map-overlay
        averages -- are simply entries 4 to 7.

        A5+0x2C1C is a seventeenth pointer to sixteen longs, the
        per-series vertical scale.  $2D594 brings each up as 1, not 0.

        The block is seeded from the city's own XGRP chunk so the
        oracle and the C model start one month apart from the same
        history.
        """
        NS, NSAMP = 16, 0x34
        blk = self.e.alloc(NS * NSAMP * 4)
        src = self.ch.get(b"XGRP") or b""
        for i in range(NS * NSAMP):
            o = i * 4
            v = int.from_bytes(src[o:o + 4], "big") if o + 4 <= len(src) else 0
            self.e.wr(blk + o, 4, v)
        for i in range(NS):
            self.e.wr(A5 + 0x2BDC + i * 4, 4, blk + i * NSAMP * 4)
        self.base[0x2BDC] = blk

        scale = self.e.alloc(NS * 4)
        for i in range(NS):
            self.e.wr(scale + i * 4, 4, 1)          # $2D594
        self.e.wr(A5 + 0x2C1C, 4, scale)
        self.base[0x2C1C] = scale

    def _blocks(self):
        for off, size in BLOCKS.items():
            p = self.e.alloc(max(size, 0x40))
            self.e.wr(A5 + off, 4, p)
            self.base[off] = p
        # the shared scratch plane: 128 rows of 128 words at A5+0x13BA
        rows = self.e.alloc(128 * 128 * 2)
        for r in range(128):
            self.e.wr(A5 + 0x13BA + r * 4, 4, rows + r * 128 * 2)
        self.base[0x13BA] = rows

    RAW_BLOCKS = {0x2BCA: b"XTHG", 0x2BC6: b"XMIC"}

    def _raw(self):
        """Chunks the save stores verbatim behind a pointer.  Without
        this the moving-object table reads as all-zero and $9DDA hands
        out slot 1 every time."""
        for off, tag in self.RAW_BLOCKS.items():
            src = self.ch.get(tag)
            if not src: continue
            n = min(len(src), BLOCKS[off])
            for i in range(n):
                self.e.wr(self.base[off] + i, 1, src[i])

    def _layers(self):
        for name, (off, h, w, bpc) in LAYERS.items():
            buf = self.e.alloc(h * w * bpc + 16)
            self.base[name] = buf
            for r in range(h):
                self.e.wr(A5 + off + r * 4, 4, buf + r * w * bpc)
            src = self.ch.get(name.encode())
            if src:
                self.e.mem[buf:buf + len(src)] = src

    def _globals(self):
        """Restore the whole A5 world from MISC.

        The map comes from tools/miscload.py, which ran the game's own
        unpacker at $295D6 with every read tagged by its MISC index, so
        each value lands at the address and the width the game gives it
        -- including the ones inside pointer blocks, which the earlier
        builder-side map could not name.  Setting up the oracle by hand
        instead is how it came to run the coverage stage with a budget
        block full of zeros.
        """
        m = json.load(open(os.path.join(HERE, "..", "out", "miscload.json")))
        placed = 0
        for idx, d in m.items():
            i = int(idx)
            if i >= len(self.misc): continue
            block, off, size = d["block"], d["off"], d["size"]
            if block is None:
                if off == 0x11B8: continue          # the MISC buffer itself
                addr = A5 + off
            else:
                if block not in self.base: continue
                addr = self.base[block] + off
            v = self.misc[i] & 0xFFFFFFFF
            self.e.wr(addr, size, v & ((1 << (8 * size)) - 1))
            placed += 1
        self.placed = placed

        # the month and the year count are derived from the date rather
        # than stored -- $1523E and $15268 in updateWindowTitle
        date = self.misc[4]
        month = (date // 25) % 12
        self.e.wr(A5 + 0x1E32, 2, month & 0xFFFF)
        #  $15256 -- the season, derived the same way and just as much
        #  not saved.  Left at zero it puts the weather walk on the wrong
        #  row of its table, which reads as the model getting the weather
        #  wrong rather than the oracle starting cold.
        self.e.wr(A5 + 0x1E34, 2, (((month + 1) % 12) // 3) & 0xFFFF)
        yrs = date // 300
        if yrs > 0x3E80: yrs = 10000
        self.e.wr(A5 + 0x1E38, 4, yrs & 0xFFFFFFFF)

    def run(self, start, stubs=(0x9728,), limit=40000000, watch=None):
        self.e.a[7] = Emu.SP_INIT
        r = self.e.run(start, -1, limit=limit, real_calls=True,
                       stubs=stubs if isinstance(stubs, dict) else set(stubs),
                       watch=watch)
        if r is not None:
            #  ANY error means the run stopped early, so the state left
            #  behind is not the original's answer.  Reporting it as
            #  ground truth invents a result -- which is exactly what a
            #  silent unhandled trap did to the earthquake.
            raise RuntimeError("$%05X stopped early: %s" % (start, r))
        return r

    def call(self, addr, args=(), lfsr=1, tb=1, stubs=(0x9728,), limit=40000000):
        """Call a routine with word arguments, Pascal order, both random
        number generators seeded so the C can be given the same dice."""
        self.e.wr(A5 + 0x11DC, 2, lfsr & 0xFFFF)
        self.e.tb_seed = tb
        self.e.a[7] = Emu.SP_INIT
        for v in reversed(args):
            self.e.a[7] -= 2
            self.e.wr(self.e.a[7], 2, v & 0xFFFF)
        self.e.a[7] -= 4
        self.e.wr(self.e.a[7], 4, 0xDEAD0000)
        r = self.e.run(addr, 0xDEAD0000, limit=limit, real_calls=True,
                       stubs=stubs if isinstance(stubs, dict) else set(stubs))
        if r is not None:
            raise RuntimeError("$%05X stopped early: %s" % (addr, r))
        return r

    def block(self, off, size):
        b = self.base[off]
        return bytes(self.e.mem[b:b + size])

    def glob(self, off, size=4):
        return self.e.rd(A5 + off, size)

    def layer(self, name):
        off, h, w, bpc = LAYERS[name]
        b = self.base[name]
        return bytes(self.e.mem[b:b + h * w * bpc])

if __name__ == "__main__":
    path = sys.argv[1]
    s = Sim(path)
    before = {n: s.layer(n) for n in ("XVAL", "XCRM", "XPOP", "XPLT")}
    err = s.run(0x2317E)
    print("run:", err or "completed")
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
        for n in ("XVAL", "XCRM", "XPOP", "XPLT"):
            open(os.path.join(sys.argv[2], n), "wb").write(s.layer(n))
    for n in ("XPLT", "XVAL", "XPOP", "XCRM"):
        after = s.layer(n)
        stored = before[n]
        same = sum(1 for a, b in zip(after, stored) if a == b)
        live = sum(1 for a, b in zip(after, stored) if a or b)
        liveok = sum(1 for a, b in zip(after, stored) if (a or b) and a == b)
        print("  %-5s %6d/%-6d cells identical to the file   live %6d/%-6d %6.2f%%"
              % (n, same, len(after), liveok, live, 100.0 * liveok / live if live else 0))

#  ---- checkpoints ------------------------------------------------------
#  Winding the clock forward is the slow part of every investigation, and
#  the answer is always the same city and the same tick, so keep it.
#
#  The key includes the mtimes of the interpreter and this file: the
#  ORACLE is the original's own code and does not change, but the
#  machinery around it does, and a checkpoint written by an older
#  interpreter is not one you want silently reused.
import hashlib as _hashlib
import pickle as _pickle

_CKPT_DIR = os.path.join(HERE, "..", "out", "ckpt")


def _ckpt_key(city_path, ticks, tag):
    h = _hashlib.sha1()
    for f in (__file__, os.path.join(HERE, "m68kemu.py"), city_path):
        h.update(("%s:%d" % (f, int(os.path.getmtime(f)))).encode())
    h.update(("%d:%s" % (ticks, tag)).encode())
    return h.hexdigest()[:16]


#  The seven generators, watched at their RTS.  clock_check needs the
#  draw log as well as the machine state, so the checkpoint carries it --
#  otherwise resuming would save the slow part and then throw away the
#  only thing the dice comparison can use.
_RNG_RTS = {0x20F4A: "0", 0x20F62: "1", 0x20F7A: "3", 0x20F92: "f",
            0x20FAA: "6", 0x20FC2: "7", 0x20F2E: "L"}


def _rng_watch(log):
    def mk(kind):
        def f(e, pc):
            log.append((kind, e.d[0] & 0xFFFF))
        return f
    return {a: mk(k) for a, k in _RNG_RTS.items()}


def prebuild(city_path, upto, stubs=None, tag="clock", entry=0x21EDE):
    """Write a checkpoint for EVERY tick up to `upto` in one pass.

    Building them one at a time re-runs the whole clock each time, which
    is the slow thing this module exists to stop.  One pass costs what
    the deepest one would have cost alone and leaves every shallower
    tick instant, so a bisect afterwards is free."""
    s = Sim(city_path)
    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    log = []
    s.e.rng_log = log
    watch = _rng_watch(log)
    os.makedirs(_CKPT_DIR, exist_ok=True)
    made = 0
    for n in range(1, upto + 1):
        s.run(entry, stubs=stubs, limit=200000000, watch=watch)
        path = os.path.join(_CKPT_DIR, _ckpt_key(city_path, n, tag) + ".pkl")
        if os.path.exists(path):
            continue
        tmp = path + ".tmp"
        snap = s.e.snapshot()
        snap["rng_log"] = [(k, v) for k, v, *_ in log]
        with open(tmp, "wb") as f:
            _pickle.dump(snap, f, protocol=4)
        os.replace(tmp, path)
        made += 1
    return made


def at_tick(city_path, ticks, stubs=None, tag="clock", entry=0x21EDE):
    """A Sim wound `ticks` ticks forward, plus the draw log that got it
    there, from a checkpoint when there is one.  Seeds both generators
    the way every checker does."""
    key = _ckpt_key(city_path, ticks, tag)
    path = os.path.join(_CKPT_DIR, key + ".pkl")
    s = Sim(city_path)
    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    if os.path.exists(path):
        with open(path, "rb") as f:
            st = _pickle.load(f)
        s.e.restore(st)
        return s, st.get("rng_log", [])
    log = []
    s.e.rng_log = log
    watch = _rng_watch(log)
    for _ in range(ticks):
        s.run(entry, stubs=stubs, limit=200000000, watch=watch)
    os.makedirs(_CKPT_DIR, exist_ok=True)
    tmp = path + ".tmp"
    snap = s.e.snapshot()
    snap["rng_log"] = [(k, v) for k, v, *_ in log]
    with open(tmp, "wb") as f:
        _pickle.dump(snap, f, protocol=4)
    os.replace(tmp, path)
    return s, [(k, v) for k, v, *_ in log]
