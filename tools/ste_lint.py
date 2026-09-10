#!/usr/bin/env python3
"""The comments are Simplified Technical English.

ASD-STE100 strips voice on purpose.  A comment states what the code does,
in short sentences, in the active voice, with one name for one thing.  A
reader who does not have English as a first language reads it at the same
speed as a reader who does.

This checks the MECHANICAL rules, which are the ones that remove slop:

  long      a sentence longer than 25 words
  semicolon a semicolon in prose.  Write two sentences
  dash      an em dash, or the `--` that stands in for one
  short     a contraction
  word      a long word where a short one says the same thing, or a
            marketing adjective
  spell     a British spelling
  header    a file whose first comment does not name the file and say
            what it holds

It cannot check whether a sentence is TRUE, or whether the noun is the
right one.  Those need a reader.

    python3 tools/ste_lint.py                # the whole tree
    python3 tools/ste_lint.py src/render/net # one directory
    python3 tools/ste_lint.py --list         # every fault, to work from
    python3 tools/ste_lint.py --rule long    # one rule at a time
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TREE = os.path.join(ROOT, "src")
SKIP = ("vendor",)

#  A word on the left, the word to use instead on the right.
LONG = {
    "utilize": "use", "utilizes": "uses", "utilise": "use",
    "leverage": "use", "leverages": "uses",
    "facilitate": "help", "facilitates": "helps",
    "ensure": "make sure", "ensures": "makes sure", "ensuring": "making sure",
    "prior": "before", "subsequent": "after", "commence": "start",
    "initiate": "start", "initiates": "starts",
    "obtain": "get", "obtains": "gets", "acquire": "get", "acquires": "gets",
    "demonstrate": "show", "demonstrates": "shows",
    "additionally": "also", "furthermore": "also", "moreover": "also",
    "regarding": "about", "concerning": "about",
    "seamless": "-", "seamlessly": "-", "robust": "-", "powerful": "-",
    "effortless": "-", "cutting-edge": "-", "world-class": "-",
    "revolutionary": "-", "next-generation": "-",
}
#  British on the left, American on the right.  Only whole words in
#  prose: an identifier keeps whatever spelling the code uses.
SPELL = {
    "colour": "color", "colours": "colors", "coloured": "colored",
    "colouring": "coloring", "behaviour": "behavior", "behaviours": "behaviors",
    "neighbour": "neighbor", "neighbours": "neighbors",
    "neighbouring": "neighboring", "neighbourhood": "neighborhood",
    "centre": "center", "centres": "centers", "centred": "centered",
    "metre": "meter", "metres": "meters", "centimetre": "centimeter",
    "centimetres": "centimeters", "kilometre": "kilometer",
    "grey": "gray", "greys": "grays", "travelled": "traveled",
    "travelling": "traveling", "modelled": "modeled", "modelling": "modeling",
    "cancelled": "canceled", "labelled": "labeled", "labelling": "labeling",
    "signalled": "signaled", "signalling": "signaling",
    "levelled": "leveled", "levelling": "leveling",
    "fuelled": "fueled", "marvellous": "marvelous",
    "practise": "practice", "licence": "license", "defence": "defense",
    "offence": "offense", "analyse": "analyze", "analysed": "analyzed",
    "recognise": "recognize", "recognised": "recognized",
    "organise": "organize", "organised": "organized",
    "normalise": "normalize", "normalised": "normalized",
    "aluminium": "aluminum", "kerb": "curb", "tyre": "tire",
    "storey": "story", "storeys": "stories", "plough": "plow",
    "draught": "draft", "sceptical": "skeptical", "judgement": "judgment",
    "acknowledgement": "acknowledgment", "programme": "program",
    "centreline": "centerline", "centrelines": "centerlines",
    "fibre": "fiber", "litre": "liter", "litres": "liters",
    "theatre": "theater", "manoeuvre": "maneuver", "manoeuvres": "maneuvers",
    "mould": "mold", "smoulder": "smolder", "sulphur": "sulfur",
    "emphasise": "emphasize", "minimise": "minimize", "maximise": "maximize",
    "optimise": "optimize", "optimised": "optimized", "optimisation": "optimization",
    "summarise": "summarize", "specialise": "specialize",
    "initialise": "initialize", "initialised": "initialized",
    "serialise": "serialize", "visualise": "visualize",
    "synchronise": "synchronize", "synchronised": "synchronized",
    "authorise": "authorize", "characterise": "characterize",
    "prioritise": "prioritize", "standardise": "standardize",
    "stabilise": "stabilize", "finalise": "finalize",
    "favourite": "favorite", "honour": "honor", "honours": "honors",
    "armour": "armor", "rumour": "rumor", "humour": "humor",
    "labour": "labor", "flavour": "flavor", "vapour": "vapor",
    "harbour": "harbor", "parlour": "parlor", "endeavour": "endeavor",
    "splendour": "splendor", "pretence": "pretense",
    "whilst": "while", "amongst": "among", "learnt": "learned",
    "spelt": "spelled", "dreamt": "dreamed", "burnt": "burned",
    "leapt": "leaped", "spilt": "spilled", "cosier": "cozier",
}
SHORT = re.compile(
    r"\b(?:\w+n't|\w+'re|\w+'ll|\w+'ve|it's|let's|that's|there's|here's|what's|who's|"
    r"don't|doesn't|isn't|aren't|wasn't|weren't|can't|won't|didn't|hasn't|haven't)\b", re.I)
DASH = re.compile(r"—|(?<=\s)--(?=\s)|(?<=\w)--(?=\w)")
WORD = re.compile(r"[A-Za-z][A-Za-z'-]*")

BLOCK = re.compile(r"/\*(.*?)\*/", re.S)
LINE = re.compile(r"(?<![:\"'])//([^\n]*)")


def comments(text):
    """Every comment in a source file, as (start offset, text)."""
    out = []
    for m in BLOCK.finditer(text):
        out.append((m.start(1), m.group(1)))
    for m in LINE.finditer(text):
        out.append((m.start(1), m.group(1)))
    return out


#  A sentence needs a verb.  A comment that opens with a label, or names
#  a thing and stops, is not a sentence and is not counted.
FINITE = set("""is are was were be am has have had do does did can could may might must
will would shall should gets get gives give takes take makes make reads read writes
write draws draw lays lay runs run goes go comes come stands stand sits sit holds hold
answers answer asks ask says say knows know needs need wants want leaves leave keeps
keep turns turn walks walk finds find sets set puts put uses use fills fill names name
counts count carries carry means mean shows show ends end starts start moves move
stops stop costs cost belongs belong lets let sees see wants adds add cuts cut
becomes become follows follow reaches reach pins pin drops drop picks pick
happens happen works work fits fit joins join meets meet""".split())


#  A line of the original's own listing: an address, then a mnemonic.
#  The assembler's semicolon is the assembler's, not prose.
LISTING = re.compile(r"^\s*(?:\$)?[0-9A-Fa-f]{4,6}\s+[A-Za-z_$.]")


def literal(para):
    """A paragraph that is not prose: a table, a list, an example."""
    for ln in para:
        t = ln.strip()
        if not t:
            continue
        if "|" in ln or LISTING.match(t):
            return True
        if t.startswith(("|", "-", "*", "+", "#", "$")) and not t.startswith("--"):
            return True
        if len(ln) - len(ln.lstrip()) >= 4:
            return True
        letters = sum(ch.isalpha() for ch in t)
        if letters < len(t) * 0.45:
            return True
    return False


def prose(body):
    """A comment block as plain prose: the leading stars gone, the lines
    of a paragraph joined, so a sentence that wraps is read as one
    sentence.  A table, an example and a line of the original's listing
    are not prose and are left out."""
    lines = [re.sub(r"^\s*\*", "", ln).rstrip() for ln in body.split("\n")]
    out, para = [], []
    for ln in lines + [""]:
        if ln.strip():
            para.append(ln)
            continue
        if para and not literal(para):
            out.append(" ".join(l.strip() for l in para))
        para = []
    return out  # one entry a paragraph


#  A sentence ends at a stop, a question mark or an exclamation, and not
#  at the dot inside `net_family.c`, `0.5` or `$21EDE`.
SENT = re.compile(r"(?<=[a-z0-9)\]}'\"`])[.!?](?=\s+[A-Z0-9(`$_-]|\s*$)")


def sentences(p):
    out, last = [], 0
    for m in SENT.finditer(p):
        out.append(p[last:m.end()].strip())
        last = m.end()
    if p[last:].strip():
        out.append(p[last:].strip())
    return out


def line_of(text, off):
    return text.count("\n", 0, off) + 1


def check(rel, text, want, full=False):
    faults = []

    def add(rule, off, why):
        if want in (None, rule):
            faults.append((rule, line_of(text, off), why))

    #  the file's own header: the first comment, and what it says
    cs = comments(text)
    base = os.path.basename(rel)
    first = cs[0][1] if cs else ""
    if not cs or cs[0][0] > 400 or base.split(".")[0] not in first:
        add("header", 0, "no header naming %s and what it holds" % base)

    for off, body in cs:
        for p in prose(body):
            check_prose(p, off, add)
    return faults


def check_prose(p, off, add, full=True):
    if True:
        for m in DASH.finditer(p):
            add("dash", off, "an em dash: " + p[max(0, m.start() - 28):m.start() + 28])
        for m in SHORT.finditer(p):
            add("short", off, "a contraction: " + m.group(0))
        for w in WORD.finditer(p):
            lw = w.group(0).lower()
            if lw in LONG:
                add("word", off, "%s -> %s" % (w.group(0), LONG[lw]))
            if lw in SPELL:
                add("spell", off, "%s -> %s" % (w.group(0), SPELL[lw]))
        for s in sentences(p):
            #  A semicolon inside a switch's spelling, a legend, or a
            #  code example is that thing's punctuation, not prose.
            bare = re.sub(r"`[^`]*`|\([^)]*\)|\[[^\]]*\]", "", s)
            if ";" in bare:
                add("semicolon", off, s[:60])
            n = len(s.split())
            if n > 25:
                add("long", off, "%d words: %s" % (n, s))


def files(where):
    if os.path.isfile(where):
        yield where
        return
    for dirpath, dirnames, names in os.walk(where):
        dirnames[:] = [d for d in dirnames if d not in SKIP]
        for n in sorted(names):
            if n.endswith((".c", ".h", ".cpp")):
                yield os.path.join(dirpath, n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("where", nargs="?", default=TREE)
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--rule")
    ap.add_argument("--full", action="store_true")
    a = ap.parse_args()
    where = a.where if os.path.isabs(a.where) else os.path.join(ROOT, a.where)
    per_rule, per_file, total = {}, {}, 0
    for path in files(where):
        rel = os.path.relpath(path, ROOT)
        text = open(path, errors="replace").read()
        fs = check(rel, text, a.rule, a.full)
        for rule, line, why in fs:
            per_rule[rule] = per_rule.get(rule, 0) + 1
            per_file[rel] = per_file.get(rel, 0) + 1
            total += 1
            if a.list:
                print("%s:%d: %s: %s" % (rel, line, rule, why))
    if not total:
        print("ste: the comments read as Simplified Technical English")
        return 0
    print("ste: %d faults in %d files" % (total, len(per_file)))
    print("  " + ", ".join("%s %d" % (k, v) for k, v in sorted(per_rule.items(), key=lambda kv: -kv[1])))
    for rel in sorted(per_file, key=lambda k: -per_file[k])[:12]:
        print("  %-34s %4d" % (rel, per_file[rel]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
