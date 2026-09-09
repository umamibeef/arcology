"""Diff the C economy against the original's own $34D04 under the emulator."""
import sys, os, glob, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SC2KSIM = os.path.join(os.path.dirname(__file__), "..", "build", "arcology")
BLK = {"hd": 0x1EDE, "eq": 0x1EE2, "le": 0x1EE6}
GLB = {"pinc": 0x1E9A, "pdec": 0x1E9E,
       "taxa": 0x1EA2, "taxc": 0x1EA6, "taxb": 0x1EAA}

def s32(v): return v - (1 << 32) if v >= (1 << 31) else v

def oracle(path):
    s = Sim(path)
    err = s.run(0x34D04, stubs=(0x9728, 0x2EDE4)) if "stubs" in s.run.__code__.co_varnames \
          else s.run(0x34D04)
    if err: return None
    out = {}
    for k, off in GLB.items(): out[k] = s32(s.glob(off))
    for pfx, off in BLK.items():
        base = s.glob(off)
        for i in range(20):
            out["%s%d" % (pfx, i)] = s32(s.e.rd(base + 4 * i, 4))
    return out

def mine(path):
    r = subprocess.run([SC2KSIM, "--economy", path], capture_output=True, text=True)
    out = {}
    for ln in r.stdout.split("\n"):
        f = ln.split()
        if len(f) == 2: out[f[0]] = int(f[1])
    return out

if __name__ == "__main__":
    files = [p for p in sorted(glob.glob(sys.argv[1] + "/*")) if os.path.isfile(p)]
    keys = list(GLB) + ["%s%d" % (p, i) for p in BLK for i in range(20)]
    bad = {}
    n = ok = 0
    for p in files:
        try: o = oracle(p)
        except Exception as e: o = None
        if not o: continue
        m = mine(p)
        n += 1
        diffs = [k for k in keys if k in m and m[k] != o[k]]
        if not diffs: ok += 1
        for k in diffs: bad.setdefault(k, []).append(os.path.basename(p))
        if diffs and len(sys.argv) > 2:
            print("%-14s %s" % (os.path.basename(p),
                  " ".join("%s:%d!=%d" % (k, m[k], o[k]) for k in diffs[:6])))
    print("\nfully exact: %d/%d" % (ok, n))
    for k in sorted(bad, key=lambda k: -len(bad[k]))[:14]:
        print("  %-6s wrong on %2d  %s" % (k, len(bad[k]), " ".join(bad[k][:4])))
