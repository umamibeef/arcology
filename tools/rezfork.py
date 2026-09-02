"""Minimal classic Mac resource-fork reader (pure stdlib)."""
import struct, sys, os

class Res:
    __slots__ = ("type","id","name","attrs","data")
    def __init__(self,t,i,n,a,d):
        self.type,self.id,self.name,self.attrs,self.data = t,i,n,a,d
    def __repr__(self):
        return "<%s %d %r %d bytes>" % (self.type, self.id, self.name, len(self.data))

def parse(buf):
    dataOff, mapOff, dataLen, mapLen = struct.unpack(">IIII", buf[:16])
    m = mapOff
    typeListOff, nameListOff = struct.unpack(">HH", buf[m+24:m+28])
    tl = m + typeListOff
    nl = m + nameListOff
    nTypes = struct.unpack(">H", buf[tl:tl+2])[0] + 1
    out = {}
    for i in range(nTypes):
        p = tl + 2 + i*8
        rtype = buf[p:p+4].decode("mac_roman")
        nRes = struct.unpack(">H", buf[p+4:p+6])[0] + 1
        refOff = struct.unpack(">H", buf[p+6:p+8])[0]
        lst = []
        for j in range(nRes):
            q = tl + refOff + j*12
            rid, nameOff = struct.unpack(">hh", buf[q:q+4])
            attrs = buf[q+4]
            doff = struct.unpack(">I", b"\x00"+buf[q+5:q+8])[0]
            name = None
            if nameOff != -1:
                ln = buf[nl+nameOff]
                name = buf[nl+nameOff+1:nl+nameOff+1+ln].decode("mac_roman")
            dp = dataOff + doff
            dlen = struct.unpack(">I", buf[dp:dp+4])[0]
            lst.append(Res(rtype, rid, name, attrs, buf[dp+4:dp+4+dlen]))
        out[rtype] = lst
    return out

def load(path):
    with open(path,"rb") as f: return parse(f.read())

if __name__ == "__main__":
    r = load(sys.argv[1])
    total = 0
    rows = []
    for t in sorted(r):
        sz = sum(len(x.data) for x in r[t]); total += sz
        ids = [x.id for x in r[t]]
        rng = "%d..%d" % (min(ids), max(ids)) if len(ids)>1 else str(ids[0])
        rows.append((sz, t, len(r[t]), rng))
    for sz,t,n,rng in sorted(rows, reverse=True):
        print("%-6s n=%-5d ids=%-14s %9d bytes" % (t, n, rng, sz))
    print("---- %d types, %d bytes total" % (len(r), total))
