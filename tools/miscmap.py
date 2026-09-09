"""Walk the MISC builder ($2A186) and recover index -> source-global mapping."""
import sys,os,re
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from m68kdis import disasm
from capstone import CS_MODE_M68K_020

d=open(os.path.join(os.path.dirname(__file__),'..','out','code','CODE_2.bin'),'rb').read()
ins=disasm(d,0x2A186,0x2AD14,mode=CS_MODE_M68K_020)
L=[(i.addr,i.mnem,i.op) for i in ins]

STORE=re.compile(r"^(.*), \(a[01], d0\.l\)$")
idx=0; out=[]; i=0
def src_of(reg, upto):
    """last instruction before `upto` that writes reg"""
    for j in range(upto-1, max(0,upto-14), -1):
        a,m,o=L[j]
        if "," not in o: continue
        dst=o.rsplit(", ",1)[1]
        if dst==reg:
            return m, o.rsplit(", ",1)[0]
    return None,None
while i < len(L):
    a,m,o = L[i]
    if m=="move.w" and o=="d4, d0" and i+1<len(L) and L[i+1][1]=="addq.w" and L[i+1][2]=="#$1, d4":
        # find the store that uses this index
        st=None
        for j in range(i+2, min(i+8,len(L))):
            mm=STORE.match(L[j][2])
            if L[j][1]=="move.l" and mm: st=(j,mm.group(1)); break
        if st:
            j,srcexpr = st
            if srcexpr.startswith("#"):
                desc="const "+srcexpr
            elif re.fullmatch(r"[ad]\d", srcexpr):
                sm,so = src_of(srcexpr, i)
                desc = ("%s %s"%(sm,so)) if so else ("reg "+srcexpr)
            else:
                desc = srcexpr
            out.append((idx, a, desc))
            idx+=1
            i=j+1; continue
        idx+=1
    i+=1
print("recovered %d sequential MISC fields\n" % len(out))
for k,a,desc in out:
    print("MISC[%4d] @0x%04x  $%06X  <- %s" % (k, k*4, a, desc))
