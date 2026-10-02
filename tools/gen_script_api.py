#!/usr/bin/env python3
"""src/script -> docs/appendix-script-api.rst: the script API as the source
registers it.

The page lists every namespace under `arc` and the functions on it, every
kind of handle and its methods, and every rule the lint knows with the
shape of its answer.  Each name is read from the registration in
src/script, so the page cannot cite a method the program does not have.
Run it after a change to the API:

    python3 tools/gen_script_api.py           # write the page
    python3 tools/gen_script_api.py --check   # fail when the page is stale

A description is the first sentence of the block comment directly above
the function, or of the entry the file's header comment gives it, or the
curated line below for a function whose source carries neither.  A
curated name that the source does not register is a fault: the table
below may describe a function, never invent one.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "src", "script")
OBJECT = os.path.join(SRC, "api_object.c")
LINT = os.path.join(SRC, "lint.c")
ENTRY = ("script.c", "arc_open")
MATERIALS = os.path.join(ROOT, "src", "render", "mesh", "materials.def")
OUT = os.path.join(ROOT, "docs", "appendix-script-api.rst")

#  Curated.  A function whose source carries no comment directly above it
#  and no entry in its file's header.  Keyed by the qualified name, and
#  checked against the registration: a name here that the source does
#  not register stops the generator.
CURATED = {
    "arc.log": "A message through the log, at note level",
    "arc.warn": "A message through the log, at warning level",
    "arc.error": "A message through the log, at error level",
    "arc.dump": "A report line through the dump sink, where `--dump-to` says",
    "arc.reload": "Reads the scripts again at once, without waiting for the watch",
    "arc.settings": "A plain snapshot of `arc.tune` and `arc.geo`, for a script that prints or walks them",
    "arc.numbers": "A table of named numbers the pipeline reads once a build, pushed down when the script is read",
    "arc.pieces": "What each building byte carries: the family, its piece in the shared layout, and the second family a meet carries",
    "arc.bands": "Which building bytes are a band, with the kind and the axis of each",
    "arc.fit": "The tangent fit as a service: points, radii and tangent budgets in, pieces out",
    "arc.city.tile": "Every layer at one cell, as a table keyed by the layer's name",
    "arc.city.line_class": "The class of the line on a tile: 0 a line, 1 an avenue, 2 a boulevard",
    "arc.put.sqrt": "The pipeline's own square root, at the pipeline's own precision",
    "arc.put.sin": "The pipeline's own sine, at the pipeline's own precision",
    "arc.put.cos": "The pipeline's own cosine, at the pipeline's own precision",
    "arc.put.tan": "The pipeline's own tangent, at the pipeline's own precision",
    "arc.put.acos": "The pipeline's own arc cosine, at the pipeline's own precision",
    "arc.put.atan2": "The pipeline's own two-argument arc tangent, at the pipeline's own precision",
}

#  What the two proxies and the rules table are, since none carries a
#  function of its own.  The registration is still checked: a name here
#  that api_tune.c or api_rules.c does not open stops the generator.
PROSE = {
    "arc.tune": (
        "A live table of the look's knobs, and no functions. A read answers "
        "the value the build runs with. A write moves the knob, holds it to "
        "its range, and asks for the world to be drawn again. A name that is "
        "not a knob is refused. ``pairs`` cannot walk it: ``arc.settings()`` "
        "answers a snapshot."),
    "arc.geo": (
        "A live table of the line works' numbers, and no functions. A read "
        "answers the value the build runs with, or nothing for a name no "
        "script has set. A write to a new name makes the number, so there is "
        "nothing to declare before a script invents one. ``pairs`` cannot "
        "walk it: ``arc.settings()`` answers a snapshot."),
    "arc.rules": (
        "An empty table when the state comes up. A script sets a rule on it "
        "as a function, and the pipeline calls the rule by name while it "
        "builds the mesh. The rules the lint knows are listed under "
        "`The rules`_ below. A family's declaration may name a rule of its "
        "own invention for a stage, and the lint learns that name from "
        "``arc.family.define``."),
}


def read(path):
    return open(path).read()


def strip_comments(src):
    """The source with every block comment blanked, so a brace or a call
    inside one does not read as code."""
    return re.sub(r"/\*(?:(?!\*/).)*\*/", "", src, flags=re.S)


#  ---- descriptions -------------------------------------------------------

#  A block comment and, after nothing but whitespace, the definition it
#  describes.  The tempered dot keeps one comment from reaching across
#  the next, so two comments back to back yield only the nearer one.
ABOVE = re.compile(r"/\*((?:(?!\*/).)*)\*/\s*static int (\w+)\(lua_State \*L\)", re.S)


def clean(comment):
    """A comment's text as one line: the leaders gone, the whitespace
    collapsed."""
    lines = []
    for ln in comment.split("\n"):
        ln = re.sub(r"^\s*\*\s?", "", ln)
        lines.append(ln.strip())
    return re.sub(r"\s+", " ", " ".join(lines)).strip()


def sentence(text):
    """The first sentence: up to the first full stop that ends a word.  A
    dot inside a qualified name or a decimal has no space after it."""
    m = re.search(r"\.(?:\s|$)", text)
    return text[:m.start()] if m else text


def comments_above(src):
    """Function name -> the comment directly above its definition."""
    return {m.group(2): clean(m.group(1)) for m in ABOVE.finditer(src)}


def header_entries(src):
    """The entries a file's header comment lists: `arc.x.y(args)` at the
    start of a line, described after a colon on the same line or on the
    deeper-indented lines under it.  Qualified name -> (args, text).  An
    entry whose text runs on without a colon is a wrapped list, and a
    wrapped list cannot say where one entry ends, so it is left out."""
    m = re.match(r"\s*/\*((?:(?!\*/).)*)\*/", src, re.S)
    if not m:
        return {}
    out = {}
    cur = None
    for raw in m.group(1).split("\n"):
        ln = re.sub(r"^\s*\*", "", raw)
        e = re.match(r"^(\s*)(arc(?:\.\w+)+)\(([^)]*)\)\s*(:)?\s*(.*)$", ln)
        if e:
            cur = None
            if e.group(5) and not e.group(4):
                continue
            cur = (e.group(2), len(e.group(1)))
            out[e.group(2)] = [e.group(3).strip(), e.group(5).strip()]
            continue
        if cur and ln.strip():
            indent = len(ln) - len(ln.lstrip())
            if indent > cur[1]:
                out[cur[0]][1] += " " + ln.strip()
                continue
        cur = None
    return {k: (v[0], re.sub(r"\s+", " ", v[1]).strip()) for k, v in out.items()}


def rst(text):
    """The text as reStructuredText: a backtick span becomes a literal,
    and the characters that open markup are escaped outside one."""
    parts = re.split(r"(`[^`]+`)", text)
    out = []
    for i, p in enumerate(parts):
        if i % 2:
            out.append("``" + p[1:-1] + "``")
        else:
            p = p.replace("*", "\\*").replace("|", "\\|")
            p = re.sub(r"(\w)_(?=\W|$)", r"\1\\_", p)
            out.append(p)
    return "".join(out)


def describe(qualified, fn, comments, header):
    """One sentence for a function, and its signature where the comment
    opens with one.  The signature is taken off the whole comment first,
    so a comment that is a signature and then a sentence keeps the
    sentence."""
    sig = None
    text = comments.get(fn, "")
    if not text and qualified in header:
        args, text = header[qualified]
        sig = "(" + args + ")"
    if not text:
        text = CURATED.get(qualified, "")
    #  The signature's brackets, one level of nesting inside them: a
    #  table argument carries a table of its own.
    lead = re.match(r"^(arc(?:\.\w+)+)([({](?:[^(){}]|\{[^{}]*\}|\([^()]*\))*[)}])\s*[:.]?\s*(.*)$", text)
    if lead and lead.group(1) == qualified:
        sig, text = lead.group(2), lead.group(3)
    text = sentence(text).rstrip(".").strip()
    text = re.sub(r"^(?:And|Also)\s+", "", text)
    if text and text[0].islower():
        text = text[0].upper() + text[1:]
    return sig, rst(text)


#  ---- the namespaces, by running the registration --------------------------

TOKEN = re.compile(r"""
    (?P<newtable>lua_(?:newtable|createtable)\(L\b)
  | lua_pushcfunction\(L,\s*(?P<fn>\w+)\)
  | (?P<push>lua_push(?:integer|number|string|boolean|lightuserdata|value|cclosure)\(L\b)
  | lua_setfield\(L,\s*-2,\s*(?:"(?P<key>\w+)"|(?P<wild>[^)]*))\)
  | (?P<seti>lua_rawseti\(L\b)
  | lua_pop\(L,\s*(?P<pop>\d+)\)
  | (?P<setglobal>lua_setglobal\(L\b)
  | (?P<setmeta>lua_setmetatable\(L\b)
  | table_open\(L,\s*"(?P<proxy>\w+)"
  | (?P<open>\w+_open)\(L\)
""", re.X)


def function_body(src, name):
    """The braces of `void name(lua_State *L)`, comments already gone."""
    m = re.search(r"void\s+" + re.escape(name) + r"\(lua_State \*L\)\s*\{", src)
    if not m:
        return None
    depth, i = 1, m.end()
    while i < len(src) and depth:
        depth += {"{": 1, "}": -1}.get(src[i], 0)
        i += 1
    return src[m.end():i - 1]


class Sim:
    """The Lua stack, followed through an open function: a table pushed,
    a function pushed, a field set.  What lands on the `arc` table is
    what a script reaches."""

    def __init__(self, sources):
        self.sources = sources  # file -> comment-free source
        self.stack = []

    def run(self, file, name):
        body = function_body(self.sources[file], name)
        if body is None:
            raise SystemExit("gen_script_api: no %s in %s" % (name, file))
        for m in TOKEN.finditer(body):
            self.step(m, file)

    def step(self, m, file):
        """One call in the registration, applied to the stack."""
        k = m.lastgroup
        if k == "newtable":
            self.stack.append(("table", {}))
        elif k == "fn":
            self.stack.append(("fn", m.group("fn"), file))
        elif k == "push":
            self.stack.append(("value",))
        elif k in ("key", "wild"):
            v = self.stack.pop()
            if k == "key":
                self.stack[-1][1][m.group("key")] = v
        elif k in ("seti", "setglobal", "setmeta"):
            self.stack.pop()
        elif k == "pop":
            del self.stack[len(self.stack) - int(m.group("pop")):]
        elif k == "proxy":
            self.stack[-1][1][m.group("proxy")] = ("proxy",)
        elif k == "open":
            self.call(m.group("open"))

    def call(self, name):
        """An open function called from another: the file that defines it.
        The handle kinds are registered through their own table and read
        from it, so api_object_open is not followed here."""
        if name == "api_object_open":
            return
        for file, src in self.sources.items():
            if re.search(r"void\s+" + re.escape(name) + r"\(lua_State \*L\)\s*\{", src):
                self.run(file, name)
                return
        raise SystemExit("gen_script_api: no definition of %s" % name)


def run_entry(sim):
    """arc_open builds the `arc` table and pops it into the globals, so
    the table is read off the stack just before the pop."""
    body = function_body(sim.sources[ENTRY[0]], ENTRY[1])
    if body is None:
        raise SystemExit("gen_script_api: no %s in %s" % (ENTRY[1], ENTRY[0]))
    arc = None
    for m in TOKEN.finditer(body):
        if m.lastgroup == "setglobal":
            arc = sim.stack[-1][1]
        sim.step(m, ENTRY[0])
    if arc is None:
        raise SystemExit("gen_script_api: %s never sets the global" % ENTRY[1])
    return arc


#  ---- the handle kinds ---------------------------------------------------

def reg_tables(src):
    """Each luaL_Reg table in a file: its C name -> [(method, function)]."""
    out = {}
    for m in re.finditer(r"(?:static )?const luaL_Reg ([A-Z]+)\[\] = \{(.*?)\n\};", src, re.S):
        out[m.group(1)] = re.findall(r'\{"([a-z_0-9]+)",\s*([a-z_0-9]+)\s*\}', m.group(2))
    return out


def kinds():
    """Kind name -> (file, [(method, function)]), from the KINDS[] rows and
    the one kind registered beside them through a method-list function.

    A kind's methods are in the file that knows the thing, so every
    source is read and the table is looked for in all of them."""
    src = read(OBJECT)
    tables, where = {}, {}
    for f in sorted(glob.glob(os.path.join(SRC, "*.c"))):
        for tbl, rows in reg_tables(read(f)).items():
            tables[tbl] = rows
            where[tbl] = os.path.basename(f)
    out = {}
    body = re.search(r"\} KINDS\[\] = \{(.*?)\n\};", src, re.S)
    if not body:
        raise SystemExit("gen_script_api: no KINDS[] in api_object.c")
    for name, tbl in re.findall(r'\{"([a-z_]+)",\s*([A-Z]+)\s*\}', body.group(1)):
        if tbl not in tables:
            raise SystemExit("gen_script_api: KINDS names %s, which no source has a table for" % tbl)
        out[name] = (where[tbl], tables[tbl])
    for lister, name in re.findall(r'luaL_setfuncs\(L, (\w+)\(&n\), 0\)[^\n]*lua_setfield\(L, -2, "([a-z_]+)"\)', src):
        for f in sorted(glob.glob(os.path.join(SRC, "*.c"))):
            s = read(f)
            m = re.search(r"const luaL_Reg \*" + re.escape(lister) + r"\(int \*n\)\s*\{[^}]*return ([A-Z]+);", s)
            if m:
                out[name] = (os.path.basename(f), reg_tables(s)[m.group(1)])
                break
        else:
            raise SystemExit("gen_script_api: no definition of %s" % lister)
    if not out:
        raise SystemExit("gen_script_api: found no kinds")
    return out


#  ---- the rules ----------------------------------------------------------

def rules():
    """Every rule the lint knows, with the shape of its answer."""
    src = read(LINT)
    m = re.search(r"\} RULES\[\] = \{(.*?)\n\};", src, re.S)
    if not m:
        raise SystemExit("gen_script_api: no RULES[] in lint.c")
    out = re.findall(r'\{"([a-z_0-9]+)",\s*"([^"]+)"\}', m.group(1))
    if not out:
        raise SystemExit("gen_script_api: RULES[] is empty")
    return sorted(out)


def materials():
    """The built-in materials arc.mat answers by name, in the numbers'
    own order."""
    out = []
    for line in open(MATERIALS):
        line = line.split("#")[0].strip()
        if not line:
            continue
        m = re.match(r"([0-9.]+)\s+([A-Z_0-9]+)\s+([a-z_0-9]+)\s+(.*)$", line)
        if not m:
            raise SystemExit("gen_script_api: cannot read %r in materials.def" % line)
        out.append((m.group(3), m.group(4).strip()))
    return out


#  ---- the page -----------------------------------------------------------

def heading(text, ch):
    return [text, ch * len(text), ""]


def table(head, rows, widths="34 66"):
    out = [".. list-table::", "   :header-rows: 1", "   :widths: %s" % widths, ""]
    out.append("   * - %s" % head[0])
    out.append("     - %s" % head[1])
    for a, b in rows:
        out.append("   * - %s" % a)
        out.append("     - %s" % (b or "\\-"))
    out.append("")
    return out


def literal(name, sig):
    return "``%s%s``" % (name, sig) if sig is not None else "``%s``" % name


def page(arc, kind_tables, rule_rows, mats, comments, headers):
    out = [".. _appendix-script-api:", "", "The script API", "==============", ""]
    out.append(".. rubric:: Generated by ``tools/gen_script_api.py`` from ``src/script``. "
               "Do not edit it by hand: change the source and run the generator.")
    out += ["",
            "Everything a script reaches is under one global, ``arc``. The pipeline "
            "hands a script an object: a handle of one kind, with a table of methods. "
            "``o.kind`` names the kind, and ``o.alive`` says whether the record behind "
            "the handle is still the pipeline's current one. A handle is dead when the "
            "rule that was given it returns.",
            "",
            "A rule is a function a script sets on ``arc.rules``. The pipeline calls "
            "the rule by name while it builds the mesh. Every index the API takes or "
            "answers counts from nought. A sequence a rule returns is Lua's own and "
            "counts from one.",
            ""]

    #  The namespaces: arc itself first, then its tables by name.
    out += heading("The namespaces", "-")
    names = ["arc"] + sorted("arc." + k for k, v in arc.items() if v[0] in ("table", "proxy"))
    for ns in names:
        entry = arc if ns == "arc" else arc[ns[4:]]
        out += heading(ns, "~")
        if ns in PROSE:
            if ns == "arc" or entry[0] not in ("proxy", "table"):
                raise SystemExit("gen_script_api: %s is described as prose but is not a table" % ns)
            out += [PROSE[ns], ""]
            if entry[0] == "table" and entry[1]:
                raise SystemExit("gen_script_api: %s carries functions the prose does not list" % ns)
            continue
        fields = entry if ns == "arc" else entry[1]
        fns = sorted((k, v) for k, v in fields.items() if v[0] == "fn")
        #  What is on the table and is not a function: a number, or a
        #  table of plain values the script reads with ipairs.
        values = sorted(k for k, v in fields.items() if v[0] != "fn" and ns != "arc")
        rows = []
        for k, v in fns:
            q = ns + "." + k
            sig, text = describe(q, v[1], comments[v[2]], headers[v[2]])
            rows.append((literal(q, sig), text))
        if rows:
            out += table(("Function", "What it does"), rows)
        if values:
            out += ["Values on the table: %s." % ", ".join("``%s``" % v for v in values), ""]
        if ns == "arc.mat":
            out += ["The built-in materials, by name and in the order of their numbers:", ""]
            out += table(("Material", "What it is"), [("``arc.mat.%s``" % n, rst(w)) for n, w in mats])
    #  Every curated name must be one the source registers.
    registered = set()
    for ns in names:
        fields = arc if ns == "arc" else arc[ns[4:]][1] if arc[ns[4:]][0] == "table" else {}
        registered |= {ns + "." + k for k in fields}
    for q in sorted(CURATED):
        if q not in registered:
            raise SystemExit("gen_script_api: CURATED names %s, which nothing registers" % q)
    for q in sorted(PROSE):
        if q not in names:
            raise SystemExit("gen_script_api: PROSE names %s, which nothing opens" % q)

    #  The handles: world first, the rest by name.
    out += heading("The handles", "-")
    out += ["Each kind is one table of methods. ``KINDS[]`` in "
            "``src/script/api_object.c`` lists them, and each table is written in the file "
            "that knows the thing, which is named under every kind below. A rule asks for the "
            "kind of its own name, and ``tools/script_api.py`` checks that every accessor in "
            "those files is named ``api_<kind>_<method>``.", ""]
    order = sorted(kind_tables, key=lambda k: (k != "world", k))
    for kind in order:
        file, methods = kind_tables[kind]
        out += heading(kind, "~")
        out += ["Its methods are in ``src/script/%s``." % file, ""]
        rows = []
        for method, fn in sorted(methods):
            _, text = describe(kind + ":" + method, fn, comments[file], headers[file])
            rows.append(("``%s:%s``" % (kind[0], method), text))
        out += table(("Method", "What it does"), rows)

    #  The rules.
    out += heading("The rules", "-")
    out += ["Every rule ``src/script/lint.c`` knows, with the shape of the answer its "
            "caller reads. A rule a script sets under any other name is reported by "
            "``arcology --lua-lint`` as one nothing will call, unless a family's "
            "declaration names it for a stage.", ""]
    out += table(("Rule", "Answers"), [("``arc.rules.%s``" % n, rst(s)) for n, s in rule_rows])
    return "\n".join(out).rstrip("\n") + "\n"


def build():
    files = sorted(glob.glob(os.path.join(SRC, "*.c")))
    raw = {os.path.basename(f): read(f) for f in files}
    comments = {k: comments_above(v) for k, v in raw.items()}
    headers = {k: header_entries(v) for k, v in raw.items()}
    sim = Sim({k: strip_comments(v) for k, v in raw.items()})
    arc = run_entry(sim)
    return page(arc, kinds(), rules(), materials(), comments, headers)


def main():
    check = "--check" in sys.argv
    text = build()
    if check:
        try:
            have = read(OUT)
        except OSError:
            have = None
        if have != text:
            print("%s is not what src/script registers; run tools/gen_script_api.py"
                  % os.path.relpath(OUT, ROOT), file=sys.stderr)
            return 1
        return 0
    open(OUT, "w").write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
