"""One way to run the renderer from a proof, where a fault is a fault.

A tool that proves a property of a world runs the binary through
`world`, never through subprocess itself.  A run that exits with an
error, crashes, times out, or whose log says the build was abandoned or
is incomplete raises Fault.  So a proof that compares two worlds can never find two faults
"the same": the first one stops it.

    from arcrun import world, Fault
    text = world(binary, [city, "--mute", "--run", "1"])

`text` is everything the run printed.  With `dump` set, the run's dumps
go to that file and the file's text is returned instead.
"""
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

ERROR = re.compile(r"^\[[^\]]*\]\[[^\]]*ERROR[^\]]*\]\[[^\]]*\] (.*)$", re.M)
ABANDONED = ("build abandoned", "INCOMPLETE", "the build FAILED", "runs away", "attempt to")


class Fault(Exception):
    """The world was not built, and the run says why."""


def world(binary, args, dump=None, timeout=600):
    cmd = [binary] + list(args)
    if dump:
        cmd += ["--dump-to", dump]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", cwd=ROOT, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise Fault("%s: no answer in %d seconds" % (" ".join(args), timeout))
    text = p.stdout + p.stderr
    if p.returncode != 0:
        m = ERROR.search(text)
        raise Fault("%s: exit %d%s" % (" ".join(args), p.returncode, ": " + m.group(1)[:160] if m else ""))
    for word in ABANDONED:
        if word in text:
            m = ERROR.search(text)
            raise Fault("%s: %s" % (" ".join(args), (m.group(1) if m else word)[:160]))
    if dump:
        if not os.path.exists(dump):
            raise Fault("%s: wrote no dump" % " ".join(args))
        with open(dump, encoding="utf-8") as f:
            return f.read()
    return text
