/*  rng.c -- the two random number generators SimCity 2000 uses.
 *
 *  The game calls both: the Toolbox trap _Random ($A861) at 329 sites,
 *  and its own generator at $20F30 at 34 sites.  Reproducing the game's
 *  behaviour needs both, and they are not equally trustworthy -- see the
 *  comments.
 */
#include "sc2k.h"

/*  An optional log of every draw, in order, so the C's dice can be
 *  compared with the original's under the interpreter.  A reconstruction
 *  that draws the same numbers in the same order is following the same
 *  branches; one that does not has diverged, and the first differing
 *  entry says where. */
#define RNG_LOG_MAX 400000
static char    rng_kind[RNG_LOG_MAX];
static int32_t rng_val[RNG_LOG_MAX];
static int     rng_n = -1;

void rng_log_start(void) { rng_n = 0; }
int  rng_log_count(void) { return rng_n < 0 ? 0 : rng_n; }
int  rng_log_entry(int i, int32_t *v)
{
    *v = rng_val[i];
    return rng_kind[i];
}
static void logdraw(char k, int32_t v)
{
    if (rng_n >= 0 && rng_n < RNG_LOG_MAX)
    {
        rng_kind[rng_n] = k;
        rng_val[rng_n]  = v;
        rng_n++;
    }
}

void rng_log_mark(int32_t v) { logdraw('S', v); }

static int32_t  tb_seed = 1;
static uint16_t lfsr    = 1;

/*  $20EE6 -- a THIRD generator, and the one that caught me out: THINK
 *  C's library rand().  State is a plain 32-bit LCG at A5-0x3EDC with
 *  the ANSI constants, and the result is the HIGH word masked to 15
 *  bits, taken modulo the argument by a divu.
 *
 *  It is NOT the LFSR at $20F30 and NOT the Toolbox _Random.  Using
 *  game_rand() for it in pickDirection ($E19A) drew from the wrong
 *  stream and put the whole cycle out of step from draw 1411 on. */
static uint32_t lcg = 1; /* A5-0x3EDC, 1 in the A5 image */

uint16_t lib_rand(uint16_t n)
{
    lcg = lcg * 0x41C64E6Du + 0x3039u; /* $20F12 */
    {
        uint16_t v = (uint16_t)((lcg >> 16) & 0x7FFF); /* $20F1E, $20F22 */
        v          = n ? (uint16_t)(v % n) : 0;        /* $20F26 divu */
        logdraw('L', v);
        return v;
    }
}

void rng_seed(int32_t toolbox_seed, uint16_t lfsr_seed)
{
    tb_seed = toolbox_seed ? toolbox_seed : 1;
    lfsr    = lfsr_seed ? lfsr_seed : 1;
    lcg     = 1;
}

/*  Toolbox _Random.
 *
 *  NOT verified from this binary -- the trap lives in ROM, not in the
 *  application, so there is nothing here to read.  This is the
 *  documented Toolbox generator: the minimal-standard Lehmer PRNG
 *  seed = seed * 16807 mod (2^31 - 1), returning the low 16 bits as a
 *  signed word.  Any claim about reproducing an exact sequence of the
 *  game's dice rolls inherits this assumption.
 */
int16_t Random(void)
{
    int64_t s = (int64_t)tb_seed * 16807 % 2147483647;
    if (s < 0)
        s += 2147483647;
    tb_seed = (int32_t)s;
    logdraw('t', (int32_t)(s & 0xFFFF));
    return (int16_t)(uint16_t)(s & 0xFFFF);
}

/*  The game's own generator, $20F30 -- verified, and short enough to
 *  quote in full:
 *
 *      moveq   #0,d0
 *      move.w  seed(a5),d0
 *      lsl.w   #1,d0          ; carry = old bit 15
 *      bcc.b   +4
 *      eori.w  #$1BF5,d0
 *      move.w  d0,seed(a5)
 *      divu.w  4(a7),d0       ; remainder ends up in the high word
 *      clr.w   d0
 *      swap    d0
 *      rts
 *
 *  A 16-bit LFSR with tap polynomial 0x1BF5, then a plain modulo.  The
 *  shift is lsl.w, so only the low word participates and the value fed
 *  to divu is always the fresh 16-bit seed.
 */
/*  One step of the shift register.  Every variant below performs
 *  exactly this and then reduces the result differently, so they all
 *  draw from the same sequence and the order of the calls matters. */
static uint16_t lfsr_step(void)
{
    uint16_t carry = (uint16_t)(lfsr & 0x8000);
    lfsr           = (uint16_t)(lfsr << 1);
    if (carry)
        lfsr ^= 0x1BF5;
    return lfsr;
}

uint16_t game_rand(uint16_t n) /* rngMod $20F30, divu.w */
{
    uint16_t v = lfsr_step();
    v          = n ? (uint16_t)(v % n) : 0;
    logdraw('0', v);
    return v;
}

/*  The masked variants.  $20F4C through $20FAC are the same routine
 *  with a different andi.w, and the game calls them where it wants a
 *  power-of-two range without paying for a divide. */
uint16_t game_rand1(void)
{
    uint16_t v = (uint16_t)(lfsr_step() & 0x01);
    logdraw('1', v);
    return v;
} /* $20F4C */
uint16_t game_rand3(void)
{
    uint16_t v = (uint16_t)(lfsr_step() & 0x03);
    logdraw('3', v);
    return v;
} /* rngAnd3 $20F64 */
uint16_t game_rand15(void)
{
    uint16_t v = (uint16_t)(lfsr_step() & 0x0F);
    logdraw('f', v);
    return v;
} /* $20F7C */
uint16_t game_rand63(void)
{
    uint16_t v = (uint16_t)(lfsr_step() & 0x3F);
    logdraw('6', v);
    return v;
} /* $20F94 */
uint16_t game_rand127(void)
{
    uint16_t v = (uint16_t)(lfsr_step() & 0x7F);
    logdraw('7', v);
    return v;
} /* rngAnd127 $20FAC */

uint16_t rng_lfsr(void) { return lfsr; }
int32_t  rng_toolbox_seed(void) { return tb_seed; }
