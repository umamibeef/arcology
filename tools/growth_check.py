"""Diff one growth phase of the C against the original's $3170E.

Both sides get the same city, the same phase, and both random number
generators seeded the same way, so any difference is a transcription
error.  Sim.call() is what seeds and pushes the arguments -- calling
s.run() instead silently runs the routine with garbage on the stack.
"""
import sys, os, glob, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim

SC2KSIM = os.path.join(os.path.dirname(__file__), "..", "build", "arcology")
LAYERS = ("XBLD", "XZON", "XBIT", "XTRF")

def one(path, yy, xx, tmp):
    s = Sim(path)
    if s.call(0x3170E, (yy, xx)):
        return None
    md = os.path.join(tmp, "m"); os.makedirs(md, exist_ok=True)
    r = subprocess.run([SC2KSIM, "--dump-growth", path, str(yy), str(xx), md],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if r.returncode: return None
    out = {}
    for n in LAYERS:
        a = s.layer(n); b = open(os.path.join(md, n), "rb").read()
        k = min(len(a), len(b))
        out[n] = sum(1 for i in range(k) if a[i] != b[i])
    return out

if __name__ == "__main__":
    tmp = tempfile.mkdtemp()
    files = [p for p in sorted(glob.glob(sys.argv[1] + "/*")) if os.path.isfile(p)]
    phases = [(0, 0)] if len(sys.argv) < 3 else \
             [(y, x) for y in range(4) for x in range(4)]
    tot = {n: 0 for n in LAYERS}; exact = 0; n = 0; bad = {}; launched = 0
    for p in files:
        for (yy, xx) in phases:
            launched += 1
            try: r = one(p, yy, xx, tmp)
            except Exception: r = None
            if r is None: continue
            n += 1
            for k in LAYERS: tot[k] += r[k]
            if not any(r.values()): exact += 1
            else:
                bad.setdefault(os.path.basename(p), []).append(
                    "(%d,%d) %s" % (yy, xx, " ".join("%s:%d" % (k, r[k])
                                                     for k in LAYERS if r[k])))
    #  Print EVERY failing phase, and the per-city count.  Truncating
    #  this list is how a sweep hides most of what it found.
    for c in sorted(bad):
        print("%-16s %2d bad" % (c, len(bad[c])))
        for line in bad[c]: print("                   %s" % line)
    print("\nbyte-exact on %d/%d city-phases  (%d launched, %d could not run)"
          % (exact, n, launched, launched - n))
    print("diff bytes:", tot)
