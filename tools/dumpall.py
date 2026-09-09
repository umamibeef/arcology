import sys,os,struct,json,collections
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from m68kdis import disasm
from capstone import CS_MODE_M68K_020
path,cs,ce,out = sys.argv[1],int(sys.argv[2],0),int(sys.argv[3],0),sys.argv[4]
d=open(path,'rb').read()
# function starts = jsr/bsr targets that are LINK prologues, plus other call targets
tg=collections.Counter()
for i in range(0,len(d)-6,2):
    if d[i:i+2]==b"\x4e\xb9":
        tg[struct.unpack(">I",d[i+2:i+6])[0]]+=1
starts=sorted(t for t in tg if t<ce and d[t:t+2]==b"\x4e\x56")
xref={t:tg[t] for t in starts}
f=open(out,'w')
ins=disasm(d,cs,ce,mode=CS_MODE_M68K_020)
sset=set(starts)
for i in ins:
    if i.addr in sset:
        f.write("\n;================ FUNC $%06X   (%d callers) ================\n" % (i.addr, xref[i.addr]))
    c=""
    if i.kind=="trap": c="   ; toolbox"
    f.write("%06X: %-18s %-9s %-36s%s\n" % (i.addr,i.bytes.hex(),i.mnem,i.op,c))
f.close()
print(out, sum(1 for _ in open(out)), "lines,", len(starts), "functions")
