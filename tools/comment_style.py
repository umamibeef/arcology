#!/usr/bin/env python3
"""Comment style: a comment says what the code DOES, once.

Three rules, checked on the joined text of each comment so that a phrase
wrapped across lines cannot hide from a line-based grep:

  attribution  no "the user", and no date from this project's own era.
               Who asked for the code, and when, is the repository's
               history, not the source's.  A date from the game's era is
               provenance of the material and stays.
  history      in src/render, src/app, src/ui, no account of what the
               code used to do.  A reader needs the rule that holds now;
               the rest is noise that goes stale and contradicts its
               neighbours.  src/sim is exempt: there "the old value" and
               "used to" describe the ORIGINAL 68k game, which is the
               subject matter.
  The docs are held to the attribution rule too: a page explains the
  renderer, it does not minute who asked for what and when.  A date in a
  page's own eyebrow or status line is the page's, and stays.

  stacked      no two multi-line block comments in a row with no code
               between them.  A change rewrites the comment beside it;
               it never leaves the old one standing next to the new, for
               the two then disagree and the reader cannot tell which
               one the code obeys.  Consecutive one-line comments, as on
               the fields of a struct, are not this.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "src")
DOCS = os.path.join(HERE, "..", "docs")
EXT = (".c", ".h", ".cpp", ".frag", ".vert")
HIST_TREES = ("render", "app", "ui")

ATTRIB = re.compile(r"\bthe user\b|\byou (?:said|asked|wanted)\b|\bas requested\b", re.I)
DATED = re.compile(r"\b\d{1,2} (?:January|February|March|April|May|June|July|August|"
                   r"September|October|November|December) 20[2-9]\d\b", re.I)
HIST = re.compile(r"\b(?:used to|it once|once took|had been|no longer|formerly|"
                  r"previously|before this|are gone|is gone|at first|"
                  r"which was wrong|we used to)\b", re.I)


def blocks(lines):
    """(first, last) of every block comment, whole-line or trailing."""
    out, i = [], 0
    while i < len(lines):
        if "/*" in lines[i]:
            s = lines[i].index("/*")
            if "*/" in lines[i][s + 2:]:
                out.append((i, i))
            else:
                j = i + 1
                while j < len(lines) and "*/" not in lines[j]:
                    j += 1
                if j < len(lines):
                    out.append((i, j))
                    i = j
        i += 1
    return out


def text(lines, a, b):
    got = []
    for k in range(a, b + 1):
        s = lines[k].strip()
        if "/*" in s:
            s = s[s.index("/*") + 2:]
        if s.endswith("*/"):
            s = s[:-2]
        if s.startswith("*"):
            s = s[1:]
        got.append(s.strip())
    return " ".join(x for x in got if x)


def main():
    bad = []
    for root, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d != "vendor"]
        rel_tree = os.path.relpath(root, ROOT).split(os.sep)[0]
        for f in sorted(files):
            if not f.endswith(EXT):
                continue
            p = os.path.join(root, f)
            rp = os.path.relpath(p, os.path.join(ROOT, ".."))
            lines = open(p, errors="replace").read().split("\n")
            bl = blocks(lines)
            for a, b in bl:
                t = text(lines, a, b)
                m = ATTRIB.search(t) or DATED.search(t)
                if m:
                    bad.append((rp, a + 1, "attribution", m.group(0)))
                if rel_tree in HIST_TREES:
                    m = HIST.search(t)
                    if m:
                        bad.append((rp, a + 1, "history", m.group(0)))
            for k in range(len(bl) - 1):
                a0, a1 = bl[k]
                b0, b1 = bl[k + 1]
                if [x for x in lines[a1 + 1:b0] if x.strip()]:
                    continue
                if a1 == a0 or b1 == b0:
                    continue          # a one-liner: a field's own note
                if lines[b0][:lines[b0].index("/*")].strip():
                    continue          # trailing a declaration, not stacked
                if not lines[a0][:1].isspace():
                    continue          # a file or section header, not the pattern
                bad.append((rp, a0 + 1, "stacked", "two comments, no code between"))
    #  The docs carry prose, not comments, but the same rule: no page
    #  minutes who asked for a change.
    for f in sorted(os.listdir(DOCS)) if os.path.isdir(DOCS) else []:
        if not f.endswith(".rst"):
            continue
        p = os.path.join(DOCS, f)
        lines = open(p, errors="replace").read().split("\n")
        skip = set()
        for i, x in enumerate(lines):
            if "container::" in x or "rubric::" in x:
                skip.update(range(i, min(i + 6, len(lines))))
        for i, x in enumerate(lines):
            if i in skip:
                continue
            m = ATTRIB.search(x) or DATED.search(x)
            if m:
                bad.append(("docs/" + f, i + 1, "attribution", m.group(0)))

    for rp, ln, kind, what in bad:
        print(f"{rp}:{ln}: {kind}: {what}")
    if bad:
        print(f"\n{len(bad)} comment(s) break the style rule -- see the head of "
              f"tools/comment_style.py.")
        return 1
    print("comment style: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
