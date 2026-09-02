"""Build a function/xref database for a Mac 68k CODE segment."""
import sys, os, re, json, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kdis import disasm
from capstone import CS_MODE_M68K_020

A5RE  = re.compile(r"(-?)\$([0-9a-f]+)\(a5[,)]")
ABSRE = re.compile(r"\$([0-9a-f]+)\.l")
IMMRE = re.compile(r"#\$([0-9a-f]+)")

def build(path, code_start, code_end):
    buf = open(path, "rb").read()
    ins = disasm(buf, code_start, code_end, mode=CS_MODE_M68K_020)
    by_addr = {i.addr: i for i in ins}
    order = [i.addr for i in ins]

    # --- call targets -------------------------------------------------
    calls = collections.defaultdict(set)     # target -> set(callsite)
    for i in ins:
        if i.mnem == "jsr":
            m = ABSRE.fullmatch(i.op)
            if m: calls[int(m.group(1),16)].add(i.addr)
        elif i.mnem.startswith("bsr"):
            m = re.fullmatch(r"\$([0-9a-f]+)", i.op)
            if m: calls[int(m.group(1),16)].add(i.addr)

    # --- function starts ----------------------------------------------
    starts = set()
    for t in calls:
        if t in by_addr: starts.add(t)
    for idx, a in enumerate(order):
        i = by_addr[a]
        if i.kind == "link":
            prev = by_addr[order[idx-1]] if idx else None
            if prev is None or prev.kind in ("ret","jmp") or prev.mnem=="bra" or prev.mnem=="nop":
                starts.add(a)
    starts.add(code_start)
    starts = sorted(starts)

    # --- assign instructions to functions -----------------------------
    funcs = {}
    bnds = starts + [code_end]
    si = 0
    cur = None
    for a in order:
        while si+1 < len(bnds) and a >= bnds[si+1]:
            si += 1
        f = bnds[si]
        if f != cur:
            cur = f
            funcs[f] = {"addr": f, "end": 0, "size": 0, "insns": 0, "a5": collections.Counter(),
                        "traps": collections.Counter(), "calls": collections.Counter(),
                        "bad": 0, "imm": collections.Counter()}
        d = funcs[f]
        i = by_addr[a]
        d["insns"] += 1
        d["end"] = a + i.size
        if i.kind == "bad": d["bad"] += 1
        if i.kind == "trap": d["traps"][i.mnem[1:]] += 1
        for sg, hx in A5RE.findall(i.op):
            v = int(hx,16); d["a5"][-v if sg=="-" else v] += 1
        if i.mnem == "jsr":
            m = ABSRE.fullmatch(i.op)
            if m: d["calls"][int(m.group(1),16)] += 1
        elif i.mnem.startswith("bsr"):
            m = re.fullmatch(r"\$([0-9a-f]+)", i.op)
            if m: d["calls"][int(m.group(1),16)] += 1
    for f,d in funcs.items():
        d["size"] = d["end"] - d["addr"]
        d["xrefs"] = len(calls.get(f, ()))
    return ins, funcs, calls

def save(funcs, calls, out):
    j = {}
    for a,d in funcs.items():
        j["%06X"%a] = {"size": d["size"], "insns": d["insns"], "xrefs": d["xrefs"],
                       "bad": d["bad"],
                       "a5": {("%d"%k): v for k,v in d["a5"].most_common()},
                       "traps": dict(d["traps"].most_common()),
                       "calls": {("%06X"%k): v for k,v in d["calls"].most_common()}}
    json.dump(j, open(out,"w"), indent=0)

if __name__ == "__main__":
    path, cs, ce, out = sys.argv[1], int(sys.argv[2],0), int(sys.argv[3],0), sys.argv[4]
    ins, funcs, calls = build(path, cs, ce)
    save(funcs, calls, out)
    print("instructions: %d   functions: %d" % (len(ins), len(funcs)))
    print("undecodable:  %d" % sum(1 for i in ins if i.kind=="bad"))
    tc = collections.Counter()
    for d in funcs.values(): tc.update(d["traps"])
    print("\n-- top 30 Toolbox traps used --")
    for k,v in tc.most_common(30): print("   %-24s %5d" % (k,v))
    print("\n-- 25 largest functions --")
    for a,d in sorted(funcs.items(), key=lambda kv:-kv[1]["size"])[:25]:
        print("   $%06X  %6d bytes  %5d insns  xrefs=%-4d traps=%s" %
              (a, d["size"], d["insns"], d["xrefs"], ",".join(list(d["traps"])[:4]) or "-"))
    print("\n-- 25 most-called functions --")
    for a,d in sorted(funcs.items(), key=lambda kv:-kv[1]["xrefs"])[:25]:
        print("   $%06X  xrefs=%-5d %6d bytes  traps=%s" %
              (a, d["xrefs"], d["size"], ",".join(list(d["traps"])[:4]) or "-"))
