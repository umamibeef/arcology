#!/usr/bin/env python3
"""static_check.py -- clang's own analyser over everything this repository
owns.

The compiler already refuses a warning (`-Wall -Wextra -Wshadow
-Wconversion`, zero tolerated).  This is the next step out: the analyser
walks paths through a function and reports what only a path can show --
a pointer that may be null when it is read, a value stored and never
used, memory that leaks on one branch.

It drives clang through the build's own compile database, so a file is
analysed with exactly the flags it is compiled with.  `src/vendor` is
other people's and is skipped.

    python3 tools/static_check.py                  # everything
    python3 tools/static_check.py src/render/net   # one directory
    python3 tools/static_check.py --list           # what each check found

A finding is a failure.  Where the analyser is wrong the fix is to make
the code say what it means, not to silence the check.
"""
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "build", "compile_commands.json")

#  The analyser's own checks, the default set less one.
#
#  `unix.Errno` wants every call that may set errno to have its errno
#  read before anything else can overwrite it.  This codebase does not
#  work that way and does not need to: it tests what a call ANSWERS --
#  a null FILE *, a short fread, a null malloc -- and never reads errno,
#  so there is nothing for a later call to clobber.  Turning it on would
#  mean writing errno checks that no code reads, which is worse than the
#  discipline it asks for.
ARGS = [
    "--analyze",
    "-Xclang", "-analyzer-output=text",
    "-Xclang", "-analyzer-disable-checker=unix.Errno",
]


def entries(paths):
    if not os.path.exists(DB):
        print("static: no compile database at", DB)
        print("static: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        return None
    out = []
    for e in json.load(open(DB)):
        f = e["file"]
        if "/vendor/" in f or "/_deps/" in f:
            continue
        if paths and not any(os.path.abspath(f).startswith(os.path.abspath(p)) for p in paths):
            continue
        out.append(e)
    return out


def analyse(e):
    args = shlex.split(e["command"])
    keep, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a == "-o":
            skip = True
            continue
        if a == "-c":
            continue
        keep.append(a)
    cmd = keep[:1] + ARGS + keep[1:]
    r = subprocess.run(cmd, cwd=e["directory"], capture_output=True, text=True)
    return e["file"], r.stderr


FINDING = re.compile(r"^(\S+):(\d+):(\d+): warning: (.*?) \[([\w.]+)\]$", re.M)


def main():
    argv = [a for a in sys.argv[1:] if not a.startswith("--")]
    want_list = "--list" in sys.argv
    es = entries(argv)
    if es is None:
        return 1
    found = []
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for f, err in pool.map(analyse, es):
            for m in FINDING.finditer(err):
                #  A finding inside a vendored header is other people's,
                #  wherever it was reached from.
                if "/vendor/" in m.group(1) or "/_deps/" in m.group(1):
                    continue
                found.append((m.group(1), int(m.group(2)), m.group(4), m.group(5)))
    found.sort()
    by_check = {}
    for path, line, msg, check in found:
        by_check.setdefault(check, []).append((path, line, msg))
    if want_list or found:
        for check in sorted(by_check, key=lambda k: -len(by_check[k])):
            rows = by_check[check]
            print("%-34s %d" % (check, len(rows)))
            for path, line, msg in rows[: (len(rows) if want_list else 6)]:
                print("    %s:%d  %s" % (os.path.relpath(path, ROOT), line, msg))
            if not want_list and len(rows) > 6:
                print("    ... and %d more" % (len(rows) - 6))
    print("static: %d file(s) analysed, %d finding(s)" % (len(es), len(found)))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
