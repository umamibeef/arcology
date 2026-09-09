/*  sim.c -- the clock, and the money it spends: sim_tick's phase table, the
 *  budget, the ordinances and the February poll.  This was the whole
 *  simulation once, 8,438 lines of it.  The phases themselves now live
 *  beside each other by subject -- sim_scan.c, sim_growth.c, sim_map.c,
 *  sim_place.c, sim_thing.c, sim_disaster.c, sim_micro.c, and the ring they
 *  share in sim_queue.c -- and what is left here is the thing that calls
 *  them in the original's order. sim_int.h names everything that crosses
 *  between them.  Layout still follows the original's phase structure
 *  rather than any tidier arrangement, so each function can be read next to
 *  the listing it came from.  Arithmetic is deliberately kept in the
 *  original's widths: the game runs on 16-bit registers in many places and
 *  the truncation is observable in the results. */
#include "ext80.h"
#include "sc2k.h"
#include "sim_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*  $41B8 and $42AC -- which infra[] counter a military building id
 *  belongs to.  The two switches in $4110 share this one table. */
static int infra_slot(int bld)
{
    /*        $DD $DE $DF $E0 $E1 $E2 $E3 $E4 $E5 $E6 $E7 $E8 $E9 $EA $EB */
    static const uint8_t SLOT[0x1D] = {
        1, 2, 0, 10, 0, 11, 6, 7, 8, 0, 12, 13, 0, 5, 0,
        /* $EC $ED $EE $EF $F0 $F1 $F2 $F3 $F4 $F5 $F6 $F7 $F8 $F9 */
        0,
        0,
        0,
        3,
        0,
        9,
        4,
        0,
        0,
        0,
        14,
        0,
        0,
        15};
    if (bld < 0xDD || bld > 0xF9)
        return 0; /* $41AA */
    return SLOT[bld - 0xDD];
}

/* ================================================================== *
 *  $4110  setTile -- the routine 168 call sites funnel through.
 *
 *  Besides writing XBLD it keeps a running census of every building id
 *  on the map.  That census is what lets the growth pass ask questions
 *  like "how many churches are there" without walking the map.
 * ================================================================== */
void sim_set_tile(City *c, int y, int x, uint8_t bld)
{
    uint8_t old;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return; /* $4124 */

    if (XZON_TYPE(c->xzon[y][x]) == ZONE_MILITARY)
    {
        /*  $418C -- a military tile keeps its own tally instead.  Two
         *  identical 29-case switches over ids $DD..$F9 pick a slot in
         *  infra[]: the old building's slot goes down, the new one's
         *  goes up.  Anything outside that range, and every id in it
         *  the table does not name, lands on slot 0. */
        c->infra[infra_slot(c->xbld[y][x])]--; /* $41B8 */
        c->infra[infra_slot(bld)]++;           /* $42AC */
        c->xbld[y][x] = bld;                   /* $4396 */
        return;
    }

    old = c->xbld[y][x]; /* $4166 */
    c->census[old]--;    /* $4174 */
    c->census[bld]++;    /* $417E */
    c->xbld[y][x] = bld; /* $4184 */
}

/*  Rebuild the census from scratch.  The game maintains it incrementally
 *  from a new-city state; we have to derive it after loading a save. */
void sim_rebuild_census(City *c)
{
    int y, x;
    memset(c->census, 0, sizeof c->census);
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            c->census[c->xbld[y][x]]++;
}

/* ================================================================== *
 *  A shared BFS queue.  The original keeps it at A5+0x13B6/0x13B8 with
 *  the ring buffer alongside; push is $21DD4 / $21DF2 and pop $21E3A.
 *  Both flood fills use it, one at a time.
 * ================================================================== */

/* ================================================================== *
 *  Phase 1 -- the power grid.  $20FC4 clears the flags and hunts for
 *  plants; $210A2 floods outward from each one.
 * ================================================================== */

/* ================================================================== *
 *  Phase 20 -- the water network.  Same shape as power, with its own
 *  pair of flag bits: 0x20 conducts, 0x10 supplied.
 * ================================================================== */

/* ================================================================== *
 *  Phase 2 -- the data layers.  $2317E rebuilds pollution from the
 *  buildings and traffic underneath each 64x64 cell, then blurs it.
 * ================================================================== */

/* ================================================================== *
 *  $22330  graphHistoryPass -- one month of graph history.
 *
 *  Phase 21 of the clock calls it, at $220F0, straight after
 *  populationPass ($33FAE) and economyPass ($34D04) -- so the readings
 *  it takes are the ones those two have just settled.
 *
 *  The pass runs in four movements: shift the monthly band along, take
 *  a fresh reading for each of the sixteen series, bring the vertical
 *  scales up to date, and then shift the two slower bands if the
 *  calendar calls for it.
 *
 *  Three details are worth keeping in view.  The arcology term at
 *  $2238E adds twenty thousand residents for every arcology past the
 *  hundred and fortieth, and the count it works from is a tile count
 *  divided by sixteen, because an arcology covers a 4x4 footprint.
 *  The scales are shared rather than independent: the four population
 *  series draw against one maximum and the four map overlays against
 *  another, so the shapes stay comparable inside each group.  And all
 *  the comparisons are unsigned, which matters because a series can
 *  hold a negative sample.
 * ================================================================== */

/* ================================================================== *
 *  $2317E stages 3-5 -- city centre and land value.
 *
 *  These share one 128-row scratch plane of words, the row-pointer
 *  array at A5+0x13BA.  The stages address it at different resolutions,
 *  which is confusing but deterministic:
 *      stage 1  scratch[y/2][x/2]        raw pollution      (64x64)
 *      stage 3  scratch[y][x] = 40       at building tiles  (128x128)
 *      stage 4  scratch[y/4][x/4]        amenity plane A    (32x32)
 *               scratch[y/4+32][x/4]     water plane B      (32x32)
 *  Nothing clears it between stages, so stage 4's accumulators start
 *  from whatever the earlier stages left at the same address.
 * ================================================================== */

/* ================================================================== *
 *  $2317E stages 6-8 ($23C3A .. $23F3C) -- police, fire and density.
 *
 *  These three layers come out of one walk of the map, so they are one
 *  function here too.  The walk first clears the 32x32 scratch plane
 *  and both coverage layers, then for every tile
 *
 *    - a developed zone building adds its population to the scratch
 *      plane and, under the two service ordinances, two points of free
 *      coverage to the cell it stands in;
 *    - anything above the zone range adds a flat 2 (12 for an arcology)
 *      and, if it is a police or fire station standing on its corner
 *      tile, stamps a coverage diamond around itself.
 *
 *  Density is then four times the scratch plane.  The station radius is
 *  what made this the last blocked stage: it scales with the
 *  department's funding level, which lives in the budget block, and the
 *  budget block was not decoded until the MISC unpacker at $295D6 was
 *  run under the interpreter (tools/miscload.py).
 * ================================================================== */

/* ================================================================== *
 *  $3258A / $33028 / $32830 -- putting a building on the map.
 *
 *  All three end in the same place: pick an id out of a group and stamp
 *  it down.  The groups are five kinds by four tiers, and which variant
 *  inside the group is chosen is random -- except for the smallest
 *  residential group, where land value decides which third of the group
 *  to draw from, so poor land gets the shacks and rich land the houses.
 * ================================================================== */

/* ================================================================== *
 *  $EEAE  allocMicro -- hands a building an XMIC slot, the eight-byte
 *  record that holds a stadium's team or an arcology's stage.  Returns
 *  the slot index, which $3590 writes into XTXT.
 *
 *  NOT RECONSTRUCTED.  It reads a kind table at A5-0x5D42 indexed by
 *  bld - 0xC6, then linear-probes XMIC for a free eight-byte record.
 *  It draws no randoms, so leaving it out cannot shift the RNG stream
 *  and cannot move XBLD, XZON, XBIT or XTRF -- the four layers the
 *  growth oracle compares.  What it costs is the XTXT label byte. */

/* ================================================================== *
 *  $5FAA  demolishAndPlace -- take a building off the map.
 *
 *  Most of its 5,680 bytes animate the collapse.  All of the change to
 *  the city happens in the loop at $73A4, which walks the footprint the
 *  size table gives and does the same six things to every tile:
 *
 *      the new building is rubble, $01 to $04 by a coin, unless the
 *          tile stands on sloped or watered ground, where it goes bare
 *      XBIT keeps only bits 0, 2, 3, 4 and 5, so conducting, powered
 *          and bit 1 all go
 *      XZON keeps its zone nibble and loses its corner marker
 *      the XTXT byte is cleared, unless it names a moving object
 *      whatever that byte pointed at is released
 *      a tile that was burnt out is taken off the burnt tally
 *
 *  Then the terrain around the footprint is put back in order.
 * ================================================================== */

/* ================================================================== *
 *  $3A000  demolishTile -- what a disaster calls to flatten one tile.
 *
 *  It takes the building down through $5FAA, then optionally scorches
 *  the footprint, then records the tile in the city's worst-problem
 *  slot.  Building ids $3F to $42 are exempt and nothing happens.
 *
 *  The problem code is the zone for an ordinary building, ten for $C6,
 *  and the id less $BD above that.  A rank table at A5-0x10D2 decides
 *  whether the new code displaces the one already recorded.
 * ================================================================== */

/* ================================================================== *
 *  $128DE  fixTerrain -- put one tile back in order after the land
 *  under it has moved.
 *
 *  First it clears what can no longer stand there: a building of $0D or
 *  more is demolished, the tile is emptied unless its building is 5,
 *  and anything underground goes.
 *
 *  Then it works out the tile's shape.  Each of the eight neighbours
 *  that stands higher raises the corners it touches -- the byte table
 *  at A5-0x4DF6 says which corners those are -- and the four-bit set of
 *  raised corners picks a slope code out of the sixteen at A5-0x4DEE.
 *  Code $32 is not a slope: it means all four corners are higher, so
 *  this tile has to come up a step itself.
 *
 *  Last it decides land or water.  A tile at or above the city's water
 *  level (MISC[912]) loses its water bit and keeps its slope code.  One
 *  below gets the water bit, has the water level written into ALTM bits
 *  5..9, is emptied, and takes a shifted code: $20 plus the slope when
 *  it sits exactly one step under the water line -- the shoreline -- and
 *  $10 plus the slope when it is deeper.
 * ================================================================== */

/* ================================================================== *
 *  The moving-object table, XTHG: forty twelve-byte records.  Slot 0 is
 *  never used -- $9DDA starts its search at 1 and treats 40 as "full".
 *
 *  Record layout, as $B0BC fills it:
 *      +0  kind        $0A locomotive, $0B carriage
 *      +1  direction   0..3, from $E18E
 *      +2  next        slot index of the next car, 0 to end the chain
 *      +3  y           where it is
 *      +4  x
 *      +5  0
 *      +6  y2          the tile ahead (the locomotive only)
 *      +7  x2
 *      +A  the XTXT byte this record displaced
 * ================================================================== */

/*  Four directions, and the two step tables the train code uses.  The
 *  scan at $B058 steps twice as far as the movement tables do. */

/* ================================================================== *
 *  $32BFA  growTo3x3 -- try to turn a 2x2 into a 3x3.
 *
 *  Four candidate anchors are tried in turn.  For each, the eight tiles
 *  around a 3x3 whose TOP-RIGHT corner is the anchor must all take the
 *  building -- the centre is neither tested nor cleared -- and at least
 *  one of four diagonal positions just outside the block must hold a
 *  road of the right orientation.  Only then is anything written.
 * ================================================================== */

/* ================================================================== *
 *  The moving-object stepper.  This is NOT part of the 25-phase clock:
 *  $09E0A runs from the main loop, once a frame, and the clock never
 *  calls it.  So a headless simulation that only ticks will never move
 *  anything, which is why a saved city reproduces exactly without it.
 *
 *  Seven of the seventeen types never move.  Types 0, 4, 11, 13 and 14
 *  are disabled in the table at A5-0x635A, and the two that are enabled
 *  but have a stepper -- 7 and 8 -- reach $D7CE and $D7D6, which are
 *  `link; unlk; rts` and do nothing at all.
 *
 *  Type 11, the train carriage, is the commonest thing in the shipped
 *  cities and is disabled here.  It still moves: the locomotive drags
 *  the whole chain through the `next` field at +2.
 * ================================================================== */

/*  A5-0x61D4 and A5-0x61C4: the same four steps the road walk uses, at
 *  another address. */

/* ================================================================== *
 *  Ships -- type 3.
 *
 *  A ship is a five-state machine on the byte at +2, dispatched through
 *  the jump table at $E68C: 0 sails, 1 lines itself up, 2 works its way
 *  out of a corner, 3 sits at the dock and 4 runs for the seaport.  It
 *  keeps a heading in 0..7 rather than the trains' four, looks THREE
 *  tiles ahead rather than one, and carries whatever XTXT it is sitting
 *  on in +$0A so it can put it back when it leaves.
 * ================================================================== */

/* ================================================================== *
 *  Helicopters -- type 2.
 *
 *  Six states on the byte at +2, jump table at $C83C.  0 climbs, 1 does
 *  nothing at all, 2 cruises, 3 descends to land, 4 sits, 5 spirals in
 *  and crashes.  The altitude lives at +5 and the target at +8/+9.
 * ================================================================== */

/* ================================================================== *
 *  Aeroplanes -- type 1.
 *
 *  Eight states, jump table at $C336: 0 climbs out, 1 lands, 2 cruises,
 *  3 turns onto the approach, 4 flies the approach, 5 and 6 do nothing,
 *  7 spirals in.  The state byte packs more than a state: the LOW nibble
 *  is the state and the HIGH nibble is the runway heading, which states
 *  3 and 4 read back out to line the aeroplane up.
 * ================================================================== */

/* ================================================================== *
 *  $333C8  placeSpecial -- grows the furniture that belongs to a
 *  military base, an airport or a seaport.  Dispatches on the building
 *  id: $E1..$E8 and $EA are one tile, $EE..$F2 and $F6 are two by two,
 *  $F9 is the military three by three.  $DD and $E0 have handlers of
 *  their own ($33844, $33A90) that the growth scan never reaches.
 *
 *  Returns $FF when the caller should consider the job done and 0 when
 *  it should fall back to a smaller building.  Note that several
 *  "rejections" still return $FF -- a tile that is already built on
 *  counts as done, not as a failure.
 * ================================================================== */

int trip_mark_log;

/* ================================================================== *
 *  tripGenerate ($245E8) -- can a journey be made from here?
 *
 *  Asked "can a building of this zone and tier work here", the game
 *  answers by trying to make a journey.  It steps onto the nearest
 *  transport tile, then walks the network at random -- turning
 *  consistently left or right, never immediately doubling back --
 *  spending a length budget as it goes.  The trip succeeds if it
 *  reaches a tile whose zone the starting zone wants (ZONE_ATTRACTS:
 *  homes want shops and factories, shops want homes and factories) or
 *  reaches a road off the edge of the map, which is a neighbouring
 *  city.  It fails if the budget runs out or the network dead-ends.
 *
 *  Every accepted step is pushed on the shared ring, and on the way out
 *  the whole route is drained again and stamped into XTRF.  That is
 *  where traffic comes from: it is not modelled, it is the residue of
 *  journeys that were actually attempted.
 * ================================================================== */

/* ================================================================== *
 *  growthScan ($3170E) -- phases 3 through 18.
 *
 *  Sixteen phases each walk a quarter-offset lattice: phase (y0,x0)
 *  visits y = y0, y0+4, ... and x = x0, x0+4, ..., so over one cycle
 *  every tile is visited exactly once.  Per tile it does one of three
 *  things:
 *
 *    unzoned, infrastructure     with probability 1/128, roll against
 *                                the owning department's funding level
 *                                and let the tile rot if underfunded
 *    unzoned, special building   the automatic builds (rail station,
 *                                marina, arcology upkeep)
 *    zoned                       accumulate this building's population,
 *                                then roll for growth or decay
 *
 *  The population accumulator is the important output: $33FAE at phase
 *  21 sums accum8[1..6] and multiplies by ten, and that is the number
 *  on the status bar.
 * ================================================================== */

/* ================================================================== *
 *  budgetPass ($263C8) -- the monthly budget pass, phase 0.
 *
 *  This is what police and fire coverage were waiting on.  It keeps
 *  sixteen department records: each holds an `amount` recomputed from
 *  the tile census, a `funding` level the mayor sets, and the year's
 *  accrual of their product, settled into the treasury every January.
 *  Coverage reads the police and fire funding levels straight out of
 *  it, which is why nothing could compute XPLC or XFIR until the block
 *  itself was located in MISC.
 *
 *  Service buildings are counted in tiles, so dividing by 9 or 16 turns
 *  a tile count back into a building count.  The infrastructure ranges
 *  overlap on purpose: a bridge tile is charged to both the road and
 *  the highway department.
 * ================================================================== */

int32_t sim_ordinance_cost(const City *c, int which) /* ordinanceCost $41368 */
{
    OrdinanceCost o;
    int32_t       v;

    if (which < 0 || which >= 20)
        return 0;
    o = ORDINANCE_COST[which];
    if (o.source < 0)
        return 0;
    if (o.source == 3) /* $41500 */
        return -c->population - c->misc[MISC_2C98];
    v = c->dept[o.source].amount * o.num;
    return v / o.den;
}

void sim_budget(City *c)
{
    int i, t;

    /*  The January reconciliation, $263E0.  It only runs if last
     *  December armed it, and it divides the year's accrual by twelve
     *  times the department's own divisor -- positive for the four
     *  revenue departments, negative for the twelve that spend. */
    if (c->year_end && c->month == 0)
    {
        for (i = 0; i < N_DEPT; i++)
        {
            int32_t div = 12 * DEPT_YEAR_DIVISOR[i];
            if (div)
                c->funds += c->dept[i].accrued / div;
            c->dept[i].accrued = 0; /* $2642C */
        }
        c->year_end = 0; /* $26438 */
        /*  $26442 -- and with the year closed, every special building
         *  on the map takes its turn.  Inside the year-end block, after
         *  the flag is cleared, exactly where the original calls it. */
        sim_microsim(c);
    }

    for (i = 0; i < N_DEPT; i++)
    { /* $26482 */
        c->dept[i].history_amount[c->month]  = c->dept[i].amount;
        c->dept[i].history_funding[c->month] = c->dept[i].funding;
        c->dept[i].accrued += c->dept[i].amount * c->dept[i].funding;
    }
    if (c->month == 11)
        c->year_end = 1; /* $264D8 */

    c->dept[DEPT_BONDS].amount   = c->bonds;            /* $264E6 */
    c->dept[DEPT_POLICE].amount  = c->census[0xD2] / 9; /* $264F0 */
    c->dept[DEPT_FIRE].amount    = c->census[0xD3] / 9;
    c->dept[DEPT_HEALTH].amount  = c->census[0xD1] / 9;
    c->dept[DEPT_SCHOOL].amount  = c->census[0xD6] / 9;
    c->dept[DEPT_COLLEGE].amount = c->census[0xD9] >> 4; /* 4x4 */

    for (i = 10; i <= 15; i++)
        c->dept[i].amount = 0; /* $26572 */

    for (t = 0x1D; t < 0x70; t++)
    { /* $2658C */
        uint16_t m = BUILDING[t].dept;
        for (i = 10; i < N_DEPT; i++)
            if (m & (uint16_t)(1u << i))
                c->dept[i].amount += c->census[t];
    }

    /*  Three station types are charged after the loop rather than in
     *  it.  The bus term is truncated to sixteen bits before it is
     *  added, which matters once a city has more than about a thousand
     *  bus stops. $26696, $266AE, $266C0 */
    c->dept[DEPT_TRANSIT].amount =
        (uint16_t)(c->transit_term + c->census[0xE9]);
    c->dept[DEPT_RAIL].amount += c->census[0xED];
    c->dept[DEPT_ROAD].amount +=
        (uint16_t)((c->census[0xEC] >> 2) * 250);

    c->dept[DEPT_ORDINANCE].amount = 0; /* $266DA */
    for (i = 0; i < 20; i++)
        if (c->ordinances & ((int32_t)1 << i))
            c->dept[DEPT_ORDINANCE].amount += sim_ordinance_cost(c, i);

    /*  $2670A -- and then, one month in eight, a city with money in the
     *  bank finds an ordinance has been passed without it.  The treasury
     *  has to beat a random figure plus fifty thousand, so it only happens
     *  to a rich city, and the newspaper announces it.  The same A5+0x13AA
     *  switch that turns disasters off turns this off too, which is the
     *  only reason to think of it as one.  Both its dice are drawn whenever
     *  the switch is on, so a model that leaves it out is a draw short
     *  every month. */
    if (!c->disasters_off && (Random() & 7) == 0)
    {
        int32_t bar = (int32_t)(uint16_t)Random() + 50000; /* $26726 */
        if (bar < c->funds)                                /* $2672C */
        {
            int k = (int)((uint16_t)Random() % 20); /* $2673C */
            c->ordinances |= (int32_t)1 << k;       /* $26748 */
        }
    }
}

/* ================================================================== *
 *  $2317E stages 6-7 -- population density.
 *
 *  Every building contributes a value from the table at A5-0x3982 into
 *  a 32x32 accumulator; the density is four times that, saturated to a
 *  byte.  Specials outside the zone range contribute a flat 2, except
 *  arcologies (0xFB..0xFE) which contribute 12.
 *
 *  The growth-rate layer that follows it, XROG, is an exponential
 *  average of the CHANGE in density -- (7*old + 8*delta + 128) / 8 --
 *  so it needs the pre-pass state and cannot be rebuilt from a save.
 * ================================================================== */
void sim_density(City *c)
{
    /*  Density is not a pass of its own: it is the tail of the same map
     *  walk that lays down police and fire coverage, and shares that
     *  walk's accumulator.  Kept as a name because that is what the
     *  layer is called. */
    sim_coverage(c);
}

/* ================================================================== *
 *  $3152A  opinionPoll -- the February poll.
 *
 *  Once a year, in month 2, phase 0 stops a hundred imaginary citizens
 *  in the street and asks each one what is wrong with the city.  The
 *  answer is not computed from the indicators directly: each of the
 *  seven complaints is given a weight, contentment is given a weight of
 *  its own, and the hundred answers are drawn from that distribution.
 *  So a city with a little crime still returns a few people who name
 *  crime, and the ranking wobbles from year to year even when nothing
 *  has changed.
 *
 *  The seven weights ($31544 to $315AE) are the raw indicators, not
 *  normalised: the three map averages as they stand, the tax rate
 *  tripled, the unemployment count, and the two shortfalls -- how far
 *  education is below 100 and life expectancy below 70.  A city that
 *  is over those two marks contributes nothing from them.
 *
 *  Contentment is 50 plus the land value average ($315B2).  That is the
 *  whole of the poll's optimism, and it is why a rich city polls well
 *  even with problems: land value is measured in the hundreds while the
 *  complaints are usually in the tens.
 *
 *  Two details are easy to get wrong.  The map averages are read as
 *  words from the middle of a long ($31548 reads offset 2 of the graph
 *  slot), so only the low sixteen bits count -- contentment takes the
 *  whole long.  And the tax term reads the same department three times
 *  over ($31568 loads a1 and a0 from one pointer), so it is three times
 *  the residential rate and not the sum of the three tax rates.
 *
 *  Afterwards the seven are sorted worst first ($31656, a bubble sort
 *  on the index array) and the sorted counts are kept beside them.  The
 *  newspaper and the advisors read the ranking; nothing else does.
 * ================================================================== */
int sim_opinion_poll(City *c)
{
    int16_t w[N_PROBLEM];
    int16_t counts[N_PROBLEM];
    int16_t index[N_PROBLEM];
    int16_t was;
    int32_t total;
    int     i, j, n;

    /*  $31544 -- the three map overlays, low word only */
    w[PROBLEM_TRAFFIC]   = (int16_t)c->graph[GRAPH_TRAFFIC][0];
    w[PROBLEM_POLLUTION] = (int16_t)c->graph[GRAPH_POLLUTION][0];
    w[PROBLEM_CRIME]     = (int16_t)c->graph[GRAPH_CRIME][0];

    /*  $31562 -- three times department 0's rate, see above */
    w[PROBLEM_TAXES] = (int16_t)(c->dept[0].funding * 3);

    /*  $31578 -- the low word of the unemployment count */
    w[PROBLEM_UNEMPLOYMENT] = (int16_t)c->unemployment;

    /*  $3157E and $31596 -- the two shortfalls, floored at zero */
    w[PROBLEM_EDUCATION] = (int32_t)c->misc[MISC_AGE_W90] >= 100
                               ? 0
                               : (int16_t)(100 - c->misc[MISC_AGE_W90]);
    w[PROBLEM_HEALTH]    = (int32_t)c->misc[MISC_AGE_W65] >= 70
                               ? 0
                               : (int16_t)(70 - c->misc[MISC_AGE_W65]);

    /*  $315B2 -- contentment, then every complaint on top of it */
    total = 50 + c->graph[GRAPH_VALUE][0];
    for (i = 0; i < N_PROBLEM; i++)
    {
        total += w[i];          /* $315C0 */
        counts[i] = 0;          /* $315CA */
        index[i]  = (int16_t)i; /* $315D4 */
    }

    /*  $315EA -- a city with no weight at all, or under a hundred
     *  people, is not polled and keeps last year's ranking. */
    if ((int16_t)total == 0 || c->population < 100)
        return SIM_EV_NONE;

    was         = c->approval; /* $315FA */
    c->approval = 0;           /* $31600 */

    /*  $31608 -- a hundred citizens, each landing in one bucket */
    for (n = 0; n < 100; n++)
    {
        int r = (uint16_t)Random() % (uint16_t)(int16_t)total; /* $31612 */

        for (i = 0; i < N_PROBLEM; i++) /* $3161C */
        {
            if (r < w[i])
                break;
            r -= w[i];
        }
        if (i == N_PROBLEM)
            c->approval++; /* $3163A -- nothing to complain about */
        else
            counts[i]++; /* $31646 */
    }

    /*  $31656 -- sort the index worst first, then read the counts back
     *  through it so the two arrays line up. */
    for (n = N_PROBLEM - 1; n > 0; n--)
        for (j = 0; j < n; j++)
            if (counts[index[j]] < counts[index[j + 1]])
            {
                int16_t t    = index[j];
                index[j]     = index[j + 1];
                index[j + 1] = t;
            }
    for (i = 0; i < N_PROBLEM; i++) /* $316B2 */
    {
        c->problem_rank[i]  = index[i];
        c->problem_votes[i] = counts[index[i]];
    }

    /*  $316D6 -- crossing four fifths approval is congratulated once,
     *  on the way up only. */
    return (was < 80 && c->approval >= 80)
               ? SIM_EV_APPROVAL /* $316EA sound, $316FC message */
               : SIM_EV_NONE;
}

/* ================================================================== *
 *  $101AC  microsimPass -- the year-end turn of every special building.
 *
 *  budgetPass calls it once a year, from the January settlement, right
 *  after the sixteen departments have been reconciled into the treasury
 *  ($26442).  It is the only thing in the game that walks XMIC.
 *
 *  XMIC is 150 records of eight bytes at A5+0x2BC6, one per special
 *  building on the map: the power plants, the stations, the schools and
 *  hospitals, the arcologies, the marina.  Records 1 to 149 are walked
 *  in order; record 0 is never used.  Each record's first byte is a
 *  building id, and `$1027C` dispatches on it -- 58 entries covering
 *  $C6 to $FF, of which 26 are distinct and two are "do nothing".
 *
 *  What the three words in a record mean is the type's own business.  A
 *  power plant keeps its age; a marina keeps how many boats it has.
 *  There is no common schema and pretending there is one would be an
 *  invention.
 * ================================================================== */

/*  $11246, twenty callers.  What a building would like, capped by what
 *  the city can support: `min(want, population / per)`.  A `per` of
 *  zero means one per hundred people. */

/* ====================================================================
 *  simTick $21EDE -- one phase of the 25-phase clock.
 *
 *  The date advances first ($21EE6) and the phase is the date modulo
 *  25 ($21EF0); there is no phase counter of its own to save.  The
 *  jump table at $21F0A has 25 arms.  Whatever an arm does that is not
 *  simulation -- the window title, dialogs, menus, the cursor, the graph
 *  windows, the newspaper -- is named here and left out.
 * ==================================================================== */
int sim_tick(City *c)
{
    int ev = SIM_EV_NONE;
    int phase;

    c->date++;
    phase = (int)(c->date % 25);
    if (phase < 0)
        return ev; /* $21EF6: bhi.w past the table */
    switch (phase)
    {
        case 0:
            /*  updateWindowTitle $1522A runs first, and it is what
             *  refreshes the month ($1523E) and the year count ($15268)
             *  that the budget and the economy read. */
            c->month = (int16_t)((c->date / 25) % 12);
            c->years = c->date / 300;
            /*  $21F42: g_yearEndDue opens the year-end budget dialog
             *  ($2535E).  $21F62: $2EAEA is a dialog in months 3 and 7.
             *  Neither touches the model; the poll below does. */
            sim_budget(c); /* $21F4E */

            /*  $21F54 -- the poll runs in month 2 and only there */
            if (c->month == 2)
            {
                int e = sim_opinion_poll(c);
                if (e != SIM_EV_NONE)
                    ev = e;
            }
            {
                int i;
                for (i = 1; i < 8; i++)
                    c->accum8[i] = 0; /* $21F88 */
            }
            break;
        case 1:
            sim_power_grid(c); /* $21FA6 */
            break;
        case 2:
            /*  cityScanPass $2317E, stages 1 to 9 in order; coverage
             *  runs inside the density stage. */
            sim_pollution(c);
            sim_land_value(c);
            sim_density(c);
            sim_crime(c);
            break;
        case 19:
            sim_traffic_total(c); /* $220D0 */
            break;
        case 20:
            sim_water_grid(c); /* $220DA */
            break;
        case 21:
            sim_population(c); /* $220E4 */
            sim_economy(c);
            sim_graph_pass(c);
            break;
        case 22:
            {
                /*  $220FA: the city is promoted a stage when its population
                 *  passes the next rung of the ladder at A5-0x3ED8, indexed
                 *  by stage + 1; a zero rung ends the ladder.  The newspaper
                 *  and the reward prompt that follow ($2EDE4, $4094, $22708)
                 *  are interface. */
                int32_t stage = c->misc[MISC_STAGE];
                int32_t need  = (stage >= 0 && stage + 1 < 10)
                                    ? CITY_STAGE_POP[stage + 1]
                                    : 0;
                int     r;
                if (need && (uint32_t)c->population > (uint32_t)need)
                {
                    c->misc[MISC_STAGE] = stage + 1; /* $22128 */
                    ev                  = SIM_EV_STAGE;
                }
                /*  $2219C: the scenario goals, once a month. */
                r = sim_scenario_check(c);
                if (r > 0)
                    ev = SIM_EV_SCEN_WON;
                else if (r < 0)
                    ev = SIM_EV_SCEN_LOST;
                /*  $222AC: below -100,000 the city is bankrupt, $230E6(2). */
                if (c->funds < -100000)
                    ev = SIM_EV_BANKRUPT;
                break;
            }
        case 23:
            /*  $222C4: the redraw and the graph windows. */
            break;
        case 24:
            /*  $222F8: $30E30 picks the newspaper's story, and then
             *  decides whether anything is about to go wrong. */
            sim_disaster_roll(c);
            break;
        default:
            /*  $21FBA..$220C4: sixteen growthScan slices, the argument
             *  packed as y0 << 16 | x0 in the order (0,0) (0,1) ... */
            sim_growth_scan(c, (phase - 3) >> 2, (phase - 3) & 3);
            break;
    }
    return ev;
}
