"""The _FP68K trap, for running the game's economy under the interpreter.

$34D04 is the only part of the simulation that uses floating point, and
it does so entirely through this trap: push a pointer to the source,
push a pointer to the destination, push an opword, trap.  This mirrors
src/sim/sane.c so the oracle and the C compute the same numbers.

Arithmetic is done on exact rationals and then rounded back to a 64-bit
significand, which is what the 68881/SANE extended format holds -- doing
it in Python floats would quietly lose eleven bits of mantissa.
"""
from fractions import Fraction

FOADD, FOSUB, FOMUL, FODIV, FOCMP, FOZ2X, FOX2Z, FOTTI = (
    0x00, 0x02, 0x04, 0x06, 0x08, 0x0E, 0x10, 0x16)
FFEXT, FFDBL, FFSGL, FFINT, FFLNG, FFCOMP = 0, 1, 2, 4, 5, 6
BIAS = 16383


def ext_decode(b):
    """10 bytes -> (sign, Fraction) with zero as (sign, 0)"""
    hi = int.from_bytes(b[0:2], "big")
    sig = int.from_bytes(b[2:10], "big")
    sign = -1 if hi & 0x8000 else 1
    exp = hi & 0x7FFF
    if sig == 0:
        return sign, Fraction(0)
    return sign, Fraction(sig) * Fraction(2) ** (exp - BIAS - 63) * sign


def ext_encode(v):
    """Fraction -> 10 bytes, rounding the significand to 64 bits."""
    if v == 0:
        return b"\x00" * 10
    neg = v < 0
    v = -v if neg else v
    exp = 0
    #  scale into [2^63, 2^64)
    while v >= Fraction(2) ** 64:
        v /= 2; exp += 1
    while v < Fraction(2) ** 63:
        v *= 2; exp -= 1
    sig = v.numerator // v.denominator
    rem = v - sig
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and sig & 1):
        sig += 1
        if sig >= 1 << 64:
            sig >>= 1; exp += 1
    e = exp + BIAS + 63
    hi = (0x8000 if neg else 0) | (e & 0x7FFF)
    return hi.to_bytes(2, "big") + sig.to_bytes(8, "big")


def _load(mem, addr, fmt):
    if fmt == FFEXT:
        return ext_decode(bytes(mem[addr:addr + 10]))[1]
    if fmt == FFINT:
        v = int.from_bytes(mem[addr:addr + 2], "big", signed=True)
        return Fraction(v)
    if fmt == FFLNG:
        v = int.from_bytes(mem[addr:addr + 4], "big", signed=True)
        return Fraction(v)
    if fmt == FFCOMP:
        v = int.from_bytes(mem[addr:addr + 8], "big", signed=True)
        return Fraction(v)
    import struct
    if fmt == FFDBL:
        return Fraction(struct.unpack(">d", bytes(mem[addr:addr + 8]))[0])
    if fmt == FFSGL:
        return Fraction(struct.unpack(">f", bytes(mem[addr:addr + 4]))[0])
    return Fraction(0)


def _trunc(v):
    """toward zero, as FOTTI and the integer stores do"""
    n = abs(v.numerator) // v.denominator
    return Fraction(-n if v < 0 else n)


def _store(mem, addr, fmt, v):
    import struct
    if fmt == FFEXT:
        mem[addr:addr + 10] = ext_encode(v); return
    if fmt == FFINT:
        mem[addr:addr + 2] = (int(_trunc(v)) & 0xFFFF).to_bytes(2, "big"); return
    if fmt == FFLNG:
        mem[addr:addr + 4] = (int(_trunc(v)) & 0xFFFFFFFF).to_bytes(4, "big"); return
    if fmt == FFCOMP:
        mem[addr:addr + 8] = (int(_trunc(v)) & ((1 << 64) - 1)).to_bytes(8, "big"); return
    if fmt == FFDBL:
        mem[addr:addr + 8] = struct.pack(">d", float(v)); return
    if fmt == FFSGL:
        mem[addr:addr + 4] = struct.pack(">f", float(v)); return


def fp68k(mem, opword, dst, src):
    """Returns the comparison result for FOCMP, 0 otherwise."""
    op = opword & 0x1F
    fmt = (opword >> 11) & 0x7
    if op == FOZ2X:
        _store(mem, dst, FFEXT, _load(mem, src, fmt)); return 0
    if op == FOX2Z:
        _store(mem, dst, fmt, _load(mem, src, FFEXT)); return 0
    if op == FOTTI:
        _store(mem, dst, FFEXT, _trunc(_load(mem, dst, FFEXT))); return 0
    d = _load(mem, dst, FFEXT)
    s = _load(mem, src, fmt)
    if op == FOADD:   r = d + s
    elif op == FOSUB: r = d - s
    elif op == FOMUL: r = d * s
    elif op == FODIV: r = d / s if s != 0 else Fraction(0)
    elif op == FOCMP: return -1 if d < s else (1 if d > s else 0)
    else:
        raise RuntimeError("fp68k: unhandled opword $%04X" % opword)
    _store(mem, dst, FFEXT, r)
    return 0
