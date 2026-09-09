/*  sim_scan.c -- the passes that read the whole map and write a plane.
 *  Power and water, handed out in queue order until capacity runs out;
 *  traffic, pollution, population and demand; the forest, the weather and
 *  the newspaper's rolls; the graphs; land value, crime, and the coverage
 *  each service station spreads.  These are the phases that produce the
 *  overlays, and between them they are most of what the city knows about
 *  itself.  Split out of sim.c; addresses still point into CODE 2. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  A5+0x13BA -- the one scratch plane the whole of $2317E shares, 128 rows
 *  of 128 words.  Nothing ever clears it between stages, so each stage
 *  reads whatever the last one left in the cells it touches.  That is not
 *  tidy, and reproducing it is the difference between land value at 89% and
 *  at 100%: stage 4 seeds its accumulators from stage 1's raw pollution and
 *  stage 3's building marks.  Modelling the two stages with separate arrays
 *  loses exactly that. */
static int16_t pad[MAP_H][MAP_W];

/*  Push the four neighbours that have not been visited yet.  The order is
 *  load bearing and is taken literally from $21250, $2128A, $212C6 and
 *  $21304: west, north, east, south.  Power is handed out in queue order
 *  until capacity runs out, so this order decides which tiles brown out
 *  when a network is short. */
static void push_neighbours(City *c, int y, int x)
{
    if (x > 0 && !(c->xbit[y][x - 1] & XBIT_VISITED))
        q_push(y, x - 1);
    if (y > 0 && !(c->xbit[y - 1][x] & XBIT_VISITED))
        q_push(y - 1, x);
    if (x < MAP_W - 1 && !(c->xbit[y][x + 1] & XBIT_VISITED))
        q_push(y, x + 1);
    if (y < MAP_H - 1 && !(c->xbit[y + 1][x] & XBIT_VISITED))
        q_push(y + 1, x);
}

/*  Pass 2 walks the marks pass 1 left, so its test is inverted: push a
 *  neighbour only while it is still marked ($21482, $214C2, ...).  The
 *  pass clears each mark as it goes, which both terminates the walk and
 *  leaves the map clean for the next plant.  Getting this backwards
 *  makes the flood die on the first tile -- it produced almost no
 *  powered tiles at all.
 */
static void push_marked(City *c, int y, int x)
{
    if (x > 0 && (c->xbit[y][x - 1] & XBIT_VISITED))
        q_push(y, x - 1);
    if (y > 0 && (c->xbit[y - 1][x] & XBIT_VISITED))
        q_push(y - 1, x);
    if (x < MAP_W - 1 && (c->xbit[y][x + 1] & XBIT_VISITED))
        q_push(y, x + 1);
    if (y < MAP_H - 1 && (c->xbit[y + 1][x] & XBIT_VISITED))
        q_push(y + 1, x);
}

/*  Output of one tile of a power plant.  Eight of the ten are constants
 *  from the switch at $21174; two are computed.  $211DA reads the
 *  terrain, which is why a wind farm on a mountain really does produce
 *  more, and $2119C reads the weather global at A5+0x1F02.
 */
static int32_t plant_output(const City *c, int y, int x, uint8_t bld)
{
    int32_t v = BUILDING[bld].power;
    if (v >= 0)
        return v;

    /*  Both rolls are UNCONDITIONAL in the original -- $211EA and $211B2
     *  draw first and divide afterwards.  Skipping the draw when the span
     *  works out at zero costs a number out of the stream, and the solar
     *  span reaches zero as soon as cloud cover passes 90, which the
     *  weather walk makes reachable.  The guard is kept on the division
     *  alone, where the original would trap.  The roll is the WHOLE sixteen
     *  bits: $211B6 and $211EE clear a register and move the word into it,
     *  so the value the divide sees runs 0..65535.  Masking to 0x7FFF loses
     *  the top bit and gives a different remainder for half of all draws. */
    if (v == -1)
    { /* Wind,  $211DA */
        int span = ASR(c->weather1, 3) + 1;
        int r    = (uint16_t)Random();
        int alt  = c->altm[y][x] & 0x1F;
        return (alt + (span ? r % span : 0)) >> 1;
    }
    { /* Solar, $2119C */
        int span = (100 - c->weather2) / 10;
        int r    = (uint16_t)Random();
        return (span ? r % span : 0) + 5;
    }
}

/*  $210A2 -- flood one power network.  Two BFS passes over the same
 *  network, which is the part that matters: pass 1  walk every conductive
 *  tile, sum generating capacity, count the tiles that draw power, and mark
 *  each tile visited. pass 2  walk it again handing out power in queue
 *  order, one unit per drawing tile, until capacity is exhausted.  Tiles
 *  the second pass reaches after that stay unpowered, and the visited mark
 *  is cleared behind it so a different plant can pick up the remainder on a
 *  later flood.  That is why $20FC4's outer loop skips tiles that already
 *  have XBIT_POWERED: a fully served network is done, a starved one is not. */
static void power_flood(City *c, int y0, int x0, int32_t *supply, int32_t *drawn)
{
    int32_t generated = 0, consumers = 0, capacity;
    int     y, x;

    /* ---- pass 1: measure ---------------------------------------- */
    q_reset();
    q_push(y0, x0);
    while (!q_empty())
    {
        uint8_t bits, bld;
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;

        bits = c->xbit[y][x];
        if (bits & XBIT_VISITED)
            continue; /* $2110C */
        if (!(bits & XBIT_CONDUCTIVE))
            continue; /* $21126 */

        bld = c->xbld[y][x];
        if (bld >= BLD_POWER_FIRST && bld <= BLD_POWER_LAST)
            generated += plant_output(c, y, x, bld); /* $21140 */
        else if (BLD_CONSUMES_POWER(bld))
            consumers++; /* $21226 */

        c->xbit[y][x] |= XBIT_VISITED; /* $2124C */
        push_neighbours(c, y, x);
    }

    /*  $21354 adds the raw total to the reported capacity, and only then
     *  does $21358 apply the ordinance's extra twelfth.  The order matters:
     *  the boost raises what consumers are allowed to draw, it does not
     *  raise the capacity the percentage divides by.  Boosting first
     *  inflates capacity by exactly 13/12 and drags the reported figure
     *  down with it. */
    *supply += generated; /* $21354, before the boost */

    if (c->ordinances & 0x10000) /* $2135C */
        generated += generated / 12;

    capacity = generated;
    if (consumers > capacity)
        consumers = capacity; /* $21374 */
    *drawn += consumers;      /* $21384 */

    /* ---- pass 2: distribute ------------------------------------- */
    q_reset();
    q_push(y0, x0); /* $21388 */
    while (!q_empty())
    {
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;
        if (!(c->xbit[y][x] & XBIT_VISITED))
            continue; /* $213E8 */

        if (capacity != 0)
        { /* $21404 */
            if (c->xbld[y][x] >= BLD_ZONE_FIRST)
                capacity--;
            c->xbit[y][x] |= XBIT_POWERED; /* $21442 */
        }
        c->xbit[y][x] &= (uint8_t)~XBIT_VISITED; /* $2145E */
        push_marked(c, y, x);
    }
}

/*  $20FC4 -- phase 1.  Note what power_pct actually is: the game divides
 *  drawn-by-capacity, not capacity-by-demand, and returns 100 when there
 *  are no plants at all.  It is a load meter, not a coverage meter.     */
void sim_power_grid(City *c)
{
    int32_t supply = 0, drawn = 0;
    int     y, x;

    for (y = 0; y < MAP_H; y++) /* $20FD4 */
        for (x = 0; x < MAP_W; x++)
            c->xbit[y][x] &= (uint8_t)~(XBIT_POWERED | XBIT_VISITED);

    for (y = 0; y < MAP_H; y++) /* $2100C */
        for (x = 0; x < MAP_W; x++)
        {
            uint8_t bld;
            if (c->xbit[y][x] & XBIT_POWERED)
                continue; /* network already served */
            bld = c->xbld[y][x];
            if (bld < BLD_POWER_FIRST || bld > BLD_POWER_LAST)
                continue;
            power_flood(c, y, x, &supply, &drawn);
        }

    c->power_capacity = supply; /* A5+0x11D6 */
    if (supply == 0)
        c->power_pct = 100; /* $21086 */
    else
        c->power_pct = drawn * 100 / supply;
    if (c->power_pct > 100)
        c->power_pct = 100; /* $2108C */
}

/*  $2182E -- flood one water network.  Structurally identical to the
 *  power flood: measure, then distribute, with the visited bit as the
 *  handshake between the two passes.  What differs is where capacity
 *  comes from, and it is worth reading:
 *
 *    Pump ($218F0)            weather/2 + 5*pumpTerm, plus 10 for every
 *                             tile of FRESH water in its 3x3 neighbourhood
 *    Desalinization ($21990)  20 for every tile of SALT water in its 3x3
 *    Reservoir ($21A10)       stores 100; contributes 100 if it was still
 *                             watered from last cycle, then is cleared
 *    Water Treatment ($21A36) contributes nothing, always clears
 *
 *  Fresh is (XBIT & 0x05) == 0x04 and salt is == 0x05, which is what
 *  identifies bit 0 as the salt flag.  Pumps and desalinators only work
 *  while powered, so the water grid genuinely depends on the power grid
 *  having run first.
 *
 *  One consequence worth stating, because it caps how exactly a saved
 *  city can be reproduced.  The schedule runs the water grid at phase 20
 *  ($220DA) and $33FAE at phase 21 ($220E4), and $33FAE rewrites both
 *  weather bytes as a running average ($34CAC, $34CD2).  So the weather
 *  saved in MISC[26] is always at least one update newer than the
 *  weather this pass actually used.  Networks with a comfortable
 *  capacity margin are insensitive to that and reproduce exactly;
 *  networks sitting right at their budget boundary cannot, and no
 *  amount of care here will fix it -- the input is gone.
 */
static int fresh_or_salt(const City *c, int y, int x, int want)
{
    int n = 0, yy, xx;
    for (yy = y - 1; yy <= y + 1; yy++)
    {
        if (yy < 0 || yy > MAP_H - 1)
            continue;
        for (xx = x - 1; xx <= x + 1; xx++)
        {
            if (xx < 0 || xx > MAP_W - 1)
                continue;
            if ((c->xbit[yy][xx] & (XBIT_WATER | XBIT_SALT)) == want)
                n++;
        }
    }
    return n;
}

static void water_flood(City *c, int y0, int x0, int32_t *cap_out, int32_t *met_out)
{
    int32_t capacity = 0, consumers = 0, reservoir = 0, budget;
    int     y, x;

    /* ---- pass 1: measure ---------------------------------------- */
    q_reset();
    q_push(y0, x0);
    while (!q_empty())
    {
        uint8_t bits, bld;
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;

        bits = c->xbit[y][x];
        if (bits & XBIT_VISITED)
            continue; /* $21884 */
        if (!(bits & XBIT_CONDUCTS_WATER))
            continue; /* 0x20, $21890 */

        bld = c->xbld[y][x];
        if (bld >= BLD_ZONE_FIRST)
        {
            switch (bld)
            {
                case 0xDC: /* Pump */
                    if (bits & XBIT_POWERED)
                    {
                        capacity += c->weather2 / 2;    /* $21918 */
                        capacity += 5 * c->water_level; /* $21926, and see the note
                                                         *  on the field itself */
                        capacity += 10 * fresh_or_salt(c, y, x, XBIT_WATER);
                    }
                    break;
                case 0xFA: /* Desalinization */
                    if (bits & XBIT_POWERED)
                        capacity += 20 * fresh_or_salt(c, y, x, XBIT_WATER | XBIT_SALT);
                    break;
                case 0xEB:            /* Reservoir */
                    reservoir += 100; /* $21A10 */
                    if (bits & XBIT_WATERED)
                        capacity += 100;
                    c->xbit[y][x] &= (uint8_t)~XBIT_WATERED; /* $21A4E */
                    break;
                case 0xF4: /* Water Treatment */
                    break;
                default:
                    consumers++; /* $21A54 */
                    break;
            }
        }
        c->xbit[y][x] |= XBIT_VISITED; /* $21A6E */
        push_neighbours(c, y, x);
    }

    /* ---- accounting, $21B76 ------------------------------------- */
    if (consumers > capacity)
        consumers = capacity;
    if (capacity - consumers < reservoir)
        reservoir = capacity - consumers;
    reservoir = (reservoir + 50) / 100; /* $21B8A */
    *cap_out += capacity;
    *met_out += consumers;

    /* ---- pass 2: distribute, $21BC2 ----------------------------- */
    budget = consumers;
    q_reset();
    q_push(y0, x0);
    while (!q_empty())
    {
        uint8_t bld;
        q_pop(&y, &x);
        if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
            continue;
        if (!(c->xbit[y][x] & XBIT_VISITED))
            continue; /* $21BE4 */

        bld = c->xbld[y][x];
        if (bld == 0xEB)
        { /* $21C36 Reservoir */
            if ((c->xbit[y][x] & XBIT_POWERED) && reservoir)
            {
                c->xbit[y][x] |= XBIT_WATERED;
                reservoir--; /* spends stored water */
            }
        }
        else if (bld == 0xF4 || bld == 0xDC || bld == 0xFA)
        {
            /*  $21C5E, reached from three tests at $21C22/$21C28/$21C2E:
             *  Water Treatment, Pump and Desalinization are watered
             *  whenever they have power, and cost nothing from the
             *  budget.  That is what keeps every source watered, so the
             *  outer sweep skips them and each network floods once. */
            if (c->xbit[y][x] & XBIT_POWERED)
                c->xbit[y][x] |= XBIT_WATERED;
        }
        else if (budget)
        { /* $21C84 */
            c->xbit[y][x] |= XBIT_WATERED;
            if (bld >= BLD_ZONE_FIRST)
                budget--;
        }
        c->xbit[y][x] &= (uint8_t)~XBIT_VISITED; /* $21CC4 */
        push_marked(c, y, x);
    }
}

/*  $2156E.  The sweep order genuinely depends on the map rotation --
 *  the original branches four ways on g_rotation and runs a different
 *  loop nest for each.  Rotation 0 is column-major, shown here.  Order
 *  decides which source claims a shared network and, since water is
 *  rationed in queue order, which tiles run dry.
 */
void sim_water_grid(City *c)
{
    int32_t capacity = 0, met = 0;
    int     y, x;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            if (c->xbld[y][x] != BLD_RESERVOIR) /* $2159A */
                c->xbit[y][x] &= (uint8_t)~XBIT_WATERED;
            c->xbit[y][x] &= (uint8_t)~XBIT_VISITED;
        }

    /*  All four sweeps, $215F8 / $21674 / $216EC / $2175E.  They apply
     *  identical tests and differ only in iteration order, which is not
     *  cosmetic: water is rationed in queue order, so the order decides
     *  which source claims a shared network and which tiles run dry.
     *      rot 0  col 0..127 outer, row 0..127 inner
     *      rot 1  row 0..127 outer, col 127..0 inner
     *      rot 2  col 127..0 outer, row 127..0 inner
     *      rot 3  row 127..0 outer, col 0..127 inner
     */
    {
        int rot = c->rotation & 3, i, j;
        for (i = 0; i < MAP_W; i++)
        {
            for (j = 0; j < MAP_H; j++)
            {
                switch (rot)
                {
                    case 0:
                        x = i;
                        y = j;
                        break;
                    case 1:
                        y = i;
                        x = MAP_W - 1 - j;
                        break;
                    case 2:
                        x = MAP_W - 1 - i;
                        y = MAP_H - 1 - j;
                        break;
                    default:
                        y = MAP_H - 1 - i;
                        x = j;
                        break;
                }
                {
                    uint8_t bld = c->xbld[y][x];
                    if (c->xbit[y][x] & XBIT_WATERED)
                        continue;
                    if (bld != 0xDC && bld != 0xFA)
                        continue; /* Pump, Desalinization */
                    if (!(c->xbit[y][x] & XBIT_POWERED))
                        continue;
                    water_flood(c, y, x, &capacity, &met);
                }
            }
        }
    }

    /*  $217D2 -- water treatment, and it has to happen here, before
     *  water_pct is rescaled, because the comparison is against the raw
     *  delivered total rather than the percentage.  Each Water Treatment
     *  plant covers two thousand units of what the network actually
     *  delivers, and the building is 2x2, so the census tile count is
     *  divided by four to count plants.  Once the plants cover the whole
     *  delivery the flag goes up, and $23308 adds it to the pollution blur
     *  divisor -- a larger divisor being how treatment shows up as less
     *  pollution.  Leaving this out costs nothing until the water phase
     *  runs before the scan, which is the order the clock uses ($220DA then
     *  $21FB0).  Driving the scan on its own hides it entirely. */
    c->misc[1043] =
        (uint32_t)((uint32_t)(c->census[0xF4] >> 2) * 2000u) >= (uint32_t)met
            ? 1
            : 0;

    /* $217FE: like power, this is a load meter -- what fraction of the
     * network's capacity is being drawn -- and 100 when there is none. */
    c->water_capacity = capacity; /* A5+0x11D2 */
    c->water_pct      = capacity ? met * 100 / capacity : 100;
}

/* ================================================================== *
 *  Phase 19 -- traffic.  $2530E both decays the layer and totals it,
 *  which is why a saved city's MISC traffic figure is a snapshot from
 *  the last time this ran rather than the sum of the layer on disk.
 * ================================================================== */
void sim_traffic_total(City *c)
{
    int y, x;
    c->traffic_tot = 0;
    for (y = 0; y < HALF_H; y++)
        for (x = 0; x < HALF_W; x++)
        {
            int v         = c->xtrf[y][x];
            v             = v - ASR(v, 2); /* $25336: decay 25% */
            c->xtrf[y][x] = (uint8_t)v;
            c->traffic_tot += v;
        }
}

/*  $23302 computes the blur divisor as 4 - A5+0x2C8A + A5+0x2CA0, plus
 *  one when ordinance bit 0x80000 is set.  All three inputs are in the
 *  save -- MISC[1037], MISC[1043] and MISC[1000] -- so the whole thing
 *  is derived here rather than passed in, which is how an earlier
 *  version managed to drop the ordinance term silently.               */
void sim_pollution(City *c)
{
    int y2, x2;
    int divisor_base = 4 - (int)c->misc[1037] + (int)c->misc[1043];
    if (c->ordinances & 0x80000)
        divisor_base++; /* $2331C */

    /* --- stage 1: raw pollution per half-resolution cell ---------- */
    for (y2 = 0; y2 < HALF_H; y2++)
    {
        for (x2 = 0; x2 < HALF_W; x2++)
        {
            int     y = y2 * 2, x = x2 * 2, i;
            int32_t acc = c->xplt[y2][x2] + c->xtrf[y2][x2] / 5; /* $231CE */

            for (i = 0; i < 4; i++)
            { /* the 2x2 tile block */
                int     ty = y + (i & 1), tx = x + (i >> 1);
                uint8_t b = c->xbld[ty][tx];
                if (b >= 0x70)
                    acc += BUILDING[b].pollution; /* $231FC */
                if (b == 5)
                    acc += 200; /* $2320E, id 5 unidentified */
            }
            pad[y2][x2] = (int16_t)acc;
        }
    }

    /* --- stage 2: 5-point blur, centre weighted twice ------------- */
    c->pollution_tot = 0;
    for (y2 = 0; y2 < HALF_H; y2++)
    {
        for (x2 = 0; x2 < HALF_W; x2++)
        {
            int32_t sum = (int32_t)pad[y2][x2] * 2;
            int     div = divisor_base < 1 ? 1 : divisor_base;
            int32_t v;

            if (y2 > 0)
            {
                sum += pad[y2 - 1][x2];
                div++;
            }
            if (y2 < HALF_H - 1)
            {
                sum += pad[y2 + 1][x2];
                div++;
            }
            if (x2 > 0)
            {
                sum += pad[y2][x2 - 1];
                div++;
            }
            if (x2 < HALF_W - 1)
            {
                sum += pad[y2][x2 + 1];
                div++;
            }

            v = sum / div; /* $233E8 */
            if (v > 255)
                v = 255; /* $233F0 */
            c->xplt[y2][x2] = (uint8_t)v;
            c->pollution_tot += v; /* $23412 */
        }
    }
}

/* ================================================================== *
 *  Phase 21 -- population.  $33FAE, short enough to read whole.
 * ================================================================== */
void sim_population(City *c)
{
    int i;

    c->accum8[0] = 0;
    for (i = 1; i <= 6; i++)
        c->accum8[0] += c->accum8[i]; /* $33FC0 */

    c->pop_increase = 0;
    c->pop_decrease = 0;
    {
        int32_t newpop = c->accum8[0] * 10; /* $33FE6 */
        if (newpop < c->population)
            c->pop_decrease = c->population - newpop;
        else
            c->pop_increase = newpop - c->population;
        c->population = newpop;
        c->misc[16] += newpop; /* $3402E, A5+0x1E22 */
    }

    /*  $34032 -- the residential, commercial and industrial split.
     *  accum8 holds six zone accumulators and the three figures are
     *  consecutive pairs of them, which is why their sum is accum8[0]
     *  and why graph series 0 equals the sum of series 1 to 3. */
    for (i = 0; i < 3; i++)
        c->rci_pop[i] = c->accum8[2 * i + 1] + c->accum8[2 * i + 2];

    /*  $345DA -- the three zone departments' tax base is that split times
     *  ten, a plain integer multiply.  The SANE sequence just above it in
     *  $33FAE looks like it feeds this and does not: its result goes to a
     *  local array at -$18(a6), and $345E0 overwrites the same scratch long
     *  with rci_pop[i] * 10 before the store.  What the float sequence
     *  actually computes is a growth ratio, (previous / (rci_pop[i] + 1)) -
     *  1, kept for the demand model.  That part is not ported. */
    for (i = 0; i < 3; i++)
        c->dept[i].amount = c->rci_pop[i] * 10;

    /*  $34602 -- and a share of the same ordinance term the graph uses,
     *  a sixth to residential and a twelfth to the other two. */
    c->dept[0].amount += c->misc[MISC_2C98] / 6;
    c->dept[1].amount += c->misc[MISC_2C98] / 12;
    c->dept[2].amount += c->misc[MISC_2C98] / 12;

    sim_demand(c);
}

/* ================================================================== *
 *  $34068 .. $34790 -- the demand model, the rest of populationPass.
 *
 *  For each of the three zone kinds the routine works out how much
 *  room there is, divides that by how much is already built, and moves
 *  the demand figure by the shortfall.  Two things shape it: what the
 *  city can support, and what the tax rate costs.
 *
 *  The arithmetic is SANE single precision throughout.  Every step is
 *  taken back to a float rather than staying extended, so the chain of
 *  conversions below is the calculation, not ceremony around it --
 *  folding it into double arithmetic gives different answers.
 *
 *  One faithful oddity: the original builds its 64-bit comp operands as
 *  a cleared high long and the value in the low long, so a negative
 *  would convert as a large positive.  Every value here is a population
 *  or a building count, so it never arises.
 * ================================================================== */
static ext80 z2x_i16(int16_t v) { return ext_from_i16(v); }

static ext80 z2x_i32(int32_t v) { return ext_from_i32(v); }

static float x2z_sgl(ext80 a) { return (float)ext_to_double(a); }

static ext80 z2x_sgl(float f) { return ext_from_float(f); }

void sim_demand(City *c)
{
    /*  room[i] is how much of each zone the city could carry; the
     *  original keeps it as three singles at -$c(a6). */
    float   room[3], ratio[3];
    int32_t cap;
    int     i;

    /*  $34068 -- the workforce, and last month's residential head count
     *  over it.  A5+0x2C7E holds the previous month's figure and is
     *  replaced with this month's at $34110, so the ratio always looks
     *  one month back. */
    const float jobs  = x2z_sgl(z2x_i32(c->rci_pop[1] + c->rci_pop[2]));
    const float share = x2z_sgl(ext_div(z2x_sgl(x2z_sgl(z2x_i32(c->misc[29]))),
                                        ext_add(ext_from_i32(1), z2x_sgl(jobs))));
    c->misc[29]       = c->rci_pop[0]; /* $34110 */

    /*  $34114 -- the city's own size against a fixed 150000, and the
     *  difficulty multiplier with a hundredth of A5+0x2C8C added. */
    const float growth =
        x2z_sgl(ext_div(z2x_sgl(x2z_sgl(z2x_i32(c->population + 50000))),
                        ext_from_i32(150000)));
    const int   lvl = (c->difficulty >= 0 && c->difficulty < 4) ? c->difficulty : 0;
    const float level =
        x2z_sgl(ext_add(z2x_sgl(DEMAND_LEVEL[lvl]),
                        ext_div(z2x_i16((int16_t)c->misc[1036]),
                                ext_from_i32(100))));

    /*  $34254 -- residential room is the workforce plus a fiftieth of
     *  the residents themselves. */
    room[0] = x2z_sgl(ext_add(z2x_sgl(jobs),
                              z2x_sgl(x2z_sgl(z2x_i32(c->rci_pop[0] / 50)))));

    /*  $34284 -- commerce and industry both scale the industrial head
     *  count by that jobs share, then by their own multiplier. */
    {
        const ext80 base =
            ext_mul(z2x_sgl(x2z_sgl(z2x_i32(c->rci_pop[2]))), z2x_sgl(share));
        room[1] = x2z_sgl(ext_mul(z2x_sgl(growth), base));
        room[2] = x2z_sgl(ext_mul(z2x_sgl(level), base));
    }
    if (room[2] < 15.0f)
        room[2] = 15.0f; /* $34362 */

    /*  $3436A -- and then four ceilings, which is where the city's own
     *  buildings come in.  Residential is held down by how much there
     *  is to do: stadium, marina, zoo, and a third of the parks. */
    {
        uint16_t t = (uint16_t)(10 + c->census[0xD7] + c->census[0xF8] +
                                c->census[0xDA] + (uint16_t)(c->census[0xD5] / 3));
        cap        = (int32_t)t * 1500 / 10;
        if (room[0] > (float)cap)
            room[0] = (float)cap; /* $343E8 */
    }
    /*  $343EE -- and by how much commerce there is to work in */
    cap = c->rci_pop[1] * 4 + 500;
    if (room[0] > (float)cap)
        room[0] = (float)cap; /* $34446 */

    /*  $3444C -- commerce is held down by the airport: runway tiles
     *  plus the counter at A5+0x2C96. */
    {
        uint16_t t = (uint16_t)(c->census[0xDD] + c->census[0xDE] +
                                (uint16_t)c->misc[1044]);
        cap        = (int32_t)(uint16_t)(t / 5 + 1) * 15000 / 10;
        if (room[1] > (float)cap)
            room[1] = (float)cap; /* $344C6 */
    }
    /*  $344CC -- and industry by the seaport: cranes plus A5+0x2C94. */
    {
        uint16_t t =
            (uint16_t)(c->census[0xE0] + 1 + (uint16_t)c->misc[1033]);
        cap = (int32_t)t * 15000 / 10;
        if (room[2] > (float)cap)
            room[2] = (float)cap; /* $34538 */
    }

    /*  $34544 -- room against what is already there.  A ratio of zero
     *  means the city is exactly as full as it can be. */
    for (i = 0; i < 3; i++)
        ratio[i] = x2z_sgl(ext_add(ext_from_i32(-1),
                                   ext_div(z2x_sgl(room[i]),
                                           z2x_sgl(x2z_sgl(z2x_i32(
                                               c->rci_pop[i] + 1))))));

    /*  $34644 -- and the month's move: six hundred times the shortfall,
     *  plus whatever the tax rate is worth.  Ordinances nudge the rate
     *  the table is read at rather than the demand itself. */
    for (i = 0; i < 3; i++)
    {
        int32_t f = c->dept[i].funding;
        int32_t v;

        if (i == 0)
        { /* $3465A */
            if (c->ordinances & (1L << 1))
                f++;
            if (c->ordinances & (1L << 14))
                f--;
        }
        else if (i == 1)
        { /* $34676 */
            if (c->ordinances & (1L << 0))
                f++;
            if (c->ordinances & (1L << 12))
                f--;
            if (c->ordinances & (1L << 15))
                f--;
            if (c->ordinances & (1L << 18))
                f--;
        }
        else
        { /* $346A6 */
            if (c->ordinances & (1L << 13))
                f--;
            if (c->ordinances & (1L << 19))
                f++;
        }
        if (f < 0)
            f = 0; /* $346BE */
        /*  the original indexes the table with no upper bound; funding
         *  never reaches the end of it, and stopping there is safer
         *  than reading past the table in C. */
        if (f > 23)
            f = 23;

        {
            ext80 acc = z2x_i16(c->rci_demand[i]); /* $34728 */
            ext80 mv  = ext_mul(ext_from_i32(600), z2x_sgl(ratio[i]));
            mv        = ext_add(mv, z2x_sgl(x2z_sgl(z2x_i16(DEMAND_TAX[f]))));
            acc       = ext_add(acc, mv); /* $3473C */
            v         = ext_to_i32(acc);  /* $34746 truncates */
        }
        /*  $3475A keeps only the low word of the long */
        c->rci_demand[i] = (int16_t)v;
        if (c->rci_demand[i] < -2000)
            c->rci_demand[i] = -2000; /* $3476C */
        else if (c->rci_demand[i] > 2000)
            c->rci_demand[i] = 2000; /* $34784 */
    }

    sim_forest(c);
    sim_news_rolls(c);
    sim_weather(c);
}

/* ================================================================== *
 *  $34792 -- the forest, still inside populationPass.
 *
 *  One tile a month, chosen at random, and then its neighbour in a
 *  random direction.  Trees advance a stage; bare ground that is not
 *  rubble puts out a sapling.  Water grows nothing, and anything above
 *  a tree is left alone.
 *
 *  It is small and it is why a city left alone slowly turns green
 *  again.  It also draws four to six dice a month, which is enough to
 *  put every later roll out of step if it is missing.
 * ================================================================== */
#define TREE_FIRST 6  /* $3480E, the first forest stage */
#define TREE_LAST  11 /* $3482E, the last one that can advance */
#define RUBBLE     5  /* $347D8 */

void sim_forest(City *c)
{
    int32_t y = (int32_t)(Random() & 0xFFFF) % 128; /* $34794 */
    int32_t x = (int32_t)(Random() & 0xFFFF) % 128; /* $347AE */
    int32_t b = c->xbld[y][x];

    /*  $347D8 -- rubble clears itself one time in sixteen */
    if (b == RUBBLE && (Random() & 0xF) == 0)
        sim_set_tile(c, (int)y, (int)x, 0);

    if (c->xbit[y][x] & XBIT_WATER) /* $34804 */
        return;

    /*  $3480E -- a tile that is not already forest only gets a turn one
     *  time in sixteen */
    if ((b < TREE_FIRST || b > 13) && (Random() & 0xF) != 0)
        return;

    if (b >= TREE_FIRST && b < 12) /* $34828 */
        sim_set_tile(c, (int)y, (int)x, (uint8_t)(b + 1));

    /*  $34846 -- and now the neighbour, one step in one direction, the
     *  map edge simply refusing to move */
    switch (Random() & 3)
    {
        case 0:
            if (y < 127)
                y++;
            break;
        case 1:
            if (y > 0)
                y--;
            break;
        case 2:
            if (x < 127)
                x++;
            break;
        default:
            if (x > 0)
                x--;
            break;
    }

    b = c->xbld[y][x];
    if (c->xbit[y][x] & XBIT_WATER) /* $348A0 */
        return;
    if (b >= 12 || b == RUBBLE) /* $348A8, $348AE */
        return;
    if (b >= TREE_FIRST)
        sim_set_tile(c, (int)y, (int)x, (uint8_t)(b + 1)); /* $348BE */
    else
        sim_set_tile(c, (int)y, (int)x, TREE_FIRST); /* $348CE */
}

/* ================================================================== *
 *  $348F0 -- the newspaper's dice.
 *
 *  Every roll here only decides whether a headline is printed, and the
 *  headlines are interface.  The rolls themselves are not optional: the
 *  original draws between fourteen and eighteen numbers a month here,
 *  and a stream that skips them puts every later roll in the month out
 *  of step.  The same lesson as the train horn, at a larger scale.
 *
 *  So the shape is kept and the messages are dropped, with one piece of
 *  real state: $34BF4 walks the seventeen deadlines at A5+0x1E4C and
 *  clears any the calendar has passed.
 * ================================================================== */
void sim_news_rolls(City *c)
{
    int32_t r;
    int     i;

    /*  $348F0 -- one of six openings, and only the first rolls again */
    if ((int32_t)(Random() & 0xFFFF) % 6 == 0)
    {
        Random(); /* $34920 */
        Random(); /* $3493A */
    }

    /*  $349A4 -- the stadium's own headline, drawn only when there is
     *  a stadium to write about */
    if (c->census[0xD7] != 0)
        Random(); /* $349B0 */

    /*  $349DC .. $34AFC -- eight rolls against four of the map-view
     *  averages, a coarse one and a fine one each */
    Random(); /* $349E6 traffic   & 0x7F */
    Random(); /* $34A10 traffic   & 0x0F */
    Random(); /* $34A3A pollution & 0x7F */
    Random(); /* $34A64 pollution & 0x0F */
    Random(); /* $34A8E crime     & 0x7F */
    Random(); /* $34AB8 crime     & 0x0F */
    Random(); /* $34ADA jobless   & 0x3F */
    Random(); /* $34AFC jobless   & 0x03 */

    /*  $34B1C and $34B80 -- education and health each take one roll,
     *  and which of the two branches runs decides what it is compared
     *  against rather than whether it happens. */
    Random(); /* $34B26 or $34B5A */
    Random(); /* $34B8A or $34BBE */

    /*  $34BE6 -- one time in eight the deadlines are looked at */
    r = Random() & 7;
    if (r == 0)
        for (i = 0; i < 17; i++)
        {
            /*  MISC[462..478] is A5+0x1E4C, seventeen years by which
             *  something has to be done.  Zero means no deadline. */
            int32_t year = c->misc[462 + i];
            if (year == 0)
                continue;
            if (c->year_founded + c->years < year) /* $34C10 */
                continue;
            c->misc[462 + i] = 0; /* $34C44 */
            break;                /* $34C4E */
        }
}

/* ================================================================== *
 *  $34C58 -- the weather.
 *
 *  Twelve states, and each month the weather steps to one of eight
 *  successors drawn from a table that changes with the season.  Cloud,
 *  wind and temperature then move half way toward what the new state
 *  calls for, so the weather drifts rather than jumps.
 *
 *  This is not scenery.  The wind plant turns weather1 into output and
 *  the solar plant reads weather2, so the weather is why a city's
 *  generating capacity is never quite the same two months running.
 * ================================================================== */
void sim_weather(City *c)
{
    /*  $15256 -- the season is the month shifted one and divided by
     *  three, so December, January and February share one set. */
    const int season = (int)(((c->month + 1) % 12) / 3);
    int       st     = c->weather_state;
    int       roll;

    if (st < 0 || st >= 12)
        st = 0;
    roll             = (int)(Random() & 7); /* $34C74 */
    st               = WEATHER_NEXT[season * 96 + st * 8 + roll];
    c->weather_state = (int16_t)st;
    c->misc[27]      = st;

    /*  $34C8A -- half way toward the new state, three times over. */
    c->weather2            = (int16_t)((c->weather2 + WEATHER_CLOUD[st]) / 2);
    c->weather1            = (int16_t)((c->weather1 + WEATHER_WIND[st]) / 2);
    c->temperature         = (int16_t)((c->temperature + WEATHER_TEMP[st]) / 2);
    c->misc[MISC_WEATHER2] = c->weather2;
    c->misc[MISC_WEATHER1] = c->weather1;
    c->misc[24]            = c->temperature;
}

/* ================================================================== *
 *  The population model, applied to a map rather than accumulated over
 *  a cycle.  $3170E credits a tile only at the corner selected by the
 *  current rotation, so a multi-tile building counts once; the amount
 *  is GROWTH_TABLE indexed by the building's tier ($31DDA), and $33FAE
 *  multiplies the total by ten.
 * ================================================================== */
int32_t sim_map_population(const City *c)
{
    int     y, x;
    int32_t units = 0;
    uint8_t mask  = (uint8_t)ROT_CORNER_MASK[c->rotation & 3];

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            uint8_t z = c->xzon[y][x], b = c->xbld[y][x];
            int     zone = XZON_TYPE(z), tier;

            if (zone < ZONE_RES_LIGHT || zone > ZONE_IND_DENSE)
                continue;
            if (b < BLD_ZONE_FIRST || b > BLD_ZONE_LAST)
                continue;
            if (!(XZON_CORNERS(z) & mask))
                continue;

            tier = BUILDING[b].tier;
            if (tier > 0 && !BUILDING[b].tier_flag)
                units += GROWTH_TABLE[tier];
        }
    return units * 10;
}

/* ================================================================== *
 *  $224BA  overlayAverages -- the four numbers under the map views.
 *
 *  These are not standalone globals.  A5+0x2BDC is XGRP, a table of
 *  sixteen pointers, one per graph series, and A5+0x2BEC, 0x2BF0,
 *  0x2BF4 and 0x2BF8 are entries 4 to 7 of it.  What this routine
 *  stores is the newest sample of the traffic, pollution, land value
 *  and crime series; the map views and the ambient rolls at $9E76 read
 *  that sample.
 *
 *  It is one step of graphHistoryPass ($22330), which shifts every
 *  series back a slot before this runs and maintains the graph scales
 *  after it.  Call sim_graph_pass for the whole month; this entry
 *  point stays because the four values are read on their own.
 *
 *  Traffic divides by three transport departments' `amount`; the other
 *  three divide by a quarter of the developed-tile count.  Both
 *  divisors add one, so an empty city divides by one rather than
 *  faulting.
 *
 *  The driver at $9E76 reads two of these to decide whether to roll for
 *  an ambient sound, which is the only place the simulation reads them
 *  back at all.
 * ================================================================== */
void sim_overlay_averages(City *c)
{
    /*  $224C0 -- departments 10, 11 and 12, field +0x60 */
    const int32_t roads = c->dept[10].amount + c->dept[11].amount +
                          c->dept[12].amount + 1; /* $224CC */
    const int32_t d     = c->developed;
    int32_t       n;

    c->graph[GRAPH_TRAFFIC][0] =
        (int32_t)((uint32_t)c->traffic_tot / (uint32_t)roads);

    /*  $224E0 -- d/4 rounded toward zero, then +1.  The shift dance is
     *  how THINK C divides a signed word by four. */
    n = ((d + ((d >> 1 >> 8 >> 6) & 3)) >> 2) + 1;

    c->graph[GRAPH_POLLUTION][0] =
        (int32_t)((uint32_t)c->pollution_tot / (uint32_t)n);
    c->graph[GRAPH_VALUE][0] =
        (int32_t)((uint32_t)c->land_value_tot / (uint32_t)n);
    c->graph[GRAPH_CRIME][0] =
        (int32_t)((uint32_t)c->crime_tot / (uint32_t)n);
}

/*  $2233C, $2263E and $226A8 -- shift a band up by one, oldest first,
 *  leaving `first` free for a new sample. */
static void graph_shift(int32_t *series, int first, int count)
{
    int k;

    for (k = first + count - 1; k > first; k--)
        series[k] = series[k - 1];
}

/*  $22490 onward -- the running maximum, always an unsigned compare. */
static int32_t graph_rise(int32_t have, int32_t sample)
{
    return (uint32_t)sample > (uint32_t)have ? sample : have;
}

void sim_graph_pass(City *c)
{
    const int32_t m = c->misc[MISC_2C98];
    int32_t       arcos, bonus, scale, jobless;
    int           i;

    /*  $2233C -- every series gives up its oldest month */
    for (i = 0; i < N_GRAPH; i++)
        graph_shift(c->graph[i], GRAPH_MONTH, GRAPH_N_MONTH);

    /*  $22370 -- the four arcology counts are tile counts and an
     *  arcology covers sixteen tiles, so >> 4 counts buildings.  The
     *  original adds them in sixteen bits, so the sum can wrap. */
    arcos = (uint16_t)(c->census[0xFB] + c->census[0xFC] +
                       c->census[0xFD] + c->census[0xFE]) >>
            4;
    bonus = arcos > 140 ? (arcos - 140) * 20000 : 0; /* $22396 */

    /*  $223B0 -- city size, then the three zone series.  The shift
     *  dances at $223CA and $22408 are signed divides by two and by
     *  four, which C rounds toward zero the same way. */
    c->graph[GRAPH_CITY_SIZE][0] = c->accum8[0] * 10 + m + bonus;
    c->graph[GRAPH_RESIDENTS][0] = c->rci_pop[0] * 10 + m / 2 + bonus / 2;
    c->graph[GRAPH_COMMERCE][0]  = c->rci_pop[1] * 10 + m / 4 + bonus / 4;
    c->graph[GRAPH_INDUSTRY][0]  = c->rci_pop[2] * 10 + m / 4 + bonus / 4;

    /*  $22490 -- the four population series share one scale, and it is
     *  city size alone that can push it up. */
    scale = graph_rise(c->graph_max[GRAPH_CITY_SIZE],
                       c->graph[GRAPH_CITY_SIZE][0]);
    for (i = GRAPH_CITY_SIZE; i <= GRAPH_INDUSTRY; i++)
        c->graph_max[i] = scale;

    /*  $224BA -- traffic, pollution, land value and crime */
    sim_overlay_averages(c);

    /*  $2252A -- the four map overlays share a scale too, and here any
     *  of the four can raise it. */
    scale = 0;
    for (i = GRAPH_TRAFFIC; i <= GRAPH_CRIME; i++)
    {
        scale = graph_rise(scale, c->graph_max[i]);
        scale = graph_rise(scale, c->graph[i][0]);
    }
    for (i = GRAPH_TRAFFIC; i <= GRAPH_CRIME; i++)
        c->graph_max[i] = scale;

    /*  $2257C -- coverage is stored as the share supplied, so the
     *  globals hold the shortfall. */
    c->graph[GRAPH_POWER][0] = 100 - c->power_pct;
    c->graph[GRAPH_WATER][0] = 100 - c->water_pct;

    /*  $22594 -- the two age-weighted scores.  The original writes
     *  education first. */
    c->graph[GRAPH_EDUCATION][0] = c->misc[MISC_AGE_W90];
    c->graph[GRAPH_HEALTH][0]    = c->misc[MISC_AGE_W65];

    /*  $225A4 -- the jobless share, kept at A5+0x2C82 as well because
     *  the economy reads it back.  The +1 keeps an empty city from
     *  dividing by zero. */
    jobless                         = (int32_t)(((uint32_t)c->accum8[7] * 100u) /
                                                (uint32_t)(c->accum8[0] + c->accum8[7] + 1));
    c->unemployment                 = jobless;
    c->graph[GRAPH_UNEMPLOYMENT][0] = jobless;

    /*  $225CE -- the three national figures */
    c->graph[GRAPH_NAT_GNP][0]  = c->misc[MISC_NAT_INDEX2];
    c->graph[GRAPH_NAT_POP][0]  = c->misc[MISC_NAT_INDEX];
    c->graph[GRAPH_FED_RATE][0] = (int16_t)c->misc[MISC_NAT_MOOD];

    /*  $225E8 -- the remaining eight each keep their own scale */
    for (i = GRAPH_POWER; i < N_GRAPH; i++)
        c->graph_max[i] = graph_rise(c->graph_max[i], c->graph[i][0]);

    /*  $22616 -- except that GNP is drawn against national population
     *  whenever that is the taller of the two. */
    c->graph_max[GRAPH_NAT_GNP] =
        graph_rise(c->graph_max[GRAPH_NAT_GNP], c->graph_max[GRAPH_NAT_POP]);

    /*  $2262C -- January and July move the half-yearly band, and the
     *  new slot takes a copy of this month rather than its own
     *  reading. */
    if (c->month == 0 || c->month == 6)
        for (i = 0; i < N_GRAPH; i++)
        {
            graph_shift(c->graph[i], GRAPH_HALFYEAR, GRAPH_N_HALFYEAR);
            c->graph[i][GRAPH_HALFYEAR] = c->graph[i][0];
        }

    /*  $2268E -- every fifth January the five-yearly band moves too */
    if (c->month == 0 && c->years % 5 == 0)
        for (i = 0; i < N_GRAPH; i++)
        {
            graph_shift(c->graph[i], GRAPH_FIVEYEAR, GRAPH_N_FIVEYEAR);
            c->graph[i][GRAPH_FIVEYEAR] = c->graph[i][0];
        }
}

/*  $23432 -- the city centre is the mean row and column of every tile
 *  carrying a developed building.  It also seeds the scratch and clears
 *  the flood bit that stage 4 reuses as a half-res mask. */
void sim_city_centre(City *c, int *cy, int *cx)
{
    int32_t sy = 0, sx = 0, n = 1;
    int     y, x;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            if (c->xbld[y][x] < BLD_ZONE_FIRST)
                continue;
            sy += y;
            sx += x;
            n++;
            pad[y][x] = 0x28;                        /* $234A8 */
            c->xbit[y][x] &= (uint8_t)~XBIT_VISITED; /* $234C6 */
        }
    /*  $234EA divides by twice the count, and the value kept in a2/a3
     *  -- the one the distance term uses -- is that half-resolution
     *  quotient.  Only the copy written to the globals at $23508 is
     *  doubled, for MISC[1030]/[1031].  Using the doubled value in the
     *  distance term is wrong and quietly halves every centrality
     *  bonus in the city. */
    n *= 2; /* $234EA */
    *cy = (int)((uint32_t)sy / (uint32_t)n);
    *cx = (int)((uint32_t)sx / (uint32_t)n);
}

/*  stage 4 ($2351A) builds the two planes and marks, at half resolution,
 *  which cells carry something developed or zoned. */
static void build_planes(City *c)
{
    int y, x;
    /*  $2351A clears the developed count before the loop that raises
     *  it at $23614.  The count is saved (MISC[1067]) and restored, so
     *  without this clear the scan adds a second city's worth to the
     *  first and the three averages that divide by it come out half. */
    c->developed = 0;
    for (y = 0; y < MAP_H; y++)
    {
        int hy = y / 2, qy = hy / 2;
        for (x = 0; x < MAP_W; x++)
        {
            int     hx = x / 2, qx = hx / 2;
            uint8_t b = c->xbld[y][x], t;
            int16_t a = pad[qy][qx], w = pad[qy + 32][qx];

            if (b == 0)
            { /* $23582 */
                if (c->xbit[y][x] & XBIT_WATER)
                {
                    a += 12;
                    w += 12;
                }
                else
                    a += 4;
            }
            else if (b == 0xD5)
                a += 0x28; /* SimPark  */
            else if (b >= 6 && b < 14)
                a += 0x14; /* trees    */
            else if (b <= 5)
                a -= 0x14; /* rubble   */

            if (b >= 0x1D || XZON_TYPE(c->xzon[y][x]))
            {
                c->xbit[hy][hx] |= XBIT_VISITED; /* $2360E half-res mask */
                c->developed++;                  /* $23614 */
            }
            if (c->xbit[y][x] & XBIT_WATERED)
            {
                a += 4;
                w += 4;
            }

            t = c->xter[y][x];
            if (t != 0 && t < 0x10)
                a += 12; /* $2365A */

            pad[qy][qx]      = a;
            pad[qy + 32][qx] = w;
        }
    }
}

/*  a 5-point mean over one of the 32x32 planes */
static int32_t stencil(int plane_row0, int qy, int qx)
{
    int32_t s = pad[plane_row0 + qy][qx];
    int     n = 1;
    if (qy > 0)
    {
        s += pad[plane_row0 + qy - 1][qx];
        n++;
    }
    if (qy < 31)
    {
        s += pad[plane_row0 + qy + 1][qx];
        n++;
    }
    if (qx > 0)
    {
        s += pad[plane_row0 + qy][qx - 1];
        n++;
    }
    if (qx < 31)
    {
        s += pad[plane_row0 + qy][qx + 1];
        n++;
    }
    return s / n;
}

/*  stage 5 ($236E4) turns the planes into XVAL.  Commercial reads plane
 *  A and gets the full centrality bonus plus a lift from population
 *  density; residential reads plane A at half the bonus; industry reads
 *  plane B, the water plane, at a quarter.  Each subtracts pollution and
 *  crime at its own weight, which is where the character of the three
 *  zone types actually lives.
 */
void sim_land_value(City *c)
{
    int cy, cx, hy, hx;

    /*  The shared plane is deliberately NOT cleared here.  Stage 4 seeds
     *  its two accumulators from whatever is already in it -- stage 1's
     *  raw, unblurred pollution, overwritten at building tiles by
     *  stage 3's marks -- so land value depends on an intermediate that
     *  no save file records.  Run after sim_pollution, as $2317E runs
     *  its stages, that intermediate is present and land value comes
     *  out exact; run on a bare save it cannot, and the gap between the
     *  two numbers in the report is the size of what the file lost. */
    sim_city_centre(c, &cy, &cx);
    /*  $23506 doubles both before storing them: sim_city_centre
     *  hands back the half-resolution centre, which is what the
     *  land value stage wants, and the globals hold it in whole
     *  tiles, which is what the disasters want. */
    c->centre_y = (int16_t)(cy * 2); /* $23508 */
    c->centre_x = (int16_t)(cx * 2); /* $23510 */
    build_planes(c);

    c->land_value_tot = 0;
    for (hy = 0; hy < HALF_H; hy++)
    {
        int qy = hy / 2;
        for (hx = 0; hx < HALF_W; hx++)
        {
            int     qx = hx / 2, zone, dist;
            int32_t v;
            uint8_t b;

            if (!(c->xbit[hy][hx] & XBIT_VISITED))
            { /* $2371E */
                c->xval[hy][hx] = 0;
                continue;
            }
            zone = XZON_TYPE(c->xzon[2 * hy][2 * hx]);
            if (zone == 0)
                zone = XZON_TYPE(c->xzon[2 * hy + 1][2 * hx + 1]);

            dist = (cy > hy ? cy - hy : hy - cy) + (cx > hx ? cx - hx : hx - cx);

            if (zone == 5 || zone == 6)
            { /* industrial */
                v = stencil(32, qy, qx);
                if (zone == 6)
                    v += 0x15;
                if (64 - dist > 0)
                    v += (64 - dist) / 4;
                v -= c->xplt[hy][hx] / 16;
                v -= c->xcrm[hy][hx] / 4;
            }
            else if (zone == 3 || zone == 4)
            { /* commercial */
                v = stencil(0, qy, qx);
                if (64 - dist > 0)
                    v += 64 - dist;
                v -= c->xplt[hy][hx] / 4;
                v -= c->xcrm[hy][hx] / 3;
                v += c->xpop[qy][qx] / 3;
            }
            else
            { /* residential */
                v = stencil(0, qy, qx);
                if (c->xpop[qy][qx] < 0x40)
                    v += 0x15; /* $23B08 */
                if (64 - dist > 0)
                    v += (64 - dist) / 2;
                v -= c->xplt[hy][hx] / 5;
                v -= c->xcrm[hy][hx] / 3;
            }

            b = c->xbld[2 * hy][2 * hx]; /* $23B96 */
            if (b >= BLD_ZONE_FIRST && BUILDING[b].tier_flag == 2)
                v -= v / 2;
            if (v > 255)
                v = 255;
            else if (v < 0)
                v = 0;
            c->xval[hy][hx] = (uint8_t)v;
            c->land_value_tot += v;
        }
    }
}

/* ================================================================== *
 *  $2317E stage 9 ($23FAE) -- crime.
 *
 *  Every input is produced earlier in the same pass, so a saved city
 *  holds exactly the values this stage saw.  That makes crime, unlike
 *  pollution and land value, reproducible from a save.
 *
 *      crime = density - police/2 - landValue/4  (+16 under one
 *              ordinance, which raises crime rather than lowering it)
 *
 *  then a 5-point blur, clamped to a byte.  Cells the developed mask
 *  does not cover are zeroed outright.
 * ================================================================== */
void sim_crime(City *c)
{
    static uint8_t mask[HALF_H][HALF_W];
    int            hy, hx, y, x;

    /*  Stage 4 marks this mask into XBIT bit 3, but that bit is also the
     *  flood-fill scratch and both flood passes clear it, so whether a
     *  save still carries it depends on when the save happened.  Derive
     *  it from the map instead -- same condition, $235DA. */
    memset(mask, 0, sizeof mask);
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            if (c->xbld[y][x] >= 0x1D || XZON_TYPE(c->xzon[y][x]))
                mask[y / 2][x / 2] = 1;

    for (hy = 0; hy < HALF_H; hy++)
    {
        int qy = hy / 2;
        for (hx = 0; hx < HALF_W; hx++)
        {
            int     qx = hx / 2;
            int32_t v;
            if (!mask[hy][hx])
            { /* $23FCA */
                pad[hy][hx] = 0;
                continue;
            }
            v = c->xpop[qy][qx];      /* $23FF0 */
            v -= c->xplc[qy][qx] / 2; /* $2401E */
            v -= c->xval[hy][hx] / 4; /* $24044 */
            if (c->ordinances & 0x04)
                v += 0x10; /* $2404E */
            pad[hy][hx] = (int16_t)v;
        }
    }

    c->crime_tot = 0;
    for (hy = 0; hy < HALF_H; hy++)
    {
        for (hx = 0; hx < HALF_W; hx++)
        {
            int32_t s = pad[hy][hx];
            int     n = 1;
            if (hy > 0)
            {
                s += pad[hy - 1][hx];
                n++;
            }
            if (hy < HALF_H - 1)
            {
                s += pad[hy + 1][hx];
                n++;
            }
            if (hx > 0)
            {
                s += pad[hy][hx - 1];
                n++;
            }
            if (hx < HALF_W - 1)
            {
                s += pad[hy][hx + 1];
                n++;
            }
            s /= n;
            if (s > 255)
                s = 255;
            else if (s < 0)
                s = 0;
            c->xcrm[hy][hx] = (uint8_t)s;
            c->crime_tot += s;
        }
    }
}

/*  $241B2 -- add to one cell of a coverage layer, clamped to a byte.
 *  Off-map cells are dropped rather than wrapped. */
static void coverage_point(uint8_t plane[QTR_H][QTR_W], int y, int x, int16_t amount)
{
    int32_t v;

    if (y < 0 || x < 0 || y >= QTR_H || x >= QTR_W)
        return; /* $241C4 */
    v = (int32_t)plane[y][x] + amount;
    if (v > 255)
        v = 255;
    else if (v < 0)
        v = 0;
    plane[y][x] = (uint8_t)v;
}

/*  $24232 -- stamp the diamond.  The original writes out all thirty-odd
 *  calls by hand; COVERAGE_KERNEL is the same list, recovered by running
 *  it.  Each ring step is done in sixteen bits, so a station funded hard
 *  enough to overflow the intermediate really does wrap, and that is
 *  reproduced rather than smoothed over. */
static void coverage_spread(uint8_t plane[QTR_H][QTR_W], int y, int x, int16_t s)
{
    int16_t ring[COVERAGE_RINGS];
    int     i;

    ring[0] = s;
    ring[1] = (int16_t)((int16_t)(s * 4) / 5);
    ring[2] = (int16_t)((int16_t)(3 * ring[1]) / 4);
    ring[3] = (int16_t)((int16_t)(2 * ring[2]) / 3);
    ring[4] = (int16_t)(ring[3] / 2);

    for (i = 0; i < COVERAGE_KERNEL_LEN; i++)
        coverage_point(plane, y + COVERAGE_KERNEL[i].dy, x + COVERAGE_KERNEL[i].dx, ring[COVERAGE_KERNEL[i].ring]);
}

/*  A station that has lost its power covers half as far.  $23E4A */
static int16_t station_range(int32_t product, int powered)
{
    int16_t s = (int16_t)(product / 2);
    if (!powered)
        s = (int16_t)((int32_t)s / 2);
    return s;
}

void sim_coverage(City *c)
{
    static int16_t acc[QTR_H][QTR_W]; /* A5+0x13BA at quarter res */
    int            y, x, qy, qx;

    memset(acc, 0, sizeof acc); /* $23C46 */
    memset(c->xplc, 0, sizeof c->xplc);
    memset(c->xfir, 0, sizeof c->xfir);

    /*  1 .. 0x7E.  The first and last row and column are skipped, which
     *  is the loop the original writes ($23CB2 sets 1, $23ED0 stops
     *  below 0x7F), not an off-by-one here. */
    for (y = 1; y < MAP_H - 1; y++)
    {
        for (x = 1; x < MAP_W - 1; x++)
        {
            uint8_t b = c->xbld[y][x];

            qy = y / 4;
            qx = x / 4;

            if (b >= BLD_ZONE_FIRST && b < BLD_POWER_FIRST)
            { /* $23CFC */
                acc[qy][qx] = (int16_t)(acc[qy][qx] + BUILDING[b].population);
                if ((c->ordinances & ORD_NEIGHBOURHOOD_WATCH) /* $23D2E */
                    && c->xplc[qy][qx] < 0xFE)
                    c->xplc[qy][qx] = (uint8_t)(c->xplc[qy][qx] + 2);
                if ((c->ordinances & ORD_VOLUNTEER_FIRE) /* $23D56 */
                    && c->xfir[qy][qx] < 0xFE)
                    c->xfir[qy][qx] = (uint8_t)(c->xfir[qy][qx] + 2);
                continue;
            }
            if (b < BLD_POWER_FIRST)
                continue; /* $23D86 */

            acc[qy][qx] = (int16_t)(acc[qy][qx] + ((b >= 0xFB && b <= 0xFE) ? 12 : 2));

            /*  Only the corner tile carrying XZON bit 0x80 spreads, so a
             *  3x3 station is stamped once and not nine times. $23DEE */
            if (!(c->xzon[y][x] & 0x80))
                continue;

            if (b == 0xD2)
            { /* $23E0C */
                int32_t r = (int32_t)(int16_t)(c->police_term + 5) * c->dept[DEPT_POLICE].funding;
                coverage_spread(c->xplc, qy, qx, station_range(r, c->xbit[y][x] & XBIT_POWERED));
            }
            else if (b == 0xD3)
            { /* $23E6C */
                int32_t r = c->dept[DEPT_FIRE].funding * 5;
                coverage_spread(c->xfir, qy, qx, station_range(r, c->xbit[y][x] & XBIT_POWERED));
            }
        }
    }

    /*  $23EE4.  The multiply by four is a word shift and the clamp is
     *  one sided -- the original never floors the result at zero. */
    for (qy = 0; qy < QTR_H; qy++)
    {
        for (qx = 0; qx < QTR_W; qx++)
        {
            int32_t v = (int16_t)(acc[qy][qx] * 4);
            int32_t d, r;
            if (v > 255)
                v = 255;

            /*  $23F30 -- the rate of growth, before the new density
             *  overwrites the old.  It is an average of the CHANGE,
             *  weighted seven to one toward the running value, and it
             *  is offset by 128 so that a quarter holding steady reads
             *  as the middle of the range rather than as zero. */
            d = (v - c->xpop[qy][qx]) * 8 + 128;        /* $23F32 */
            r = (int16_t)(c->xrog[qy][qx] * 7 + d) / 8; /* $23F52 */
            if (r < 0)
                r = 0; /* $23F6A */
            if (r > 255)
                r = 255; /* $23F72 */

            c->xpop[qy][qx] = (uint8_t)v; /* $23F3C */
            c->xrog[qy][qx] = (uint8_t)r; /* $23F7C */
        }
    }
}
