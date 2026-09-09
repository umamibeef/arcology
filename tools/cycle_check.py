"""Diff a WHOLE growth cycle -- all sixteen phases in sequence -- against
the original.  A single phase from a fresh load never reaches the
automatic builds at $31B30, so this is the only mode that exercises
autoRailStation $B058."""
import sys, os, glob, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SC2KSIM = os.path.join(os.path.dirname(__file__), "..", "build", "arcology")
LAYERS = ("XBLD", "XZON", "XBIT", "XTRF", "XTXT")

def one(path, tmp):
    s = Sim(path)
    s.e.wr(A5 + 0x11DC, 2, 1); s.e.tb_seed = 1
    for yy in range(4):
        for xx in range(4):
            s.e.a[7] = 0x00300000
            for v in (xx, yy):
                s.e.a[7] -= 2; s.e.wr(s.e.a[7], 2, v)
            s.e.a[7] -= 4; s.e.wr(s.e.a[7], 4, 0xDEAD0000)
            err = s.e.run(0x3170E, 0xDEAD0000, limit=40000000,
                          real_calls=True, stubs={0x9728})
            if err: return None, err
    md = os.path.join(tmp, "m"); os.makedirs(md, exist_ok=True)
    r = subprocess.run([SC2KSIM, "--dump-growth-all", path, md],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if r.returncode: return None, "C failed"
    out = {}
    for n in LAYERS:
        a = s.layer(n); b = open(os.path.join(md, n), "rb").read()
        k = min(len(a), len(b))
        out[n] = sum(1 for i in range(k) if a[i] != b[i])
    #  XTHG is a raw block behind a pointer, not a row-pointer layer.
    #  It is the ONLY place the train and boat spawns are observable --
    #  they never touch XBLD/XZON/XBIT/XTRF.
    try:
        a = s.block(0x2BCA, 480)
        b = open(os.path.join(md, "XTHG"), "rb").read()
        k = min(len(a), len(b))
        out["XTHG"] = sum(1 for i in range(k) if a[i] != b[i])
    except Exception:
        out["XTHG"] = 0
    return out, None

if __name__ == "__main__":
    tmp = tempfile.mkdtemp()
    files = [p for p in sorted(glob.glob(sys.argv[1] + "/*")) if os.path.isfile(p)]
    if len(sys.argv) > 2: files = [p for p in files if os.path.basename(p) in sys.argv[2:]]
    tot = {n: 0 for n in list(LAYERS) + ["XTHG"]}; exact = 0; ran = 0; launched = 0; skipped = []
    for p in files:
        launched += 1
        try: r, err = one(p, tmp)
        except Exception as e: r, err = None, "EXC %s" % type(e).__name__
        if r is None:
            skipped.append((os.path.basename(p), err)); continue
        ran += 1
        for k in tot: tot[k] += r.get(k, 0)
        if not any(r.values()): exact += 1
        else: print("%-18s %s" % (os.path.basename(p),
                    " ".join("%s:%d" % (k, v) for k, v in r.items() if v)))
    print("\nwhole cycle byte-exact on %d/%d cities  (%d launched, %d skipped)"
          % (exact, ran, launched, launched - ran))
    for n, e in skipped: print("   skipped %-18s %s" % (n, e))
    print("diff bytes:", tot)
