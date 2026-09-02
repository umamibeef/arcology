"""THINK C 'DATA' image decompressor (algorithm read out of CODE 1 $13A)."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rezfork import load

def expand(src):
    """Returns list of (destOffset, bytes) for the 3 blocks."""
    blocks = []
    p = 0
    for _ in range(3):
        off = struct.unpack(">i", src[p:p+4])[0]; p += 4
        out = bytearray()
        while True:
            t = src[p]; p += 1
            if t & 0x80:
                n = (t & 0x7F) + 1
                out += src[p:p+n]; p += n
            elif t & 0x40:
                out += b"\x00" * ((t & 0x3F) + 1)
            elif t & 0x20:
                n = (t & 0x1F) + 2
                out += bytes([src[p]]) * n; p += 1
            elif t & 0x10:
                out += b"\xff" * ((t & 0x0F) + 1)
            else:
                c = t & 0x0F
                if c == 0: break
                elif c == 1: out += b"\x00"*4 + b"\xff\xff" + src[p:p+2]; p += 2
                elif c == 2: out += b"\x00"*4 + b"\xff" + src[p:p+3]; p += 3
                elif c == 3: out += b"\xa9\xf0" + b"\x00"*2 + src[p:p+2] + b"\x00" + src[p+2:p+3]; p += 3
                elif c == 4: out += b"\xa9\xf0" + b"\x00" + src[p:p+3] + b"\x00" + src[p+3:p+4]; p += 4
                else: raise ValueError("bad token %d" % c)
        blocks.append((off, bytes(out)))
    return blocks

if __name__ == "__main__":
    r = load(os.path.join(os.path.dirname(__file__), "..", "rsrc", "sc2k.rsrc"))
    d0 = [x for x in r["DATA"] if x.id == 0][0].data
    bs = expand(d0)
    for i,(off,b) in enumerate(bs):
        print("block %d: destOffset=%d (0x%X)  expanded=%d bytes" % (i, off, off & 0xffffffff, len(b)))
    build()
    def at(a5off, n=24):
        img, LOW = A5IMG, A5LOW
        return img[LOW + a5off : LOW + a5off + n]
    print("\nA5-0x7DD4 rotTable      :", at(-0x7DD4,16).hex())
    print("A5-0x3A12 pollutionTbl   :", at(-0x3A12,24).hex())
    print("A5-0x1570 bldValue       :", at(-0x1570,24).hex())
    print("A5-0x141E popTbl         :", at(-0x141E,24).hex())
    i = img.find(b"Light Industrial")
    print("\n'Light Industrial' at image[0x%X] = A5%+d" % (i, i-LOW))

A5LOW = 0x8067          # image[A5LOW + a5off] == A5[a5off]
A5HIGH = 0x3270

def build(rsrc=None):
    rsrc = rsrc or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "rsrc", "sc2k.rsrc")
    r = load(rsrc)
    d0 = [x for x in r["DATA"] if x.id == 0][0].data
    img = bytearray(A5LOW + A5HIGH)
    b = expand(d0)
    img[0:len(b[0][1])] = b[0][1]                      # block 0 -> A5-0x8067 .. A5
    for off, dat in b[1:]:                             # blocks 1,2 -> A5+off
        img[A5LOW+off : A5LOW+off+len(dat)] = dat
    return img

def rd(img, a5off, n):   return bytes(img[A5LOW+a5off : A5LOW+a5off+n])
def pstr(img, a5off):
    n = img[A5LOW+a5off]
    return rd(img, a5off+1, n).decode("mac_roman", "replace")
def words(img, a5off, n): return list(struct.unpack(">%dh"%n, rd(img, a5off, n*2)))
def bytesat(img, a5off, n): return list(rd(img, a5off, n))
