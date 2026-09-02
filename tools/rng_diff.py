"""Compare the C's dice with the original's, draw by draw, over a whole
growth cycle.  The first differing entry is where the reconstruction
took a different branch -- everything after it is noise."""
import sys, os, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SC2KSIM = os.path.join(os.path.dirname(__file__), "..", "build", "arcology")
#  the six generator entry points, watched at their rts so d0 holds the
#  masked result
#  kind letters match rng.c's logdraw() so the two logs line up
RTS = {0x20F4A: "0", 0x20F62: "1", 0x20F7A: "3",
       0x20F92: "f", 0x20FAA: "6", 0x20FC2: "7"}
RTS[0x20F2E] = "L"   # $20EE6/$20F12, THINK C rand()

def oracle(path):
    s = Sim(path); out = []
    def at_rts(kind):
        def f(e, pc): out.append((kind, e.d[0] & 0xFFFF))
        return f
    w = {a: at_rts(k) for a, k in RTS.items()}
    #  the emulator appends ("t", v) here for every toolbox _Random, so
    #  both generators land in ONE ordered list
    s.e.rng_log = out
    s.e.wr(A5 + 0x11DC, 2, 1); s.e.tb_seed = 1
    for yy in range(4):
        for xx in range(4):
            s.e.a[7] = 0x00300000
            for v in (xx, yy):
                s.e.a[7] -= 2; s.e.wr(s.e.a[7], 2, v)
            s.e.a[7] -= 4; s.e.wr(s.e.a[7], 4, 0xDEAD0000)
            err = s.e.run(0x3170E, 0xDEAD0000, limit=40000000,
                          real_calls=True, stubs={0x9728}, watch=w)
            if err: return None, err
    return out, None

def mine(path):
    r = subprocess.run([SC2KSIM, "--dump-growth-all", path, "/tmp", "--rng"],
                       capture_output=True, text=True)
    out = []
    for ln in r.stdout.split("\n"):
        f = ln.split()
        if len(f) == 2 and f[0] != "S":   # 'S' is a C-side marker, not a draw
            out.append((f[0], int(f[1])))
    return out

if __name__ == "__main__":
    path = sys.argv[1]
    o, err = oracle(path)
    if o is None:
        print("oracle failed:", err); sys.exit(1)
    m = mine(path)
    #  the emulator records ("t", value, pc) for toolbox draws so a
    #  divergence can be located; the C log has no pc.  Compare the two
    #  fields they share, or every entry differs at draw 0.
    o = [(k, v) for k, v, *_ in o]
    print("oracle drew %d, C drew %d" % (len(o), len(m)))
    n = min(len(o), len(m))
    for i in range(n):
        if o[i] != m[i]:
            print("first divergence at draw %d" % i)
            lo = max(0, i - 6)
            for j in range(lo, min(n, i + 7)):
                print("  %6d  oracle %-6s %-6d   C %-6s %-6d %s"
                      % (j, o[j][0], o[j][1], m[j][0], m[j][1],
                         "<<<" if j == i else ""))
            sys.exit(0)
    print("identical for the first %d draws" % n)
