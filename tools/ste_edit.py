#!/usr/bin/env python3
"""Rewrite one sentence of a comment, and wrap the paragraph again.

`tools/ste_lint.py` names a sentence that is too long.  A reader splits
it.  This applies that split: it finds the paragraph the sentence is in,
puts the new text in its place, and wraps the paragraph at the width the
tree writes.

    python3 tools/ste_edit.py <file> <old sentence> <new text>

The old sentence is matched against the paragraph with its line breaks
taken out, so it is given as one line.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ste_fix as F  # noqa: E402


def edit(path, old, new):
    text = open(path, errors="replace").read()
    lines = text.split("\n")
    out, i, done = [], 0, 0
    while i < len(lines):
        ln = lines[i]
        st = ln.lstrip()
        if st.startswith("/*") and "*/" not in ln:
            j = i
            while j < len(lines) and "*/" not in lines[j]:
                j += 1
            if j < len(lines):
                block = lines[i:j + 1]
                joined = " ".join(re.sub(r"^\s*(/\*|\*)|\*/", "", l).strip() for l in block)
                if old in re.sub(r"\s+", " ", joined):
                    indent = ln[:len(ln) - len(st)]
                    body = list(block)
                    body[0] = re.sub(r"^\s*/\*", "", body[0])
                    body[-1] = re.sub(r"\*/\s*$", "", body[-1])
                    body = [re.sub(r"^\s*\*", "", l).rstrip()
                            if l.lstrip().startswith("*") else l.rstrip()
                            for l in body]
                    paras, cur, res = [], [], []
                    for b in body + [""]:
                        if b.strip():
                            cur.append(b)
                        else:
                            if cur:
                                paras.append(cur)
                            paras.append(None)
                            cur = []
                    flat = []
                    for para in paras:
                        if para is None:
                            flat.append("")
                            continue
                        lead = min((len(l) - len(l.lstrip()) for l in para if l.strip()), default=2)
                        p = re.sub(r"\s+", " ", " ".join(l.strip() for l in para))
                        if old in p and not F.literal(para, lead):
                            p = p.replace(old, new)
                            done += 1
                        if F.literal(para, lead):
                            flat.extend(para)
                        else:
                            pad = " " * lead
                            flat.extend(pad + l for l in F.wrap(p, F.WIDTH - len(indent) - 3 - lead))
                    while flat and not flat[-1].strip():
                        flat.pop()
                    new_block = []
                    for k, l in enumerate(flat):
                        if k == 0:
                            new_block.append(indent + "/*" + l)
                        elif l.strip():
                            new_block.append(indent + " *" + l)
                        else:
                            new_block.append(indent + " *")
                    new_block[-1] += " */"
                    out.extend(new_block)
                    i = j + 1
                    continue
        out.append(ln)
        i += 1
    if done:
        open(path, "w").write("\n".join(out))
        return done
    #  The paragraph walk did not reach it: a table, or a block whose
    #  shape the walk does not keep.  Replace the words across the line
    #  breaks and let the next ste_fix run wrap it again.
    pat = re.compile(r"\s*\*?\s*".join(re.escape(w) for w in old.split()))
    def sub(m):
        pre = re.match(r"[ \t]*", m.group(0)).group(0)
        return new
    t, n = pat.subn(sub, text, count=1)
    if n:
        open(path, "w").write(t)
    return n


if __name__ == "__main__":
    n = edit(sys.argv[1], sys.argv[2], sys.argv[3])
    print("%s: %d" % (sys.argv[1], n))
    sys.exit(0 if n else 1)
