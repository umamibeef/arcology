"""68k disassembler front-end for Mac CODE resources (capstone + A-trap decoding)."""
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000, CS_MODE_M68K_020, CS_MODE_M68K_040
from traps import trap_name

class Insn:
    __slots__=("addr","size","mnem","op","bytes","kind","target")
    def __init__(self,addr,size,mnem,op,bs,kind=None,target=None):
        self.addr,self.size,self.mnem,self.op,self.bytes=addr,size,mnem,op,bs
        self.kind,self.target=kind,target
    def __str__(self):
        return "%06X  %-20s %-10s %s" % (self.addr, self.bytes.hex(), self.mnem, self.op)

def make_md(mode=CS_MODE_M68K_020):
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | mode)
    md.detail = False
    md.skipdata = False
    return md

SWITCH_W = 0x5A6      # CODE 1 %_SWITCH.W  (inline table follows the jsr)
SWITCH_L = 0x5CA      # CODE 1 %_SWITCH.L

def _switch_table(buf, pc, wide):
    """Parse a THINK C inline switch table at pc. Returns (size, ncases) or None."""
    try:
        vs = 4 if wide else 2                       # case-value size
        p = pc + 2                                  # skip default offset word
        p += 2*vs                                   # min, max
        n = struct.unpack(">H", buf[p:p+2])[0] + 1  # count
        if not (1 <= n <= 512): return None
        size = 2 + 2*vs + 2 + n*(vs+2)
        return size, n
    except Exception:
        return None

def disasm(buf, start, end, base=0, mode=CS_MODE_M68K_020):
    """Linear sweep. Yields Insn. A-line/F-line and %_SWITCH tables handled manually."""
    md = make_md(mode)
    pc = start
    out = []
    while pc < end:
        w = struct.unpack(">H", buf[pc:pc+2])[0]
        if 0xA000 <= w <= 0xAFFF:
            out.append(Insn(base+pc, 2, "_"+trap_name(w), "", buf[pc:pc+2], "trap"))
            pc += 2; continue
        got = False
        for ins in md.disasm(bytes(buf[pc:min(pc+16,end)]), base+pc, count=1):
            n = ins.size
            k = None; tgt = None
            m = ins.mnemonic
            if m.startswith("bsr") or m == "jsr": k="call"
            elif m.startswith("b") and m not in ("bset","bclr","bchg","btst","bfins","bfextu","bfexts","bftst","bfchg","bfclr","bfset","bfffo"): k="branch"
            elif m in ("jmp",): k="jmp"
            elif m in ("rts","rtd","rte","rtr"): k="ret"
            elif m.startswith("link"): k="link"
            elif m.startswith("unlk"): k="unlk"
            op = ins.op_str
            if k in ("call","branch","jmp") and op.startswith("$"):
                try: tgt = int(op[1:].split()[0].rstrip(","),16)
                except Exception: pass
            out.append(Insn(base+pc, n, m, op, buf[pc:pc+n], k, tgt))
            pc += n; got = True
            if m == "jmp" and buf[pc-n:pc-n+4] == b"\x4e\xfb\x00\x02":
                # compiler switch: jmp (2,pc,d0.w) followed by a word offset table.
                # entry count comes from the preceding "cmpi #N,d0 / bhi" bound check.
                cnt = None
                for q in range(len(out)-2, max(0,len(out)-6), -1):
                    if out[q].mnem.startswith("cmpi"):
                        mm = re.match(r"#\$([0-9a-f]+), d0", out[q].op)
                        if mm: cnt = int(mm.group(1),16)+1
                        break
                if cnt and 1 <= cnt <= 512:
                    sz = cnt*2
                    out.append(Insn(base+pc, sz, ".jmptbl", "%d entries" % cnt, buf[pc:pc+sz], "data"))
                    pc += sz
            if m == "jsr" and op in ("$5a6.l", "$5ca.l"):
                r = _switch_table(buf, pc, op == "$5ca.l")
                if r:
                    sz, nc = r
                    out.append(Insn(base+pc, sz, ".switch", "%d cases" % nc, buf[pc:pc+sz], "data"))
                    pc += sz
            break
        if not got:
            out.append(Insn(base+pc, 2, "dc.w", "$%04X" % w, buf[pc:pc+2], "bad"))
            pc += 2
    return out

def stats(insns):
    bad = sum(1 for i in insns if i.kind=="bad")
    return len(insns), bad, 100.0*bad/max(1,len(insns))

if __name__ == "__main__":
    path, start, end = sys.argv[1], int(sys.argv[2],0), int(sys.argv[3],0)
    buf = open(path,"rb").read()
    for name, mode in (("68000",CS_MODE_M68K_000),("68020",CS_MODE_M68K_020),("68040",CS_MODE_M68K_040)):
        ins = disasm(buf, start, min(end,len(buf)), mode=mode)
        n,b,p = stats(ins)
        print("mode %-6s  %6d insns, %5d undecodable (%.2f%%)" % (name,n,b,p))

def fmt(insns, buf=None):
    lines=[]
    for i in insns:
        c=""
        if i.kind=="trap": c="  ; A-trap"
        lines.append("%06X: %-18s %-9s %-34s%s" % (i.addr, i.bytes.hex(), i.mnem, i.op, c))
    return "\n".join(lines)

def dump(path, start, end, base=None, mode=CS_MODE_M68K_020):
    buf=open(path,"rb").read()
    if base is None: base=0
    return fmt(disasm(buf,start,min(end,len(buf)),base=base,mode=mode))
