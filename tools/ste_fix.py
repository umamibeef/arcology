#!/usr/bin/env python3
"""Put the comments into Simplified Technical English, mechanically.

This does the four changes a machine can make without reading for sense.

  1. An em dash becomes a stop, a colon or a pair of commas.
  2. A semicolon becomes a stop.
  3. A British spelling becomes the American one.
  4. The paragraph is wrapped again at the width it had.

It leaves a paragraph alone when the paragraph is not prose: a table, a
list, an indented example, a line of code.  It leaves `generated/` alone
as well: a generated file is rewritten by rewriting its GENERATOR and
running it again, or the two fall out of step and `materials_fresh`
says so.  It cannot split a long
sentence, and it cannot make a hollow sentence true.  Those need a
reader, and `tools/ste_lint.py` names the ones that are left.

    python3 tools/ste_fix.py src/render        # rewrite a directory
    python3 tools/ste_fix.py --show src/x.c    # print, change nothing
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from ste_lint import SPELL  # noqa: E402

WIDTH = 74

#  What opens a sentence.  A colon is always safe, so a stop is used only
#  where the text that follows plainly stands on its own.
OPENER = ("it ", "they ", "this ", "that ", "these ", "there ", "each ", "every ",
          "nothing ", "one ", "two ", "both ", "c ", "lua ", "the pipeline",
          "the script", "the rule", "the drive", "the loft", "the fit")
FINITE = set("""is are was were has have had does do did can may must will would should
gets get gives give takes take makes make reads read writes write draws draw lays lay
runs run goes go comes come stands stand holds hold answers answer asks ask says say
knows know needs need keeps keep leaves leave finds find sets set puts put uses use
fills fill names name carries carry means mean shows show ends end starts start
moves move stops stop costs cost belongs belong""".split())


def stands_alone(seg):
    low = seg.strip().lower()
    if not low.startswith(OPENER):
        return False
    words = [w.strip(".,:;()`").lower() for w in seg.split()[:6]]
    return len(seg.split()) >= 8 and any(w in FINITE for w in words)


def dashes(p):
    """An em dash, or the `--` that stands in for one."""
    p = p.replace("—", " -- ")
    #  An aside between two dashes.  A short one becomes an aside between
    #  two commas.  A long one is a sentence of its own, because a comma
    #  between two clauses is a splice.
    def aside(m):
        inner = m.group(1)
        words = [w.strip(".,:;()`").lower() for w in inner.split()]
        if not any(w in FINITE for w in words):
            return ", " + inner + ", "   # a list, not a clause
        return ".  " + inner[:1].upper() + inner[1:] + ".  "
    p = re.sub(r"(?<=\w) -- ([^-]{1,140}?) -- (?=\w)", aside, p)
    out, i = [], 0
    for m in re.finditer(r"(?<=\S) -- (?=\S)", p):
        out.append(p[i:m.start()])
        after = p[m.end():]
        stop = re.search(r"[.!?](\s|$)", after)
        seg = after[:stop.start()] if stop else after
        if stands_alone(seg):
            out.append(".  " + after[:1].upper())
            i = m.end() + 1
        else:
            out.append(": ")
            i = m.end()
    out.append(p[i:])
    p = "".join(out)
    #  a dash left at the very start of a line of prose
    p = re.sub(r"^\s*--\s+", "", p)
    #  a new sentence starts with a capital.  An identifier keeps its own
    #  spelling: `arc.rules.junction` opens a sentence as it is written.
    def cap(m):
        w = m.group(2)
        return m.group(1) + (w.upper() if w.isalpha() else w)
    p = re.sub(r"([.!?]\s\s)([a-z])(?=[a-z]*[\s.,)])", cap, p)
    return p


def semicolons(p):
    """A semicolon in prose becomes a stop.  One inside a code example,
    a `for`, a struct, or a switch spelling like `--edit C,R[;C,R...]`,
    is left as it is."""
    out, i = [], 0
    for m in re.finditer(r"; +(?=\S)", p):
        before, after = p[:m.start()], p[m.end():]
        if before.count("`") % 2 or before.count("(") != before.count(")"):
            continue
        if before.rstrip().endswith((")", "}")) and after[:1].isupper():
            pass
        nxt = after[:1]
        out.append(p[i:m.start()])
        out.append(".  " + (nxt.upper() if nxt.isalpha() else nxt))
        i = m.end() + 1
    out.append(p[i:])
    return "".join(out)


JOINT = re.compile(r", (and|but|so|or|yet|then|because|though|while|which|where) (?=\S)")
#  What the second half opens with, once the joint is cut.
OPEN = {"and": "", "or": "", "but": "But ", "so": "So ", "yet": "Yet ",
        "then": "Then ", "because": "This is because ", "though": "Even so, ",
        "while": "At the same time, ", "which": "This ", "where": "There "}


def split_long(p):
    """A sentence longer than 25 words, cut at a joint where the text on
    the right stands on its own.  A cut is made only where both halves
    are a sentence.  A sentence with no such joint is left for a reader."""
    out = []
    for s in re.split(r"(?<=[.!?])(?=\s+[A-Z0-9(`$_-])", p):
        while len(s.split()) > 25:
            cut = None
            #  a colon that introduces a whole clause
            for m in re.finditer(r": (?=[a-z])", s):
                left, right = s[:m.start()], s[m.end():]
                if len(left.split()) < 8 or len(right.split()) < 8:
                    continue
                own = right.split(".")[0]
                head = [w.strip(",:;()`").lower() for w in own.split()[:5]]
                if not any(w in FINITE for w in head) or len(own.split()) < 8:
                    continue
                s = left + ".  " + right[0].upper() + right[1:]
                cut = "colon"
                break
            if cut == "colon":
                continue
            cut = None
            for m in JOINT.finditer(s):
                left, word, right = s[:m.start()], m.group(1), s[m.end():]
                if len(left.split()) < 6 or len(right.split()) < 6:
                    continue
                #  the verb must be in the RIGHT SIDE'S OWN sentence,
                #  not in the one after it
                own = right.split(".")[0]
                head = [w.strip(",:;()`").lower() for w in own.split()[:5]]
                if not any(w in FINITE for w in head) or len(own.split()) < 6:
                    continue
                cut = (left, word, right)
                break
            if not cut:
                break
            left, word, right = cut
            lead = OPEN[word]
            if lead:
                join = lead + right[0].lower() + right[1:]
            else:
                join = right[0].upper() + right[1:]
            s = left + ".  " + join
            #  and look again at what is left on the right
            s_head, _, s_tail = s.partition(".  ")
            if len(s_tail.split()) <= 25:
                break
        out.append(s)
    return "".join(out)


#  A long sentence that introduces a list.  STE asks for a vertical
#  list, one item a line, and a vertical list is easier to read than a
#  run of commas.
def vertical(p):
    out = []
    for s in re.split(r"(?<=[.!?])(?=\s+[A-Z0-9(`$_-])", p):
        m = re.match(r"(\s*)([^.:]{8,}?): ([^.]+)\.?\s*$", s)
        if not m or len(s.split()) <= 25:
            out.append(s)
            continue
        lead, rest = m.group(2), m.group(3).strip()
        if rest.count(",") < 2 or ":" in rest or "." in rest:
            out.append(s)
            continue
        rest = rest.rstrip(".")
        items = [x.strip() for x in re.split(r", (?:and )?", rest) if x.strip()]
        if len(items) < 3 or any(len(x.split()) > 10 or not x[0].isalpha() for x in items):
            out.append(s)
            continue
        out.append(m.group(1) + lead + ".\n\n" +
                   "\n".join("    " + x[0].upper() + x[1:] + "." for x in items) + "\n\n")
    return "".join(out)


def spelling(p):
    def one(m):
        w = m.group(0)
        a = SPELL.get(w.lower())
        if not a:
            return w
        return a.capitalize() if w[0].isupper() else a
    return re.sub(r"[A-Za-z]+", one, p)


def literal(lines, base):
    """A paragraph that is not prose: a table, a list, an example."""
    for ln in lines:
        t = ln[base:] if len(ln) > base else ""
        if ln.strip().startswith(("|", "-", "*", "+", "#")) and not ln.strip().startswith("--"):
            return True
        if "|" in ln:
            return True
        if t[:4] == "    ":
            return True
        letters = sum(ch.isalpha() for ch in ln)
        if ln.strip() and letters < len(ln.strip()) * 0.45:
            return True
    return False


def rewrap(text, indent, first, cont, width):
    words, lines, cur = text.split(), [], first
    room = width - len(indent)
    for w in words:
        add = (" " if cur.strip() and not cur.endswith("  ") else "") + w
        if cur != first and len(cur) + len(add) > room and cur.strip() != cont.strip():
            lines.append(indent + cur.rstrip())
            cur = cont + w
        else:
            cur += add if cur != first else w
    lines.append(indent + cur.rstrip())
    return lines


def fix_block(src, indent):
    """One /* ... */ block, as a list of source lines, rewritten."""
    body = [ln for ln in src]
    #  strip the frame
    body[0] = re.sub(r"^\s*/\*", "", body[0])
    body[-1] = re.sub(r"\*/\s*$", "", body[-1])
    stripped, base = [], None
    for ln in body:
        s = re.sub(r"^\s*\*", "", ln) if ln.lstrip().startswith("*") else ln
        stripped.append(s.rstrip())
    #  paragraphs, split on blank lines
    paras, cur = [], []
    for s in stripped:
        if s.strip():
            cur.append(s)
        else:
            paras.append(cur)
            cur = []
            paras.append(None)
    paras.append(cur)
    out = []
    changed = False
    for para in paras:
        if para is None:
            out.append(None)
            continue
        if not para:
            continue
        lead = min((len(l) - len(l.lstrip()) for l in para if l.strip()), default=2)
        if literal(para, lead):
            #  A table or an example keeps its shape.  A line of PROSE
            #  inside one is still prose, so it takes the same changes
            #  without being wrapped again.
            keep = []
            for l in para:
                t = spelling(semicolons(dashes(l))) if len(l.split()) >= 6 else l
                if t != l:
                    changed = True
                keep.append(t)
            out.append(keep)
            continue
        joined = " ".join(l.strip() for l in para)
        new = vertical(split_long(spelling(semicolons(dashes(joined)))))
        if new != joined:
            changed = True
        pad = " " * lead
        if "\n" in new:
            block = []
            for chunk in new.split("\n"):
                if not chunk.strip():
                    block.append("")
                elif chunk.startswith("    "):
                    block.append(pad + chunk)
                else:
                    block.extend(pad + l for l in wrap(chunk.strip(), WIDTH - len(indent) - 3 - lead))
            out.append(block)
        else:
            out.append([pad + l for l in wrap(new, WIDTH - len(indent) - 3 - lead)])
    if not changed:
        return None
    lines = []
    flat = []
    for para in out:
        if para is None:
            flat.append("")
        else:
            flat.extend(para)
    while flat and not flat[-1].strip():
        flat.pop()
    for i, l in enumerate(flat):
        if i == 0:
            lines.append(indent + "/*" + l)
        elif l.strip():
            lines.append(indent + " *" + l)
        else:
            lines.append(indent + " *")
    lines[-1] = lines[-1] + " */"
    return lines


def wrap(text, room):
    """Wrapped at `room`, with the two spaces after a stop that the rest
    of the tree writes."""
    text = re.sub(r"(?<=[.!?]) (?=[A-Z(`])", "  ", text)
    words, lines, cur = text.split("  "), [], ""
    #  keep a sentence break as one unit so the two spaces survive
    parts = []
    for k, seg in enumerate(words):
        parts.extend(seg.split())
        if k + 1 < len(words):
            parts.append("\x00")
    lines, cur = [], ""
    for w in parts:
        if w == "\x00":
            cur += " "
            continue
        add = w if not cur else " " + w
        if cur and len(cur) + len(add) > room:
            lines.append(cur.rstrip())
            cur = w
        else:
            cur += add
    if cur:
        lines.append(cur.rstrip())
    return lines


ONE_LINE = re.compile(r"/\*([^*][^\n]*?)\*/")
SLASHES = re.compile(r"(?<![:\"'])//([^\n]*)")


def fix_short(text, base):
    """A comment on one line: the same changes, no wrapping.  The file's
    own first line reads `name.c: what it holds`, never a dash."""
    def block(m):
        return "/*" + one(m.group(1)) + "*/"

    def slash(m):
        return "//" + one(m.group(1))

    def one(t):
        return spelling(semicolons(dashes(t)))
    text = ONE_LINE.sub(block, text)
    text = SLASHES.sub(slash, text)
    #  the header's own dash, which names the file
    text = re.sub(r"(?m)^(\s*(?:/\*|\*|//)\s*)(%s(?:\.[ch]|\.cpp|\.h)?) -- " % re.escape(base),
                  r"\1\2: ", text)
    return text


def fix_file(path, show):
    text = open(path, errors="replace").read()
    base = os.path.basename(path).split(".")[0]
    text = fix_short(text, base)
    lines = text.split("\n")
    out, i, n = [], 0, 0
    while i < len(lines):
        ln = lines[i]
        st = ln.lstrip()
        if st.startswith("/*") and not st.startswith("/*!") and "*/" not in ln:
            j = i
            while j < len(lines) and "*/" not in lines[j]:
                j += 1
            if j < len(lines):
                indent = ln[:len(ln) - len(st)]
                new = fix_block(lines[i:j + 1], indent)
                if new:
                    out.extend(new)
                    n += 1
                    i = j + 1
                    continue
        out.append(ln)
        i += 1
    joined = "\n".join(out)
    if not show and joined != open(path, errors="replace").read():
        open(path, "w").write(joined)
        n = max(n, 1)
    if show and n:
        print("\n".join(out))
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("where", nargs="+")
    ap.add_argument("--show", action="store_true")
    a = ap.parse_args()
    total, files = 0, 0
    for w in a.where:
        p = w if os.path.isabs(w) else os.path.join(ROOT, w)
        paths = [p] if os.path.isfile(p) else [
            os.path.join(d, f)
            for d, ds, fs in os.walk(p) if "vendor" not in d and "generated" not in d
            for f in sorted(fs) if f.endswith((".c", ".h", ".cpp"))]
        for path in paths:
            k = fix_file(path, a.show)
            if k:
                files += 1
                total += k
    print("ste fix: %d comment blocks in %d files" % (total, files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
