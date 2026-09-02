/*  economy.c -- economyPass ($34D04), phase 21.
 *
 *  The only part of the simulation that uses floating point, and it does
 *  so entirely through the SANE trap: every arithmetic step is a call to
 *  _FP68K with an opword saying what to do and in which format.  The
 *  formats matter -- values are repeatedly rounded down to single
 *  precision between steps -- so this mirrors the original's operations
 *  through ext80 rather than computing the same formula in double and
 *  hoping the rounding agrees.
 *
 *  The operation sequence was not read off the listing.  It was recorded
 *  by watching all 186 _FP68K sites under the interpreter
 *  (tools/trace_economy.py), which is what the comments cite.
 */
#include "ext80.h"
#include "sc2k.h"
#include <stdio.h>
#include <stdlib.h>

/*  Shorthands for the conversions the trace shows, named after what the
 *  opword actually says. */
static ext80 z2x_i16(int16_t v) { return ext_from_i16(v); }
static ext80 z2x_i32(int32_t v) { return ext_from_i32(v); }
static float x2z_sgl(ext80 a) { return (float)ext_to_double(a); }
static ext80 z2x_sgl(float f) { return ext_from_float(f); }

/*  $36332 -- the shape all four float blocks in the emigration loop
 *  share: truncate cc * b / a, with every operand rounded to single
 *  precision on the way in and the quotient rounded to single again
 *  before it is truncated.  Doing this in plain integers gives a
 *  different answer on about one bracket in twenty. */
static int32_t emig_share(float a, float b, float cc)
{
    ext80 q = ext_div(ext_mul(z2x_sgl(cc), z2x_sgl(b)), z2x_sgl(a));
    return ext_to_i32(z2x_sgl(x2z_sgl(q)));
}

/*  The original builds its float operands out of a 64-bit comp whose
 *  high half it clears and whose low half it fills with a long.  A
 *  negative long therefore arrives as a large POSITIVE number -- and
 *  the two weighted series are allowed to go negative.  Transcribed as
 *  written, not as intended. */
static float comp_sgl(int32_t v) { return (float)(uint32_t)v; }

void sim_economy(City *c)
{
    /*  $34D0C -- the one constant the routine embeds, 1200.0: twelve
     *  months times a hundred, because the national growth rate is a
     *  percentage per year. */
    const ext80 twelve_hundred = ext_from_i32(1200);

    /*  $34D1C..$34D9A -- how much the national indicator moves this
     *  month.  Every step is taken back to single precision, which is
     *  why this is not simply nat * rate / 1200. */
    float rate  = x2z_sgl(z2x_i16((int16_t)c->misc[MISC_NAT_CYCLE])); /* $1EB0 */
    float nat   = x2z_sgl(z2x_i32(c->misc[MISC_NAT_INDEX]));          /* $1EB2 */
    ext80 step  = z2x_sgl(nat);
    step        = ext_mul(step, z2x_sgl(rate));
    step        = ext_div(step, twelve_hundred);
    float delta = x2z_sgl(step);

    /*  $34DA2 -- past five million the indicator falls instead of
     *  rising, which is what stops a runaway. */
    {
        ext80 v = z2x_i32(c->misc[MISC_NAT_INDEX]);
        if (c->misc[MISC_NAT_INDEX] > 0x4C4B40)
            v = ext_sub(v, z2x_sgl(delta)); /* $34DC2 */
        else
            v = ext_add(v, z2x_sgl(delta));      /* $34E02 */
        c->misc[MISC_NAT_INDEX] = ext_to_i32(v); /* $34E0C truncates */
    }

    /*  $34E22..$34F3A -- the same shape again for the second indicator,
     *  except that its rate is looked up in a table by the national
     *  cycle rather than being the cycle itself, and it turns over at
     *  three and a half million instead of five. */
    {
        int   cyc   = (int16_t)c->misc[MISC_NAT_CYCLE];
        float rate2 = x2z_sgl(z2x_i16(NAT_RATE_TABLE[cyc & 0x0F])); /* $34E3C */
        float nat2  = x2z_sgl(z2x_i32(c->misc[MISC_NAT_INDEX2]));
        ext80 step2 = z2x_sgl(nat2);
        float delta2;
        ext80 v;

        step2  = ext_mul(step2, z2x_sgl(rate2));
        step2  = ext_div(step2, twelve_hundred);
        delta2 = x2z_sgl(step2);

        v = z2x_i32(c->misc[MISC_NAT_INDEX2]);
        if (c->misc[MISC_NAT_INDEX2] > 0x3567E0)
            v = ext_sub(v, z2x_sgl(delta2)); /* $34EE2 */
        else
            v = ext_add(v, z2x_sgl(delta2)); /* $34F22 */
        c->misc[MISC_NAT_INDEX2] = ext_to_i32(v);
    }

    /*  $34F42..$350EA -- one month in ten the nation takes stock.  The
     *  number everything turns on is the second indicator as a
     *  percentage of the first; below 45 the nation is in a slump and
     *  above 75 it is booming, with two bands in between. */
    if ((uint16_t)Random() % 10 == 0) /* $34F4C */
    {
        float   top = x2z_sgl(z2x_i32(c->misc[MISC_NAT_INDEX2]));
        float   bot = x2z_sgl(z2x_i32(c->misc[MISC_NAT_INDEX] + 1));
        ext80   pct = ext_from_i32(100); /* $34FA8, the embedded 100.0 */
        int32_t health;
        int     lim;

        pct    = ext_mul(pct, z2x_sgl(top)); /* $34FC0 */
        pct    = ext_div(pct, z2x_sgl(bot)); /* $34FCE */
        pct    = z2x_sgl(x2z_sgl(pct));      /* $34FDC, $34FEA */
        health = ext_to_i32(pct);            /* $34FF4 truncates */

        if ((uint16_t)Random() % 5 < 2) /* $3501C */
        {
            /*  Two rolls against a window that widens as the mood
             *  counter grows, so a settled nation changes its mind less
             *  often than a jumpy one. */
            lim = 25 * (int16_t)c->misc[MISC_NAT_MOOD]; /* $35022 */
            if (lim > 0 && (int32_t)((uint16_t)Random() % (uint16_t)lim) < health)
                c->misc[MISC_NAT_MOOD]++;               /* $35044 */
            lim = 25 * (int16_t)c->misc[MISC_NAT_MOOD]; /* $35058 */
            if (lim > 0 && (int32_t)((uint16_t)Random() % (uint16_t)lim) > health)
            {
                c->misc[MISC_NAT_MOOD]--; /* $3507A */
                if (c->misc[MISC_NAT_MOOD] == 0)
                    c->misc[MISC_NAT_MOOD] = 1;
            }
        }
        if ((uint16_t)Random() % 3 == 0) /* $350AC */
        {
            int cyc = health < 0x2D ? 0 : health < 0x3C ? 1
                                      : health < 0x4B   ? 2
                                                        : 3;
            if (cyc != (int16_t)c->misc[MISC_NAT_CYCLE])
                c->misc[MISC_NAT_CYCLE] = cyc; /* $350D8 */
        }
    }

    /*  NOT VERIFIABLE ON ITS OWN, for a structural reason worth stating:
     *  economyPass draws random numbers throughout, and the blocks
     *  between here and the two above ($34F44, $35062, $3509C, $350A0)
     *  are not reconstructed yet.  By the time control reaches this
     *  loop the C is a few draws out of phase with the original, so the
     *  jittered rate differs and the four indicators land a little
     *  apart.  The transcription below is faithful; it simply cannot be
     *  checked until the routine is complete in order.  That is why the
     *  national indicators above, which run before any draw, are exact
     *  on all eighteen cities and these are not.
     *
     *  $350F0..$35460 -- four more indicators in the block at A5+0x1ECE,
     *  each grown the same way but at a rate jittered by the dice:
     *  (Random() % 3) added to the national cycle.  An indicator whose
     *  step rounds to nothing still creeps up half the time, which is
     *  what stops a small one from being stuck at its value for ever. */
    {
        int i;
        for (i = 0; i < 4; i++)
        {
            int32_t *slot = &c->misc[MISC_IND4_BASE + 4 * i];
            int32_t  v    = *slot;
            int32_t  jrate, jstep;
            float    jv, jr;
            ext80    jacc;

            if (v == 0)
                continue; /* $350F8 */

            jv    = x2z_sgl(z2x_i32(v));                                                  /* $3512C, via comp64 */
            jrate = (int32_t)((uint16_t)Random() % 3) + (int16_t)c->misc[MISC_NAT_CYCLE]; /* $35146 */
            jr    = x2z_sgl(z2x_i32(jrate));                                              /* $35164 */
            jacc  = z2x_sgl(jr);                                                          /* $35180 */
            jacc  = ext_mul(jacc, z2x_sgl(jv));                                           /* $3518E */
            jacc  = ext_div(jacc, twelve_hundred);                                        /* $3519C */
            jacc  = z2x_sgl(x2z_sgl(jacc));                                               /* $351AA then $351B8 */
            jstep = ext_to_i32(jacc);                                                     /* $351C2 truncates */

            if (v > 0x4C4B40)
                *slot -= jstep; /* $351EA */
            else
                *slot += jstep; /* $351FA */

            if (jstep == 0)
                *slot += (Random() & 1); /* $35200 */

            /*  $3521C -- then the partner series at A5+0x1ECA moves too,
             *  at a rate set by how the two stand against each other.
             *  The same 45/60/75 bands as the national cycle, so a
             *  series that has fallen behind its partner is pushed
             *  harder. */
            {
                int32_t *mate = &c->misc[MISC_IND4_B + 4 * i];
                int32_t  m    = *mate;
                int32_t  band, rate2;
                float    fstep2;
                ext80    acc2;

                acc2 = ext_from_i32(100);                               /* $35278 */
                acc2 = ext_mul(acc2, z2x_sgl(x2z_sgl(z2x_i32(m))));     /* $35290 */
                acc2 = ext_div(acc2, z2x_sgl(x2z_sgl(z2x_i32(*slot)))); /* $3529E */
                acc2 = z2x_sgl(x2z_sgl(acc2));                          /* $352AC */
                band = ext_to_i32(acc2);                                /* $352C4 */
                band = band < 0x2D ? 0 : band < 0x3C ? 1
                                     : band < 0x4B   ? 2
                                                     : 3;

                /*  $352FC -- the nation's rate for this cycle, less how
                 *  excitable it is, less the band, plus a die. */
                rate2 = (int32_t)((uint16_t)Random() % 5) + (NAT_RATE_TABLE[(int16_t)c->misc[MISC_NAT_CYCLE] & 0x0F] - (int16_t)c->misc[MISC_NAT_MOOD]) - band;

                acc2 = z2x_sgl(x2z_sgl(z2x_i32(rate2)));            /* $3533C */
                acc2 = ext_mul(acc2, z2x_sgl(x2z_sgl(z2x_i32(m)))); /* $353AC */
                acc2 = ext_div(acc2, twelve_hundred);               /* $353BA */
                /*  $353C8 -- and here, unlike the first half, the step
                 *  is NOT truncated before it is applied: it stays a
                 *  single and the truncation happens once, after the
                 *  addition.  Rounding it early costs one unit on the
                 *  slower-moving series. */
                fstep2 = x2z_sgl(acc2);

                if (m > 0x3567E0)
                    *mate = ext_to_i32(ext_sub(z2x_i32(m), z2x_sgl(fstep2))); /* $353F8 */
                else
                    *mate = ext_to_i32(ext_add(z2x_i32(m), z2x_sgl(fstep2))); /* $35438 */
            }
        }
    }

    /*  $35464 -- once in sixty-four months one of the four indicators is
     *  knocked back: itself to three quarters and its partner to a half.
     *  The first die is drawn every month whatever happens and the
     *  second only when it comes up zero, so leaving this out draws one
     *  fewer number a month and every later roll is somebody else's.
     *
     *  Both multipliers are doubles, not singles: $354AE and $354FC
     *  pass format 0x0800 rather than 0x1000. */
    if ((Random() & 0x3F) == 0)
    {
        const int i    = (int)(Random() & 3); /* $35474 */
        int32_t  *slot = &c->misc[MISC_IND4_BASE + 4 * i];
        int32_t  *mate = &c->misc[MISC_IND4_B + 4 * i];

        *slot = ext_to_i32(
            ext_mul(z2x_i32(*slot), ext_from_double(0.75))); /* $354AE */
        *mate = ext_to_i32(
            ext_mul(z2x_i32(*mate), ext_from_double(0.5))); /* $354FC */
    }

    /*  $3551E -- how much of each of eleven industries the nation wants
     *  this month.  The mix is tabulated for five technology eras fifty
     *  years apart, and the pass interpolates linearly between the era
     *  the city is in and the next one by how far through it is; past
     *  the last era the final row is used unchanged. */
    {
        int era  = (int)((c->year_founded - 1900) / 50 + c->years / 50); /* $3551E */
        int into = (int)(c->years % 50);                                 /* $3554C */
        int k;

        for (k = 0; k < 11; k++)
        {
            if (era >= 4)
                c->industry_mix[k] = NAT_ERA_MIX[4 * 11 + k]; /* $35566 */
            else
            {
                int32_t next = NAT_ERA_MIX[(era + 1) * 11 + k]; /* $3557C */
                int32_t cur  = NAT_ERA_MIX[era * 11 + k];       /* $355B0 */
                c->industry_mix[k] =
                    (int32_t)(((uint32_t)(next * into + cur * (50 - into))) / 50u);
            }
        }
    }

    /*  $35608 -- what the nation actually buys from each of the eleven
     *  industries.  Each industry's level is pulled a quarter of the way
     *  toward the era mix every month, and the pull is jittered by four
     *  128-sided dice summed together -- a crude bell curve centred on
     *  254, so mix * r / 256 is the mix itself give or take.  The
     *  quarter-weighting is what makes an industry take a few years to
     *  respond to a change of era rather than snapping to it. */
    {
        int     k;
        int32_t unmet = 0, workers_total = 0;

        for (k = 0; k < 11; k++)
        {
            int16_t *lvl = &c->industry_level[k];
            int32_t  acc = 3 * (int32_t)*lvl;                                                          /* $35610 */
            int32_t  r   = (int32_t)game_rand127() + game_rand127() + game_rand127() + game_rand127(); /* $35626.. */
            acc += (c->industry_mix[k] * r) / 256;                                                     /* $3565E, $35666 */
            *lvl = (int16_t)(acc / 4);                                                                 /* $35674, $3568C */
            /*  $356A4 -- the working copy is taken unconditionally... */
            c->industry_scaled[k] = *lvl;

            /*  ...and under one ordinance four of them are knocked back
             *  a tenth, every time round this loop.
             *
             *  FOUR, but NOT the first four.  The original unrolls the
             *  multiply and the four frame slots it names are -$2c,
             *  -$28, -$24 and -$18 ($356C6, $35710, $3575A, $357A4).
             *  The array starts at -$2c with a stride of four, so those
             *  are industries 0, 1, 2 and **5** -- the fourth is index
             *  five, not index three.  Reading it as "the first four"
             *  knocks back an industry that should be left alone and
             *  spares one that should not be, which is a difference of
             *  0.9^8 on each. */
            if (c->ordinances & 0x80000) /* $356A8 */
            {
                static const int HIT[4] = {0, 1, 2, 5};
                int              j;
                ext80            nine_tenths = ext_from_double(0.9); /* $356B6 */
                for (j = 0; j < 4; j++)
                    c->industry_scaled[HIT[j]] = ext_to_i32(ext_mul(
                        z2x_i32(c->industry_scaled[HIT[j]]), nine_tenths));
            }

            /*  $357DE -- then the mix is tilted by how the population is
             *  doing.  A city that is growing wants more of industry 4,
             *  and the second age average moves six industries in bands.
             *  These compound: they are inside the per-industry loop and
             *  so run eleven times. */
            {
                int32_t q = c->misc[MISC_AGE_W90];
                int     j;
                if (c->misc[MISC_POP_INCREASE] != 0) /* $357E2 */
                    c->industry_scaled[4] = ext_to_i32(ext_mul(
                        z2x_i32(c->industry_scaled[4]), ext_from_double(1.1)));
                if (q > 0x82) /* $3582E, over 130 */
                {
                    static const int up2[2] = {6, 9};
                    for (j = 0; j < 2; j++)
                        c->industry_scaled[up2[j]] = ext_to_i32(ext_mul(
                            z2x_i32(c->industry_scaled[up2[j]]), ext_from_double(1.2)));
                }
                else if (q > 100) /* $358D2 */
                {
                    static const int up1[6] = {2, 5, 7, 8, 6, 9};
                    for (j = 0; j < 6; j++)
                        c->industry_scaled[up1[j]] = ext_to_i32(ext_mul(
                            z2x_i32(c->industry_scaled[up1[j]]), ext_from_double(1.1)));
                }
                else if (q < 60) /* $35A9C */
                {
                    static const int dn[2] = {6, 9};
                    for (j = 0; j < 2; j++)
                        c->industry_scaled[dn[j]] = ext_to_i32(ext_mul(
                            z2x_i32(c->industry_scaled[dn[j]]), ext_from_double(0.8)));
                }
            }

            /*  $35B44 -- what is left once what the nation already has is
             *  taken off is the UNMET demand; a surplus counts for nothing. */
            c->industry_scaled[k] -= (int16_t)c->misc[MISC_IND_SUPPLIED + 3 * k];
            if (c->industry_scaled[k] > 0)
                unmet += c->industry_scaled[k];
            else
                c->industry_scaled[k] = 0;
        }

        /*  $35B7C -- the labour market clears.  Every industry carries a
         *  workforce; the total is compared against the jobs available
         *  and the difference shared out in proportion.  The fractional
         *  part of each share is not rounded -- it is settled with a
         *  die, so an industry owed 3.4 layoffs loses three, and a
         *  fourth two times in five. */
        for (k = 0; k < 11; k++)
            workers_total += c->misc[MISC_IND_WORKERS + 3 * k];

        if (workers_total > c->rci_pop[2])
        {
            int32_t surplus = (workers_total - c->rci_pop[2]) * 100; /* $35B88 */
            for (k = 0; k < 11; k++)
            {
                int32_t *w     = &c->misc[MISC_IND_WORKERS + 3 * k];
                int32_t  share = (int32_t)((uint32_t)(surplus * *w) / (uint32_t)workers_total);
                int32_t  whole = share / 100;
                *w -= whole;
                if ((int32_t)((uint16_t)Random() % 100) < share - whole * 100)
                    (*w)--; /* $35BFC */
            }
        }
        else if (unmet != 0) /* $35C0C */
        {
            int32_t deficit = (c->rci_pop[2] - workers_total) * 100; /* $35C12 */
            for (k = 0; k < 11; k++)
            {
                int32_t *w = &c->misc[MISC_IND_WORKERS + 3 * k];
                int32_t  share, whole;
                if (c->industry_scaled[k] == 0)
                    continue; /* $35C36 */
                share = (int32_t)((uint32_t)(deficit * c->industry_scaled[k]) / (uint32_t)unmet);
                whole = share / 100;
                *w += whole;
                if ((int32_t)((uint16_t)Random() % 100) < share - whole * 100)
                    (*w)++; /* $35CA0 */
            }
        }
    }

    /*  ---------------------------------------------------------------
     *  $35CAE -- the two industry indices.  Neither is used by anything
     *  in this file: they are read out in phase 2, where the pollution
     *  blur divides by 4 - MISC[1037] + the water-treatment flag.  A
     *  city whose industry is mostly clean gets -1 there, which is one
     *  MORE unit of division and so visibly less pollution, and a city
     *  running on the dirty six gets 0, 1 or 2, which is less division
     *  and more.  Missing this block does not move a single die, which
     *  is exactly why it went unnoticed for so long -- the whole of the
     *  clock's dice matched while 1898's pollution ran ten per cent
     *  high.
     *
     *  Both indices are a percentage of the industrial population, and
     *  both floor at twenty per cent before scaling, so a small
     *  industrial sector registers as nothing at all.
     *  --------------------------------------------------------------- */
    {
        int      k;
        uint32_t base = (uint32_t)(c->rci_pop[2] + 1); /* $35CD0, never 0 */
        uint32_t share;
        uint32_t worst = 0;

        /*  $35CB4 -- industries 0, 1, 2 and 5 are the dirty ones. */
        share         = (uint32_t)(100 * (c->misc[MISC_IND_WORKERS + 3 * 0] +
                                          c->misc[MISC_IND_WORKERS + 3 * 1] +
                                          c->misc[MISC_IND_WORKERS + 3 * 2] +
                                          c->misc[MISC_IND_WORKERS + 3 * 5])) /
                        base;
        c->misc[1037] = share < 20 ? -1 : ((int32_t)share - 20) / 30; /* $35CE4 */

        /*  $35CFC -- and how concentrated the biggest single industry
         *  is, on a five-point scale.  Only the newspaper reads it. */
        for (k = 0; k < 11; k++)
        {
            uint32_t v =
                (uint32_t)(100 * c->misc[MISC_IND_WORKERS + 3 * k]) / base;
            if (v > worst)
                worst = v; /* $35D32 */
        }
        c->misc[1036] = worst < 20 ? 0 : ((int32_t)worst - 20) / 5; /* $35D42 */

        /*  $35D58 -- a city with nobody left in it stops here, and the
         *  age pyramid is wiped rather than aged. */
        if (c->population == 0)
        {
            int b;
            for (b = 0; b < 20; b++) /* $35D62, three arrays interleaved */
            {
                c->misc[MISC_HIST_BASE + 3 * b]     = 0;
                c->misc[MISC_HIST_BASE + 3 * b + 1] = 0;
                c->misc[MISC_HIST_BASE + 3 * b + 2] = 0;
            }
            return; /* $35D8E jumps to the epilogue at $36698 */
        }
    }

    /*  ---------------------------------------------------------------
     *  $35DFA..$362F4 -- the age pyramid: mortality, ageing, births and
     *  the migration that reconciles the pyramid with the population.
     *
     *  Three parallel twenty-entry arrays describe the city's people.
     *  blk1EDE is a head count per five-year bracket; blk1EE2 and
     *  blk1EE6 are sums over those same heads of two per-person
     *  quantities.  What those two quantities ARE is settled by how the
     *  code below uses them:
     *
     *    blk1EE6 is a LIFE EXPECTANCY in years.  $35EB4 divides it by
     *    the head count to get a per-person average and $35ED4 compares
     *    that against the bracket's age -- bracket b is age b*5 -- and
     *    people start dying once the average falls below their age.
     *
     *    blk1EE2 is an EDUCATION QUOTIENT.  $3607A adds 35 per child who
     *    fits in a school as a cohort ages up, and $36098 adds a
     *    proportional share again for those who fit in a college.
     *
     *  Note what popIncrease/popDecrease turn out to mean: the pyramid
     *  does NOT set the city's population, it is slaved to it.  Deaths
     *  open vacancies that $36204 fills with immigrants; surplus births
     *  are pushed back out at $36314.  Population itself comes from the
     *  buildings on the map.  --------------------------------------- */
#define HEADS(i) (c->misc[MISC_HIST_BASE + 3 * (i)])
#define EDUQ(i)  (c->misc[MISC_HIST_BASE + 3 * (i) + 1])
#define LIFE(i)  (c->misc[MISC_HIST_BASE + 3 * (i) + 2])
    {
        int      b;
        uint32_t poll_per_head; /* a2 -- survives into the birth block  */
        int32_t  hosp_cap;      /* -$60(a6) beds, in people             */
        int32_t  life_base;     /* -$64(a6) years a covered baby gets   */
        int32_t  school_cap;    /* a3 -- desks, in people               */
        int32_t  college_cap;   /* a4 -- places, in people              */

        /*  $35DFA -- hospital capacity.  census[0xD1] counts hospital
         *  tiles, nine tiles to a building, and each building takes 25
         *  people scaled by what the health department is funded at. */
        hosp_cap = (int32_t)(c->census[0xD1] / 9) * 25;           /* $35E0E */
        hosp_cap = hosp_cap * c->dept[DEPT_HEALTH].funding / 100; /* $35E28 */

        /*  $35E32 -- 85 years before any policy, then three health
         *  ordinances worth five years each.  A fourth ordinance buys
         *  extra capacity out of the residential tax take instead. */
        life_base = 85;
        if (c->ordinances & 0x200)
            life_base += 5; /* $35E44 */
        if (c->ordinances & 0x400)
            life_base += 5; /* $35E54 */
        if (c->ordinances & 0x20)
            life_base += 5; /* $35E60 */
        if (c->ordinances & 0x40)
            hosp_cap += c->dept[0].amount / 400; /* $35E80 */

        /*  --- $35E8A: mortality ------------------------------------
         *  For each bracket, the average life expectancy of the people
         *  in it, as a percentage of their actual age.  At or above 100
         *  nobody dies.  Below it, the shortfall in percentage points
         *  is the share of the bracket that dies over two years. */
        for (b = 1; b < 20; b++)
        {
            uint32_t pool = (uint32_t)HEADS(b);
            uint32_t avg, share;
            int32_t  pct, dead, rem;

            if (pool == 0)
                continue; /* $35E96 */

            avg = (uint32_t)LIFE(b) / pool;                 /* $35EAE */
            pct = (int32_t)(avg * 100u) / (int32_t)(b * 5); /* $35ED4 */
            if (pct >= 100)
                continue; /* $35EE0 */

            share = ((uint32_t)(100 - pct) * pool) / 24u; /* $35EF2 */
            dead  = (int32_t)share / 100;                 /* $35EFE */
            rem   = (int32_t)share - dead * 100;          /* $35F12 */
            /*  the leftover hundredth of a person is settled by a die,
             *  the same trick the labour market uses */
            /*  the leftover hundredth of a person is settled by a die,
             *  the same trick the labour market uses */
            if ((int32_t)((uint16_t)Random() % 100) < rem)
                dead++; /* $35F32 */
            if ((uint32_t)dead > pool)
                dead = (int32_t)pool; /* $35F38 */
            if (dead == 0)
                continue; /* $35F3C */

            {
                uint32_t eq   = (uint32_t)EDUQ(b);
                uint32_t take = ((uint32_t)dead * eq) / pool; /* $35F5A */
                if (take > eq)
                    take = eq;                  /* $35F68 */
                EDUQ(b) = (int32_t)(eq - take); /* $35F78 */
            }
            {
                uint32_t le   = (uint32_t)LIFE(b);
                uint32_t take = ((uint32_t)dead * le) / pool; /* $35F94 */
                LIFE(b)       = (int32_t)(le - take);         /* $35F9C */
            }
            HEADS(b) -= dead;                                     /* $35FAA */
            c->pop_increase += dead; /* a vacancy to be filled */ /* $35FAC */
        }

        /*  --- $35FBA: pollution per head, capped at three ----------
         *  This is subtracted from the life expectancy that moves with
         *  a cohort every time it ages up a bracket.  Pollution never
         *  kills anyone directly; it shortens the lives of everyone who
         *  ages through it, permanently. */
        poll_per_head = (uint32_t)c->pollution_tot /
                        (uint32_t)(c->population + 1 + c->accum8[7] * 10); /* $35FD6 */
        if ((int32_t)poll_per_head > 3)
            poll_per_head = 3; /* $35FE0 */

        /*  $35D92 -- school and college capacity, in people.  These cap
         *  how much of a cohort's education improves as it ages up. */
        school_cap  = (int32_t)(c->census[0xD6] / 9) * 15;               /* $35DA4 */
        school_cap  = school_cap * c->dept[DEPT_SCHOOL].funding / 100;   /* $35DC0 */
        college_cap = (int32_t)(c->census[0xD9] >> 4) * 50;              /* $35DD6 */
        college_cap = college_cap * c->dept[DEPT_COLLEGE].funding / 100; /* $35DEC */

        /*  --- $35FEE: ageing ---------------------------------------
         *  A sixtieth of each bracket moves up one bracket per month,
         *  so a cohort takes five years to clear a bracket and sixty to
         *  cross all twelve of the ones people live in.  Both weighted
         *  series move their proportional share with the heads. */
        for (b = 19; b > 0; b--)
        {
            uint32_t pool = (uint32_t)HEADS(b - 1);
            uint32_t up, rem, moved;

            if (pool == 0)
                continue; /* $35FFC */

            up  = pool / 60u;      /* $36004 */
            rem = pool - up * 60u; /* $36018 */
            if ((uint32_t)((uint16_t)Random() % 60) < rem)
                up++; /* $36038 */
            if (up > pool)
                up = pool; /* $3603E */
            if (up == 0)
                continue; /* $36042 */

            {
                uint32_t eq = (uint32_t)EDUQ(b - 1);
                moved       = (up * eq) / pool;      /* $36062 */
                EDUQ(b - 1) = (int32_t)(eq - moved); /* $3606A */

                /*  schooling.  The heads that fit in a school get 35
                 *  points each added as they age up; the rest get
                 *  nothing and carry the gap for life. */
                if (b < 3) /* $3606E */
                {
                    uint32_t seats = (uint32_t)school_cap;
                    if (seats > up)
                        seats = up;       /* $36078 */
                    moved += seats * 35u; /* $36084 */
                }
                else if (b == 3) /* $3608C */
                {
                    uint32_t seats = (uint32_t)college_cap;
                    if (seats > up)
                        seats = up;                      /* $36094 */
                    seats = ((seats * moved) / up) >> 1; /* $360A8 */
                    moved += seats;                      /* $360AC */
                }
                /*  and without this ordinance every cohort loses one
                 *  point per head on the way up */
                if (!(c->ordinances & 0x100))
                    moved -= up;           /* $360BA */
                EDUQ(b) += (int32_t)moved; /* $360C8 */
            }
            {
                uint32_t le = (uint32_t)LIFE(b - 1);
                moved       = (up * le) / pool;              /* $360EA */
                LIFE(b - 1) = (int32_t)(le - moved);         /* $360F2 */
                LIFE(b) += (int32_t)(moved - poll_per_head); /* $36104 */
            }
            HEADS(b - 1) -= (int32_t)up; /* $36112 */
            HEADS(b) += (int32_t)up;     /* $36120 */
        }

        /*  --- $3612A: births ---------------------------------------
         *  Brackets 4 through 8 -- ages 20 to 40 -- have children, one
         *  per three hundred of them per month.  A baby the hospitals
         *  can take gets the full life expectancy; a baby they cannot
         *  gets 35 years.  Every baby starts at a fifth of the city's
         *  average education. */
        {
            int32_t fertile = HEADS(4) + HEADS(5) + HEADS(6) + HEADS(7) + HEADS(8);
            int32_t born    = fertile / 300; /* $3614E */
            /*  NOTE: the original subtracts poll_per_head * 300 here,
             *  not born * 300 -- a2 is still holding the pollution term
             *  from $35FE6.  The intent was plainly the remainder, and
             *  the effect is that the die at $3617E almost always fires
             *  and births round up.  Transcribed as written. */
            int32_t rem = fertile - (int32_t)poll_per_head * 300; /* $36166 */
            if ((int32_t)((uint16_t)Random() % 300) < rem)
                born++; /* $36186 */

            if (born != 0) /* $3618A */
            {
                int32_t covered = hosp_cap;
                if ((uint32_t)covered > (uint32_t)born)
                    covered = born;                                     /* $36194 */
                LIFE(0) += covered * life_base + (born - covered) * 35; /* $361BC */
                EDUQ(0) += (int32_t)(((uint32_t)born *
                                      (uint32_t)c->misc[MISC_AGE_W90]) /
                                     5u);                              /* $361D8 */
                HEADS(0) += born;                                      /* $361DE */
                c->pop_decrease += born; /* a head to be pushed out */ /* $361E0 */
            }
        }

        /*  --- $36204: immigration ----------------------------------
         *  If more died than were born, the difference arrives from
         *  outside.  Newcomers land in the working brackets first, in
         *  batches of a sixteenth of the shortfall, and keep going
         *  round until the shortfall is used up.  They arrive with a
         *  life expectancy of 65 minus their bracket and an education
         *  of 90 minus it -- worse than a well-schooled native. */
        if ((uint32_t)c->pop_increase > (uint32_t)c->pop_decrease) /* $361EC */
        {
            int32_t batch;
            c->pop_increase -= c->pop_decrease;                    /* $361F4 */
            batch = (int32_t)((uint32_t)c->pop_increase >> 4) + 1; /* $361FE */

            while (c->pop_increase != 0) /* $362F4 */
            {
                for (b = 4; b < 8; b++) /* $36260 */
                {
                    int32_t take = batch;
                    if ((uint32_t)take > (uint32_t)c->pop_increase)
                        take = c->pop_increase; /* $36210 */
                    LIFE(b) += (65 - b) * take; /* $3622E */
                    EDUQ(b) += (90 - b) * take; /* $3624A */
                    HEADS(b) += take;           /* $36258 */
                    c->pop_increase -= take;    /* $3625A */
                }
                for (b = 0; b < 12 && c->pop_increase != 0; b++) /* $362E2 */
                {
                    int32_t take = batch;
                    if ((uint32_t)take > (uint32_t)c->pop_increase)
                        take = c->pop_increase; /* $36272 */
                    LIFE(b) += (65 - b) * take; /* $36290 */
                    /*  children arrive with an education that depends on
                     *  how old they are, not on the flat 90 - b */
                    if (b < 3)                           /* $36296 */
                        EDUQ(b) += (b * 35 + 17) * take; /* $362AE */
                    else
                        EDUQ(b) += (90 - b) * take; /* $362BE */
                    HEADS(b) += take;               /* $362DA */
                    c->pop_increase -= take;        /* $362DC */
                }
            }
        }

        /*  --- $36314: emigration ----------------------------------
         *  And if more were born than died, the surplus leaves.  Each
         *  bracket gives up its proportional share of the surplus,
         *  computed in single-precision float; a bracket whose share
         *  rounds to nobody still loses one person one time in four,
         *  which is what stops small cities from never shedding anyone.
         *  The sweep repeats until the surplus is gone. */
        else if ((uint32_t)c->pop_decrease > (uint32_t)c->pop_increase) /* $36304 */
        {
            c->pop_decrease -= c->pop_increase; /* $3630C */

            while (c->pop_decrease != 0) /* $36606 */
            {
                int32_t quota = c->pop_decrease; /* $36602 */

                for (b = 0; b < 20 && c->pop_decrease != 0; b++) /* $365F4 */
                {
                    int32_t pool = HEADS(b);
                    int32_t go;

                    if (pool == 0)
                        continue; /* $36326 */

                    go = emig_share(comp_sgl(c->population + c->pop_decrease),
                                    comp_sgl(pool),
                                    (float)quota); /* $363F8 */
                    /*  the Random is drawn ONLY when the share rounds to
                     *  zero, so it must stay inside this test or the
                     *  whole stream shifts */
                    if (go == 0 && (Random() & 3) == 0)
                        go = 1; /* $3640C */
                    if ((uint32_t)go > (uint32_t)pool)
                        go = pool; /* $36412 */
                    if ((uint32_t)go > (uint32_t)c->pop_decrease)
                        go = c->pop_decrease; /* $3641A */
                    if (go == 0)
                        continue; /* $36420 */

                    LIFE(b) -= emig_share(comp_sgl(pool), comp_sgl(LIFE(b)), comp_sgl(go)); /* $36500 */
                    EDUQ(b) -= emig_share(comp_sgl(pool), comp_sgl(EDUQ(b)), comp_sgl(go)); /* $365DE */
                    HEADS(b) -= go;                                                         /* $365EC */
                    c->pop_decrease -= go;                                                  /* $365EE */
                }
            }
        }
    }

    /*  $3661A..$36694 -- the tail.  Three twenty-entry series
     *  live interleaved in MISC[31..90]; brackets 4 through 10 -- the
     *  working-age ones -- are summed, and the first sum then normalises the other two and
     *  itself against the population.  Everything here is UNSIGNED
     *  division, which matters: the series can go negative and the
     *  original does not care. */
    {
        int i;

        /*  $3660A -- the three accumulators are cleared here every month
         *  before anything is summed into them.  They are running totals
         *  over the brackets, not carried state; missing this clear is
         *  what made the tail wrong on ten of eighteen cities. */
        c->misc[MISC_AGE_W90]  = 0;
        c->misc[MISC_AGE_W65]  = 0;
        c->misc[MISC_AGE_HEAD] = 0;

        for (i = 4; i < 11; i++)
        {
            c->misc[MISC_AGE_W65] += c->misc[MISC_HIST_BASE + 3 * i + 2]; /* $36626 */
            c->misc[MISC_AGE_W90] += c->misc[MISC_HIST_BASE + 3 * i + 1]; /* $36636 */
            c->misc[MISC_AGE_HEAD] += c->misc[MISC_HIST_BASE + 3 * i];    /* $36646 */
        }
        if (c->misc[MISC_AGE_HEAD] != 0) /* $36652 */
        {
            uint32_t a            = (uint32_t)c->misc[MISC_AGE_HEAD];
            c->misc[MISC_AGE_W65] = (int32_t)((uint32_t)c->misc[MISC_AGE_W65] / a);
            c->misc[MISC_AGE_W90] = (int32_t)((uint32_t)c->misc[MISC_AGE_W90] / a);
            c->misc[MISC_AGE_HEAD] =
                (int32_t)((uint32_t)(a * 100u) / (uint32_t)(c->population + 1));
        }
    }
}
