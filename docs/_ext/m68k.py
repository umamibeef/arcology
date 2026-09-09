"""A Pygments lexer for the Motorola 68000 listings in these documents.

Pygments ships no 68k lexer, and the nearest ones (gas, nasm) get the
size suffixes and the addressing modes wrong, which is worse than no
highlighting at all.  This one is written for the exact shape the
disassembly takes here::

    021C28: cmpi.w  #$dc, d3        ; Pump -> $21C5E
    0004B8  movem.l d2-d3,-(a7)

Register it with `extensions = [..., "m68k"]` and use it as
`.. code-block:: m68k`.
"""
from pygments.lexer import RegexLexer, bygroups, words
from pygments.token import (Comment, Keyword, Name, Number, Operator,
                            Punctuation, Text, Generic)

#  every mnemonic the listings actually use, longest first so that `move`
#  cannot match the front of `movem`
MNEMONICS = (
    "movem", "movep", "moveq", "movea", "move", "lea", "pea", "link", "unlk",
    "addq", "adda", "addi", "addx", "add", "subq", "suba", "subi", "subx", "sub",
    "muls", "mulu", "divs", "divu", "neg", "not", "clr", "ext", "swap", "exg",
    "andi", "and", "ori", "or", "eori", "eor", "cmpm", "cmpa", "cmpi", "cmp",
    "tst", "tas", "btst", "bchg", "bclr", "bset",
    "lsl", "lsr", "asl", "asr", "rol", "ror", "roxl", "roxr",
    "bsr", "bra", "bhi", "bls", "bcc", "bcs", "bne", "beq", "bvc", "bvs",
    "bpl", "bmi", "bge", "blt", "bgt", "ble",
    "dbra", "dbf", "dbeq", "dbne", "jsr", "jmp", "rts", "rte", "rtr", "rtd",
    "nop", "trap", "trapv", "chk", "stop", "reset", "illegal",
    "scc", "scs", "seq", "sne", "spl", "smi", "sge", "slt", "sgt", "sle",
    "st", "sf", "dc", "ds", "dcb",
)


class M68kLexer(RegexLexer):
    name = "Motorola 68000"
    aliases = ["m68k", "68k", "m68000"]
    filenames = ["*.s68", "*.x68"]

    tokens = {
        "root": [
            (r"[ \t]+", Text),
            (r"[;|].*?$", Comment.Single),
            #  the address column, with or without a colon
            (r"^([0-9A-Fa-f]{4,8})(:?)(?=[ \t])",
             bygroups(Name.Label, Punctuation)),
            #  a Mac toolbox trap reads as its own thing
            (r"\b_[A-Za-z][A-Za-z0-9_]*", Name.Function),
            #  mnemonic plus optional size suffix
            (words(MNEMONICS, prefix=r"\b", suffix=r"(?:\.[bwlBWL])?\b"),
             Keyword),
            (r"\b[ad][0-7]\b|\b(?:sp|pc|sr|ccr|usp)\b", Name.Builtin),
            (r"#\$[0-9A-Fa-f]+|#-?\d+", Number.Integer),
            (r"\$[0-9A-Fa-f]+", Number.Hex),
            (r"%[01]+", Number.Bin),
            (r"\b\d+\b", Number.Integer),
            (r"[-+*/=<>]", Operator),
            (r"[(),.\[\]{}#:@]", Punctuation),
            (r"\b[A-Za-z_][A-Za-z0-9_]*\b", Name),
            (r".", Text),
        ],
    }


def setup(app):
    from sphinx.highlighting import lexers
    lexers["m68k"] = M68kLexer()
    return {"version": "1.0", "parallel_read_safe": True,
            "parallel_write_safe": True}
