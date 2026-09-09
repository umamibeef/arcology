"""Run the MISC builder ($2A186) under the interpreter and print the
index -> source-global map it produces."""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kemu import Emu
from thinkdata import build, A5LOW

L = open(os.path.join(os.path.dirname(__file__), "..", "out", "CODE_2.asm")).read().split("\n")
emu = Emu(L, build(), A5LOW)

# pointer globals the builder dereferences: give each a real block.
ptrs = set()
for line in L:
    m = re.match(r'^(02A[0-9A-F]{3}|02AC[0-9A-F]{2}|02AD[0-1][0-9A-F]): ', line)
    if m and 'movea.l' in line:
        mm = re.search(r'movea\.l\s+\$([0-9a-f]+)\(a5\)', line)
        if mm: ptrs.add(int(mm.group(1), 16))
for off in sorted(ptrs):
    blk = emu.alloc(0x2000)
    emu.wr(Emu.A5 + off, 4, blk)
    if off == 0x11b8:
        emu.miscbuf = blk
print("pointer globals populated: %s" % ", ".join("$%04X" % p for p in sorted(ptrs)))
print("miscBuf at 0x%X\n" % emu.miscbuf)

err = emu.run(0x2A186, 0x2AD14)
print("run: %s   stores=%d\n" % (err or "completed", len(emu.stores)))

seen = {}
for idx, prov in emu.stores:
    seen.setdefault(idx, prov)
known = {v: k for k, v in {
    "g_yearFounded":0x0BF2,"g_gameLevel":0x139E,"g_cityDate":0x1E1E,"g_MISC16":0x1E22,
    "g_funds":0x1E26,"g_bondCount":0x1E2A,"g_speed":0x1E32,"g_MISC8":0x1E36,
    "RCIdemand":0x1E3C,"g_ordinances":0x1E6E,"g_cityValue":0x1E72,"g_landValueTot":0x1E76,
    "g_crimeTot":0x1E7A,"g_trafficTot":0x1E7E,"g_pollutionTot":0x1E82,"g_powerPct":0x1E86,
    "g_waterPct":0x1E8A,"g_population":0x1E96,"weather1":0x1F01,"weather2":0x1F02,
    "g_rotation":0x2C24,"g_2C86":0x2C86,
}.items()}
def label(p):
    if not p: return "-"
    m = re.match(r'A5([+-]\d+)$', p)
    if not m: return p
    off = int(m.group(1))
    return "%s  (A5+0x%04X)" % (known.get(off, ""), off) if off >= 0 else "(A5-0x%04X)" % -off
print("recovered %d distinct MISC indices" % len(seen))
hits = [(i, label(p)) for i, p in sorted(seen.items()) if p]
print("of which %d carry a source global\n" % len(hits))
for i, l in hits[:60]:
    print("  MISC[%4d] @0x%04X <- %s" % (i, i*4, l))
