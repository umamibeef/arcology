#!/usr/bin/env python3
"""Check source comments against the project's three comment rules.

A comment states what the code does now.  It is not a changelog, not a
record of the conversation that produced the code, and not a notebook of
measurements taken from one city.

    1. CURRENT FACT   No narrative about what the code did before.
    2. NO TRANSCRIPT  No quotes from, or references to, a conversation.
    3. NO DATA CASES  No per-city coordinates or measured constants as
                      illustration.

The checker reads comments only.  A coordinate pair in a table literal is
code and is never reported.  Run it over the tree, over a path, or with
``--hook`` to read a Claude Code hook payload on stdin.

    python3 tools/comment_lint.py                 # whole tree
    python3 tools/comment_lint.py src/render/net  # one directory
    python3 tools/comment_lint.py --stats         # counts per rule
"""

import argparse
import json
import os
import re
import sys

# --------------------------------------------------------------------------
#  What to read.
# --------------------------------------------------------------------------

C_LIKE = {".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".frag", ".vert", ".glsl", ".metal"}
PY_LIKE = {".py"}
#  CMake takes # comments and needs no tokenizer.  The build files carry
#  prose about the tree, so the same rules hold there.
HASH_LIKE = {".cmake"}
HASH_NAMES = {"CMakeLists.txt"}
#  A document under docs/ is prose end to end, and the same two rules hold
#  for it.  docs/conventions.rst is the one file that states those rules,
#  so it quotes the words they forbid and is left alone.
RST_LIKE = {".rst"}
RST_EXEMPT = {"conventions.rst"}
#  A script carries the same prose as the code it drives, and the same two
#  rules hold for it.  Lua comments start with -- and a long one with --[[.
LUA_LIKE = {".lua"}
EXTENSIONS = C_LIKE | PY_LIKE | HASH_LIKE | RST_LIKE | LUA_LIKE

#  Third-party sources and build products keep their own comments.
SKIP_PARTS = {
    ".git",
    ".claude",
    ".venv",
    "__pycache__",
    "_build",
    "build",
    "debug",
    "node_modules",
    "out",
    "site-packages",
    "vendor",
}


def wanted(path, root=None):
    """Answer whether the checker reads this file.

    The skip list is matched against the path BELOW the project root.  A
    checkout under a directory that is itself on the list -- a worktree in
    .claude/worktrees, say -- would otherwise reject every file in the tree.
    """
    if (os.path.splitext(path)[1] not in EXTENSIONS
            and os.path.basename(path) not in HASH_NAMES):
        return False
    if os.path.basename(path) in RST_EXEMPT:
        return False
    p = os.path.abspath(path)
    if root:
        try:
            rel = os.path.relpath(p, os.path.abspath(root))
        except ValueError:
            rel = p
        if not rel.startswith(".." + os.sep) and rel != "..":
            p = rel
    parts = os.path.normpath(p).split(os.sep)
    return not any(part in SKIP_PARTS for part in parts)


def walk(target, root=None):
    """Yield every readable source file under a target, or the target itself."""
    if os.path.isfile(target):
        if wanted(target, root):
            yield target
        return
    for base, dirs, names in os.walk(target):
        dirs[:] = [d for d in dirs if d not in SKIP_PARTS]
        for n in sorted(names):
            p = os.path.join(base, n)
            if wanted(p, root):
                yield p


# --------------------------------------------------------------------------
#  Comment extraction.
#
#  Both scanners return (line_number, text) for the comment body alone, so a
#  rule never sees an identifier or a string literal.
# --------------------------------------------------------------------------


def comments_c(src):
    """Extract // and /* */ comments, skipping string and character literals."""
    out = []
    i, n, line = 0, len(src), 1
    while i < n:
        ch = src[i]
        if ch == "\n":
            line += 1
            i += 1
        elif ch == '"' or ch == "'":
            quote, i = ch, i + 1
            while i < n and src[i] != quote:
                if src[i] == "\\":
                    i += 1
                elif src[i] == "\n":
                    line += 1
                i += 1
            i += 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append((line, src[i + 2 : j]))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j
            body = src[i + 2 : j]
            for k, text in enumerate(body.split("\n")):
                out.append((line + k, text))
            line += body.count("\n")
            i = j + 2
        else:
            i += 1
    return out


def comments_py(src):
    """Extract # comments and docstrings, using the tokenizer for exactness."""
    import io
    import tokenize

    out = []
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(src).readline))
    except (tokenize.TokenError, IndentationError, SyntaxError):
        #  A file the tokenizer cannot finish still yields its # comments.
        return [
            (i, m.group(1))
            for i, l in enumerate(src.split("\n"), 1)
            for m in [re.match(r"^\s*#(.*)$", l)]
            if m
        ]
    prev = tokenize.INDENT
    for tok in toks:
        if tok.type == tokenize.COMMENT:
            out.append((tok.start[0], tok.string.lstrip("#")))
        elif tok.type == tokenize.STRING and prev in (
            tokenize.INDENT,
            tokenize.DEDENT,
            tokenize.NEWLINE,
            tokenize.NL,
            tokenize.ENCODING,
        ):
            #  A string alone on a statement line is a docstring.
            for k, text in enumerate(tok.string.split("\n")):
                out.append((tok.start[0] + k, text))
        if tok.type not in (tokenize.NL, tokenize.COMMENT):
            prev = tok.type
    return out


def comments_hash(src):
    """Extract # comments from a file that has no other comment form."""
    return [
        (i, m.group(1))
        for i, l in enumerate(src.split("\n"), 1)
        for m in [re.match(r"^\s*#(.*)$", l)]
        if m
    ]


def comments_lua(src):
    """Extract Lua comments as (line, text): -- to the end of a line, and
    the long form --[[ ... ]] whole."""
    out = []
    i = 0
    line = 1
    n = len(src)
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
        elif src.startswith("--[[", i):
            end = src.find("]]", i + 4)
            end = n if end < 0 else end
            out.append((line, src[i + 4:end]))
            line += src.count("\n", i, end)
            i = end + 2
        elif src.startswith("--", i):
            end = src.find("\n", i)
            end = n if end < 0 else end
            out.append((line, src[i + 2:end]))
            i = end
        elif c in "\"'":
            i += 1
            while i < n and src[i] != c:
                i += 2 if src[i] == "\\" else 1
            i += 1
        else:
            i += 1
    return out


def comments_rst(src):
    """Extract the prose of a reStructuredText file as (line, text).

    Everything that is not prose is dropped: a literal block holds code, a
    directive marker and its options hold markup, and a section underline
    holds neither.  A literal block runs from a line ending in "::" until
    the indentation returns to that line's own.
    """
    out = []
    lines = src.split("\n")
    lit_indent = None
    for i, line in enumerate(lines, 1):
        stripped = line.strip()
        indent = len(line) - len(line.lstrip())
        if lit_indent is not None:
            #  Still inside the literal block while blank or further in.
            if not stripped or indent > lit_indent:
                continue
            lit_indent = None
        if not stripped:
            continue
        #  A section underline or overline.
        if re.fullmatch(r"[=\-~^\"#*+:.'_`]{3,}", stripped):
            continue
        #  A directive, a comment, a target, or a directive option.
        if stripped.startswith("..") or re.match(r"^:[\w-]+:\s", stripped):
            if stripped.endswith("::"):
                lit_indent = indent
            continue
        if stripped.endswith("::"):
            lit_indent = indent
            #  The paragraph introducing the block is still prose.
            out.append((i, stripped[:-2]))
            continue
        out.append((i, stripped))
    return out


def comments(path):
    """Extract every comment in a file as (line, text)."""
    with open(path, "r", errors="replace") as f:
        src = f.read()
    ext = os.path.splitext(path)[1]
    if ext in PY_LIKE:
        return comments_py(src)
    if ext in LUA_LIKE:
        return comments_lua(src)
    if ext in RST_LIKE:
        return comments_rst(src)
    if ext in HASH_LIKE or os.path.basename(path) in HASH_NAMES:
        return comments_hash(src)
    return comments_c(src)


# --------------------------------------------------------------------------
#  The cities, for rule 3.
#
#  A name is a data case only where the tree ships a city by that name.  The
#  list comes from cities/ so it stays true as the collection changes.
# --------------------------------------------------------------------------

EXTRA_CITIES = ["atlanta", "toronto", "paris", "hilton", "oakland", "manhattan", "flint"]


def city_pattern(root):
    names = set(EXTRA_CITIES)
    cities = os.path.join(root, "cities")
    if os.path.isdir(cities):
        for f in os.listdir(cities):
            stem = os.path.splitext(f)[0]
            for w in re.split(r"[-_.0-9]+", stem):
                if len(w) > 3:
                    names.add(w.lower())
    #  A city whose name is also an ordinary word of this codebase, or the
    #  game itself.  Keeping them would report "dry land", "four arms" and
    #  "the volcano" as data cases, and a hook that cries wolf gets turned
    #  off.  The cost is that a citation of one of these cities by name
    #  goes unreported.
    names -= {"city", "cities", "simcity", "garden", "wells", "moore",
              "island", "point", "river", "test", "none",
              "land", "four", "seven", "falls", "floating", "volcano",
              "valley", "parade", "retreat", "homestead", "starter",
              "spires", "gorge", "arco", "maxis", "silicon"}
    return r"\b(?:" + "|".join(sorted(names, key=len, reverse=True)) + r")\b"


# --------------------------------------------------------------------------
#  The rules.
#
#  Each is (id, regex, message).  A rule fires on comment text only, so it is
#  written to be tight rather than forgiving: a false report costs more here
#  than a missed one, because the hook blocks on it.
# --------------------------------------------------------------------------

TRANSCRIPT = [
    (
        "C1",
        #  "the user" alone is the player, and stays.  A colon, a comma or a
        #  verb of speech makes it the person on the other end of a chat.
        r"\(\s*the users?\b"
        r"|\bthe user(?:'s)?\s*[:,]"
        r"|\bthe user (?:said|asked|wants|wanted|complained|reported|noted|put it|would rather|would prefer)\b",
        "quotes or cites the user; state the behaviour instead",
    ),
    (
        "C2",
        r"\b(?:you|we) (?:asked|requested|wanted|said|complained)\b|\bas (?:you )?requested\b|\bper (?:the user|your)\b",
        "refers to the conversation; state the behaviour instead",
    ),
    (
        "C3",
        r"\b(?:ChatGPT|Copilot|the assistant|an LLM|the model said)\b|(?<![./\w])Claude\b(?!\s*Code)",
        "refers to an assistant; state the behaviour instead",
    ),
    (
        "C4",
        #  "the last session" alone is the APP's own session and is left alone.
        r"\b(?:this|an earlier|the earlier|a previous) (?:conversation|chat)\b"
        r"|\bearlier session (?:said|noted|worked|added|had|thought)\b"
        r"|\bmy (?:earlier|first|previous|last) (?:note|pass|attempt|reading|version|table)\b",
        "refers to an earlier session; state the behaviour instead",
    ),
    (
        "C5",
        #  A subject may sit between the owner and the noun, so the two
        #  are matched with a gap between them.
        r"\b(?:screenshot|transcript) of\b"
        r"|\bthe user's\b[^\n]{0,30}?\b(?:screenshot|words|phrase|request|complaint|message|note|wording)\b",
        "cites conversation material; state the behaviour instead",
    ),
    (
        "C6",
        #  A date inside the project's own working period is a work log.  A
        #  citation of something published earlier keeps its date.
        r"\b(?:\d{1,2} )?(?:January|February|March|April|May|June|July|August"
        r"|September|October|November|December) 20(?:2[4-9]|[3-9]\d)\b",
        "dates the work rather than the code",
        #  A status stamp standing alone on its line -- a word, a separator
        #  and a date -- is a register's own data.  A date inside a sentence
        #  is not.
        r"^[A-Z][a-z]+(?: [a-z]+)?\s*[\u00b7:|-]\s*(?:\d{1,2} )?(?:January|February"
        r"|March|April|May|June|July|August|September|October|November"
        r"|December) \d{4}$",
    ),
]

HISTORY = [
    (
        "H1",
        #  A subject in the past habitual describes history.  The same two
        #  words after a form of "be" state a purpose instead, and so does a
        #  sentence that opens with them and leaves the subject out.  The
        #  word in front is what separates the two.
        r"\b(?!is\b|are\b|was\b|were\b|be\b|been\b|being\b|not\b|also\b)\w+\s+used to\b",
        "describes a past implementation",
    ),
    (
        "H2",
        r"\bno longer\b|\b(?:not|isn't|aren't|doesn't|don't) any ?more\b",
        "describes a past implementation",
        #  A modal makes it the running program's own state, not the code's
        #  history: a building that can no longer stand is current fact.
        r"\b(?:can|could|will|would|may|must|shall|cannot)\s+no longer\b",
    ),
    ("H3", r"\bpreviously\b|\bformerly\b|\boriginally\b", "describes a past implementation"),
    (
        "H4",
        #  "for a while" also measures distance along a line, so it counts
        #  only beside a past-tense verb.
        r"\bat first\b|\bfor months\b|\bat one point\b"
        r"|\bfor a while\b(?=[^\n]*\b(?:was|were|had|did)\b)"
        r"|\b(?:was|were|had|did)\b[^\n]*\bfor a while\b",
        "narrates the work, not the code",
    ),
    ("H5", r"\bturned out\b|\bit turns out\b", "narrates the work, not the code"),
    (
        "H6",
        #  The past perfect describes a state the code has since left,
        #  which is history whatever the subject.
        r"\bhad been\b|\bwe had\b",
        "describes a past implementation",
    ),
    (
        "H7",
        #  A determiner in front makes it a named piece of work.  Without one
        #  -- "produced earlier in the same pass" -- it orders a pipeline and
        #  is current fact, so it never matches at all.
        r"\b(?:an|the|two|three|some|our|my|a|\d[\d,]*) earlier\b",
        "describes a past implementation",
        #  A step of the running program, rather than a draft of the code.
        r"\bearlier (?:stage|phase|pass|step|tile|row|column|sample|point"
        r"|frame|segment|cell|band|tick|record|entry|call|draw)s?\b",
    ),
    ("H8", r"\bonce (?:was|did|had|were|drew|read|ran)\b|\bas it once\b|\bit was also\b", "describes a past implementation"),
    (
        "H9",
        #  "now" also means "at this point in the sequence" and "in the state
        #  the code has reached", both of which are current fact.  Only a verb
        #  of implementation behaviour after it makes it a contrast with the
        #  past.
        r"\bnow (?:runs|reads|lives|holds|raises|carries|answers|means|draws|keeps|returns"
        r"|uses|sits|stops|starts|emits|treats|owns|reports|refuses|accepts|understands|supports)\b"
        #  The contrast has to be in the same clause: a comma or a full stop
        #  between them means the two are unrelated.
        r"|\bnow\b[^,.\n]{0,25}\b(?:rather than|instead of|no longer|used to)\b",
        "contrasts with an earlier state; say what is true",
    ),
    (
        "H10",
        #  Past tense only: undoing a change is history, while the same verb
        #  in the present names an action the game offers the player.
        r"\breverted\b|\brefactor(?:ed|ing|s)\b|\bthe rewrite\b|\bwas rewritten\b",
        "narrates a change to the code",
    ),
    (
        "H11",
        #  "nothing was wrong" and "fixed at 8" are current fact, so the
        #  subject and the object both have to be named.
        r"\bwas (?:invisible|a bug|broken|the bug)\b"
        r"|\b(?:it|this|that|which|the \w+) (?:was|were) wrong\b"
        r"|\bthe (?:bug|regression) (?:was|is|that)\b|\bhid a bug\b"
        r"|\bthe fix (?:was|for (?:it|this|that))\b"
        r"|\bfixed (?:it|this|that) (?:by|in|with|when|and)\b"
        r"|\bbroke (?:twice|once|when the|on the)\b"
        r"|\bcost (?:an? (?:hour|day|week|afternoon|morning|debugging|pass|sweep)|me\b|us\b)",
        "narrates a defect and its repair",
    ),
    (
        "H12",
        #  "the old form, still understood" is current fact about what the
        #  code accepts, so only words that name an implementation count.
        r"\bthe old (?:code|way|version|implementation|behaviour|behavior|approach|gate|routine|loop|scheme)\b"
        r"|\binstead of the old\b|\bbefore (?:this|the) (?:change|fix|rewrite)\b",
        "describes a past implementation",
    ),
    (
        "H13",
        r"\bTried and reverted\b|\bdid not work\b|\bwe tried\b|\bI (?:tried|assumed|thought|wrote|read|added|had)\b",
        "narrates the work, not the code",
    ),
]


def data_rules(root):
    """Build the per-city rules, which depend on the names in cities/."""
    city = city_pattern(root)
    return compile_rules([
        (
            "D1",
            city + r"[^\n]{0,24}?\b\d{1,3}\s*,\s*\d{1,3}\b",
            "cites a city and a coordinate as an example",
        ),
        (
            "D2",
            r"\([^)\n]{0,60}\b" + city + r"\b[^)\n]{0,60}?\d[^)\n]{0,60}\)",
            "cites a per-city measurement as an example",
        ),
        (
            "D3",
            r"\b(?:box|arms?|spoke|spur|slot|band|slab|verge|shelf)\s+\d+\.\d+",
            "cites a measured value from one case",
            #  A dimension carries a unit or a second number: "box 2.4 x 0.5 m"
            #  describes a thing, not a reading taken from one city.
            r"\d+(?:\.\d+)?\s*(?:m\b|mm\b|cm\b|px\b|s\b|ms\b|[x\u00d7])",
        ),
        (
            #  The same citation spelled out in words.
            "D4",
            r"\bcolumn \d{1,3},? row \d{1,3}\b|\brow \d{1,3},? column \d{1,3}\b",
            "cites one tile as an example",
        ),
        (
            #  A coordinate pair next to what came of it, which is an
            #  observation rather than a rule.
            "D5",
            r"\b\d{1,3}\s*,\s*\d{1,3}\b[^\n]{0,24}?\b(?:asked|got|gave|showed"
            r"|came out|ended up|reported|floated|failed)\b",
            "cites one case and its outcome",
        ),
        (
            #  A timing taken from one city.
            "D6",
            city + r"[^\n]{0,60}?\b\d+(?:\.\d+)?\s*ms\b"
            r"|\b\d+(?:\.\d+)?\s*ms\b[^\n]{0,60}?" + city,
            "cites a measurement from one city",
        ),
    ])


def compile_rules(raw):
    """Compile (id, pattern, message[, exception]) into matchable rules."""
    out = []
    for rule in raw:
        rid, pat, msg = rule[0], rule[1], rule[2]
        exc = rule[3] if len(rule) > 3 else None
        out.append((rid, re.compile(pat, re.I), msg, re.compile(exc, re.I) if exc else None))
    return out


TEXT_RULES = compile_rules(TRANSCRIPT + HISTORY)

#  A comment may carry a marker where the rule genuinely does not apply.
ALLOW = re.compile(r"comment-lint:\s*allow", re.I)


def paragraphs(items):
    """Group (line, text) into runs of consecutive lines, joined.

    A rule has to see a whole paragraph, because a comment wraps wherever
    the width runs out: a citation split as "(the" / "user:" over a line
    break is invisible to anything reading one line at a time.  Each run
    yields the joined text and the line each character came from.
    """
    runs, cur = [], []
    for line, text in items:
        if cur and line != cur[-1][0] + 1:
            runs.append(cur)
            cur = []
        cur.append((line, text))
    if cur:
        runs.append(cur)

    for run in runs:
        joined, owner = "", []
        for line, text in run:
            #  Drop the block comment's own leading star, or the joined text
            #  carries it into the middle of a sentence and hides a match.
            t = re.sub(r"^\s*\*+\s?", "", text).strip()
            if not t:
                continue
            if joined:
                joined += " "
                owner.append(line)
            joined += t
            owner.extend([line] * len(t))
        yield joined, owner, {line for line, text in run if ALLOW.search(text)}


def check_file(path, rules):
    """Report every rule a file's comments break, as (line, id, message, text)."""
    found = []
    for text, owner, allowed in paragraphs(comments(path)):
        if not text.strip():
            continue
        for rid, pat, msg, exc in rules:
            for m in pat.finditer(text):
                if exc and exc.search(text):
                    break
                line = owner[m.start()] if m.start() < len(owner) else owner[-1]
                if line in allowed:
                    continue
                lo = max(0, m.start() - 60)
                found.append((line, rid, msg, text[lo : m.end() + 60].strip(), m.group(0).strip()))
                break
    return found


def project_root(start):
    """Find the tree a path belongs to, by looking for this tool above it.

    The hook is given an absolute path that may sit in a worktree rather than
    the directory CLAUDE_PROJECT_DIR names, so the root is taken from the file
    itself and the environment is only the fallback.
    """
    d = os.path.dirname(os.path.abspath(start))
    while True:
        if os.path.isdir(os.path.join(d, "cities")) or os.path.exists(
            os.path.join(d, "tools", "comment_lint.py")
        ):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*", default=None, help="files or directories (default: the tree)")
    ap.add_argument("--root", default=None, help="project root, for the city list")
    ap.add_argument("--stats", action="store_true", help="print a count per rule")
    ap.add_argument("--hook", action="store_true", help="read a Claude Code hook payload on stdin")
    ap.add_argument("--quiet", action="store_true", help="print nothing, answer with the exit status")
    args = ap.parse_args(argv)

    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    root = args.root or os.environ.get("CLAUDE_PROJECT_DIR") or here

    paths = args.paths
    if args.hook:
        try:
            payload = json.load(sys.stdin)
        except (json.JSONDecodeError, ValueError):
            return 0
        tool = payload.get("tool_input") or {}
        resp = payload.get("tool_response") or {}
        p = resp.get("filePath") or tool.get("file_path") or tool.get("notebook_path")
        if not p or not os.path.exists(p):
            return 0
        root = project_root(p) or root
        if not wanted(p, root):
            return 0
        paths = [p]
    if not paths:
        paths = [os.path.join(root, d) for d in ("src", "tools", "tests", "docs")]
        paths = [p for p in paths if os.path.exists(p)]

    rules = TEXT_RULES + data_rules(root)
    counts = {}
    report = []
    total = 0
    for target in paths:
        for f in walk(target, root):
            for line, rid, msg, text, hit in check_file(f, rules):
                total += 1
                counts[rid] = counts.get(rid, 0) + 1
                report.append((line, rid, msg, text, hit))
                if not args.quiet and not args.stats and not args.hook:
                    rel = os.path.relpath(f, root)
                    print(f"{rel}:{line}: {rid} {msg}")
                    print(f"    {text[:150]}")

    if args.hook:
        #  Exit 2 puts the report in front of the model that made the edit.
        if total:
            print("Comment rules broken -- see 'Comments in the code' in", file=sys.stderr)
            print(".claude/CLAUDE.md.  Rewrite each comment as a statement of", file=sys.stderr)
            print("current fact: no earlier implementations, no conversation,", file=sys.stderr)
            print("no per-city examples.\n", file=sys.stderr)
            for line, rid, msg, text, hit in report:
                print(f"  {os.path.relpath(paths[0], root)}:{line}: {rid} {msg}", file=sys.stderr)
                print(f"      {text[:150]}", file=sys.stderr)
            return 2
        return 0

    if args.stats:
        for rid in sorted(counts):
            print(f"{rid:4} {counts[rid]:5}")
        print(f"{'all':4} {total:5}")
    elif total and not args.quiet:
        print(f"\n{total} comment(s) break the rules in .claude/CLAUDE.md.")
        print("A comment states what the code does now.  Rewrite the comment as a")
        print("statement of current fact: no earlier implementations, no conversation,")
        print("no per-city examples.")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
