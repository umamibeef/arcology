#!/usr/bin/env python3
"""code_smells.py -- the call graph of src/, and the shapes of code it may not have.

    python3 tools/code_smells.py              # the rules, and every place that breaks one
    python3 tools/code_smells.py --map F.json # also the whole graph, for the code map page

The graph is read from the sources, no build needed.  A call is a name
followed by a bracket that resolves to a function the tree defines: the
same file's static one first, else the one exported.  A function handed
over by name, or held in a table a function reads, is a reference.

The rules, each a shape that a change must not bring back:

    upward    a call up a level (tools/layering.py's levels).  Under the
              top of the renderer, a call into the top is up, except into
              geo.c, material.c and prof.c, the vocabulary every level reads.
    copy      two functions of eight lines or more with the same body, token
              for token, the literals set aside.
    table     one enum's names, or one run of three or more strings, written
              in two places: an array of strings sized by the same count, or
              the same strings in the same order.
    dead      a function nothing calls, names or holds in a table.
    exported  a function that is not static, and only its own file calls.
    cycle     files that call round a loop: two or more, each reached from
              every other through calls.
    home      a function only one other file calls, and that calls more in
              that file than in its own.
    state     a variable of a file of the road network (src/network/) that
              its file's state list leaves out, or a name the list holds that
              is no variable of the file.  The store keeps a derivation by
              that list (network/state.h), so a variable left out of it would
              belong to whichever network was derived last.  The store's own
              files keep them, and hold no list: store.c the networks and
              solved.c each interchange's search.  A _Thread_local
              variable is a thread's own scratch, shared with no network,
              and is not in the list.

EXCEPTIONS names each place a rule allows, with the reason.  A place the
list does not name fails, and so does a listed place the rule does not
find, so the list can only shrink.
"""
import collections
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'src')
sys.path.insert(0, HERE)
import layering  # noqa: E402  the levels, and what each may include

#  A file that stands in for others in another configuration of the build.
#  It defines the same names as the file it replaces, so it is left out.
ALTERNATIVES = {'ui/ui_none.c', 'app/stub.c'}

#  The top of the renderer that every level under it reads.
SHARED_TOP = {'geo.c', 'material.c', 'prof.c'}

#  (rule, place) -> why the rule allows it.
EXCEPTIONS = {
    ('table', 'sim/net_cell.c:NAME = ui/names.c:RUI_SUB_NAME'):
        'two facts that share words: the networks, and the palette\'s sub-tools, which the original names in its own '
        'table at A5+0x28',
    ('table', 'ui/names.c:RUI_SUB_NAME = ui/windows.cpp:ZONE_NAME'):
        'two facts that share words: the zone tools as the original\'s menus name them, and the zone a tile holds '
        '(XZON) as the query window names it',
}

KEYWORDS = set('if for while switch return sizeof do else case goto break continue defined '
               'static_assert _Static_assert alignof offsetof'.split())


def sources():
    out = []
    for dp, dn, fs in os.walk(SRC):
        dn[:] = [d for d in dn if d not in ('vendor', 'generated', 'shaders')]
        for f in fs:
            if f.endswith(('.c', '.cpp')):
                rel = os.path.relpath(os.path.join(dp, f), SRC)
                if rel not in ALTERNATIVES:
                    out.append(rel)
    return sorted(out)


def headers():
    out = []
    for dp, dn, fs in os.walk(SRC):
        dn[:] = [d for d in dn if d not in ('vendor', 'generated', 'shaders')]
        out += [os.path.relpath(os.path.join(dp, f), SRC) for f in fs if f.endswith(('.h', '.hpp'))]
    return sorted(out)


def blank(s):
    """Comments, strings and characters blanked to spaces, so lines and offsets stay."""
    s = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), s, flags=re.S)
    s = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), s)
    s = re.sub(r"'(?:\\.|[^'\\\n])'", lambda m: ' ' * len(m.group(0)), s)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"', s)


def clean(c):
    return '\n'.join(re.sub(r'^\s*\*? ?', '', l).rstrip() for l in c.split('\n')).strip()


def first_comment(src):
    m = re.match(r'\s*/\*(.*?)\*/', src, flags=re.S)
    return clean(m.group(1)) if m else ''


def comment_before(src, pos):
    """The block comment that ends right before `pos`, past blank space only."""
    k = pos
    while k > 0 and src[k - 1] in ' \t\n':
        k -= 1
    if src[k - 2:k] != '*/':
        return ''
    i = src.rfind('/*', 0, k)
    if src[src.rfind('\n', 0, i) + 1:i].strip():
        return ''
    return clean(src[i + 2:k - 2])


def parse(rel):
    """The functions a file defines, each with its body."""
    src = open(os.path.join(SRC, rel), errors='replace').read()
    code = blank(src)
    funcs, depth, i, n, start = [], 0, 0, len(code), 0
    while i < n:
        ch = code[i]
        at_line = i == 0 or code[i - 1] == '\n'
        if depth == 0 and ch == '{' and re.search(r'(extern\s*"\s*"|namespace\s*\w*)\s*$', code[start:i]):
            i += 1
            start = i
            continue
        if depth == 0 and ch == '}':
            i += 1
            start = i
            continue
        if depth == 0 and at_line and re.match(r'[A-Z][A-Z0-9_]*\([^\n]*\)[ \t]*\n', code[i:i + 400]):
            i = code.find('\n', i)
            start = i
            continue
        if ch == '#' and at_line:
            e = code.find('\n', i)
            while e > 0 and code[e - 1] == '\\':
                e = code.find('\n', e + 1)
            i = e if e > 0 else n
            start = i
            continue
        if ch == '{':
            if depth == 0:
                head = code[start:i]
                m = re.search(r'([A-Za-z_][\w:~]*)\s*\(([^;{}]*)\)\s*(?:const\s*)?(?:R_NONNULL\([^)]*\)\s*)?$', head)
                name = m.group(1).split('::')[-1] if m else None
                last = head.strip().split('\n')[-1] if head.strip() else ''
                if m and name not in KEYWORDS and not re.search(r'=\s*$', head) and not re.search(r'\b(struct|enum|union|typedef)\b', last):
                    d, j = 0, i
                    while j < n:
                        d += code[j] == '{'
                        d -= code[j] == '}'
                        if d == 0:
                            break
                        j += 1
                    hs = start + len(head) - len(head.lstrip())
                    decl = src[hs:i].strip()
                    funcs.append({'name': name,
                                  'line': src.count('\n', 0, hs) + 1,
                                  'end': src.count('\n', 0, j) + 1,
                                  'static': bool(re.match(r'(static|inline\s+static|static\s+inline)\b', decl)),
                                  'decl': ' '.join(decl.split())[:220],
                                  'comment': comment_before(src, hs),
                                  'body': code[i:j + 1]})
                    i = j + 1
                    start = i
                    continue
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                start = i + 1
        elif ch == ';' and depth == 0:
            start = i + 1
        i += 1
    return src, code, funcs


def level_of(rel):
    return layering.level_of(os.path.join(SRC, rel))


def norm(body):
    t = re.findall(r'[A-Za-z_]\w*|\d[\w.]*|\S', body)
    return ' '.join('N' if re.match(r'\d', x) else x for x in t)


def graph():
    F, defs, mac = {}, collections.defaultdict(list), {}
    for rel in headers() + sources():
        for m in re.finditer(r'#define\s+(\w+)\(\.\.\.\)\s+(\w+)\(', open(os.path.join(SRC, rel), errors='replace').read()):
            mac[m.group(1)] = m.group(2)
    for rel in sources():
        src, code, funcs = parse(rel)
        F[rel] = {'path': rel, 'level': level_of(rel), 'lines': src.count('\n') + 1, 'about': first_comment(src),
                  'funcs': funcs, 'src': src, 'code': code}
        for fn in funcs:
            defs[fn['name']].append((rel, fn))

    def resolve(name, here):
        name = mac.get(name, name)
        cands = defs.get(name)
        if not cands:
            return None
        for rel, fn in cands:
            if rel == here:
                return '%s::%s' % (rel, name)
        ext = [rel for rel, fn in cands if not fn['static']]
        return '%s::%s' % (ext[0], name) if len(ext) == 1 else None

    for rel, f in F.items():
        for fn in f['funcs']:
            calls, refs = {}, {}
            for m in re.finditer(r'\b([A-Za-z_]\w*)\b(\s*\()?', fn['body']):
                nm, paren = m.group(1), m.group(2)
                if nm in KEYWORDS or (nm == fn['name'] and paren):
                    continue
                r = resolve(nm, rel)
                if r:
                    (calls if paren else refs)[r] = 1
            fn['calls'], fn['refs'] = list(calls), [r for r in refs if r not in calls]
    # a table: a file-scope initializer that names functions.  Whoever reads
    # the table refers to them.
    holders = collections.defaultdict(set)
    for rel, f in F.items():
        code, tables = f['code'], {}
        spans = [(fn['line'], fn['end']) for fn in f['funcs']]
        for m in re.finditer(r'(?m)^[^\n;{()]*?\b([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*=\s*\{', code):
            line = code.count('\n', 0, m.start()) + 1
            if any(a <= line <= b for a, b in spans):
                continue
            d, j = 0, m.end() - 1
            while j < len(code):
                d += code[j] == '{'
                d -= code[j] == '}'
                if d == 0:
                    break
                j += 1
            refs = sorted({r for nm in re.findall(r'\b([A-Za-z_]\w*)\b(?!\s*\()', code[m.end():j]) if (r := resolve(nm, rel))})
            if refs:
                tables[m.group(1)] = refs
                for r in refs:
                    holders[r].add('%s::(table %s)' % (rel, m.group(1)))
        for fn in f['funcs']:
            for tv, refs in tables.items():
                if re.search(r'\b' + tv + r'\b', fn['body']):
                    fn['refs'] += [r for r in refs if r not in fn['refs'] and r not in fn['calls']]
    # a source calls what its own macros call
    for rel in sources():
        code = blank(open(os.path.join(SRC, rel), errors='replace').read())
        for line in re.findall(r'#define[^\n]*', code.replace('\\\n', ' ')):
            for nm in set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', line)):
                r = resolve(nm, rel)
                if r:
                    holders[r].add('%s::(macro)' % rel)
    # a header names functions too: in a table, or in a macro's body
    for rel in headers():
        code = blank(open(os.path.join(SRC, rel), errors='replace').read())
        named = set(re.findall(r'\b([A-Za-z_]\w*)\b(?!\s*\()', code))
        for line in re.findall(r'#define[^\n]*', code.replace('\\\n', ' ')):
            named |= set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', line))
        for nm in named:
            ext = [r for r, fn in defs.get(nm, []) if not fn['static']]
            if len(ext) == 1:
                holders['%s::%s' % (ext[0], nm)].add('%s::(header)' % rel)
    callers = collections.defaultdict(set)
    for k, v in holders.items():
        callers[k] |= v
    for rel, f in F.items():
        for fn in f['funcs']:
            me = '%s::%s' % (rel, fn['name'])
            for c in fn['calls'] + fn['refs']:
                callers[c].add(me)
    for rel, f in F.items():
        for fn in f['funcs']:
            fn['callers'] = sorted(callers.get('%s::%s' % (rel, fn['name']), ()))
    return F, defs


def file_statics(src):
    """The variables a file declares outside its functions, by name."""
    code = '\n'.join('' if l.lstrip().startswith('#') else l for l in blank(src).split('\n'))
    depth, top = 0, []
    for ch in code:
        if ch == '{':
            depth += 1
        if depth == 0:
            top.append(ch)
        if ch == '}':
            depth -= 1
            if depth == 0:
                top.append(';')
    names = []
    for st in ''.join(top).split(';'):
        st = ' '.join(st.split())
        if not st.startswith('static ') or 'typedef' in st or st.startswith('static inline') or '_Thread_local' in st:
            continue
        m = re.match(r'((?:unsigned |struct |const )*[A-Za-z_]\w*)', st[7:])
        base, rest = m.group(1), st[7:][m.end():]
        if re.match(r'\s*\**\s*[A-Za-z_]\w*\s*\(', rest):
            continue
        for part in re.split(r',(?![^{]*\})', rest):
            d = re.match(r'\s*(\**)\s*([A-Za-z_]\w*)', part)
            if d and not (base.startswith('const') and not d.group(1)):
                names.append(d.group(2))
    return names


def smells(F, defs):
    out = []

    def add(kind, where, text):
        out.append({'kind': kind, 'where': where, 'text': text})

    for rel, f in F.items():
        for fn in f['funcs']:
            me = '%s::%s' % (rel, fn['name'])
            cs = [c for c in fn['callers'] if c != me]
            files = {c.split('::')[0] for c in cs}
            twice = sum(1 for g in f['funcs'] if g['name'] == fn['name']) > 1
            if not cs and me != 'app/main.c::main' and not twice:
                add('dead', me, 'nothing calls it, names it or holds it in a table')
            elif not fn['static'] and files == {rel} and not rel.endswith('.cpp'):
                add('exported', me, 'it is not static, and only its own file calls it')
            elif not fn['static'] and len(files) == 1 and rel not in files and '::(' not in cs[0]:
                other = next(iter(files))
                mine = sum(1 for c in fn['calls'] if c.split('::')[0] == rel)
                theirs = sum(1 for c in fn['calls'] if c.split('::')[0] == other)
                if theirs > mine:
                    add('home', me, 'only %s calls it, and it calls %d functions there and %d in its own file' % (other, theirs, mine))
            for c in fn['calls']:
                to = c.split('::')[0]
                a, b = level_of(rel), level_of(to)
                below = a in ('mesh', 'draw', 'gpu', 'soft', 'atlas') and b == 'render' and os.path.basename(to) not in SHARED_TOP
                if a != b and (b not in layering.MAY.get(a, set()) or below):
                    add('upward', me, 'calls %s, a level up (%s -> %s)' % (c, a, b))
    seen = collections.defaultdict(list)
    for rel, f in F.items():
        for fn in f['funcs']:
            if fn['end'] - fn['line'] >= 7:
                seen[hashlib.sha1(norm(fn['body']).encode()).hexdigest()].append('%s::%s' % (rel, fn['name']))
    for where in seen.values():
        if len(where) > 1:
            add('copy', ' = '.join(sorted(where)), 'the same body, token for token, in %d places' % len(where))
    deps = collections.defaultdict(set)
    for rel, f in F.items():
        for fn in f['funcs']:
            for c in fn['calls']:
                if c.split('::')[0] != rel:
                    deps[rel].add(c.split('::')[0])
    # Files that reach each other through calls, by Tarjan's strongly
    # connected components: two files or more that call round a loop.
    index, low, stack, on, comps = {}, {}, [], set(), []

    def strong(v):
        index[v] = low[v] = len(index)
        stack.append(v)
        on.add(v)
        for w in sorted(deps.get(v, ())):
            if w not in index:
                strong(w)
                low[v] = min(low[v], low[w])
            elif w in on:
                low[v] = min(low[v], index[w])
        if low[v] == index[v]:
            comp = []
            while True:
                w = stack.pop()
                on.discard(w)
                comp.append(w)
                if w == v:
                    break
            if len(comp) > 1:
                comps.append(sorted(comp))

    for v in sorted(F):
        if v not in index:
            strong(v)
    for comp in comps:
        add('cycle', ' <-> '.join(comp), 'these %d files call round a loop' % len(comp))
    # tables of names: string arrays sized by one count, or one run of strings
    by_count, runs = collections.defaultdict(list), {}
    for rel in sources() + headers():
        src = open(os.path.join(SRC, rel), errors='replace').read()
        code = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), src, flags=re.S)
        for m in re.finditer(r'char\s*\*\s*const\s+(\w+)\s*\[\s*([A-Za-z_]\w*)\s*\]\s*=\s*\{', code):
            by_count[m.group(2)].append('%s:%s' % (rel, m.group(1)))
        for m in re.finditer(r'\b(\w+)\s*(?:\[[^\]]*\])*\s*=\s*\{', code):
            d, j = 0, m.end() - 1
            while j < len(code):
                d += code[j] == '{'
                d -= code[j] == '}'
                if d == 0:
                    break
                j += 1
            words = {re.sub(r'[^a-z0-9]', '', w.lower()) for w in re.findall(r'"([^"\n]*)"', code[m.end():j])} - {''}
            if len(words) >= 4:
                runs['%s:%s' % (rel, m.group(1))] = words
    for rel, f in F.items():
        if not rel.startswith('network/') or not rel.endswith('.c') or rel in ('network/store.c', 'network/solved.c'):
            continue
        have = file_statics(f['src'])
        m = re.search(r'#define\s+\w+_STATE\(X\)((?:[^\n]*\\\n)*[^\n]*)', f['src'])
        listed = re.findall(r'X\((\w+)\)', m.group(1)) if m else []
        for name in have:
            if name not in listed:
                add('state', '%s:%s' % (rel, name), 'a variable of the network that its state list leaves out')
        for name in listed:
            if name not in have:
                add('state', '%s:%s' % (rel, name), 'the state list names it, and it is no variable of the file')
    for count, where in by_count.items():
        if len(where) > 1:
            add('table', ' = '.join(sorted(where)), 'arrays of names sized by %s in %d places' % (count, len(where)))
    keys = sorted(runs)
    for x in range(len(keys)):
        for y in range(x + 1, len(keys)):
            a, b = runs[keys[x]], runs[keys[y]]
            both = a & b
            if len(both) >= 4 and 2 * len(both) >= min(len(a), len(b)):
                add('table', '%s = %s' % (keys[x], keys[y]), 'two tables of names share %d of them: %s' % (len(both), ', '.join(sorted(both)[:6])))
    return out


def main():
    F, defs = graph()
    found = smells(F, defs)
    if '--map' in sys.argv:
        path = sys.argv[sys.argv.index('--map') + 1]
        for f in F.values():
            for fn in f['funcs']:
                fn.pop('body', None)
            f.pop('src', None)
            f.pop('code', None)
        json.dump({'files': list(F.values()), 'smells': found}, open(path, 'w'))
    keys = {(s['kind'], s['where']) for s in found}
    bad = ['%-8s %s: %s' % (s['kind'], s['where'], s['text']) for s in found if (s['kind'], s['where']) not in EXCEPTIONS]
    gone = ['%-8s %s: the rule does not find it, so take it off the list' % k for k in EXCEPTIONS if k not in keys]
    if bad or gone:
        print('code_smells: %d place%s break%s a rule:' % (len(bad) + len(gone), '' if len(bad) + len(gone) == 1 else 's',
                                                          's' if len(bad) + len(gone) == 1 else ''))
        for b in sorted(bad) + gone:
            print('   ', b)
        return 1
    print('code_smells: %d files, %d functions, and every rule holds or is on the list of %d exceptions' %
          (len(F), sum(len(f['funcs']) for f in F.values()), len(EXCEPTIONS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
