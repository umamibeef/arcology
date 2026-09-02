/*  sc2k.h -- SimCity 2000 simulation, reconstructed from the 68k Macintosh
 *  binary (SimCity 2000(R) 1.2, 22 Jun 1995).
 *
 *  Every address in a comment like $3170E is a byte offset into the CODE 2
 *  resource, which is where the whole game lives.  A5+0x1FC2 style comments
 *  give the original global's offset from the A5 world pointer.
 */
#ifndef SC2K_H
#define SC2K_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ *
 *  Map geometry.  The game keeps the same city at three resolutions:
 *  full 128x128 tile layers, 64x64 "data" layers that each cover a 2x2
 *  tile block, and 32x32 layers covering 4x4.
 * ------------------------------------------------------------------ */
#define MAP_W  128
#define MAP_H  128
#define HALF_W 64
#define HALF_H 64
#define QTR_W  32
#define QTR_H  32

/* ------------------------------------------------------------------ *
 *  XBIT -- per-tile flag bits.
 *
 *  Four of these are pinned by code that unambiguously reads or writes
 *  them; WATER is pinned by correlating a real city (0.998 against
 *  XTER != 0 in Manhattan).  The other three are honestly unknown --
 *  see the comments.  Bit 3 is a scratch bit: both flood fills clear it
 *  before they run and set it as they visit.
 * ------------------------------------------------------------------ */
enum
{
    XBIT_SALT           = 0x01, /* salt water -- $21968 vs $219EA          */
    XBIT_UNK1           = 0x02, /* set on 0.4% of tiles; role unconfirmed    */
    XBIT_WATER          = 0x04, /* water covered   -- 51 btst sites          */
    XBIT_VISITED        = 0x08, /* flood-fill scratch  $20FC4 / $2156E       */
    XBIT_WATERED        = 0x10, /* supplied with water $2156E                */
    XBIT_CONDUCTS_WATER = 0x20, /* water network, tested at $21890       */
    XBIT_POWERED        = 0x40, /* supplied with power $20FC4                */
    XBIT_CONDUCTIVE     = 0x80  /* conducts power      $210A2                */
};

/* ------------------------------------------------------------------ *
 *  XZON packs two things into one byte.
 *
 *  Low nibble: the zone type.  High nibble: a corner mask for multi-tile
 *  buildings -- one bit per corner, so a building is simulated exactly
 *  once per pass, at whichever corner is nearest the viewer for the
 *  current rotation ($3173A picks the bit out of ROT_CORNER_MASK).
 *  Single-tile buildings carry all four bits (0xF0).
 * ------------------------------------------------------------------ */
#define XZON_TYPE(z)    ((z) & 0x0F)
#define XZON_CORNERS(z) ((z) & 0xF0)

enum
{
    ZONE_NONE      = 0,
    ZONE_RES_LIGHT = 1,
    ZONE_RES_DENSE = 2,
    ZONE_COM_LIGHT = 3,
    ZONE_COM_DENSE = 4,
    ZONE_IND_LIGHT = 5,
    ZONE_IND_DENSE = 6,
    ZONE_MILITARY  = 7,
    ZONE_AIRPORT   = 8,
    ZONE_SEAPORT   = 9
};

/* Zone type 1..6 -> 0=Residential, 1=Commercial, 2=Industrial. $31D9A */
#define ZONE_TO_RCI(z) (((z) - 1) / 2)

enum
{
    RCI_RES = 0,
    RCI_COM = 1,
    RCI_IND = 2
};

/* ------------------------------------------------------------------ *
 *  Building IDs of interest.  The full 0xC6..0xFF name table is
 *  generated into tables.c from the binary's own global image.
 * ------------------------------------------------------------------ */
enum
{
    BLD_NONE        = 0x00,
    BLD_ZONE_FIRST  = 0x70, /* developed zone buildings, tier 1..4   */
    BLD_ZONE_LAST   = 0xC5,
    BLD_POWER_FIRST = 0xC6, /* the ten power plants                  */
    BLD_POWER_LAST  = 0xCF,
    BLD_CHURCH      = 0xF7, /* $3170E reads its census at index 0xF7 */
    BLD_RESERVOIR   = 0xEB  /* keeps its water flag in $2156E        */
};

/* Does this building consume power?  $21226 / $21222 */
#define BLD_CONSUMES_POWER(b) \
    (((b) >= BLD_ZONE_FIRST && (b) < BLD_POWER_FIRST) || (b) >= 0xD0)

/* ------------------------------------------------------------------ *
 *  City state.  This mirrors the game's A5 globals; the layer members
 *  are the arrays the row-pointer tables at those offsets point into.
 * ------------------------------------------------------------------ */
#define MISC_LONGS 1200 /* the MISC chunk is 4800 bytes */

/*  MISC indices.  0..13 come from the straight-line part of the builder
 *  at $2A186 and are unambiguous.  MISC_POPULATION was recovered the
 *  other way round -- by computing population from the map and finding
 *  the one slot that tracks it across all 103 shipped cities. */
enum
{
    MISC_MAGIC      = 0, /* always 0x122            */
    MISC_ROTATION   = 2,
    MISC_YEAR       = 3,
    MISC_DATE       = 4,
    MISC_FUNDS      = 5,
    MISC_BONDS      = 6,
    MISC_DIFFICULTY = 7,
    MISC_LANDVAL    = 10,
    MISC_CRIME      = 11,
    MISC_TRAFFIC    = 12, /* snapshot from phase 19, not a live sum */
    MISC_POLLUTION  = 13,
    MISC_WEATHER1   = 25,   /* A5+0x1F01, drives wind-plant output   */
    MISC_WEATHER2   = 26,   /* A5+0x1F02, drives solar and water pumps*/
    MISC_2C86       = 912,  /* A5+0x2C86, the water level (and a term
                             *  in pump capacity)                     */
    MISC_ORDINANCES = 1000, /* A5+0x1E6E bitmask                      */

    /*  The scenario block, MISC[1050..1066].  The resource fork seeds
     *  it once at load; after that a save carries the whole thing, so
     *  the goals can be checked without touching the resource fork. */
    MISC_SCEN_ACTIVE  = 1050, /* A5+0x2C78 byte                         */
    MISC_SCEN_MONTHS  = 1051, /* A5+0x2C48 word, counts DOWN            */
    MISC_GOAL_POP     = 1052, /* A5+0x2C4A                              */
    MISC_GOAL_RES     = 1053,
    MISC_GOAL_COM     = 1054,
    MISC_GOAL_IND     = 1055,
    MISC_GOAL_CASH    = 1056, /* against funds minus bonds              */
    MISC_GOAL_LANDVAL = 1057,
    MISC_GOAL_LIFE    = 1058, /* word, against MISC_AGE_W65             */
    MISC_GOAL_EDU     = 1059, /* word, against MISC_AGE_W90             */
    MISC_LIMIT_PLT    = 1060, /* zero means NO limit                    */
    MISC_LIMIT_CRM    = 1061,
    MISC_LIMIT_TRF    = 1062,
    MISC_BUILD_ONE    = 1063, /* a building id, 0 for none              */
    MISC_BUILD_TWO    = 1064,
    MISC_TILES_ONE    = 1065,
    MISC_TILES_TWO    = 1066,
    MISC_1EFE         = 1018, /* A5+0x1EFE, a term in the transit budget*/
    MISC_POPULATION   = 1035, /* A5+0x1E96, offset 0x102C               */
    MISC_STAGE        = 8,    /* A5+0x1E36, the city's size stage, 0..  */
    MISC_SPEED        = 1019, /* A5+0x0C0A, 1 paused, 2..5 the speeds    */
    MISC_IND_SUPPLIED = 92,   /* A5+0x1EEE, already supplied, stride 3   */
    MISC_IND_WORKERS  = 93,   /* A5+0x1EF2, workers per industry, stride 3 */
    MISC_IND_LEVEL    = 91,   /* A5+0x1EEA, eleven industry levels, stride 3 */
    MISC_IND4_B       = 440,  /* A5+0x1ECA, its partner series, stride 4 */
    MISC_IND4_BASE    = 439,  /* A5+0x1ECE, four more indicators, stride 4 */
    MISC_POP_INCREASE = 16,   /* A5+0x1E22 -- NOT 0x1E9A; the arrival
                               *  and departure counters are transient,
                               *  cleared by $33FD8, and never saved.    */
    MISC_ACCUM8       = 380,  /* A5+0x1EBA block, population per zone   */
    MISC_AGE_HEAD     = 17,   /* A5+0x1EA2, heads in brackets 4..10     */
    MISC_AGE_W65      = 18,   /* A5+0x1EAA, weighted by (65 - bracket)  */
    MISC_AGE_W90      = 19,   /* A5+0x1EA6, weighted by the other curve  */
    MISC_HIST_BASE    = 31,   /* A5+0x1EDE/0x1EE2/0x1EE6 interleaved    */
    MISC_NAT_INDEX    = 20,   /* A5+0x1EB2, the national indicator      */
    MISC_NAT_INDEX2   = 21,   /* A5+0x1EB6, the second indicator        */
    MISC_NAT_MOOD     = 22,   /* A5+0x1EAE, how excitable the nation is */
    MISC_NAT_CYCLE    = 23,   /* A5+0x1EB0, growth rate and table index */
    MISC_RCI_DEMAND   = 454,  /* A5+0x1E3C..0x1E40, R/C/I demand       */
    MISC_MIL_MODE     = 915,  /* A5+0x1FC0, which stage the base is at */
    MISC_TRANSIT_BUS  = 1045, /* A5+0x1246, trips that used a bus      */
    MISC_TRANSIT_RAIL = 1046, /* A5+0x124A                             */
    MISC_TRANSIT_SUB  = 1047, /* A5+0x124E                             */
    MISC_POLICE_TERM  = 1039, /* A5+0x2C8E, scales the police radius    */
    /*  A5+0x2C98.  The ARCOLOGY POPULATION, and the microsim pass at
     *  $101AC is the only thing that writes it: $1114A stores what the
     *  loop accumulated.  Graph series 0 adds it to the head count
     *  ($223BE) and the three tax departments take a share of it
     *  ($34602), so a city whose arcologies are not being counted has a
     *  frozen population term and a frozen tax base. */
    MISC_ARCO_POP = 1032,
    MISC_2C98     = 1032, /* the older name, kept for the call sites */
    /*  A5+0x2C92.  The city-wide POLICE WORKLOAD: every station's share
     *  of the crime total, summed by the microsim pass at $10580 and
     *  stored at $11144.  It saturates at 0xFFFF rather than wrapping. */
    MISC_POLICE_LOAD = 1038,

    /*  Two blocks rather than single slots.  The census is one word per
     *  building id; the budget is 16 department records of 27 longs
     *  each, laid out amount, funding, accrued, then the twelve-month
     *  history of each of the first two. */
    MISC_CENSUS   = 124, /* .. 379, 256 words                      */
    MISC_YEAR_END = 911, /* A5+0x2C7A, January reconciliation due  */
    MISC_BUDGET   = 479  /* .. 910, 16 * 27 longs                  */
};

/* ------------------------------------------------------------------ *
 *  The budget block ($2C30, NewPtr 0x700): sixteen department records
 *  of 0x70 bytes.  $263C8 recomputes `amount` every month from the tile
 *  census, multiplies it by `funding` into `accrued`, and settles the
 *  year's accrual into the treasury each January.
 *
 *  Departments 0..3 are revenue and 4..15 are costs -- the sign is in
 *  DEPT_YEAR_DIVISOR rather than in the amounts.  The two the
 *  simulation reads back are police and fire, whose `funding` sets how
 *  far a station's coverage reaches ($23E0C, $23E6C).
 * ------------------------------------------------------------------ */
#define N_DEPT 16

enum
{
    DEPT_ORDINANCE = 3,
    DEPT_BONDS     = 4,
    DEPT_POLICE    = 5,
    DEPT_FIRE      = 6,
    DEPT_HEALTH    = 7,
    DEPT_SCHOOL    = 8,
    DEPT_COLLEGE   = 9,
    DEPT_ROAD      = 10,
    DEPT_HIGHWAY   = 11,
    DEPT_SUBWAY    = 12,
    DEPT_RAIL      = 13,
    DEPT_TRANSIT   = 14,
    DEPT_POWER     = 15
};

/*  ------------------------------------------------------------------
 *  XGRP -- the graph history.
 *
 *  $2D52E allocates one 3328-byte block and $2D54A hands out 0x34
 *  longs of it to each of sixteen series, storing the sixteen pointers
 *  at A5+0x2BDC.  The save file's XGRP chunk is that block verbatim,
 *  which is why it is exactly 16 * 52 * 4 bytes.
 *
 *  Every series carries three time bases in the one array:
 *
 *      [0 .. 11]   one sample a month, the last twelve months
 *      [12 .. 31]  one sample every six months, the last twenty
 *      [32 .. 51]  one sample every five years, the last twenty
 *
 *  $22330 shifts the monthly band on every pass, the half-yearly band
 *  in January and July, and the five-yearly band every fifth January.
 *  A band's newest slot takes a copy of [0] rather than a fresh
 *  reading, so the three bands sample the same series at three rates.
 * ------------------------------------------------------------------ */
enum
{
    GRAPH_CITY_SIZE = 0, /* accum8[0] * 10, plus the arcology bonus  */
    GRAPH_RESIDENTS,     /* rci_pop[0] * 10                         */
    GRAPH_COMMERCE,      /* rci_pop[1] * 10                         */
    GRAPH_INDUSTRY,      /* rci_pop[2] * 10                         */
    GRAPH_TRAFFIC,       /* the four map-overlay averages, $224BA   */
    GRAPH_POLLUTION,
    GRAPH_VALUE,
    GRAPH_CRIME,
    GRAPH_POWER,        /* 100 - power_pct                         */
    GRAPH_WATER,        /* 100 - water_pct                         */
    GRAPH_HEALTH,       /* misc[MISC_AGE_W65], life expectancy     */
    GRAPH_EDUCATION,    /* misc[MISC_AGE_W90], the education score */
    GRAPH_UNEMPLOYMENT, /* accum8[7]*100 / (accum8[0]+accum8[7]+1) */
    GRAPH_NAT_GNP,      /* misc[MISC_NAT_INDEX2]                   */
    GRAPH_NAT_POP,      /* misc[MISC_NAT_INDEX]                    */
    GRAPH_FED_RATE,     /* misc[MISC_NAT_MOOD]                     */
    N_GRAPH = 16
};

/* ------------------------------------------------------------------ *
 *  The seven things a citizen can complain about, in the order the
 *  poll at $3152A builds its weights.  Each weight is how much of the
 *  hundred-citizen sample that problem draws; whatever is left over
 *  after all seven is contentment, and land value alone supplies it.
 * ------------------------------------------------------------------ */
enum
{
    PROBLEM_TRAFFIC = 0,
    PROBLEM_POLLUTION,
    PROBLEM_CRIME,
    PROBLEM_TAXES,
    PROBLEM_UNEMPLOYMENT,
    PROBLEM_EDUCATION,
    PROBLEM_HEALTH,
    N_PROBLEM = 7
};

enum
{
    GRAPH_MONTH      = 0,  /* the three bands, as first slot and    */
    GRAPH_N_MONTH    = 12, /*  length within each series            */
    GRAPH_HALFYEAR   = 12,
    GRAPH_N_HALFYEAR = 20,
    GRAPH_FIVEYEAR   = 32,
    GRAPH_N_FIVEYEAR = 20,
    GRAPH_SAMPLES    = 52
};

/* ------------------------------------------------------------------ *
 *  XMIC, the microsimulation table.  A5+0x2BC6 points at 150 records of
 *  eight bytes, one per special building on the map -- power plants,
 *  the stations, the arcologies, the marina.  `$101AC` walks records 1
 *  to 149 once a year, at the January settlement, and dispatches on the
 *  type through a 58-entry table at `$1027C`.
 *
 *  The layout is read off what the arms write: byte 0 is the building
 *  id, byte 1 a per-type byte (a funding percentage on the stations, a
 *  count elsewhere), and then three big-endian words.  What the three
 *  MEAN is the type's business and nothing else's -- a power plant's
 *  word 0 is its age, a marina's is its boat count.
 * ------------------------------------------------------------------ */
#define N_MICRO 150

typedef struct
{
    uint8_t type;  /* +0  a building id, 0 for an empty slot */
    uint8_t byte1; /* +1                                     */
    int16_t w[3];  /* +2, +4, +6                             */
} Micro;

typedef struct
{
    int32_t history_amount[12];  /* +0x00  one slot per month */
    int32_t history_funding[12]; /* +0x30                     */
    int32_t amount;              /* +0x60  what it costs or earns */
    int32_t funding;             /* +0x64  the funding level      */
    int32_t accrued;             /* +0x68  running total this year*/
} Dept;

/*  Ordinance bits the simulation itself reads.  The rest only move
 *  money, which is $41368's business. */
enum
{
    ORD_LEGALIZE_GAMBLING   = 0x0004, /* $2404E, raises crime      */
    ORD_VOLUNTEER_FIRE      = 0x0010, /* $23D56, +2 fire per tile  */
    ORD_NEIGHBOURHOOD_WATCH = 0x0800  /* $23D2E, +2 police per tile*/
};

/*  All five above came out of tools/miscmap2.py, which runs the MISC
 *  builder at $2A186 under a small 68k interpreter and records which A5
 *  global feeds each slot.  MISC_POPULATION was found independently by
 *  searching the corpus first; the emulator agreeing with that search is
 *  the reason to trust both. */

typedef struct
{
    /* --- full-resolution tile layers ----------------------------- */
    uint16_t altm[MAP_H][MAP_W]; /* A5+0x1FC2  altitude, low 5 bits   */
    uint8_t  xbld[MAP_H][MAP_W]; /* A5+0x21C2  building id            */
    uint8_t  xzon[MAP_H][MAP_W]; /* A5+0x23C2  zone + corner mask     */
    uint8_t  xter[MAP_H][MAP_W]; /* A5+0x25C2  terrain / slope        */
    uint8_t  xund[MAP_H][MAP_W]; /* A5+0x27C2  pipes, subway, tunnels */
    uint8_t  xtxt[MAP_H][MAP_W]; /* A5+0x29C2  sign / label index     */
    uint8_t  xbit[MAP_H][MAP_W]; /* A5+0x1BBA  flag bits              */

    /* --- half-resolution data layers ----------------------------- */
    uint8_t xtrf[HALF_H][HALF_W]; /* A5+0x15BA  traffic     */
    uint8_t xplt[HALF_H][HALF_W]; /* A5+0x16BA  pollution   */
    uint8_t xval[HALF_H][HALF_W]; /* A5+0x17BA  land value  */
    uint8_t xcrm[HALF_H][HALF_W]; /* A5+0x18BA  crime       */

    /* --- quarter-resolution data layers -------------------------- */
    uint8_t xplc[QTR_H][QTR_W]; /* A5+0x19BA  police coverage    */
    uint8_t xfir[QTR_H][QTR_W]; /* A5+0x1A3A  fire coverage      */
    uint8_t xpop[QTR_H][QTR_W]; /* A5+0x1ABA  population density */
    uint8_t xrog[QTR_H][QTR_W]; /* A5+0x1B3A  rate of growth     */

    /* --- scalars, named where the disassembly named them --------- */
    int32_t date;         /* A5+0x1E1E  days since founding   */
    int32_t funds;        /* A5+0x1E26                        */
    int32_t bonds;        /* A5+0x1E2A                        */
    int16_t rotation;     /* A5+0x2C24  0..3                  */
    int16_t year_founded; /* A5+0x0BF2                        */
    int16_t difficulty;   /* A5+0x139E                        */

    int32_t land_value_tot; /* A5+0x1E76                        */
    int32_t crime_tot;      /* A5+0x1E7A                        */
    int32_t traffic_tot;    /* A5+0x1E7E                        */
    int32_t pollution_tot;  /* A5+0x1E82  cleared by $2317E     */
    /*  The four map-overlay averages live in the graph history, not in
     *  globals of their own: A5+0x2BEC, 0x2BF0, 0x2BF4 and 0x2BF8 are
     *  entries 4 to 7 of the pointer table at A5+0x2BDC, and each
     *  average is that series' newest sample.  Read them as
     *  graph[GRAPH_TRAFFIC][0] and its three neighbours. */
    int16_t developed;      /* A5+0x11D0  tiles that set the half-res
                             *  developed mark at $2360E; the divisor
                             *  three of the four averages use   */
    int32_t unemployment;   /* A5+0x2C82, also graph series 12   */
    int32_t power_pct;      /* A5+0x1E86  computed by $20FC4    */
    int32_t power_capacity; /* A5+0x11D6, what the plants make */
    int32_t water_capacity; /* A5+0x11D2, what the pumps make  */
    int32_t water_pct;      /* A5+0x1E8A  computed by $2156E    */
    int32_t population;     /* A5+0x1E96  set by $33FAE, MISC[1035] */
    int32_t pop_increase;   /* A5+0x1E9A                        */
    int32_t pop_decrease;   /* A5+0x1E9E                        */

    int32_t ordinances;    /* A5+0x1E6E  MISC[1000]            */
    int16_t temperature;   /* A5+0x1F00  MISC[24]           */
    int16_t weather1;      /* A5+0x1F01  MISC[25]              */
    int16_t weather2;      /* A5+0x1F02  MISC[26]              */
    int16_t weather_state; /* A5+0x1F03  MISC[27], 0..11     */
    /*  A5+0x2C86, MISC[912].  This is the city's WATER LEVEL: $128DE
     *  compares a tile's altitude against it to decide land or water,
     *  and writes it into ALTM bits 5..9 of every tile it drowns.  In
     *  Charleston, Hollywood and Flint every single water tile carries
     *  exactly this value there.  It also feeds pump capacity, which is
     *  how it came to be called pump_term. */
    int16_t water_level;
    int16_t worst_problem;  /* A5-0x1254, $FFFF until something
                             *  is recorded                   */
    int16_t city_mode;      /* A5-0x7DE6  0 while the terrain is
                             *  being edited, 1 in a city        */
    int16_t  rci_demand[3]; /* A5+0x1E3C  R,C,I in -2000..+2000 */
    int32_t  accum8[8];     /* A5+0x1EBA  per-zone population    */
    uint16_t census[256];   /* A5+0x1EF6  count per building id  */
    int16_t  infra[16];     /* A5+0x1EFA  counters for 0xDD..0xF9*/

    Dept dept[N_DEPT]; /* A5+0x2C30  the budget block       */

    /*  A5+0x2BDC -> a 0xD00-byte block, sixteen series of 52 longs.
     *  This is the XGRP chunk.  See the note above the GRAPH_ names. */
    int32_t graph[N_GRAPH][GRAPH_SAMPLES];
    /*  A5+0x2C1C -> sixteen longs (_NewPtr #$40), the vertical scale
     *  the graph window draws each series against, kept as a running
     *  maximum.  Series 0 to 3 share one scale and 4 to 7 share
     *  another; series 13 borrows 14's when 14's is the larger.  Not
     *  in the save file: it is rebuilt from the samples. */
    int32_t graph_max[N_GRAPH];
    int16_t industry_level[11];  /* A5+0x1EEA, what it buys now */
    int32_t industry_scaled[11]; /* the pass's own working copy */
    int32_t industry_mix[11];    /* what the nation wants, $3551E */
    /*  A5+0x1EBE -- three longs (_NewPtr #$c at $2DCA2), the
     *  residential, commercial and industrial share of the head count.
     *  They sum to accum8[0], which is why graph series 0 equals the
     *  sum of series 1 to 3.  Index 2 is what the employment balance
     *  at $35B88 weighs the workforce against.  Not saved. */
    int32_t rci_pop[3];
    int32_t years;           /* A5+0x1E38, cityDate / 300, $15268 */
    int16_t month;           /* A5+0x1E32  (date/25) % 12, $1523E */
    int16_t police_term;     /* A5+0x2C8E  MISC[1039]             */
    int16_t plane_count;     /* A5+0x12E0  at most two        */
    int16_t heli_count;      /* A5+0x12E2  at most one        */
    int32_t heli_timer;      /* A5+0x12F0  a LONG: $C92C compares
                              *  and $C948 stores four bytes      */
    int32_t ticks;           /* what _TickCount answers.  The
                              *  interpreter hands back a counter
                              *  that rises 64 a call, and $C928 is
                              *  the only place the simulation asks
                              *  the clock at all.                */
    int16_t view_y;          /* A5-0x7982  where the player looks */
    int16_t view_x;          /* A5-0x7980                     */
    int32_t raise_cost;      /* A5+0x61E   25 a step          */
    int16_t hurricane_timer; /* A5+0x1056                     */
    int16_t flood_timer;     /* A5+0x1058  flood countdown    */
    int16_t burnt_other;     /* A5+0x2C94                     */
    int16_t burnt_road;      /* A5+0x2C96                     */
    /*  A5+0x13A0, MISC[28].  Which disaster the trigger at $3151C
     *  chose, as an index into the nineteen-way table.  The two
     *  weather-driven ones set it directly instead. */
    /*  A5+0x13AA, MISC[1024].  The player's "no disasters" switch;
     *  $310A6 returns on it before anything is rolled. */
    int16_t disasters_off;
    int16_t disaster_kind;
    /*  A5-0x7982 and A5-0x7980, written by the scan at $23508 -- the
     *  listing prints the displacement as $867e, which is signed and so
     *  negative.  MISC[1030] and [1031].  The disasters that start near
     *  the middle of town read them. */
    int16_t centre_y;
    int16_t centre_x;
    /* ---------------------------------------------------------- *
     *  The February opinion poll ($3152A).  None of the three is in
     *  MISC: the poll runs afresh every year, and a freshly loaded
     *  city polls as if nobody had ever been asked.
     * ---------------------------------------------------------- */
    int16_t problem_rank[N_PROBLEM];  /* A5+0x106E, worst first  */
    int16_t problem_votes[N_PROBLEM]; /* A5+0x107C, votes for it */
    int16_t approval;                 /* A5+0x108A, of a hundred */

    int16_t disaster_h;     /* A5+0x13A4  a Mac Point, h then v */
    int16_t disaster_v;     /* A5+0x13A2                     */
    int16_t monster_count;  /* A5+0x12EA  at most one; while it
                             *  is set no aeroplane will fly  */
    int16_t road_count;     /* A5+0x12EC                     */
    int16_t anim_phase;     /* A5+0x12F8  wraps past $3FF    */
    int16_t tornado_count;  /* A5+0x12EE  at most one        */
    int16_t thing_focus;    /* A5+0x2C9C  the record the view
                             *  follows                      */
    int16_t ship_count;     /* A5+0x12E4  one ship at a time  */
    int16_t ship_y, ship_x; /* A5+0x12F4 / 0x12F6            */
    int16_t boat_count;     /* A5+0x12E8  sailboats alive, at most
                             *  four, and no more than one per nine
                             *  marina tiles ($31BB8)          */
    int16_t count_12E6;     /* A5+0x12E6  raised at $BF6C and $D412,
                             *  capped at one by $BD8E         */
    int16_t transit_term;   /* A5+0x1EFE  MISC[1018]             */
    int32_t transit_bus;    /* A5+0x1246  MISC[1045]             */
    int32_t transit_rail;   /* A5+0x124A  MISC[1046]             */
    int32_t transit_subway; /* A5+0x124E  MISC[1047]             */
    uint8_t year_end;       /* A5+0x2C7A  MISC[911]              */

    /* --- raw MISC, so unmodelled fields survive a round trip ----- */
    int32_t misc[MISC_LONGS];

    /* --- chunks we do not model yet, kept verbatim --------------- */
    uint8_t *xlab;
    size_t   xlab_len; /* 6400  */
    uint8_t *xmic;
    size_t   xmic_len; /* 1200 = 150 records of 8 bytes, see Micro */
    uint8_t *xthg;
    size_t   xthg_len; /*  480  */
    uint8_t *xgrp;
    size_t   xgrp_len; /* 3328  */
    uint8_t *cnam;
    size_t   cnam_len;

    /* original chunk order, so a re-save matches the source file */
    char order[24][5];
    int  n_chunks;
} City;

/* ---- city.c ------------------------------------------------------ */
size_t sc2_rle_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap);
size_t sc2_rle_encode(const uint8_t *src, size_t size, uint8_t *dst); /* writeChunkRLE $293EC */
/*  Scatter the raw MISC block into the named scalars.  city_load calls
 *  it, and so does the .arco reader, so a world loaded either way goes
 *  through the same code and the two cannot drift apart. */
void city_misc_to_scalars(City *c);
int  city_load(const char *path, City *c);
int  city_save(const char *path, const City *c);
void city_free(City *c);

/* ---- sim.c ------------------------------------------------------- */
void sim_set_tile(City *c, int y, int x, uint8_t bld);            /* $4110  */
int  sim_place_special(City *c, int y, int x, int bld, int zone); /* $333C8 */
int  sim_auto_rail_station(City *c, int y, int x);                /* $B058 */
/*  $0221A8.  Returns 1 for a win, -1 for a loss, 0 while the scenario
 *  is still running.  It decrements the month counter, so call it once
 *  a month and no more. */
int sim_scenario_check(City *c); /* $0221A8 */

/*  simTick $21EDE.  One phase of the 25-phase clock: the date advances,
 *  and the date modulo 25 selects the phase.  Returns one of the events
 *  the phase raised, or SIM_EV_NONE.  The interface work the phases do
 *  in the original -- dialogs, the newspaper, the graph windows -- is
 *  named in the code and left to the caller. */
enum
{
    SIM_EV_NONE = 0,
    SIM_EV_STAGE,    /* phase 22 promoted the city a stage, MISC[8]  */
    SIM_EV_SCEN_WON, /* phase 22, sim_scenario_check                 */
    SIM_EV_SCEN_LOST,
    SIM_EV_BANKRUPT, /* phase 22, funds below -100000, $230E6(2)     */
    SIM_EV_APPROVAL  /* phase 0, the poll crossed 80%, $3152A        */
};
int  sim_tick(City *c);
int  sim_start_fire_near(City *c);                        /* $38002 */
int  sim_disaster_riot(City *c);                          /* $37D34 */
int  sim_burn_tile(City *c, int y, int x, int even_bare); /* $39B70 */
int  sim_disaster_firestorm(City *c);                     /* $37C66 */
int  sim_flood_spread(City *c);                           /* $379FC */
int  sim_disaster_flood(City *c);                         /* $37940 */
int  sim_disaster_chemical(City *c);                      /* $37888 */
int  sim_disaster_pollution(City *c);                     /* $37FB6 */
int  sim_disaster_air_crash(City *c);                     /* $38186 */
int  sim_disaster_fire(City *c);                          /* $38290 */
int  sim_disaster_tornado(City *c);                       /* $38766 */
int  sim_disaster_monster(City *c);                       /* $38574 */
int  sim_disaster_microwave(City *c);                     /* $38B6C */
void sim_demolish_tile(City *c, int y, int x, int flag_c, int scorch);
/* $3A000 */
void sim_demolish_and_place(City *c, int y, int x, int even_bare);
/* $5FAA */
void sim_step_things(City *c);                     /* $09E0A */
void sim_fix_terrain(City *c, int y, int x);       /* $128DE */
void sim_fix_neighbourhood(City *c, int y, int x); /* $12C04 */
int  sim_can_raise(City *c, int y, int x);         /* $8758 */
int  sim_disaster_hurricane(City *c);              /* $3755A */
int  sim_disaster_meltdown(City *c);               /* $38916 */
int  sim_disaster_earthquake(City *c);             /* $383D4 */
int  sim_disaster_volcano(City *c);                /* $37DD6 */
void sim_raise_tile(City *c, int y, int x);        /* $896C */
int  sim_footprint_origin(const City *c, int *py, int *px, int bld);
/* $763A */
void       sim_rebuild_census(City *c);
void       sim_power_grid(City *c);                                         /* powerGridReset $20FC4 */
void       sim_water_grid(City *c);                                         /* waterGrid $2156E */
void       sim_traffic_total(City *c);                                      /* trafficTotal $2530E */
void       sim_pollution(City *c);                                          /* $2317E stages 1-2 */
void       sim_population(City *c);                                         /* populationPass $33FAE */
void       sim_demand(City *c);                                             /* $34068, the rest of $33FAE */
void       sim_forest(City *c);                                             /* $34792, trees creeping back */
void       sim_news_rolls(City *c);                                         /* $348F0, dice the paper eats */
void       sim_weather(City *c);                                            /* $34C58, the weather walk    */
void       sim_city_centre(City *c, int *cy, int *cx);                      /* $23432 */
void       sim_land_value(City *c);                                         /* $2317E stages 4-5 */
void       sim_crime(City *c);                                              /* $2317E stage 9   */
void       sim_density(City *c);                                            /* $2317E stage 7   */
void       sim_coverage(City *c);                                           /* $2317E stages 6-7*/
void       sim_place(City *c, int y, int x, int tier, int kind);            /* placeBuilding $3258A */
void       sim_grow_footprint(City *c, int y, int x, int tier, int zone);   /* growFootprint $32998 */
void       sim_upgrade(City *c, int y, int x, int tier, int coin);          /* upgradeBuilding $33028 */
void       sim_build_church(City *c, int y, int x);                         /* buildChurch $32830 */
int        sim_trip(City *c, int y, int x, int zone, int tier, int budget); /* tripGenerate $245E8 */
void       sim_growth_scan(City *c, int y0, int x0);                        /* growthScan $3170E, phases 3-18 */
int32_t    sim_growth_unimplemented(void);
int32_t    sim_growth_stub(int i);
extern int trip_mark_log;
void       sim_overlay_averages(City *c); /* $224BA, inside $22330 */
void       sim_graph_pass(City *c);       /* $22330 one month      */
void       sim_economy(City *c);          /* economyPass $34D04, phase 21 */
/*  $101AC.  The year-end microsimulation: every special building on the
 *  map gets its turn.  budgetPass calls it from the January settlement,
 *  right after the departments are reconciled. */
void    sim_microsim(City *c);
void    sim_budget(City *c);               /* budgetPass $263C8, phase 0  */
int     sim_opinion_poll(City *c);         /* $3152A February poll  */
int32_t sim_map_population(const City *c); /* $31DDA rule */

/* ---- rng.c ------------------------------------------------------- */
void     rng_seed(int32_t toolbox_seed, uint16_t lfsr_seed);
int16_t  Random(void);          /* Toolbox _Random, 329 call sites */
uint16_t game_rand(uint16_t n); /* $20F30 LFSR, 34 call sites      */
uint16_t lib_rand(uint16_t n);  /* $20EE6 THINK C rand()           */

/*  The same shift register reduced by a mask instead of a divide.
 *  $20F4C..$20FAC are one routine repeated with a different andi.w;
 *  they all advance the shared state, so call order is part of the
 *  behaviour. */
uint16_t game_rand1(void);   /* $20F4C  & 1   */
uint16_t game_rand3(void);   /* $20F64  & 3   */
uint16_t game_rand15(void);  /* $20F7C  & 15  */
uint16_t game_rand63(void);  /* $20F94  & 63  */
uint16_t game_rand127(void); /* $20FAC  & 127 */

void     rng_log_start(void);
void     rng_log_mark(int32_t v);
int      rng_log_count(void);
int      rng_log_entry(int i, int32_t *v);
uint16_t rng_lfsr(void); /* current $11DC, for the oracle */
int32_t  rng_toolbox_seed(void);

/* ---- tables.c (generated) ---------------------------------------- */
extern const uint8_t WEATHER_CLOUD[12];     /* A5+0x0A4E             */
extern const uint8_t WEATHER_WIND[12];      /* A5+0x0A5A             */
extern const uint8_t WEATHER_TEMP[12];      /* A5+0x0A66             */
extern const uint8_t WEATHER_NEXT[384];     /* A5+0x0A72 [4][12][8]  */
extern const int16_t DISASTER_ODDS[4];      /* A5-0x15EA, $310B0     */
extern const int16_t DEMAND_TAX[24];        /* A5-0x1404, $346CE        */
extern const float   DEMAND_LEVEL[4];       /* A5-0x12FA, $341D8        */
extern const int16_t GROWTH_TABLE[515];     /* A5-0x141E               */
extern const int16_t ROT_CORNER_MASK[4];    /* A5-0x7DD4               */
extern const int16_t DEPT_YEAR_DIVISOR[16]; /* A5-0x387C, read by $263F4 */

/*  One cell of the coverage diamond a station stamps out: an offset in
 *  32x32 coverage-layer units, and which strength ring it belongs to. */
typedef struct
{
    int8_t dy, dx, ring;
} CoverageCell;
extern const CoverageCell COVERAGE_KERNEL[];
extern const int          COVERAGE_KERNEL_LEN;
#define COVERAGE_RINGS 5

/*  One entry of the road walk's state machine: where a step of the trip
 *  in $245E8 leaves it. */
typedef struct
{
    int8_t mode;  /* transport mode to continue in */
    int8_t cost;  /* added to the trip length      */
    int8_t moved; /* the step was accepted         */
    int8_t arrived;
} WalkStep;

extern const int16_t ZONE_ATTRACTS[16];  /* A5-0x38BC the trip model */
extern const int32_t NAT_ERA_MIX[55];    /* A5-0x13D6, 5 eras x 11 industries */
extern const int32_t CITY_STAGE_POP[10]; /* A5-0x3ED8, stage promotion ladder */
extern const int16_t NAT_RATE_TABLE[12]; /* A5-0x12EA */
/*  One building, and everything the simulation knows about it.  This
 *  replaced five parallel arrays with three different index bases. */
typedef struct
{
    const char *name;       /* A5-0x6D42, only the specials from $C6 up */
    uint8_t     pollution;  /* A5-0x3A12, read by the city scan         */
    uint8_t     population; /* A5-0x3982, read by the density pass      */
    /*  A5-0x1570.  One to four for the zone buildings it was written
     *  for, but $31D2E indexes it by id - 0x70 for every id from $70
     *  up, so ids $C6 and above read past its 86 entries into the
     *  neighbouring A5 data.  A runway standing on an industrial zone
     *  reads 514 that way, which is why this is a word.              */
    int16_t tier;
    uint8_t tier_flag; /* A5-0x14C4, gates growth at $31DD0        */
    uint8_t size;      /* A5-0x1252, the footprint edge, $7694.  The
                        *  original table starts at id $70; ids below
                        *  that are all one tile.                  */
    uint16_t dept;     /* which maintenance departments the tile is
                        *  charged to, one bit each.  The original
                        *  decides this with fifteen nested range
                        *  tests at $2658C; ids from $70 up pay
                        *  nothing.                                */
    int16_t power;     /* the plant's output in MW, from the switch
                        *  at $21174.  Two plants compute their own:
                        *  -1 is wind, -2 solar.                   */
    uint8_t sprite_h;  /* the shape height from the TSET resource.
                        *  Not just art: $C2DA divides it by three
                        *  and crashes an aeroplane into anything
                        *  taller than it is flying.              */
} Building;
extern const Building BUILDING[256];

extern const int16_t BLD_CHOICE_BASE[20];   /* A5-0x146E 5 kinds x 4 tiers */
extern const int16_t BLD_CHOICE_COUNT[20];  /* A5-0x1446                   */
extern const int16_t MICRO_REBUILD_IDX[11]; /* A5-0x5198 */
extern const int32_t BUILD_COST[24];        /* A5+0x6A6   */
extern const int16_t ROT_CORNER_4[16];      /* A5-0x7DCC                   */
extern const uint8_t WALK_TURN_MASK[4];     /* A5-0x38AE                */
extern const int16_t WALK_DY[4];            /* A5-0x392C                */
extern const int16_t WALK_DX[4];            /* A5-0x3924                */
extern const int16_t NEIGHBOUR_ORDER[48];   /* A5-0x391C 24 {dx,dy}     */

/*  What one ordinance does to the treasury: a signed fraction of a
 *  department's amount.  source 0..2 is a department, 3 is the
 *  population term, -1 is free. */
typedef struct
{
    int8_t source, num, den;
} OrdinanceCost;
extern const OrdinanceCost ORDINANCE_COST[20];
int32_t                    sim_ordinance_cost(const City *c, int which); /* ordinanceCost $41368 */

#endif /* SC2K_H */
