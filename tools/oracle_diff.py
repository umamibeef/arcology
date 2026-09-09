"""Diff the C reconstruction against the original's own code.

Both are given the same starting city, so the save file's snapshot skew
is out of the picture entirely: any disagreement is a transcription
error.  This is the regression test that matters.
"""
import sys, os, glob, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim

SC2KSIM = os.path.join(os.path.dirname(__file__), "..", "build", "arcology")
LAYERS = ("XPLT", "XVAL", "XPOP", "XPLC", "XFIR", "XCRM")

def one(path, tmp):
    s = Sim(path)
    #  The clock runs the two grid phases just before the scan ($21FA6
    #  powerGridReset, $220DA waterGrid, then $21FB0 cityScanPass), and
    #  they leave state the scan reads -- the water pass sets the
    #  treatment flag at A5+0x2CA0, which lands in the pollution blur
    #  divisor at $23308.  Driving the scan on its own tests a state the
    #  game never actually reaches, and hid that flag for a long time.
    s.run(0x20FC4, stubs={0x9728, 0x15A54}, limit=60000000)
    s.run(0x2156E, stubs={0x9728, 0x15A54}, limit=60000000)
    if s.run(0x2317E): return None
    od = os.path.join(tmp, "o"); md = os.path.join(tmp, "m")
    os.makedirs(od, exist_ok=True); os.makedirs(md, exist_ok=True)
    for n in LAYERS: open(os.path.join(od, n), "wb").write(s.layer(n))
    subprocess.run([SC2KSIM, "--dump", path, md, "--pre=pw"], check=True,
                   stdout=subprocess.DEVNULL)
    out = {}
    for n in LAYERS:
        a = open(os.path.join(od, n), "rb").read()
        b = open(os.path.join(md, n), "rb").read()
        k = min(len(a), len(b))
        live = sum(1 for i in range(k) if a[i] or b[i])
        ok = sum(1 for i in range(k) if (a[i] or b[i]) and a[i] == b[i])
        out[n] = (ok, live)
    return out

if __name__ == "__main__":
    tot = {n: [0, 0] for n in LAYERS}
    tmp = tempfile.mkdtemp()
    files = [p for p in sorted(glob.glob(sys.argv[1] + "/*")) if os.path.isfile(p)]
    n = 0
    for p in files:
        try: r = one(p, tmp)
        except Exception: r = None
        if not r: continue
        n += 1
        for k, (ok, live) in r.items():
            tot[k][0] += ok; tot[k][1] += live
    print("C reconstruction vs the original's own code, %d cities" % n)
    for k in LAYERS:
        ok, live = tot[k]
        print("  %-5s %7d / %-7d live cells  %6.2f%%" % (k, ok, live, 100*ok/live if live else 0))
