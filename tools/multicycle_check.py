"""Diff N whole growth cycles against the original.

One cycle gives the aircraft path only about forty chances at a one in
thirty roll, so it almost never fires and stays untested.  Repeating the
cycle reaches it.
"""
import sys, os, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5

SC2KSIM = os.path.join(os.path.dirname(__file__), "..", "build", "arcology")
LAYERS = ("XBLD", "XZON", "XBIT", "XTRF", "XTXT")

def one(path, reps, tmp):
    s = Sim(path)
    s.e.wr(A5 + 0x11DC, 2, 1); s.e.tb_seed = 1
    for _ in range(reps):
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
    r = subprocess.run([SC2KSIM, "--dump-growth-all", path, md,
                        "--cycles", str(reps)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if r.returncode: return None, "C failed"
    out = {}
    for n in LAYERS:
        a = s.layer(n); b = open(os.path.join(md, n), "rb").read()
        k = min(len(a), len(b))
        out[n] = sum(1 for i in range(k) if a[i] != b[i])
    a = s.block(0x2BCA, 480); b = open(os.path.join(md, "XTHG"), "rb").read()
    out["XTHG"] = sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i])
    return out, None

if __name__ == "__main__":
    reps = int(sys.argv[1]); tmp = tempfile.mkdtemp()
    for city in sys.argv[2:]:
        r, err = one(city, reps, tmp)
        name = os.path.basename(city)
        if r is None: print("%-18s FAILED %s" % (name, err)); continue
        bad = " ".join("%s:%d" % (k, v) for k, v in r.items() if v)
        print("%-18s %s" % (name, bad if bad else "exact over %d cycles" % reps))
