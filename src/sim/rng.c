/*  rng.c: the two random number generators SimCity 2000 uses.  The game
 *  calls both: the Toolbox trap _Random ($A861) at 329 sites, and its
 *  own generator at $20F30 at 34 sites.  Reproducing the game's behavior
 *  needs both, and they are not equally trustworthy: see the comments.
 *
 *  THE STATE IS THE CITY'S.  Every draw is made from the city it is
 *  made for.  Two cities ticked in one program keep two streams.  A
 *  file-scope seed would tie them together: the same city, ticked
 *  second, would grow differently. */
#include <stdlib.h>

#include "sim.h"

/*  An optional log of every draw, in order, so the C's dice can be
 *  compared with the original's under the interpreter.  A reconstruction
 *  that draws the same numbers in the same order is following the same
 *  branches.  One that does not has diverged, and the first differing
 *  entry says where.
 *
 *  It is two megabytes, so it is allocated only for a city that asks. */
#define RNG_LOG_MAX 400000
struct RngLog
{
    char    kind[RNG_LOG_MAX];
    int32_t val[RNG_LOG_MAX];
    int     n;
};

void rng_log_start(City *c)
{
    if (!c)
        return;
    if (!c->rng_log)
        c->rng_log = (RngLog *)malloc(sizeof *c->rng_log);
    if (c->rng_log)
        c->rng_log->n = 0;
}

void rng_log_free(City *c)
{
    if (!c)
        return;
    free(c->rng_log);
    c->rng_log = NULL;
}

int rng_log_count(const City *c)
{
    return c && c->rng_log ? c->rng_log->n : 0;
}

int rng_log_entry(const City *c, int i, int32_t *v)
{
    if (!c || !c->rng_log || i < 0 || i >= c->rng_log->n)
        return 0;
    *v = c->rng_log->val[i];
    return c->rng_log->kind[i];
}

static void logdraw(City *c, char k, int32_t v)
{
    RngLog *g = c ? c->rng_log : NULL;
    if (g && g->n < RNG_LOG_MAX)
    {
        g->kind[g->n] = k;
        g->val[g->n]  = v;
        g->n++;
    }
}

void rng_log_mark(City *c, int32_t v) { logdraw(c, 'S', v); }

/*  $20EE6: a THIRD generator, and the one that caught me out: THINK C's
 *  library rand().  State is a plain 32-bit LCG at A5-0x3EDC with the
 *  ANSI constants.  The result is the HIGH word masked to 15 bits, taken
 *  modulo the argument by a divu.  It is NOT the LFSR at $20F30 and NOT
 *  the Toolbox _Random.  Using game_rand() for it in pickDirection
 *  ($E19A) drew from the wrong stream and put the whole cycle out of
 *  step from draw 1411 on. */
uint16_t lib_rand(City *c, uint16_t n)
{
    c->lcg = c->lcg * 0x41C64E6Du + 0x3039u; /* $20F12 */
    {
        uint16_t v = (uint16_t)((c->lcg >> 16) & 0x7FFF); /* $20F1E, $20F22 */
        v          = n ? (uint16_t)(v % n) : 0;           /* $20F26 divu */
        logdraw(c, 'L', v);
        return v;
    }
}

/*  The seeds a city starts from.  Nought is refused, because a shift
 *  register at nought never leaves it and a Lehmer seed of nought draws
 *  nought forever.  The A5 image holds one for each. */
void rng_seed(City *c, int32_t toolbox_seed, uint16_t lfsr_seed)
{
    if (!c)
        return;
    c->tb_seed = toolbox_seed ? toolbox_seed : 1;
    c->lfsr    = lfsr_seed ? lfsr_seed : 1;
    c->lcg     = 1;
}

/*  Toolbox _Random.  NOT verified from this binary: the trap lives in
 *  ROM, not in the application, so there is nothing here to read.  This
 *  is the documented Toolbox generator.  It is the minimal-standard
 *  Lehmer PRNG, seed = seed * 16807 mod (2^31 - 1), returning the low 16
 *  bits as a signed word.  Any claim about reproducing an exact sequence
 *  of the game's dice rolls inherits this assumption. */
int16_t Random(City *c)
{
    int64_t s = (int64_t)c->tb_seed * 16807 % 2147483647;
    if (s < 0)
        s += 2147483647;
    c->tb_seed = (int32_t)s;
    logdraw(c, 't', (int32_t)(s & 0xFFFF));
    return (int16_t)(uint16_t)(s & 0xFFFF);
}

/*  The game's own generator, $20F30: verified, and short enough to quote
 *  in full:
 *
 *      moveq   #0,d0
 *      move.w  seed(a5),d0
 *      lsl.w   #1,d0          .  Carry = old bit 15
 *      bcc.b   +4
 *      eori.w  #$1BF5,d0
 *      move.w  d0,seed(a5)
 *      divu.w  4(a7),d0       .  Remainder ends up in the high word
 *      clr.w   d0
 *      swap    d0
 *      rts
 *
 *  A 16-bit LFSR with tap polynomial 0x1BF5, then a plain modulo.  The
 *  shift is lsl.w, so only the low word participates and the value fed
 *  to divu is always the fresh 16-bit seed.
 *
 *  So this is ONE STEP of the shift register.  Every variant below
 *  performs exactly this and then reduces the result differently.  They
 *  all draw from the same sequence, and the order of the calls
 *  matters. */
static uint16_t lfsr_step(City *c)
{
    uint16_t carry = (uint16_t)(c->lfsr & 0x8000);
    c->lfsr        = (uint16_t)(c->lfsr << 1);
    if (carry)
        c->lfsr ^= 0x1BF5;
    return c->lfsr;
}

uint16_t game_rand(City *c, uint16_t n) /* rngMod $20F30, divu.w */
{
    uint16_t v = lfsr_step(c);
    v          = n ? (uint16_t)(v % n) : 0;
    logdraw(c, '0', v);
    return v;
}

/*  The masked variants.  $20F4C through $20FAC are the same routine with
 *  a different andi.w.  The game calls them where it wants a
 *  power-of-two range without paying for a divide. */
uint16_t game_rand1(City *c)
{
    uint16_t v = (uint16_t)(lfsr_step(c) & 0x01);
    logdraw(c, '1', v);
    return v;
} /* $20F4C */
uint16_t game_rand3(City *c)
{
    uint16_t v = (uint16_t)(lfsr_step(c) & 0x03);
    logdraw(c, '3', v);
    return v;
} /* rngAnd3 $20F64 */
uint16_t game_rand15(City *c)
{
    uint16_t v = (uint16_t)(lfsr_step(c) & 0x0F);
    logdraw(c, 'f', v);
    return v;
} /* $20F7C */
uint16_t game_rand63(City *c)
{
    uint16_t v = (uint16_t)(lfsr_step(c) & 0x3F);
    logdraw(c, '6', v);
    return v;
} /* $20F94 */
uint16_t game_rand127(City *c)
{
    uint16_t v = (uint16_t)(lfsr_step(c) & 0x7F);
    logdraw(c, '7', v);
    return v;
} /* rngAnd127 $20FAC */

uint16_t rng_lfsr(const City *c) { return c ? c->lfsr : 0; }
int32_t  rng_toolbox_seed(const City *c) { return c ? c->tb_seed : 0; }
