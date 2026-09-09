/*  sim_micro.c -- the microsimulators, and the year-end pass over them.
 *  $101AC: the XMIC records the special buildings carry, aged one year at a
 *  time -- a power plant approaching the end of its life, a prison setting
 *  the police radius around it, an arcology counting toward the ending.
 *  Allocating a record when a building goes up is here too, since it is the
 *  same table.  Split out of sim.c; addresses still point into CODE 2. */
#include "ext80.h"
#include "sc2k.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  The record, in place in the loaded chunk.  XMIC is stored big-endian
 *  like every other chunk, so the words are read and written through
 *  helpers rather than cast. */
uint8_t *micro_rec(const City *c, int i)
{
    if (!c->xmic || (size_t)(i * 8 + 8) > c->xmic_len)
        return NULL;
    return c->xmic + i * 8;
}

int micro_w(const uint8_t *r, int k)
{
    return (int)(int16_t)((uint16_t)r[2 + k * 2] << 8 | r[3 + k * 2]);
}

void micro_set_w(uint8_t *r, int k, int v)
{
    r[2 + k * 2] = (uint8_t)((v >> 8) & 0xFF);
    r[3 + k * 2] = (uint8_t)(v & 0xFF);
}

/*  $1127E.  Which tile owns micro record `rec`?  XTXT carries the
 *  record index offset by 0x33 on the building's own tile, so the
 *  search is for that one byte.  Returns (col << 8) | row, or 0.
 *
 *  `moveq #$cd` at $112BA is -51, not 205: the test is
 *  `rec == xtxt - 51`, which is the offset read the other way. */
static int micro_find_tile(const City *c, int rec)
{
    int y, x;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            int v = c->xtxt[y][x];
            if (v == 0 || v < 0x33 || v >= 0xC9) /* $112A6 */
                continue;
            if (rec == v - 51)
                return (x << 8) | y; /* $112C2 */
        }
    return 0; /* $112DE */
}

/*  $10392.  What the city is charged to put a worn-out plant back:
 *  A5-0x5198 maps the plant's id onto an index, and A5+0x616 is the
 *  cost table the build menu prices from. */
static int32_t micro_rebuild_cost(int type)
{
    int n;
    if (type < 0xC6 || type > 0xD0)
        return 0;
    n = MICRO_REBUILD_IDX[type - 0xC6];
    return (n >= 0 && n < 24) ? BUILD_COST[n] : 0;
}

int micro_cap(const City *c, int want, int per)
{
    /*  Everything here is a WORD.  $1124E and $11252 read both arguments
     *  as words, $1126A truncates the quotient to one, and $1126C
     *  compares them SIGNED.  A big city divides out past 32767 and the
     *  comparison then reads it as negative, so the cap does not clamp
     *  -- it wraps, and the museum in a million-strong city reports a
     *  negative attendance.  Computing this in ints gives a tidier
     *  answer and the wrong one. */
    int16_t w = (int16_t)want;
    int16_t p = (int16_t)per;
    int16_t have;
    if (p == 0)
        p = 100; /* $1125A */
    have = (int16_t)((uint32_t)c->population / (uint32_t)(int32_t)p);
    return have >= w ? (int)w : (int)have; /* $1126C, signed */
}

/*  $FD28.  What a brand-new record starts life holding.  A fixed-slot
 *  building ADDS to a record shared with every other copy of itself; a slot
 *  of its own is cleared first and then filled.  The seven plant figures
 *  are the game's megawatt ratings -- gas 50, oil 220, nuclear 500, solar
 *  50, microwave 1600, fusion 2500, coal 200, with hydro worth 20 apiece
 *  and wind 4 -- which is a useful check that the id chain has been read
 *  the right way round. */
static void micro_init(City *c, int slot, int bld)
{
    uint8_t *r    = micro_rec(c, slot);
    int      year = (int)(c->years + c->year_founded);
    if (!r)
        return;
    if (MICRO_CLASS[bld - 0xC6] >= 0x11) /* $FD48, a shared record */
    {
        switch (bld)
        {
            case 0xC6:
            case 0xC7: /* hydro: 20 MW each */
                micro_set_w(r, 0, micro_w(r, 0) + 1);
                micro_set_w(r, 1, micro_w(r, 1) + 20);
                break;
            case 0xC8: /* wind: 4 MW each */
                micro_set_w(r, 0, micro_w(r, 0) + 1);
                micro_set_w(r, 1, micro_w(r, 1) + 4);
                break;
            case 0xD5: /* a park adds nine to its acreage */
                micro_set_w(r, 1, micro_w(r, 1) + 9);
                break;
            case 0xE9: /* subway, bus, rail: one more of them */
            case 0xEC:
            case 0xED:
                micro_set_w(r, 0, micro_w(r, 0) + 1);
                break;
            default:
                break;
        }
        return;
    }

    /*  $FE0C -- a slot of its own: cleared, then filled by type. */
    r[1] = 0;
    micro_set_w(r, 0, 0);
    micro_set_w(r, 1, 0);
    micro_set_w(r, 2, 0);
    switch (bld)
    {
        case 0xC9:
            micro_set_w(r, 0, 50);
            break; /* gas       */
        case 0xCA:
            micro_set_w(r, 0, 220);
            break; /* oil       */
        case 0xCB:
            micro_set_w(r, 0, 500);
            break; /* nuclear   */
        case 0xCC:
            micro_set_w(r, 0, 50);
            break; /* solar     */
        case 0xCD:
            micro_set_w(r, 0, 1600);
            break; /* microwave */
        case 0xCE:
            micro_set_w(r, 0, 2500);
            break; /* fusion    */
        case 0xCF:
            micro_set_w(r, 0, 200);
            break; /* coal      */

        case 0xD0: /* city hall, $FF58 */
            micro_set_w(r, 0, micro_cap(c, 0xC8, 0x384));
            micro_set_w(r, 1, year);
            break;
        case 0xD2: /* police, $FF8E */
            micro_set_w(r, 0, micro_cap(c, (int)(c->dept[DEPT_POLICE].funding * 2), 0x5A));
            break;
        case 0xD3: /* fire, $FFB8 */
            micro_set_w(r, 0, micro_cap(c, (int)ASR(c->dept[DEPT_FIRE].funding, 1), 0x46));
            micro_set_w(r, 1, 4); /* $FFE6, one crew to start */
            break;
        case 0xD4: /* museum, $FFF2 */
            r[1] = 100;
            break;
        case 0xD1: /* hospital, school, college: $10060 */
        case 0xD6:
        case 0xD9:
            r[1] = 6;
            break;
        case 0xDB: /* a statue remembers the year, $10090 */
            micro_set_w(r, 0, year);
            break;
        case 0xF3: /* the mayor's house, $10006 */
            micro_set_w(r, 0, year);
            micro_set_w(r, 1, (int)((uint16_t)Random() % 30) + 10); /* $1001E */
            micro_set_w(r, 2, (int)((uint16_t)Random() % 60));      /* $10040 */
            break;
        /*  The four arcologies, $100AA/$100E4/$1011E/$1015A: a starting
         *  population, a life of five, and the YEAR they went up, which
         *  is what the Launch Arco's ending counts from.  The llama dome
         *  keeps its year the same way ($1018E). */
        case 0xFB:
            micro_set_w(r, 0, 0x37);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFC:
            micro_set_w(r, 0, 0x1E);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFD:
            micro_set_w(r, 0, 0x2D);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFE:
            micro_set_w(r, 0, 0x41);
            r[1] = 5;
            micro_set_w(r, 2, year);
            break;
        case 0xFF:
            micro_set_w(r, 2, year);
            break;
        default:
            break;
    }
}

/* ================================================================== *
 *  $EEAE  allocMicro -- give a newly placed special building its XMIC
 *  record, and answer the marker the caller writes into XTXT.
 *
 *  Which slot it gets is a table, not a search: `MICRO_CLASS` maps the
 *  building id to zero (no record at all -- runways and cranes are
 *  here, which is why the growth scan never allocates one), to a FIXED
 *  slot shared with every other copy of that building, or to "take the
 *  first free slot from ten up".
 *
 *  When they are all taken the table is not simply full: an arcology
 *  gives up, and anything else EVICTS the first record below 0xFB and
 *  scrubs its marker off the map, so the arcologies outlive everything
 *  else in the table.
 *
 *  The marker is the slot plus 0x33, which is what micro_find_tile
 *  searches XTXT for.
 * ================================================================== */
int sim_alloc_micro(City *c, int y, int x, int bld)
{
    int slot, cls;
    (void)y;
    (void)x;
    if (bld < 0xC6 || bld > 0xFF) /* $EEBC */
        return 0;
    cls = MICRO_CLASS[bld - 0xC6];
    if (cls == 0) /* $EED8, this building keeps no record */
        return 0;

    if (cls >= 0x11)
        slot = cls - 16; /* $EEE8, `moveq #$f0` is -16 */
    else
    {
        for (slot = 10; slot < N_MICRO; slot++) /* $EEF2 */
        {
            const uint8_t *r = micro_rec(c, slot);
            if (!r || r[0] == 0)
                break;
        }
    }

    if (slot >= N_MICRO) /* $EF0C, the table is full */
    {
        if (bld >= 0xFB)
            return 0; /* $EF16, an arcology does not evict */
        for (slot = 10; slot < N_MICRO; slot++)
        {
            const uint8_t *r = micro_rec(c, slot);
            if (!r || r[0] < 0xFB)
                break; /* $EF30 */
        }
        if (slot >= N_MICRO)
            return 0; /* $EF88 */
        /*  $EF4C -- and the evicted record's marker comes off the map,
         *  or a tile would point at a record that is now somebody
         *  else's. */
        {
            int yy, xx;
            for (yy = 0; yy < MAP_H; yy++)
                for (xx = 0; xx < MAP_W; xx++)
                    if (c->xtxt[yy][xx] == (uint8_t)(slot + 0x33))
                        c->xtxt[yy][xx] = 0;
        }
    }

    {
        uint8_t *r = micro_rec(c, slot);
        if (!r)
            return 0;
        r[0] = (uint8_t)bld; /* $EFA0 */
        micro_init(c, slot, bld);
    }

    /*  $EFB6 -- the default name, unless this is a shared record that
     *  has been named already. */
    if (c->xlab && (size_t)((slot + 0x33) * 25 + 16) <= c->xlab_len)
    {
        uint8_t *lab = c->xlab + (slot + 0x33) * 25;
        if (!(cls >= 0x11 && lab[0] != 0))
        {
            int k;
            for (k = 0; k < 16; k++)
                lab[k] = MICRO_LABEL[(bld - 0xC6) * 16 + k];
        }
    }
    return slot + 0x33; /* $F00C */
}

void sim_microsim(City *c)
{
    int i;
    /*  $101E4 -- a2, the ceiling nearly every staffed building uses. */
    const int pop_50 = (int)((uint32_t)c->population / 50u);
    /*  What the loop accumulates.  $101B4 counts the Launch Arcos it
     *  passes; $101DA totals the police workload, which the tail stores
     *  at A5+0x2C92; $101DE totals the population living in arcologies.
     *
     *  $101D4 takes a copy of the police term BEFORE $101E0 clears it,
     *  because the police arm divides by five minus last year's value
     *  while the tail is computing this year's. */
    int       launch_arco  = 0;
    int32_t   police_load  = 0;
    int32_t   arco_pop     = 0;
    const int police_term0 = c->police_term;
    /*  $10240 -- raised when any school's rating falls under four. */
    int school_failing = 0;
    int riot_brewing   = 0;

    /*  $101B4 -- the counts the arms share, each a tile census divided
     *  by the building's footprint. */
    const int arco_n    = (c->census[0xFB] + c->census[0xFC] + c->census[0xFD] +
                           c->census[0xFE]) >>
                          4;                    /* $101F8, 4x4 */
    const int prison_n  = c->census[0xD8] >> 4; /* $10210, 4x4 */
    const int school_n  = c->census[0xD6] / 9;  /* $1021A, 3x3 */
    const int college_n = c->census[0xD9] >> 4; /* $10228, 4x4 */
    const int hosp_n    = c->census[0xD1] / 9;  /* $10232, 3x3 */

    (void)arco_pop;
    (void)arco_n;
    (void)school_n;
    (void)college_n;
    (void)hosp_n;

    for (i = 1; i < N_MICRO; i++)
    {
        uint8_t *r = micro_rec(c, i);
        int      t;
        if (!r)
            break;
        t = r[0];
        if (t == 0) /* $1025E, an empty slot */
            continue;
        if (t < 0xC6 || t > 0xFF) /* $10266, off the table */
            continue;
        switch (t)
        {
            /*  $1042C -- the stadium.  Attendance is the city divided
             *  between its stadiums, held under 25000, and then capped
             *  again at a fifth of the population. */
            case 0xD7:
                {
                    int32_t att;
                    int     n = c->census[0xD7] < 1 ? 1 : (int)c->census[0xD7];
                    att       = (int32_t)((uint32_t)c->population / (uint32_t)n);
                    if (att > 0x61A8)
                        att = 0x61A8 - (int32_t)(Random() & 0xFF); /* $10456 */
                    att = micro_cap(c, (int)att, 5);
                    micro_set_w(r, 0, (int)(att + (int32_t)(Random() & 0xFF)));
                    r[1] = (uint8_t)((Random() & 0x1F) + 9); /* $10498 */
                    break;
                }

            /*  $104B6 -- city hall.  One number, and it is the only arm
             *  that takes both of $11246's arguments as constants: the
             *  long pushed at $104B6 is two words, 200 then 900. */
            case 0xD0:
                micro_set_w(r, 0, micro_cap(c, 0xC8, 0x384));
                break;

            /*  $10614 -- a prison.  Three quarters of last year's
             *  inmates stay, the city's police workload sends more, and
             *  what comes out is added to the police term -- so the
             *  prisons, not the stations, are what set the coverage
             *  radius at the end of the pass. */
            case 0xD8:
                {
                    int32_t held = (int32_t)(uint16_t)micro_w(r, 0);
                    int32_t rate;
                    int     n = prison_n < 1 ? 1 : prison_n;
                    held -= ASR(held + (int32_t)((uint32_t)ASR(held, 1) >> 30), 2);
                    /*  $10646 reads the GLOBAL at A5+0x2C92, which the
                     *  pass does not write until its tail -- so a prison
                     *  sees LAST year's city-wide police workload, not the
                     *  total this year's loop is busy accumulating.  Using
                     *  the running total instead is wrong by a year and the
                     *  arithmetic never gives it away. */
                    held += c->misc[MISC_POLICE_LOAD] / n;
                    if (held > 0x2710)
                        held = (int32_t)(Random() & 0x3FF) + 0x2710; /* $1065E */
                    micro_set_w(r, 0, micro_cap(c, (int)held, 0x14));
                    micro_set_w(r, 1, micro_cap(c, (int)(c->dept[DEPT_POLICE].funding * 3), 0x78));
                    rate = held / 100; /* $106B6 */
                    micro_set_w(r, 2, (int)rate);
                    c->police_term = (int16_t)(c->police_term + rate); /* $106D0 */
                    if (rate > 0x69)
                        riot_brewing = 1; /* $106DC */
                    if (rate <= 0x5A)
                    {
                        r[1] = 0; /* $10732 */
                        break;
                    }
                    /*  $106E8 -- `moveq #$a6` is -90, so this is rate - 90. */
                    rate = rate - 90 + (100 - c->dept[DEPT_POLICE].funding) / 10;
                    r[1] = (uint8_t)(rate ? (int32_t)Random() % rate : 0);
                    break;
                }

            /*  $10C56, $10C5A -- the arcologies.  Their residents are
             *  what the structure holds, what the region's arcologies
             *  can between them support, and what the tax rates allow;
             *  the smallest wins, and the total is the population term
             *  graph series 0 adds to the head count.
             *
             *  $FE, the Launch Arco, counts itself on the way past --
             *  see the end of the pass for what that is for. */
            case 0xFE:
                launch_arco++; /* $10C56, then falls through */
                /* fall through */
            case 0xFB:
            case 0xFC:
            case 0xFD:
                {
                    int32_t held = (int32_t)(uint16_t)micro_w(r, 0) * 1000;
                    int32_t room, tax;
                    int     n = arco_n < 1 ? 1 : arco_n;
                    room      = micro_cap(c, (int)(held / 10), n * 20);
                    tax       = ((20 - c->dept[0].funding) + (20 - c->dept[1].funding) +
                                 (20 - c->dept[2].funding)) /
                                6;                                        /* $10CA8 */
                    tax       = ((tax + (int32_t)r[1]) * 2) * 100 - 2000; /* $10CE0 */
                    if (tax < room)
                        room = tax; /* $10CF4 */
                    {
                        int32_t was = (int32_t)(uint16_t)micro_w(r, 1);
                        room += was / 50; /* $10D12 */
                        room += was;
                        if (room > held)
                            room = held;                /* $10D44 */
                        room += (int32_t)game_rand63(); /* $10D4C */
                        micro_set_w(r, 1, (int)room);
                        arco_pop += (int32_t)(uint16_t)micro_w(r, 1); /* $10D76 */
                    }
                    break;
                }

            /*  $10D7C -- the Llama Dome, which reports three figures
             *  about llamas and derives them from the city's size and
             *  four dice.  It is a joke building and the code treats it
             *  as one. */
            case 0xFF:
                {
                    int32_t d3;
                    r[1] = (uint8_t)(Random() & 0xFF);
                    d3   = (int32_t)((uint32_t)c->population >> 3) +
                           (int32_t)(Random() & 0x3FF);
                    micro_set_w(r, 0, (int)d3);
                    micro_set_w(r, 1, (int)(ASR(d3 + (int32_t)((uint32_t)ASR(d3, 2) >> 29), 3) + (int32_t)(Random() & 0x7F)));
                    micro_set_w(r, 2, (int)(ASR(d3 + (int32_t)((uint32_t)ASR(d3, 3) >> 28), 4) + (int32_t)(Random() & 0x3F)));
                    break;
                }

            /*  $102F0 -- the seven burning power plants.  They age a year,
             *  report an output that wanders a little around the supplied
             *  percentage, get a newspaper story at forty-eight, and at
             *  fifty they are finished.  Which way "finished" goes depends
             *  on the DISASTER switch, and not the way round you would
             *  guess: with disasters turned OFF ($13AA set) the city
             *  quietly rebuilds the plant and bills you for it, falling
             *  back to demolition if the treasury cannot cover it.  With
             *  disasters ON the plant simply goes. */
            case 0xC9:
            case 0xCA:
            case 0xCB:
            case 0xCC:
            case 0xCD:
            case 0xCE:
            case 0xCF:
                {
                    int at;
                    r[1]++; /* $102FA */
                    micro_set_w(r, 1, (int)(c->power_pct + (int32_t)(Random() & 7)));
                    /*  $1032E -- the story at forty-eight is interface. */
                    if (r[1] <= 0x32) /* $10358 */
                        break;
                    at = micro_find_tile(c, i); /* $1127E */
                    if (at == 0)
                        break;
                    {
                        int y = at & 0xFF, x = (at >> 8) & 0xFF;
                        if (c->disasters_off) /* $10380 */
                        {
                            int32_t cost = micro_rebuild_cost(t);
                            if (cost <= c->funds) /* $103A8 */
                            {
                                c->funds -= cost;
                                r[1] = 0; /* a new plant, no years on it */
                                break;
                            }
                        }
                        /*  $103C4 and $103F8, the same two lines twice.
                         *  The call they guard with MISC[1021] is $392E,
                         *  which SCROLLS THE VIEW to the tile so you watch
                         *  the plant go -- interface, not simulation, and
                         *  nothing to port. (symbols.json carried it as
                         *  `powerLineRemove` for a while, which is why an
                         *  earlier note here said a routine was missing.
                         *  None is.) No shipped city has a fifty-year-old
                         *  plant, so this arm is transcription only: read
                         *  off the listing and never checked against the
                         *  oracle. */
                        sim_demolish_and_place(c, y, x, 0xFF);
                        r[0] = 0; /* $103EE, the record dies with it */
                    }
                    break;
                }

            /*  $1073A -- a school.  Pupils come out of the youngest
             *  two age brackets, teachers out of the education budget
             *  less a penalty for last year's rating, and the new
             *  rating is pupils per teacher on a scale that runs
             *  backwards.  A rating under four raises a flag the pass
             *  carries to its end. */
            case 0xD6:
                {
                    int32_t fund = c->dept[DEPT_SCHOOL].funding;
                    int32_t pupils, staff;
                    int     per, w0, w1;
                    /*  $1073E -- signed divide by four, THINK C's way. */
                    micro_set_w(r, 2, (int)ASR(fund + (int32_t)((uint32_t)ASR(fund, 1) >> 30), 2));
                    per    = school_n < 1 ? 1 : school_n;
                    pupils = (int32_t)(Random() & 0xF); /* $10790, drawn FIRST */
                    pupils += (int32_t)((uint32_t)(HEADS_AT(c, 1) + HEADS_AT(c, 2)) /
                                        (uint32_t)per);
                    if (pupils > 0x5DC)
                        pupils = (int32_t)(Random() & 0xFF) + 0x5DC; /* $107C6 */
                    w0 = micro_cap(c, (int)pupils, 0x14);
                    micro_set_w(r, 0, w0);
                    staff = (int32_t)(Random() & 7); /* $107F6 */
                    staff += (fund * 6) / 10;        /* $10810 */
                    staff -= 12 - (int32_t)r[1];     /* $10834, NOT doubled */
                    if (staff < 0)
                        staff = 0;
                    w1 = micro_cap(c, (int)staff, 0x64);
                    micro_set_w(r, 1, w1);
                    {
                        int d = w1 < 1 ? 1 : w1;
                        int v = w0 / d; /* $1086E */
                        v     = v < 0x0F   ? 12
                                : v > 0x33 ? 0
                                           : (0x33 - v) / 3;
                        r[1]  = (uint8_t)v;
                        if (v < 4)
                            school_failing = 1; /* $108B2 */
                    }
                    break;
                }

            /*  $108BC -- a college.  The same shape as the school one
             *  bracket up, and the penalty for a bad year is four times
             *  as heavy. */
            case 0xD9:
                {
                    int32_t fund = c->dept[DEPT_COLLEGE].funding;
                    int32_t students, staff;
                    int     per, w0, w1;
                    micro_set_w(r, 2, (int)(fund & 0xFFFF)); /* $108CA, low word */
                    per      = college_n < 1 ? 1 : college_n;
                    students = (int32_t)((uint32_t)HEADS_AT(c, 3) / (uint32_t)per);
                    students += (int32_t)(Random() & 0x1F); /* $108F4 */
                    if (students > 0x1388)
                        students = (int32_t)(Random() & 0x1FF) + 0x1388; /* $10910 */
                    w0 = micro_cap(c, (int)students, 0x1E);
                    micro_set_w(r, 0, w0);
                    staff = (int32_t)(Random() & 0xF) + fund * 2; /* $10940 */
                    staff -= (int32_t)((12 - (int)r[1]) << 2);    /* $1096A */
                    if (staff < 0)
                        staff = 0;
                    w1 = micro_cap(c, (int)staff, 0x64);
                    micro_set_w(r, 1, w1);
                    {
                        int d = w1 < 1 ? 1 : w1;
                        int v = (w0 * 4) / d; /* $109A6 */
                        r[1]  = (uint8_t)(v < 0x32   ? 12
                                          : v > 0x6E ? 0
                                                     : (0x6E - v) / 5);
                    }
                    break;
                }

            /*  $109E8 -- a hospital.  Patients are the city's people
             *  divided between the hospitals; staff is what health
             *  funding buys, less a penalty for last year's rating; and
             *  the new rating is patients per staff, on a scale that
             *  runs backwards -- fewer patients each is better. */
            case 0xD1:
                {
                    int32_t fund = c->dept[DEPT_HEALTH].funding;
                    int32_t pat, staff;
                    int     per, w0, w1;
                    /*  $109EC -- (x + (x >>> 31)) >> 1 is how THINK C
                     *  divides a signed long by two. */
                    micro_set_w(r, 2, (int)ASR(fund + (int32_t)((uint32_t)fund >> 31), 1));
                    per = 25 * hosp_n;
                    if (per < 1)
                        per = 1; /* $10A14 */
                    pat = (int32_t)((uint32_t)c->population / (uint32_t)per);
                    pat += (int32_t)(Random() & 0xF); /* $10A2E */
                    if (pat > 1000)
                        pat = (int32_t)(Random() & 0x7F) + 1000; /* $10A4A */
                    w0 = micro_cap(c, (int)pat, 0x1E);
                    micro_set_w(r, 0, w0);
                    staff = (int32_t)(Random() & 7) + fund; /* $10A7A */
                    staff -= (12 - (int32_t)r[1]) * 2;      /* $10AA0 */
                    if (staff < 0)
                        staff = 0;
                    w1 = micro_cap(c, (int)staff, 0x78);
                    micro_set_w(r, 1, w1);
                    {
                        int d = w1 < 1 ? 1 : w1;
                        int v = (w0 * 10) / d; /* $10ADC */
                        r[1]  = (uint8_t)(v < 0x32   ? 12
                                          : v > 0x6E ? 0
                                                     : (0x6E - v) / 5);
                    }
                    break;
                }

            /*  $10B24 -- the zoo.  Four numbers about the animals, and
             *  not one of them is derived from anything: the zoo is
             *  scenery that reports on itself.  Note the generator --
             *  $20EE6 is THINK C's rand, not the Toolbox's. */
            case 0xDA:
                r[1] = (uint8_t)lib_rand(100);
                micro_set_w(r, 0, (int)lib_rand(100));
                micro_set_w(r, 1, (int)lib_rand(100));
                micro_set_w(r, 2, (int)lib_rand(100));
                break;

            /*  $10B8A -- the mayor's house.  It keeps the approval
             *  figure the February poll last wrote, and counts down a
             *  timer whose expiry is somebody else's business. */
            case 0xF3:
                micro_set_w(r, 1, (int)c->approval);
                if (micro_w(r, 2) != 0) /* $10BB0 */
                {
                    micro_set_w(r, 2, micro_w(r, 2) - 1);
                    r[1]++;
                }
                break;

            /*  $11028 -- the library system, funded from the school
             *  budget.  Its second word is the only ACCUMULATING figure
             *  in the whole pass: knowledge is added to, not
             *  recomputed, and a library funded below fifty per cent
             *  subtracts -- `moveq #$ce` is -50, not 206. */
            case 0xF5:
                {
                    int32_t fund = c->dept[DEPT_SCHOOL].funding;
                    int32_t n    = (int32_t)c->census[0xF5];
                    int32_t know;
                    micro_set_w(r, 0, micro_cap(c, (int)(n * (fund << 2)), 0x12));
                    know = n * (fund - 50) + (int32_t)(uint16_t)micro_w(r, 1);
                    /*  $1109A -- kept only while it is under 32000 and
                     *  above zero; outside that the old figure stands. */
                    if (know < 0x7D00 && know > 0)
                        micro_set_w(r, 1, (int)know);
                    {
                        int32_t pop = c->population < 1 ? 1 : c->population;
                        int32_t v   = (n * fund * 300) / pop;
                        if (v > 12)
                            v = 12; /* $110EC */
                        r[1] = (uint8_t)v;
                    }
                    break;
                }

            /*  $104D6 -- a police station.  Its beat is what the
             *  budget pays for, capped by the city's size; its workload
             *  is the city's crime spread over the stations; and the
             *  workload accumulates into a city-wide total that the
             *  tail turns into the coverage radius. */
            case 0xD2:
                {
                    int32_t fund = c->dept[DEPT_POLICE].funding;
                    int     n, load, per;
                    r[1] = (uint8_t)(fund & 0xFF); /* $104E4, the low byte */
                    micro_set_w(r, 0, micro_cap(c, (int)(fund * 2), 0x5A));
                    n    = c->census[0xD2] < 1 ? 1 : (int)c->census[0xD2]; /* $10518 */
                    load = (int)((uint32_t)c->crime_tot / (uint32_t)n);
                    micro_set_w(r, 1, load);
                    /*  $1053E -- five minus the term the pass started with,
                     *  never less than one. */
                    per = 5 - police_term0;
                    if (per < 1)
                        per = 1;
                    {
                        int v = load / per + (int)(Random() & 0xF); /* $1055E */
                        micro_set_w(r, 2, v);
                        /*  $10580 -- and into the city-wide total, which
                         *  saturates rather than wrapping. */
                        if (police_load + v >= 0xFFFF)
                            police_load = 0xFFFF;
                        else
                            police_load += v;
                    }
                    break;
                }

            /*  $105A0 -- a fire station.  Engines from the budget, crews
             *  from the engines, and a response time nobody can predict. */
            case 0xD3:
                {
                    int32_t fund = c->dept[DEPT_FIRE].funding;
                    int     w0;
                    r[1] = (uint8_t)(fund & 0xFF); /* $105AE */
                    w0   = micro_cap(c, (int)ASR(fund, 1), 0x46);
                    micro_set_w(r, 0, w0);
                    micro_set_w(r, 1, (int)((uint16_t)w0 >> 4) + 1); /* $105E8 */
                    micro_set_w(r, 2, (int)((uint16_t)Random() % 20) + 2);
                    break;
                }

            /*  $10F18 -- the park system.  Visitors are what the parks
             *  could take, and what the city has people to send; both
             *  are held under 65000 before the smaller wins. */
            case 0xD5:
                {
                    int32_t vis = (int32_t)(uint16_t)(micro_w(r, 1) * 0x19C);
                    int32_t can = (int32_t)((uint32_t)c->population / 6u);
                    int     w1;
                    if (vis > 0xFDE8)
                        vis = 0xFDE8;
                    if (can > 0xFDE8)
                        can = 0xFDE8;
                    if (can < vis)
                        vis = can; /* $10F58 */
                    micro_set_w(r, 0, (int)vis);
                    w1 = (int)(uint16_t)(c->census[0xD5] + c->census[0x0D]);
                    micro_set_w(r, 1, w1);
                    micro_set_w(r, 2, micro_cap(c, (int)((uint16_t)w1 / 9), 0x78));
                    break;
                }

            /*  $10FBC -- a museum, funded out of the education budget
             *  like the schools and the library. */
            case 0xD4:
                {
                    int32_t fund = c->dept[DEPT_COLLEGE].funding;
                    micro_set_w(r, 0, micro_cap(c, (int)(c->census[0xD4] * (fund << 2)), 0x14));
                    micro_set_w(r, 1, (int)((fund / 10) * c->census[0xD4]));
                    break;
                }

            /*  $10EDE -- hydro dams.  Two ids because a dam has two
             *  orientations, and neither ages: they are the one plant
             *  the game never makes you replace. */
            case 0xC6:
            case 0xC7:
                {
                    int n = (int)(uint16_t)(c->census[0xC6] + c->census[0xC7]);
                    micro_set_w(r, 0, n);
                    micro_set_w(r, 1, (int)(uint16_t)(20 * (unsigned)n)); /* $10F0C */
                    break;
                }

            /*  $10EAE -- wind.  Also ageless. */
            case 0xC8:
                micro_set_w(r, 0, (int)c->census[0xC8]);
                micro_set_w(r, 1, (int)(uint16_t)(c->census[0xC8] << 2));
                break;

            /*  $10E86, $10E1A, $10E5A -- the three transit systems.  Each
             *  takes a tile count and last year's ridership, which the tail
             *  then clears ready for the year ahead.  The ridership
             *  counters are longs and only their LOW word is read here
             *  ($1248, $124C, $1250 are the second halves of $1246, $124A,
             *  $124E). */
            case 0xE9: /* subway */
                micro_set_w(r, 0, (int)c->census[0xE9]);
                micro_set_w(r, 2, (int)(int16_t)(c->transit_subway & 0xFFFF));
                break;
            case 0xEC: /* bus */
                micro_set_w(r, 0, (int)(uint16_t)(c->census[0xEC] >> 2));
                micro_set_w(r, 1, (int)c->census[0xEC]);
                micro_set_w(r, 2, (int)(int16_t)(c->transit_bus & 0xFFFF));
                break;
            case 0xED: /* rail */
                micro_set_w(r, 0, (int)(uint16_t)(c->census[0xED] >> 2));
                micro_set_w(r, 2, (int)(int16_t)(c->transit_rail & 0xFFFF));
                break;

            /*  $10BCA -- a statue has nothing to simulate.  It picks a
             *  number so the plaque can say something. */
            case 0xDB:
                micro_set_w(r, 1, (int)((uint16_t)Random() % 42));
                break;

            /*  $10BEC -- water treatment and desalinization.  Both keep
             *  the supply percentage they saw, a satisfaction roll, and
             *  a staff figure the city's size caps. */
            case 0xF4:
            case 0xFA:
                r[1] = (uint8_t)(c->water_pct + (Random() & 7));    /* $10BFA */
                micro_set_w(r, 0, (int)((uint16_t)Random() % 100)); /* $10C16 */
                {
                    int v = (int)(Random() & 0x1F) + 0x87; /* $10C34 */
                    if (v > pop_50)
                        v = pop_50; /* $10C42 */
                    micro_set_w(r, 1, v);
                }
                break;

            /*  $11104 -- the marina's boats: eight a berth plus a few,
             *  and never more than the city has people to sail them. */
            case 0xF8:
                {
                    int v = (int)(uint16_t)(c->census[0xF8] << 3) +
                            (int)game_rand(0x14);             /* $1110C */
                    micro_set_w(r, 0, micro_cap(c, v, 0x96)); /* $11122 */
                    break;
                }

            default:
                break;
        }
    }

    /*  $11144 -- the tail. */
    c->misc[MISC_POLICE_LOAD] = police_load; /* $11144 */
    c->misc[MISC_ARCO_POP]    = arco_pop;    /* $1114A */
    /*  $1114E -- the three ridership counters start the year at zero.  The
     *  transit arms above have just copied them into their records, which
     *  is the only place last year's figure survives. */
    c->transit_bus = c->transit_rail = c->transit_subway = 0;

    /*  $1115A -- the police term the prisons accumulated, per prison.  It
     *  comes out as 0 or 1 and nothing else: under eighty it is 1, at
     *  eighty or over it is 0, and with no prisons at all it is 0. $23E0C
     *  reads it as `(term + 5) * funding`, so all this decides is whether
     *  the beat reaches five tiles or six. */
    if (prison_n > 0)
        c->police_term =
            (int16_t)((c->police_term / prison_n) < 0x50 ? 1 : 0);
    else
        c->police_term = 0; /* $11182 */

    /*  $11186 and $1119A -- a failing school and a prison over capacity
     *  each get a newspaper story.  Interface, both of them. */
    (void)school_failing;
    (void)riot_brewing;

    /*  $111AE -- and the ending.  More than three hundred Launch Arcos
     *  with six million people in the city's arcologies, and they all
     *  leave: every $FE tile is demolished and the treasury is paid a
     *  hundred thousand a piece. */
    if ((c->census[0xFE] >> 4) > 0x12C &&
        c->misc[MISC_ARCO_POP] > 0x5B8D80)
    {
        int y, x;
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
                if (c->xbld[y][x] == 0xFE)
                    sim_demolish_and_place(c, y, x, 0xFF); /* $11200 */
        c->funds += (int32_t)launch_arco * 100000;         /* $11222 */
    }
}
