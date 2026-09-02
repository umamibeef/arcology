import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from rezfork import load
r = load(sys.argv[1]); outdir = sys.argv[2]
os.makedirs(outdir, exist_ok=True)
for c in sorted(r["CODE"], key=lambda x: x.id):
    p = os.path.join(outdir, "CODE_%d.bin" % c.id)
    open(p,"wb").write(c.data)
    print("CODE %d  %8d bytes  name=%r  first16=%s" % (c.id, len(c.data), c.name, c.data[:16].hex()))
