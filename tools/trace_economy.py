"""Write down what economyPass ($34D04) actually computes.

The routine is 1,976 instructions and almost all of them are the four
that make up one SANE call: push the destination, push the source, push
an opword, trap.  Reading 186 of those by eye is exactly the kind of
bookkeeping that goes wrong, so this runs the routine under the
interpreter instead and records the operation sequence it really
executes -- opcode, operand formats, and where each operand lives.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from runsim import Sim, A5
import sane

OP = {0x00: "add", 0x02: "sub", 0x04: "mul", 0x06: "div", 0x08: "cmp",
      0x0E: "toext", 0x10: "fromext", 0x16: "trunc"}
FMT = {0: "ext", 1: "dbl", 2: "sgl", 4: "int16", 5: "int32", 6: "int64"}


def where(addr, a6):
    """name an operand by where it sits: a frame slot or an A5 global"""
    if abs(addr - a6) < 0x400:
        return "fp%+d" % (addr - a6)
    if abs(addr - A5) < 0x9000:
        return "A5%+d" % (addr - A5)
    return "$%X" % addr


def trace(city, limit=40000000):
    s = Sim(city)
    ops = []

    def at_fp(e, pc):
        sp = e.a[7]
        opw = e.rd(sp, 2)
        op = opw & 0x1F
        if op == 0x16:
            dst = e.rd(sp + 2, 4); src = None
        else:
            dst = e.rd(sp + 2, 4); src = e.rd(sp + 6, 4)
        ops.append((pc, opw, OP.get(op, "op%02X" % op), FMT.get((opw >> 11) & 7, "?"),
                    where(dst, e.a[6]), where(src, e.a[6]) if src is not None else None))

    s.e.wr(A5 + 0x11DC, 2, 1)
    s.e.tb_seed = 1
    s.e.a[7] = 0x00300000
    s.e.a[7] -= 4
    s.e.wr(s.e.a[7], 4, 0xDEAD0000)
    err = s.e.run(0x34D04, 0xDEAD0000, limit=limit, real_calls=True,
                  stubs={0x9728, 0x2EDE4}, watch={0x34D2E - 0x34D2E + 0xA9EB and 0: None})
    return err, ops, s


if __name__ == "__main__":
    city = sys.argv[1]
    s = Sim(city)
    ops = []

    def at_fp(e, pc):
        sp = e.a[7]
        opw = e.rd(sp, 2)
        op = opw & 0x1F
        if op == 0x16:
            dst = e.rd(sp + 2, 4); src = None
        else:
            dst = e.rd(sp + 2, 4); src = e.rd(sp + 6, 4)
        ops.append((pc, opw, OP.get(op, "op%02X" % op),
                    FMT.get((opw >> 11) & 7, "?"),
                    where(dst, e.a[6]), where(src, e.a[6]) if src is not None else None))

    #  every _FP68K site in the routine, so the watch fires on each call
    sites = [a for a, (mn, _ops) in s.e.code.items()
             if mn == "_FP68K" and 0x34D04 <= a < 0x366A0]
    s.e.wr(A5 + 0x11DC, 2, 1); s.e.tb_seed = 1
    s.e.a[7] = 0x00300000 - 4
    s.e.wr(s.e.a[7], 4, 0xDEAD0000)
    err = s.e.run(0x34D04, 0xDEAD0000, limit=40000000, real_calls=True,
                  stubs={0x9728, 0x2EDE4}, watch={a: at_fp for a in sites})
    sys.stderr.write("run: %s   %d SANE operations, %d distinct sites\n"
                     % (err or "completed", len(ops), len(sites)))
    for pc, opw, op, fmt, dst, src in ops:
        print("%06X  %-7s %-5s  %-10s %s" % (pc, op, fmt, dst, src or ""))
