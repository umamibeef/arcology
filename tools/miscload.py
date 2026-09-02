"""Recover the MISC -> destination map by running the game's own unpacker.

`$295D6` is the inverse of the MISC builder: it walks the 1200-long MISC
array with a single running index and scatters it across the A5 world.
Running it under the interpreter with every read tagged by its MISC index
gives the exact layout -- including the parts that land inside pointer
blocks (the budget records and the tile census), which the builder-side
map could not name because they are not A5 globals.
"""
import sys, os, json, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

HERE = os.path.dirname(os.path.abspath(__file__))
A5 = Emu.A5

# pointer globals the unpacker dereferences, and the size the game gives them
BLOCKS = {0x11B8: 4800, 0x2C30: 0x700, 0x1EF6: 0x200, 0x1EBA: 0x20,
          0x1EFA: 0x20, 0x13B2: 0x800, 0x1EBE: 0xC, 0x1EC2: 0x80,
          0x1EC6: 8, 0x1ECA: 0x10, 0x1ECE: 0x10, 0x1ED2: 0x10,
          0x1ED6: 0x48, 0x1EDA: 0x24, 0x1EDE: 0x50, 0x1EE2: 0x50,
          0x1EE6: 0x50, 0x1EEA: 0x16, 0x1EEE: 0x16, 0x1EF2: 0x2C,
          0x2BC2: 0x100, 0x2BC6: 0x100, 0x2BCA: 0x100, 0x2BDC: 0x100}
NAMES = {v: k for k, v in
         {"simParams": 0x2C30, "census": 0x1EF6, "accum8": 0x1EBA,
          "blk1EFA": 0x1EFA, "blk1EBE": 0x1EBE, "blk1EC2": 0x1EC2,
          "blk1EC6": 0x1EC6, "blk1ECA": 0x1ECA, "blk1ECE": 0x1ECE,
          "blk1ED2": 0x1ED2, "blk1ED6": 0x1ED6, "neighbors36": 0x1EDA,
          "blk1EDE": 0x1EDE, "blk1EE2": 0x1EE2, "blk1EE6": 0x1EE6,
          "blk1EEA": 0x1EEA, "blk1EEE": 0x1EEE, "blk1EF2": 0x1EF2}.items()}


class Tracer(Emu):
    """Tags every read out of the MISC buffer with its index, then records
    where that value comes to rest."""
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.dests = []          # (miscIndex, destAddr, size)
        self.miscbuf = None

    def read(self, t, size):
        val, prov = super().read(t, size)
        if self.miscbuf is not None and not t.startswith('#') \
           and not re.fullmatch(r'[da]\d', t):
            addr = self.ea_addr(t)
            if self.miscbuf <= addr < self.miscbuf + 4800:
                return val, "MISC%d" % ((addr - self.miscbuf) // 4)
        return val, prov

    def write(self, t, size, val, prov=None):
        if prov and prov.startswith("MISC") and not re.fullmatch(r'[da]\d', t):
            self.dests.append((int(prov[4:]), self.ea_addr(t), size))
        super().write(t, size, val, prov)


def run():
    listing = open(os.path.join(HERE, "..", "out", "CODE_2.asm")).read().split("\n")
    e = Tracer(listing, build(), A5LOW)
    base = {}
    for off, size in BLOCKS.items():
        p = e.alloc(max(size, 0x40) + 0x40)
        e.wr(A5 + off, 4, p)
        base[off] = p
    # the row-pointer tables, so map writes land somewhere sane
    for off, h, w, bpc in [(0x1FC2,128,128,2),(0x21C2,128,128,1),(0x23C2,128,128,1),
                           (0x25C2,128,128,1),(0x27C2,128,128,1),(0x29C2,128,128,1),
                           (0x1BBA,128,128,1),(0x15BA,64,64,1),(0x16BA,64,64,1),
                           (0x17BA,64,64,1),(0x18BA,64,64,1),(0x19BA,32,32,1),
                           (0x1A3A,32,32,1),(0x1ABA,32,32,1),(0x1B3A,32,32,1)]:
        buf = e.alloc(h * w * bpc + 64)
        for r in range(h):
            e.wr(A5 + off + r * 4, 4, buf + r * w * bpc)
    e.miscbuf = base[0x11B8]
    # identity fill so a stray value is still traceable by eye; the magic
    # in slot 0 is the record count the unpacker sanity-checks.
    for i in range(1200):
        e.wr(e.miscbuf + i * 4, 4, i)
    e.wr(e.miscbuf, 4, 0x122)
    # enter at the top so link/unlk balance; the chunk reader is stubbed
    # and its success flag supplied in d0.
    e.d[0] = 1
    err = e.run(0x295D6, -1, limit=20000000, real_calls=True,
                stubs={0x292F4, 0x320E, 0x140A, 0x31F6, 0x2F7E, 0x23EE})
    return e, base, err


def locate(addr, base):
    """(block, offset) -- block is the A5 offset of the pointer global the
    address lands inside, or None when it is an A5 global itself."""
    for off, p in sorted(base.items(), key=lambda kv: -kv[1]):
        if p <= addr < p + BLOCKS[off] + 0x40:
            return off, addr - p
    return None, addr - A5


def describe(block, rel):
    if block is None:
        return "A5%+d" % rel
    if block == 0x2C30:
        return "simParams[%d]+0x%02X" % (rel // 0x70, rel % 0x70)
    if block == 0x1EF6:
        return "census[%d]" % (rel // 2)
    return "%s+0x%X" % (NAMES.get(block, "blk%04X" % block), rel)


if __name__ == "__main__":
    e, base, err = run()
    print("run: %s   %d traced stores, final index d4=%d"
          % (err or "completed", len(e.dests), e.d[4] & 0xFFFF))
    seen = {}
    for idx, addr, size in e.dests:
        seen.setdefault(idx, (addr, size))
    out = {}
    for idx in sorted(seen):
        addr, size = seen[idx]
        block, rel = locate(addr, base)
        out[idx] = {"dest": describe(block, rel), "size": size,
                    "block": block, "off": rel}
    json.dump(out, open(os.path.join(HERE, "..", "out", "miscload.json"), "w"), indent=0)
    print("recovered %d of 1200 MISC indices" % len(out))
    if len(sys.argv) > 1 and sys.argv[1] == "-v":
        for idx in sorted(out):
            print("  MISC[%4d] -> %-24s .%d" % (idx, out[idx]["dest"], out[idx]["size"]))
