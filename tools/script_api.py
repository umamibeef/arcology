#!/usr/bin/env python3
"""The script API's own house style, checked.

The pipeline hands a script an OBJECT: a handle of some KIND, with a table
of methods.  Five rules hold that API together, and every one of them was
broken at least once while the API was being written, each time silently
-- geometry moved and nothing said so.  They are mechanical, so they are
checked here rather than remembered.

  kind        every rule asks for the kind of its own name, so a reader
              who knows one knows the other.  A kind that answers SEVERAL
              rules is listed in SHARED below and says why.
  prefix      an accessor is api_<kind>_<method>, and lives in that
              kind's table.  The pipeline's own functions are named for
              what they do, so a bare <kind>_<method> is ambiguous: the
              api_ prefix is what tells the two apart.
  index       every index the API takes or hands out counts from NOUGHT.
              A `- 1` on an argument means a script somewhere counts from
              one, and the two conventions cannot both be right.
  info        `info` answers a TABLE -- its own, or one api_fields builds
              from a Field list.  A rule reads it by name, so a plain list
              of values makes every field positional and a field added in
              the middle silently renames the rest.
  rules       every rule the scripts define is one the linter knows, and
              the other way about.  A rule nothing exercises is a rule
              nothing checks.

Run from anywhere; prints each breach and fails.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
API = os.path.join(ROOT, "src", "script", "api_object.c")
LINT = os.path.join(ROOT, "src", "script", "lint.c")
SCRIPTS = os.path.join(ROOT, "scripts")

#  A kind that answers more than one rule, and why it may.
SHARED = {
    "strip":   ("strip", "curves", "walks"),      # a strip is asked three things
    "footway": ("footway", "walk_curves"),        # and a footway two
    "tile":    ("tile", "zone_tint"),             # and a tile its ground and its tint
}

#  The metatable's own handlers are not accessors.
NOT_ACCESSORS = ("obj_index", "obj_tostring")


def tables(src):
    """Each luaL_Reg table: its C name and its (method, function) pairs."""
    for m in re.finditer(r"static const luaL_Reg ([A-Z]+)\[\] = \{(.*?)\n\};", src, re.S):
        yield m.group(1), re.findall(r'\{"([a-z_]+)",\s*([a-z_0-9]+)\s*\}', m.group(2))


def kinds_of(src):
    """The kind each table is registered under, from api_object_open."""
    out = {}
    for m in re.finditer(r'luaL_setfuncs\(L, ([A-Z]+), 0\), lua_setfield\(L, -2, "([a-z_]+)"\)', src):
        out[m.group(1)] = m.group(2)
    return out


def main():
    bad = []
    api = open(API).read()
    kind_of = kinds_of(api)

    #  prefix: an accessor is api_<kind>_<method>, in its own kind's table
    for tbl, entries in tables(api):
        kind = kind_of.get(tbl)
        if not kind:
            continue
        for meth, fn in entries:
            if not fn.startswith("api_%s_" % kind):
                bad.append(("prefix", "%s.%s is %s, not api_%s_%s"
                            % (kind, meth, fn, kind, meth)))

    #  index: nothing counts from one
    for m in re.finditer(r"luaL_(?:check|opt)integer\([^)]*\)\s*-\s*1", api):
        line = api[:m.start()].count("\n") + 1
        bad.append(("index", "api_object.c:%d turns a script's index into a C one; "
                             "both count from nought" % line))

    #  info: answers a table
    for tbl, entries in tables(api):
        for meth, fn in entries:
            if meth != "info":
                continue
            body = re.search(r"static int %s\(lua_State \*L\)\n\{(.*?)\n\}" % re.escape(fn), api, re.S)
            if body and not any(t in body.group(1) for t in
                                ("lua_newtable", "lua_createtable", "api_fields")):
                bad.append(("info", "%s answers plain values; info answers a table" % fn))

    #  kind: a rule asks for the kind of its own name
    for root, dirs, files in os.walk(os.path.join(ROOT, "src", "render")):
        for f in files:
            if not f.endswith(".c"):
                continue
            for m in re.finditer(r'script_rule_object\("([a-z_]+)", "([a-z_]+)"',
                                 open(os.path.join(root, f)).read()):
                rule, kind = m.group(1), m.group(2)
                if rule == kind:
                    continue
                if rule in SHARED.get(kind, ()):
                    continue
                bad.append(("kind", 'rule "%s" asks for kind "%s"; name them alike, '
                                    "or list the kind in SHARED" % (rule, kind)))

    #  rules: the scripts and the linter agree on what exists
    defined = set()
    for root, dirs, files in os.walk(SCRIPTS):
        for f in files:
            if f.endswith(".lua"):
                defined |= set(re.findall(r"arc\.rules\.([a-z_0-9]+)\s*=",
                                          open(os.path.join(root, f)).read()))
    known = set(re.findall(r'\{"([a-z_0-9]+)",\s*"[^"]*"\s*\}', open(LINT).read()))
    for r in sorted(defined - known):
        bad.append(("rules", "arc.rules.%s is defined and the linter does not know it" % r))
    for r in sorted(known - defined):
        bad.append(("rules", "the linter knows arc.rules.%s and no script defines it" % r))

    for kind, what in bad:
        print("%s: %s" % (kind, what))
    if bad:
        print("\n%d breach(es) of the script API's style -- see the head of "
              "tools/script_api.py." % len(bad))
        return 1
    print("script api: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
