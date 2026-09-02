"""A tiny 68k interpreter, just big enough to run the MISC builder.

It executes the disassembly text rather than the bytes, which keeps it
short: the builder at $2A186 uses about twenty opcodes and a handful of
addressing modes.  Every register carries a provenance string alongside
its value, so when a value is finally stored into the MISC buffer we can
say which A5 global it came from -- which is the whole point.
"""
import re, struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MASK = 0xFFFFFFFF
def s32(v): return v - (1 << 32) if v & 0x80000000 else v
def s16(v): return v - (1 << 16) if v & 0x8000 else v
def s8(v):  return v - (1 << 8)  if v & 0x80 else v

_PARSE_CACHE = {}


class Emu:
    A5 = 0x00400000                     # where we place the A5 world
    #  Memory map.  The heap used to start at 0x200000 and the stack at
    #  0x300000, which left the two exactly 1 MB apart -- and shapedec.py
    #  allocates 1.09 MB of span streams, so the heap grew straight through
    #  the stack and the game's own pushes scribbled on the sprite art.
    #  That surfaced as "shape 1252 cannot be decoded", a defect that did
    #  not exist: 1252 decodes perfectly on its own.  The A5 world occupies
    #  0x3F7000..0x404000 and everything above 0x480000 was unused, so the
    #  heap now lives there with the stack at the top of memory, and
    #  alloc() refuses to cross instead of corrupting silently.
    HEAP_BASE  = 0x00480000
    HEAP_LIMIT = 0x007A0000             # 3.25 MB of heap
    SP_INIT    = 0x007F0000             # stack grows down from here

    def __init__(self, listing, a5image, a5low):
        # Parsing 90k lines costs far more than running the code, and the
        # listing never changes, so do it once per process.
        key = len(listing)
        hit = _PARSE_CACHE.get(key)
        if hit is not None:
            self.code, self.next_pc = hit
        else:
            code = {}
            for line in listing:
                m = re.match(r'^([0-9A-F]{6}): ([0-9a-f]+)\s+(\S+)\s*(.*?)\s*$', line)
                if m:
                    code[int(m.group(1), 16)] = (m.group(3), m.group(4))
            #  The disassembler decoded the six-word jump table at
            #  $34912 as instructions, and the `ori.l` it invented at
            #  $3491C swallowed the two real instructions after it.
            #  Case 0 of the newspaper switch jumps to $3491E, so a run
            #  long enough to roll it walks into a hole and stops with
            #  "no code".  These two entries are read straight off the
            #  bytes: 554f a861.
            code[0x3491E] = ("subq.w", "#$2, a7")
            code[0x34920] = ("_Random", "")

            ks = sorted(code)                     # precomputed fall-through
            nxt = {a: b for a, b in zip(ks, ks[1:])}
            if ks: nxt[ks[-1]] = -1
            self.code, self.next_pc = code, nxt
            _PARSE_CACHE[key] = (code, nxt)
        self.mem = bytearray(1 << 23)
        self.mem[self.A5 - a5low:self.A5 - a5low + len(a5image)] = a5image
        # CODE 2 at its own address 0, so a routine that reads a switch
        # table out of the code segment finds the real bytes there.
        try:
            here = os.path.dirname(os.path.abspath(__file__))
            code = open(os.path.join(here, "..", "out", "code", "CODE_2.bin"), "rb").read()
            self.mem[0:len(code)] = code
        except OSError:
            pass
        self.d = [0]*8; self.a = [0]*8
        self.prov = {}                  # register name -> provenance string
        self.a[5] = self.A5
        self.a[7] = self.SP_INIT
        self.heap = self.HEAP_BASE
        self.Z = self.N = self.C = self.V = 0
        self.V = False
        self._sz = 4
        self.stores = []                # (miscIndex, provenance)
        self.trail = []                 # recent pcs, for diagnosing a bad jump
        self.tb_seed = 1                # Toolbox _Random, same LCG as rng.c
        self.rng_log = None             # set to a list to record every draw
        self.miscbuf = None

    # ---- snapshots ----------------------------------------------
    #  Every investigation of a late tick used to replay the whole clock
    #  from the save -- two to four minutes of interpreter before the
    #  interesting instruction was even reached, once per probe.  The
    #  machine state is a few megabytes of plain data, so it can simply
    #  be kept.
    #
    #  The code and the parsed listing are NOT in here: they come from
    #  the binary and never change during a run, and re-reading them
    #  from the cache is what the constructor already does.
    SNAP = ("d", "a", "heap", "Z", "N", "C", "V", "_sz", "tb_seed",
            "miscbuf", "stores", "prov")

    def snapshot(self):
        st = {k: getattr(self, k) for k in self.SNAP}
        st = {k: (list(v) if isinstance(v, list)
                  else dict(v) if isinstance(v, dict) else v)
              for k, v in st.items()}
        st["mem"] = bytes(self.mem)
        return st

    def restore(self, st):
        self.mem = bytearray(st["mem"])
        for k in self.SNAP:
            v = st[k]
            setattr(self, k, list(v) if isinstance(v, list)
                    else dict(v) if isinstance(v, dict) else v)
        #  Diagnostics, not state: a restored machine has no history.
        self.trail = []
        return self

    # ---- memory -------------------------------------------------
    def rd(self, addr, size):
        addr &= MASK
        b = self.mem[addr:addr+size]
        return int.from_bytes(b, "big") if b else 0
    def wr(self, addr, size, val):
        addr &= MASK
        self.mem[addr:addr+size] = (val & ((1 << (8*size)) - 1)).to_bytes(size, "big")

    def alloc(self, n=0x8000):
        if self.heap + n > self.HEAP_LIMIT:
            raise MemoryError(
                "emulator heap exhausted: %d bytes requested at %#x would "
                "pass the limit %#x (stack is at %#x). Allocate less, or "
                "raise HEAP_LIMIT -- do NOT let this wrap, it corrupts "
                "whatever was allocated earlier and looks like a renderer "
                "defect." % (n, self.heap, self.HEAP_LIMIT, self.SP_INIT))
        p = self.heap; self.heap += n; return p

    # ---- effective addresses ------------------------------------
    EA_IDX = re.compile(r'^(-?\$[0-9a-f]+)?\((a\d)(?:, (d\d|a\d)\.(w|l)(?: \* (\d))?)?\)$')

    def _disp(self, t):
        if not t: return 0
        neg = t.startswith('-')
        return (-1 if neg else 1) * int(t.lstrip('-$'), 16)

    def ea_addr(self, t):
        m = self.EA_IDX.match(t)
        if m:
            base = self.a[int(m.group(2)[1])]
            addr = (base + self._disp(m.group(1))) & MASK
            if m.group(3):
                r = m.group(3)
                v = self.d[int(r[1])] if r[0] == 'd' else self.a[int(r[1])]
                v = s16(v & 0xFFFF) if m.group(4) == 'w' else s32(v)
                addr = (addr + v * int(m.group(5) or 1)) & MASK
            return addr
        m = re.match(r'^-\((a\d)\)$', t)          # predecrement
        if m:
            i = int(m.group(1)[1])
            # byte pushes through a7 still move it by two: the stack stays
            # word aligned, which is what the Pascal calling convention
            # counts on when it passes a Boolean.
            step = 2 if (i == 7 and self._sz == 1) else self._sz
            self.a[i] = (self.a[i] - step) & MASK
            return self.a[i]
        m = re.match(r'^\((a\d)\)\+$', t)          # postincrement
        if m:
            i = int(m.group(1)[1]); addr = self.a[i]
            step = 2 if (i == 7 and self._sz == 1) else self._sz
            self.a[i] = (addr + step) & MASK
            return addr
        m = re.match(r'^(-?\$[0-9a-f]+)\.(w|l)$', t)
        if m: return self._disp(m.group(1)) & MASK
        # pc-relative, with the base already resolved by the disassembler.
        # Reaching one of these means the routine is reading a constant out
        # of the code segment -- a switch table, usually -- so the CODE
        # bytes have to be loaded into memory for it to find anything.
        m = re.match(r'^(\$[0-9a-f]+)\(pc(?:, (d\d|a\d)\.(w|l))?\)$', t)
        if m:
            addr = self._disp(m.group(1))
            if m.group(2):
                r = m.group(2)
                v = self.d[int(r[1])] if r[0] == 'd' else self.a[int(r[1])]
                addr += s16(v & 0xFFFF) if m.group(3) == 'w' else s32(v)
            return addr & MASK
        raise ValueError("ea? " + t)

    def a5off(self, t):
        """if t addresses the A5 world, return its signed offset"""
        m = self.EA_IDX.match(t)
        if m and m.group(2) == 'a5' and not m.group(3):
            return self._disp(m.group(1))
        return None

    def read(self, t, size):
        self._sz = size
        if t.startswith('#'):
            v = t[1:]
            return (int(v.lstrip('$'), 16) if '$' in v else int(v)) & MASK, None
        if re.fullmatch(r'd\d', t): return self.d[int(t[1])], self.prov.get(t)
        if re.fullmatch(r'a\d', t): return self.a[int(t[1])], self.prov.get(t)
        off = self.a5off(t)
        addr = self.ea_addr(t)
        val = self.rd(addr, size)
        return val, ("A5%+d" % off if off is not None else None)

    def write(self, t, size, val, prov=None):
        self._sz = size
        if re.fullmatch(r'd\d', t):
            i = int(t[1])
            if size == 4: self.d[i] = val & MASK
            elif size == 2: self.d[i] = (self.d[i] & 0xFFFF0000) | (val & 0xFFFF)
            else: self.d[i] = (self.d[i] & 0xFFFFFF00) | (val & 0xFF)
            self.prov[t] = prov; return
        if re.fullmatch(r'a\d', t):
            self.a[int(t[1])] = val & MASK; self.prov[t] = prov; return
        addr = self.ea_addr(t)
        if self.miscbuf is not None and size == 4 and self.miscbuf <= addr < self.miscbuf + 4800:
            self.stores.append(((addr - self.miscbuf)//4, prov))
        self.wr(addr, size, val)

    def setcc(self, v, size, V=False):
        bits = size*8
        v &= (1 << bits) - 1
        self.Z = (v == 0)
        self.N = bool(v >> (bits-1))
        #  Every 68000 instruction that writes the condition codes also
        #  writes V.  Moves and the logicals clear it; only the add/sub
        #  family and asl can set it.  Leaving it stale is what made
        #  `blt` after `subq` answer the PREVIOUS `cmpi`.
        self.V = bool(V)

    @staticmethod
    def _ovf(a, b, r, size, sub):
        """Signed overflow for r = b -/+ a, all already masked."""
        sign = 1 << (size * 8 - 1)
        a &= (sign << 1) - 1; b &= (sign << 1) - 1; r &= (sign << 1) - 1
        if sub:   # b - a overflows when the operands differ in sign and
                  # the result takes the sign of the subtrahend
            return bool((b ^ a) & (b ^ r) & sign)
        return bool(~(b ^ a) & (b ^ r) & sign)

    # ---- helpers the compiler emits calls to (they live in CODE 1) --
    def _helper(self, tgt):
        """Returns True if tgt was a CODE 1 runtime helper and was executed."""
        d0, d1 = self.d[0], self.d[1]
        s0, s1 = s32(d0), s32(d1)
        if tgt == 0x4B8:                       # __mul32
            self.d[0] = (s0 * s1) & MASK
        elif tgt == 0x4D8:                     # __udiv32
            self.d[0] = (d0 // d1) & MASK if d1 else 0
        elif tgt == 0x524:                     # __sdiv32, truncates toward zero
            self.d[0] = (int(s0 / s1) & MASK) if s1 else 0
        elif tgt == 0x546:                     # __umod32
            self.d[0] = (d0 % d1) & MASK if d1 else 0
        elif tgt == 0x58E:                     # __smod32
            self.d[0] = ((s0 - int(s0 / s1) * s1) & MASK) if s1 else 0
        else:
            return False
        return True

    # ---- toolbox traps ------------------------------------------
    # Pascal convention: the caller reserves room for the result, pushes
    # the arguments, and the trap pops the arguments and leaves the
    # result where the room was reserved.  (bytes popped, result size,
    # result value).  Only the traps the simulation passes actually
    # reach are listed -- anything else is an error rather than a guess,
    # because a wrong stack effect corrupts the frame silently.
    #  Traps that hand their result back in D0 rather than on the stack.
    REG_TRAPS = {'_FreeMem', '_MaxMem'}

    TRAPS = {
        '_GetMHandle':  (2, 4, 0),
        '_CheckItem':   (8, 0, 0),
        '_SetItmMark':  (8, 0, 0),
        '_EnableItem':  (6, 0, 0),
        '_DisableItem': (6, 0, 0),
        # _Random is filled in by the run loop, not by this table: it
        # has to advance the same Lehmer generator rng.c uses, or the
        # oracle and the C draw different dice from the same state.
        '_Random':      (0, 2, None),
        #  filled in by the run loop like _Random: a constant makes
        #  every 'wait until TickCount passes t + n' loop spin for ever,
        #  which is how the earthquake's shake hung the interpreter.
        '_TickCount':   (0, 4, None),
        #  The earthquake's screen shake ($383DC) borrows a scratch
        #  buffer through selector $80005 and returns it with $80006.
        #  Both push two pointers and neither touches the city, so a
        #  no-op that clears the arguments is faithful for our purpose.
        '_QDExtensions': (8, 0, 0),
        #  the rest of the shake: it points QuickDraw at the city
        #  bitmap and blits it back and forth 24 times.
        '_SetPort':      (4, 0, 0),
        '_CopyBits':     (22, 0, 0),
        #  $16B74 calls idlePump between diagonals, which asks for the
        #  front window before animating the palette.  Answering NULL
        #  makes $97B2 skip the animation, which is what a headless
        #  render wants: the phase is set explicitly, not pumped.
        '_FrontWindow':  (0, 4, 0),
        #  A memory query.  $3F636 gates a spawn path on
        #  `_FreeMem() >= $9C40`, and the C models no memory pressure at
        #  all, so answering generously puts the two sides on different
        #  paths -- and drove the emulator into unmapped code on two
        #  cities.  Answer just under the threshold so the gate CLOSES
        #  on both sides.  This is a deliberate harness choice, not a
        #  fact about the game: revisit it when $3F64A is ported.
        '_FreeMem':     (0, 4, 0x9000),
        '_MaxMem':      (0, 4, 0x9000),
        #  A font lookup on the sign-drawing path.  It writes the font
        #  number through its second argument; leaving that alone is fine
        #  here because nothing we compare depends on the font, and
        #  without the stub three tiles per city abort mid-render and
        #  leave holes that look exactly like renderer defects.
        '_GetFNum':     (8, 0, 0),
        #  The rest of $F44's font setup.  Text is not something a pixel
        #  comparison can check anyway -- what matters is that a sign does
        #  not abort the whole map render half way through.
        '_TextFont':    (2, 0, 0),
        '_TextSize':    (2, 0, 0),
        '_TextFace':    (2, 0, 0),
        '_TextMode':    (2, 0, 0),
        #  The rest of what $FABA's sign and label path can reach.  None of
        #  it touches the 8bpp buffer the renderer compares -- signs are
        #  QuickDraw text and lines, and $399D8's rect calls only maintain
        #  a dirty region -- so no-ops let a whole-map render run to the
        #  end instead of aborting on the first sign.  _Debugger is left
        #  alone deliberately: that one means something is wrong.
        '_DrawString':   (4, 0, 0),
        '_StringWidth':  (4, 2, 0),
        '_MoveTo':       (4, 0, 0),
        '_LineTo':       (4, 0, 0),
        '_Line':         (4, 0, 0),
        '_PenNormal':    (0, 0, 0),
        #  $242C, reached by the fire ($38290) and earthquake ($383D4)
        #  arms while they draw.  One word in, nothing out.  Without it
        #  disaster_check could not run either of those two at all.
        '_PenMode':      (2, 0, 0),
        '_PaintRect':    (4, 0, 0),
        '_PenPat':       (4, 0, 0),
        '_PenSize':      (4, 0, 0),
        '_FillRect':     (8, 0, 0),
        '_RGBForeColor': (4, 0, 0),
        '_GetForeColor': (4, 0, 0),
        '_SetRect':      (12, 0, 0),
        '_UnionRect':    (12, 0, 0),
        '_HLock':       (4, 0, 0),
        '_HUnlock':     (4, 0, 0),
    }

    CONDS = ('ra','t','f','eq','ne','lt','ge','le','gt','cs','cc','hi','ls','mi','pl')

    def cond(self, cc):
        return {'ra': True, 't': True, 'f': False,
                'eq': self.Z, 'ne': not self.Z,
                'lt': self.N != self.V, 'ge': self.N == self.V,
                'le': (self.N != self.V) or self.Z,
                'gt': (self.N == self.V) and not self.Z,
                'cs': self.C, 'cc': not self.C,
                'hi': (not self.C) and (not self.Z), 'ls': self.C or self.Z,
                'mi': self.N, 'pl': not self.N}[cc]

    REGLIST = re.compile(r'([da])(\d)(?:-([da])(\d))?')

    def _reglist(self, txt):
        out = []
        for part in txt.split('/'):
            m = self.REGLIST.match(part.strip())
            if not m: continue
            b, i = m.group(1), int(m.group(2))
            if m.group(3):
                for k in range(i, int(m.group(4)) + 1): out.append((b, k))
            else:
                out.append((b, i))
        return out

    # ---- run ----------------------------------------------------
    def run(self, pc, stop, limit=4000000, real_calls=False, stubs=(), watch=None):
        SZ = {'b':1, 'w':2, 'l':4}
        n = 0
        while pc != stop and n < limit:
            n += 1
            self.trail.append(pc)
            if len(self.trail) > 24: self.trail.pop(0)
            if pc not in self.code: return "no code at $%X" % pc
            if watch and pc in watch: watch[pc](self, pc)
            mn, ops = self.code[pc]
            base, _, sz = mn.partition('.')
            size = SZ.get(sz, 4)
            nxt = None
            o = [x.strip() for x in ops.split(',')] if ops else []
            # re-join index modes that contain a comma
            if len(o) > 2 or (len(o) == 2 and o[1].endswith(')') and '(' not in o[1]):
                o = [x.strip() for x in re.split(r',(?![^()]*\))', ops)]

            if base in ('move','movea'):
                v, p = self.read(o[0], size)
                if base == 'movea':
                    v = s16(v & 0xFFFF) & MASK if size == 2 else v
                self.write(o[1], 4 if base=='movea' else size, v, p)
                if base == 'move': self.setcc(v, size)
            elif base == 'moveq':
                v = s8(int(o[0].lstrip('#$'), 16)) & MASK
                self.write(o[1], 4, v, None); self.setcc(v, 4)
            elif base == 'lea':
                off = self.a5off(o[0])
                self.write(o[1], 4, self.ea_addr(o[0]), "A5%+d" % off if off is not None else None)
            elif base == 'clr':
                self.write(o[0], size, 0, None); self.setcc(0, size)
            elif base == 'ext':
                i = int(o[0][1]); v = self.d[i]
                self.d[i] = (s8(v & 0xFF) & 0xFFFF) if size == 2 else (s16(v & 0xFFFF) & MASK)
                self.setcc(self.d[i], size)
            elif base in ('addq','subq','add','sub','adda','suba','addi','subi'):
                #  The SOURCE is read at the instruction's own size.
                #  Reading it as a long for adda/suba and then keeping
                #  the low word picks the WRONG half on a big-endian
                #  memory operand: `adda.w $a(a6), a2` would fetch the
                #  long at a6+10 and take a6+12.  That silently added a
                #  neighbouring argument instead of the intended one.
                a, _ = self.read(o[0], size)
                b, p = self.read(o[1], 4 if base in ('adda','suba') else size)
                if base in ('adda','suba') and size == 2: a = s16(a & 0xFFFF) & MASK
                r = (b + a) if base in ('addq','add','adda','addi') else (b - a)
                self.write(o[1], 4 if base in ('adda','suba') else size, r & MASK, p)
                #  Arithmetic on an ADDRESS register leaves the condition
                #  codes alone -- a documented 68000 quirk, and the
                #  compiler relies on it: $24716 puts `addq.w #2, a7`
                #  between a `cmpi.w` and the `bne` that reads its result.
                #  Setting the flags here silently inverts that branch.
                dest_is_areg = re.fullmatch(r'a\d', o[1]) is not None
                if base not in ('adda','suba') and not dest_is_areg:
                    self.setcc(r, size,
                               self._ovf(a, b, r, size,
                                         base in ('subq','sub','subi')))
            elif base in ('lsl','lsr','asl','asr','rol','ror'):
                # These operate on the low byte/word/long only; the rest of
                # the register is untouched.  Shifting the full 32 bits for a
                # .w shift drags stale high-word bits into the result, which
                # silently corrupts the sign-extension idiom the compiler
                # emits for signed division.
                cnt, _ = self.read(o[0], 4)
                i = int(o[1][1])
                bits = size * 8
                mask = (1 << bits) - 1
                v = self.d[i] & mask
                if base in ('lsl','asl'):
                    r = (v << cnt) & mask
                elif base == 'lsr':
                    r = v >> cnt
                elif base == 'ror':
                    cnt %= bits; r = ((v >> cnt) | (v << (bits - cnt))) & mask
                elif base == 'rol':
                    cnt %= bits; r = ((v << cnt) | (v >> (bits - cnt))) & mask
                else:                                     # asr
                    sv = v - (1 << bits) if v & (1 << (bits - 1)) else v
                    r = (sv >> cnt) & mask
                self.d[i] = (self.d[i] & ~mask & MASK) | r
                self.setcc(r, size)
                #  Carry is the last bit shifted out, and the LFSR at
                #  $20F30 branches on it: `lsl.w #1` then `bcc`.  A shift
                #  that sets Z and N but leaves C stale makes that
                #  generator take the wrong arm and diverge immediately.
                if cnt:
                    if cnt > bits:
                        self.C = False
                    elif base in ('lsl', 'asl', 'rol'):
                        self.C = bool((v >> (bits - cnt)) & 1)
                    else:                                  # lsr, asr, ror
                        self.C = bool((v >> (cnt - 1)) & 1)
            elif base in ('muls','mulu'):
                a, _ = self.read(o[0], 2); i = int(o[1][1])
                x = s16(a & 0xFFFF) if base == 'muls' else (a & 0xFFFF)
                y = s16(self.d[i] & 0xFFFF) if base == 'muls' else (self.d[i] & 0xFFFF)
                self.d[i] = (x*y) & MASK; self.setcc(self.d[i], 4)
            elif base == 'neg':
                v, _ = self.read(o[0], size)
                bits = size * 8
                self.write(o[0], size, (-v) & ((1 << bits) - 1), None)
                self.setcc((-v), size)
            elif base in ('and','andi','or','ori','eor','eori'):
                a, _ = self.read(o[0], size)
                b, p = self.read(o[1], size)
                r = (b & a) if base in ('and','andi') else \
                    (b | a) if base in ('or','ori') else (b ^ a)
                self.write(o[1], size, r, p); self.setcc(r, size)
            elif base in ('divs','divu'):
                a, _ = self.read(o[0], 2); i = int(o[1][1])
                dv = s16(a & 0xFFFF) or 1
                q = int(s32(self.d[i]) / dv); r = s32(self.d[i]) - q*dv
                self.d[i] = ((r & 0xFFFF) << 16) | (q & 0xFFFF)
            elif base in ('cmpi','cmp','cmpa'):
                w = 4 if base == 'cmpa' else size
                a, _ = self.read(o[0], w)
                b, _ = self.read(o[1], w)
                if base == 'cmpa' and size == 2: a = s16(a & 0xFFFF) & MASK
                #  Mask to the operand size before comparing.  A data
                #  register read hands back all 32 bits whatever the size
                #  is, and a `move.w` leaves the top half of the register
                #  alone, so a `cmp.w` against a register whose high word
                #  is stale would otherwise set carry from bits the
                #  instruction never looks at -- which flips `bhi` and
                #  sends a switch statement down its default arm.
                m = (1 << (8 * w)) - 1
                a &= m; b &= m
                sa = s32(a) if w == 4 else (s16(a) if w == 2 else s8(a))
                sb = s32(b) if w == 4 else (s16(b) if w == 2 else s8(b))
                diff = (b - a) & m
                self.Z = (sa == sb)
                self.N = bool(diff >> (8 * w - 1))
                self.C = b < a
                self.V = self._ovf(a, b, diff, w, True)
            elif base == 'tst':
                v, _ = self.read(o[0], size); self.setcc(v, size)
            elif base in ('btst','bset','bclr','bchg'):
                #  The bit family.  Two sizes, chosen by the DESTINATION,
                #  not by the mnemonic suffix: a data register is a long
                #  and the bit number is taken mod 32; anything in memory
                #  is a byte and the bit number is mod 8.  Z is set from
                #  the bit BEFORE the operation, and nothing else in the
                #  condition codes moves.
                #
                #  None of these were implemented, which aborted every
                #  run that reached the software _Random at $20F22
                #  (`bclr.b #$f, d0`, clearing the sign bit) or the water
                #  test inside stampFootprint at $36CC.
                bn, _ = self.read(o[0], 4)
                dst_is_dreg = re.fullmatch(r'd\d', o[1]) is not None
                w = 4 if dst_is_dreg else 1
                bn %= (32 if dst_is_dreg else 8)
                v, prov = self.read(o[1], w)
                self.Z = not (v >> bn) & 1
                if base != 'btst':
                    if base == 'bset':   v |= (1 << bn)
                    elif base == 'bclr': v &= ~(1 << bn)
                    else:                v ^= (1 << bn)
                    self.write(o[1], w, v & MASK, prov)
            elif base.startswith('b') and base[1:] in self.CONDS:
                if self.cond(base[1:]): nxt = int(o[0].lstrip('$'), 16)
            elif base.startswith('s') and base[1:] in self.CONDS and len(base) > 1:
                # set-on-condition: all ones or all zeros in the low byte
                self.write(o[0], 1, 0xFF if self.cond(base[1:]) else 0x00, None)
            elif base in ('dbra','dbf'):
                # decrement the low word and branch unless it went past zero
                i = int(o[0][1])
                v = (self.d[i] & 0xFFFF) - 1
                self.d[i] = (self.d[i] & 0xFFFF0000) | (v & 0xFFFF)
                if (v & 0xFFFF) != 0xFFFF: nxt = int(o[1].lstrip('$'), 16)
            elif base == 'link':
                i = int(o[0][1]); self.a[7] -= 4; self.wr(self.a[7], 4, self.a[i])
                self.a[i] = self.a[7]; self.a[7] = (self.a[7] + s16(int(o[1].lstrip('#$'), 16))) & MASK
            elif base == 'unlk':
                i = int(o[0][1]); self.a[7] = self.a[i]; self.a[i] = self.rd(self.a[7], 4); self.a[7] += 4
            elif base == 'movem':
                regs = self._reglist(o[0] if '-(a7)' in ops or '/' in o[0] or re.match(r'^[da]\d', o[0]) else o[1])
                if '-(a7)' in ops:                       # save
                    for b, i in reversed(regs):
                        self.a[7] = (self.a[7] - 4) & MASK
                        self.wr(self.a[7], 4, self.d[i] if b == 'd' else self.a[i])
                else:                                     # restore
                    for b, i in regs:
                        v = self.rd(self.a[7], 4); self.a[7] = (self.a[7] + 4) & MASK
                        if b == 'd': self.d[i] = v
                        else: self.a[i] = v
            elif base == 'pea':
                v = self.ea_addr(o[0])
                self.a[7] = (self.a[7] - 4) & MASK
                self.wr(self.a[7], 4, v)
            elif base in ('jsr','bsr'):
                m = re.fullmatch(r'\$([0-9a-f]+)\.l', o[0]) or \
                    re.fullmatch(r'\$([0-9a-f]+)\(pc\)', o[0]) or \
                    re.fullmatch(r'\$([0-9a-f]+)', o[0])
                tgt = int(m.group(1), 16) if m else None
                if tgt is None or self._helper(tgt) or tgt in stubs or not real_calls:
                    #  A stub is skipped, so D0 keeps whatever the code
                    #  before the call left in it -- which is an accident,
                    #  not an answer.  Where the caller TESTS the result,
                    #  pass `stubs` as a dict and give the address the
                    #  value the real routine would have returned.
                    if isinstance(stubs, dict):
                        v = stubs.get(tgt)
                        if v is not None:
                            self.d[0] = v & MASK
                else:
                    ret = self.next_pc.get(pc, -1)
                    self.a[7] = (self.a[7] - 4) & MASK
                    self.wr(self.a[7], 4, ret)
                    nxt = tgt
            elif base == 'rts':
                #  Below the initial SP means we are inside a nested call,
                #  so pop and carry on; at or above it we have returned to
                #  the top level and the run is over.  This used to test a
                #  hard-coded 0x300000: moving the stack made it always
                #  false, so every rts ended the run and any routine that
                #  called another returned early with plausible garbage.
                if real_calls and self.a[7] < self.SP_INIT:
                    nxt = self.rd(self.a[7], 4)
                    self.a[7] = (self.a[7] + 4) & MASK
                else:
                    return None
            elif base == '_FP68K':
                #  SANE.  The caller pushes dst, then src, then the
                #  opword; the trap pops all ten bytes.  A compare
                #  reports through the condition codes, which is how the
                #  economy branches on its own arithmetic.
                import sane as _sane
                sp = self.a[7]
                opw = self.rd(sp, 2)
                #  FOTTI truncates in place and takes one operand; every
                #  other opword takes a destination and a source.
                #  Pascal order: the source is pushed first and the
                #  destination second, so the DESTINATION is nearest the
                #  stack pointer.  $34D80 reads `pea 1200.0 ; pea acc ;
                #  FODIV`, which is acc /= 1200, and gets that backwards
                #  if the two are swapped.
                if (opw & 0x1F) == 0x16:
                    dst = self.rd(sp + 2, 4); src = dst; popped = 6
                else:
                    dst = self.rd(sp + 2, 4)
                    src = self.rd(sp + 6, 4)
                    popped = 10
                r = _sane.fp68k(self.mem, opw, dst, src)
                self.a[7] = (sp + popped) & MASK
                if (opw & 0x1F) == 0x08:
                    self.Z = (r == 0); self.N = (r < 0)
                    self.V = False
                    self.C = (r < 0)
            elif base == '_SetPt':
                #  SetPt(pt, h, v).  A Mac Point is {short v; short h},
                #  and the Pascal push order puts v nearest the stack
                #  pointer.  $24610 reads the two halves straight back
                #  out of the frame, so the order matters.
                v = self.rd(self.a[7], 2)
                h = self.rd(self.a[7] + 2, 2)
                pt = self.rd(self.a[7] + 4, 4)
                self.wr(pt, 2, v); self.wr(pt + 2, 2, h)
                self.a[7] = (self.a[7] + 8) & MASK
            elif base == '_BlockMove':
                src, dst, cnt = self.a[0], self.a[1], self.d[0] & MASK
                self.mem[dst:dst+cnt] = self.mem[src:src+cnt]
            elif base in self.TRAPS:
                pop, res, val = self.TRAPS[base]
                if val is None and base == '_TickCount':
                    self.ticks = getattr(self, 'ticks', 0) + 64
                    val = self.ticks
                elif val is None:                     # _Random
                    self.tb_seed = (self.tb_seed * 16807) % 2147483647
                    val = self.tb_seed & 0xFFFF
                    if self.rng_log is not None:
                        #  the PC makes a dice divergence locatable
                        self.rng_log.append(('t', val, pc))
                self.a[7] = (self.a[7] + pop) & MASK
                if base in self.REG_TRAPS:
                    #  A register-based trap returns in D0 and reserves
                    #  no stack.  Writing the result at a7 the way the
                    #  Pascal traps do would clobber the RETURN ADDRESS,
                    #  which is how _FreeMem sent the interpreter to
                    #  $D0C13A30.
                    self.d[0] = val & MASK
                elif res:
                    self.wr(self.a[7], res, val)
            elif base.startswith('_'):
                return "unhandled trap %s at $%X" % (base, pc)
            elif base == 'swap':
                i = int(o[0][1])
                v = self.d[i]
                self.d[i] = ((v << 16) | (v >> 16)) & MASK
                self.setcc(self.d[i], 4)
            elif base == 'jmp':
                nxt = self.ea_addr(o[0])
            elif base in ('nop','dc'):
                pass
            else:
                return "unhandled %s %s at $%X" % (mn, ops, pc)

            pc = self.next_pc.get(pc, -1) if nxt is None else nxt
        return None if pc == stop else "step limit"
