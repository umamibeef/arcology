import json,re,sys,os
S=json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'symbols.json')))
G={int(k,16):v for k,v in S["globals"].items()}
F={int(k,16):v for k,v in S["funcs"].items()}
def a5(m):
    sg,hx=m.group(1),m.group(2)
    v=int(hx,16); v=-v if sg=="-" else v
    return (G[v]+"(a5") if v in G else m.group(0)
def fn(m):
    v=int(m.group(1),16)
    return ("%s.l"%F[v]) if v in F else m.group(0)
inp,out=sys.argv[1],sys.argv[2]
w=open(out,'w')
for l in open(inp):
    l=re.sub(r"(-?)\$([0-9a-f]+)\(a5",a5,l)
    l=re.sub(r"\$([0-9a-f]+)\.l",fn,l)
    w.write(l)
w.close(); print(out,"written")
