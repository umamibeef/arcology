/*  sim_thing.c -- the moving things: trains, boats, ships, helicopters and
 *  planes.  The XTHG slot table and everything that walks it -- allocating
 *  and freeing a slot, the per-kind step routines, and the two spawners the
 *  rest of the simulation calls when a station or a seaport wants one.
 *  Split out of sim.c, which is where the rest of the phase structure still
 *  lives; the same rules apply here as there, and the addresses in the
 *  comments still point into CODE 2. */
#include "ext80.h"
#include "sim.h"
#include "sim_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int STEP_DY[4] = {0, 1, 0, -1}; /* A5-0x622E, and A5-0x619C */
const int STEP_DX[4] = {-1, 0, 1, 0}; /* A5-0x6226, and A5-0x6194 */
const int TURN_A[4]  = {0, 1, 3, 2};  /* A5-0x61A4 */
const int TURN_B[4]  = {0, 3, 1, 2};  /* A5-0x61A0 */

/*  A5-0x635A, indexed by type.  A zero here and the driver skips the
 *  record entirely. */
static const uint8_t THING_ENABLED[18] = {0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0};

/*  A5-0x61B4, four bytes a direction: the sprite a train shows when it
 *  turns from one heading to another. */
static const uint8_t TURN_SPRITE[16] = {0, 1, 2, 7, 1, 2, 3, 4, 2, 3, 4, 5, 7, 4, 5, 6};

/*  A5-0x618C and A5-0x6184, the pixel step per heading.  The two tables
 *  are four words each and adjacent, so the column table is simply the
 *  row table read four entries further on. */
static const int SUBSTEP[8] = {0, 16, 0, -16, -16, 0, 16, 0};

/*  A5-0x612C and A5-0x611C: how far ahead the ship checks. */
static const int SHIP_DY[8] = {0, 3, 3, 3, 0, -3, -3, -3};
static const int SHIP_DX[8] = {-3, -3, 0, 3, 3, 3, 0, -3};

/*  A5-0x6102 and A5-0x60F2: the pixel step it actually takes.  The two
 *  differ -- the diagonals move four pixels on their long axis but the
 *  lookahead is three tiles in every direction. */
static const int SHIP_SUB_DY[8] = {0, 3, 4, 3, 0, -3, -4, -3};
static const int SHIP_SUB_DX[8] = {-4, -3, 0, 3, 4, 3, 0, -3};

/*  A5-0x613C and A5-0x6134: the four tiles two steps out that state 0
 *  checks for a wharf. */
static const int DOCK_DY[4] = {2, 0, -2, 0};
static const int DOCK_DX[4] = {0, 2, 0, -2};

/*  A5-0x615C and A5-0x614C: the order headings are tried in when the
 *  way ahead is shut -- clockwise and anticlockwise.  Both are nine
 *  long, and the ninth entry is not a continuation of the sweep. */
static const int TURN_CW[9]  = {0, 1, 2, 3, 4, 5, 6, 7, 0};
static const int TURN_CCW[9] = {0, 7, 6, 5, 4, 3, 2, 1, 2};

/*  The nine building ids at A5-0x610C that a ship may sail over: the
 *  bridge pieces it passes under. */
static const uint8_t SHIP_PASSABLE[9] = {0x51, 0x52, 0x54, 0x55, 0x58, 0x59, 0x5B, 0x5C, 0x6B};

/*  A5-0x638A and A5-0x637A: a plain eight-way unit step.  A5-0x621E
 *  scales it -- the helicopter passes index 2, so four pixels a frame. */
static const int STEP8_DY[8]   = {0, 1, 1, 1, 0, -1, -1, -1};
static const int STEP8_DX[8]   = {-1, -1, 0, 1, 1, 1, 0, -1};
static const int MOVE_SPEED[8] = {0, 8, 4, 2, 0, 8, 0, 0};

/*  A5-0x61F4 / A5-0x61E4, the same three-tile lookahead the ship uses. */
static const int HELI_DY[8] = {0, 3, 3, 3, 0, -3, -3, -3};
static const int HELI_DX[8] = {-3, -3, 0, 3, 3, 3, 0, -3};

/*  A5-0x61FC: the order alternative headings are tried in, alternating
 *  outward from the current one. */
static const int HELI_SWEEP[7] = {1, 7, 2, 6, 3, 5, 4};

/*  $9DDA  allocThing -- first free record, or 0 when the table is full. */
int alloc_thing(City *c)
{
    int i;
    if (!c->xthg || c->xthg_len < (size_t)(THING_N * THING_SZ))
        return 0;
    for (i = 1; i < THING_N; i++) /* $9DF4 */
        if (c->xthg[i * THING_SZ] == 0)
            break;
    return i == THING_N ? 0 : i; /* $9DFA */
}

uint8_t *thing(City *c, int slot) { return c->xthg + slot * THING_SZ; }

/*  $9D7E  freeThing -- give a record back and clear the tile it held. */
void free_thing(City *c, int slot)
{
    uint8_t *r = thing(c, slot);
    int      y, x;
    r[0] = 0; /* $9D90 */
    y    = r[3];
    x    = r[4];
    if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W)
        c->xtxt[y][x] = 0; /* $9DD0 */
}

/* ================================================================== *
 *  $38766  disasterTornado and $38574  disasterMonster -- disaster
 *  types 7 and 8.  Both put one record on the map at the disaster
 *  point and let the mover take it from there, and only one of each
 *  may exist at a time.
 *
 *  Both clamp the point into the map rather than refusing an off-map
 *  one, and both throw away whatever record already stood on the tile.
 * ================================================================== */
int spawn_disaster_thing(City *c, int kind)
{
    uint8_t *r;
    int      slot, y, x, t;

    slot = alloc_thing(c); /* $3877C */
    if (slot == 0)
        return 0;

    y = c->disaster_h;
    x = c->disaster_v;
    if (y < 0)
        y = 0; /* $38796 */
    if (x < 0)
        x = 0;
    if (y > 0x7F)
        y = 0x7F;
    if (x > 0x7F)
        x = 0x7F;

    t = c->xtxt[y][x]; /* $387C6 */
    if (t >= 0xC9 && t < 0xF1)
        free_thing(c, t - 0xC9); /* $387E2 */

    r    = thing(c, slot);
    r[0] = (uint8_t)kind;
    r[3] = (uint8_t)y;
    r[4] = (uint8_t)x;
    r[6] = 8;
    r[7] = 8;

    if (kind == 0x0F) /* the tornado */
    {
        r[1] = (uint8_t)(Random() & 7);             /* $387FA */
        r[8] = (uint8_t)((uint16_t)Random() % 128); /* $38844 */
        r[9] = (uint8_t)((uint16_t)Random() % 128);
        r[5] = (uint8_t)(c->altm[y][x] & 0x1F); /* $3889E */
    }
    else /* the monster */
    {
        r[1] = 2;                                   /* $3860E */
        r[8] = (uint8_t)((uint16_t)Random() % 128); /* $3864A */
        r[9] = (uint8_t)((uint16_t)Random() % 128);
        r[5] = 0x0F; /* $38698 */
    }

    r[0x0A] = c->xtxt[y][x]; /* $388C4, after the record above went away */
    r[2]    = 0;

    if (kind == 0x05) /* $386C6 -- in a scenario the monster is fixed */
    {
        if ((uint8_t)c->misc[MISC_SCEN_ACTIVE])
            r[0x0B] = 0;
        else if (Random() & 1)
            r[0x0B] = 0; /* $386FC */
        else
            r[0x0B] = (uint8_t)(((uint16_t)Random() % 3) + 1); /* $386F0 */
        c->monster_count++;
    }
    else
        c->tornado_count++;

    c->xtxt[y][x]  = (uint8_t)(slot + 0xC9); /* $38726 */
    c->thing_focus = (int16_t)slot;          /* $3872A */
    return 1;
}

/* ================================================================== *
 *  $DD7E  canTravel -- may a thing of this kind stand on this tile?
 *  A train wants rail; anything else wants the underground layer.  In
 *  both cases a tile already carrying a thing (XTXT >= $C9) is refused,
 *  which is what stops two trains sharing a tile.
 * ================================================================== */
static int can_travel(const City *c, int y, int x, int kind)
{
    int b;
    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0; /* $DDA4 */
    if (c->xtxt[y][x] >= 0xC9)
        return 0; /* $DDC0 */

    b = c->xbld[y][x];
    if (kind == 0x0A) /* $DDE2 -- rail */
    {
        if ((b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) ||
            (b >= 0x6C && b < 0x70))
            return 1; /* $DE0E */
        if (b == 0x4D || b == 0x4E || b == 0x5A || b == 0x5B)
            return 1;
        return 0; /* $DE9E */
    }
    {
        int u = c->xund[y][x]; /* $DE44 */
        if ((u >= 1 && u < 0x10) || u == 0x1F || u == 0x20 || u == 0x22 ||
            u == 0x23)
            return 1; /* $DE76 */
    }
    return (b >= 0x6C && b <= 0x70) ? 1 : 0; /* $DE9A */
}

/* ================================================================== *
 *  $E18E  pickDirection -- which way should a new thing face?  Try the
 *  four directions in one of two fixed orders, chosen by a coin flip,
 *  each rotated by `turn`, and take the first that is passable.
 *  Returns -1 when the thing is boxed in.
 * ================================================================== */
int pick_direction(City *c, int y, int x, int turn, int kind)
{
    int flip = (int)lib_rand(2); /* $E19A -- THINK C rand(), not the LFSR */
    int i;
    for (i = 0; i < 4; i++) /* $E250 */
    {
        int d   = flip ? TURN_A[i] : TURN_B[i]; /* $E1AE / $E1BA */
        int dir = (d + turn) & 3;               /* $E1CA */
        int ty  = y + STEP_DY[dir];             /* $E1D8 */
        int tx  = x + STEP_DX[dir];             /* $E1E6 */
        if (ty < 0 || ty >= MAP_H || tx < 0 || tx >= MAP_W)
            continue;
        if (!can_travel(c, ty, tx, kind))
            continue; /* $E222 */
        if (c->xtxt[ty][tx] >= 0xC9)
            continue; /* $E242 */
        return dir;   /* $E24A */
    }
    return -1; /* $E258 */
}

/*  $DEA6  derails -- is this tile something a train may NOT stand on?  Off
 *  the map counts as fine, which is what stops a train leaving the edge
 *  from being wrecked. */
static int derails(const City *c, int y, int x)
{
    int b, u;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0; /* $DECA */

    b = c->xbld[y][x];
    if ((b >= 0x2C && b < 0x3F) || (b >= 0x45 && b < 0x49) ||
        (b >= 0x6C && b < 0x70))
        return 0; /* $DF08, rail of some kind */
    if (b == 0x4D || b == 0x4E || b == 0x5A || b == 0x5B)
        return 0; /* $DF0E, the crossings */

    u = c->xund[y][x];
    if ((u >= 1 && u < 0x10) || u == 0x1F || u == 0x20 || u == 0x22 ||
        u == 0x23)
        return 0; /* $DF70, a subway underneath */

    b = c->xbld[y][x];
    if (b >= 0x6C && b <= 0x70)
        return 0; /* $DF94 -- note <= here, unlike the < above */
    return 1;     /* $DF98 */
}

/*  $DF9E  copyCar -- move one car's whole state onto the car behind it,
 *  demoting a locomotive to a carriage as it goes. */
static void copy_car(City *c, int src, int dst)
{
    uint8_t *a = thing(c, src), *b = thing(c, dst);
    b[3]    = a[3];
    b[4]    = a[4];
    b[6]    = a[6];
    b[7]    = a[7];
    b[0x0A] = a[0x0A];
    b[1]    = a[1];
    b[8]    = a[8];
    b[0]    = a[0];
    if (b[0] == 0x0A)
        b[0] = 0x0B; /* $E05E */
    if (b[0] == 0x0C)
        b[0] = 0x0D; /* $E072 */
}

/*  $E07E  reverseTrain -- the head and the tail change places and the
 *  whole train faces the other way. */
static void reverse_train(City *c, int head, int tail)
{
    uint8_t  *h  = thing(c, head);
    uint8_t  *t  = thing(c, tail); /* a2, the record the reads come from */
    const int oy = t[3], ox = t[4], keep = t[0x0A];
    int       dir = t[1]; /* $E0BE -- the TAIL's heading, not the head's */

    t[3]    = h[3]; /* $E0CA -- the tail takes the head's place */
    t[4]    = h[4];
    t[6]    = h[6];
    t[7]    = h[7];
    t[0x0A] = h[0x0A];

    h[3]    = (uint8_t)oy; /* $E126 -- and the head takes the tail's */
    h[4]    = (uint8_t)ox;
    h[6]    = (uint8_t)oy;
    h[7]    = (uint8_t)ox;
    h[0x0A] = (uint8_t)keep;

    /*  $E15C -- both of these land on the HEAD's record, indexed by the
     *  first argument, which is what makes the whole train face about. */
    dir  = ((dir & 0x0F) + 2) & 3;
    h[8] = (uint8_t)((dir + 4) & 7);
    h[1] = (uint8_t)dir;
}

/*  $BF96  spawnWreck -- what a derailed train leaves behind: one
 *  type-6 record on the tile it came off at. */
static void spawn_wreck(City *c, int y, int x)
{
    uint8_t *t;
    int      slot = alloc_thing(c); /* $BFA6 */

    if (slot == 0)
        return;
    if (c->xtxt[y][x] >= 0xC9)
        return; /* $BFC8 */

    t             = thing(c, slot);
    t[0]          = 6; /* $BFD8 */
    t[1]          = 0;
    t[3]          = (uint8_t)y;
    t[4]          = (uint8_t)x;
    t[6]          = 8;
    t[7]          = 8;
    t[5]          = 0;
    t[0x0A]       = c->xtxt[y][x]; /* $C036 */
    t[0x0B]       = 0;
    t[2]          = 0;
    c->xtxt[y][x] = (uint8_t)(slot + 0xC9); /* $C064 */
}

/* ================================================================== *
 *  $D7DE  stepTrain -- types 10 and 12, and with them every carriage.
 *
 *  A locomotive advances onto the tile it was already pointing at, the
 *  cars behind shuffle up one, and then a new tile ahead is chosen.
 *  Four things can interrupt that: a station beside it, a car already
 *  standing on the tile ahead, track that has gone, or nowhere left to
 *  go.
 * ================================================================== */
static void step_train(City *c, int slot)
{
    uint8_t *t = thing(c, slot);
    int      y = t[3], x = t[4];
    int      ay = t[6], ax = t[7]; /* the tile it is heading for */
    int      type = t[0];
    int      car2, car3, dir, nd, v, b;

    /*  $D81C -- half the time a locomotive stops beside a station. */
    if (type == 0x0A && game_rand1())
    {
        const int k = (t[1] & 1) ? (int)lib_rand(2) + 2 : (int)lib_rand(2);
        if (k == 0 && y > 0 && c->xbld[y - 1][x] == 0xED)
            return;
        if (k == 1 && y < 0x7F && c->xbld[y + 1][x] == 0xED)
            return;
        if (k == 2 && x > 0 && c->xbld[y][x - 1] == 0xED)
            return;
        if (k == 3 && x < 0x7F && c->xbld[y][x + 1] == 0xED)
            return;
    }

    car2 = t[2]; /* $D91C */
    car3 = thing(c, car2)[2];

    if (derails(c, y, x)) /* $D92E -- the track under it has gone */
    {
        uint8_t *b2 = thing(c, car2), *b3 = thing(c, car3);
        t[0] = b2[0] = b3[0] = 0; /* $D944 */
        if (c->xtxt[y][x] == slot + 0xC9)
            c->xtxt[y][x] = 0;
        if (c->xtxt[b2[3]][b2[4]] == car2 + 0xC9)
            c->xtxt[b2[3]][b2[4]] = 0;
        if (c->xtxt[b3[3]][b3[4]] == car3 + 0xC9)
            c->xtxt[b3[3]][b3[4]] = 0;
        spawn_wreck(c, y, x); /* $DA12 */
        return;
    }

    /*  $DA1E -- is the tile ahead free?  Our own tail car does not
     *  count, because it is about to move out of the way. */
    v = c->xtxt[ay][ax];
    if (v >= 0xC9 && (v - 0xC9) != car3)
        return; /* $DA52 */

    if (!(y == ay && x == ax)) /* $DA56 */
    {
        uint8_t *b2 = thing(c, car2), *b3 = thing(c, car3);
        c->xtxt[y][x]         = (uint8_t)(car2 + 0xC9); /* $DA7C */
        c->xtxt[b2[3]][b2[4]] = (uint8_t)(car3 + 0xC9); /* $DAB2 */
        c->xtxt[b3[3]][b3[4]] = b3[0x0A];               /* $DAD2 */
        copy_car(c, car2, car3);                        /* $DADC */
        copy_car(c, slot, car2);                        /* $DAE6 */
        t[0x0A]         = c->xtxt[ay][ax];              /* $DB0C */
        c->xtxt[ay][ax] = (uint8_t)(slot + 0xC9);       /* $DB26 */
    }

    t[3] = t[6]; /* $DB3C -- the locomotive is now where it was pointing */
    t[4] = t[7];
    dir  = t[1] & 0x0F;
    y    = t[3];
    x    = t[4];

    /*  $DB92 -- a tunnel mouth flips the train between the two forms. */
    b = c->xbld[y][x];
    if (b >= 0x6C && b <= 0x70)
    {
        t[0] = (uint8_t)((type == 0x0A) ? 0x0C : 0x0A);
        type = t[0];
    }

    /*  $DBD8 -- one turn in four is considered, left or right by a coin,
     *  and taken only if the track goes that way. */
    if (game_rand(4) == 0)
    {
        const int nw = game_rand(2) ? (dir + 1) & 3 : (dir + 3) & 3;
        if (can_travel(c, y + TRAIN_DY[nw], x + TRAIN_DX[nw], type))
            dir = nw; /* $DC50 */

        /*  $DC54 -- and then it sounds the horn, one time in 256.  The
         *  draw is taken either way and changes nothing, but it comes
         *  from the Toolbox generator that the boats and ships also
         *  read, so leaving it out is invisible in a city with only
         *  trains and wrong in every other one. */
        if ((Random() & 0xFF) == 0)
            ; /* $DC64, sound $20C */
    }

    {
        const int ty = y + TRAIN_DY[dir], tx = x + TRAIN_DX[dir];

        /*  $DCA2 is a `bne`: the track ahead being clear is the common
         *  case, and it simply carries on.  Note it masks the heading
         *  rather than storing the turned one, so a train that turned
         *  above keeps its old value at +1 while +6, +7 and +8 use the
         *  new one. */
        if (can_travel(c, ty, tx, type))
        {
            t[1] = (uint8_t)(t[1] & 0x0F); /* $DD44 */
            t[8] = (uint8_t)(dir * 2);
            t[6] = (uint8_t)ty;
            t[7] = (uint8_t)tx;
            return;
        }

        /*  blocked, so look for any way out at all */
        nd = pick_direction(c, y, x, dir, type); /* $DCB0 */
        if (nd < 0)
        {
            reverse_train(c, slot, car3); /* $DCC4 */
            return;
        }
        t[1] = (uint8_t)nd;                           /* $DCD8 */
        t[8] = TURN_SPRITE[(dir & 3) * 4 + (nd & 3)]; /* $DCF6 */
        t[6] = (uint8_t)(y + TRAIN_DY[nd]);           /* $DD10 */
        t[7] = (uint8_t)(x + TRAIN_DX[nd]);           /* $DD2C */
    }
}

/* ================================================================== *
 *  $E388  advanceSubtile -- move one step along the heading at +1.  The
 *  pixel offset at +6/+7 carries into the tile coordinate at +3/+4.
 *  Returns 0 only when the carry took the thing off the map, in which
 *  case the slot has been released.
 * ================================================================== */
static int advance_subtile(City *c, int slot)
{
    uint8_t  *t   = thing(c, slot);
    const int dir = t[1];
    int       py  = t[6] + SUBSTEP[dir];     /* $E3B0 */
    int       px  = t[7] + SUBSTEP[dir + 4]; /* $E3BE */
    int       cy = 0, cx = 0, carry = 0;
    int       ny, nx;

    /*  The tests are `> 16`, not `>= 16`, so a thing that lands exactly
     *  on a tile boundary spends one step sitting on it. */
    if (py > 16)
    {
        cy = 1;
        py -= 16;
        carry = 1;
    } /* $E3CA */
    if (py < 0)
    {
        cy = -1;
        py += 16;
        carry = 1;
    } /* $E3DA */
    if (px > 16)
    {
        cx = 1;
        px -= 16;
        carry = 1;
    } /* $E3E8 */
    if (px < 0)
    {
        cx = -1;
        px += 16;
        carry = 1;
    } /* $E3FC */

    t[6] = (uint8_t)py; /* $E416 */
    t[7] = (uint8_t)px;
    if (!carry)
        return 1; /* $E426 */

    ny                  = t[3] + cy;
    nx                  = t[4] + cx;
    c->xtxt[t[3]][t[4]] = 0; /* $E474, leave the old tile first */

    /*  $E482 guards with $7F, not $80.  The last row and column of the
     *  map are out of bounds here even though they are real tiles, so a
     *  thing that reaches the far edge is released rather than parked. */
    if (ny < 0 || nx < 0 || ny >= 0x7F || nx >= 0x7F)
    {
        free_thing(c, slot); /* $E492 */
        return 0;
    }
    t[3]            = (uint8_t)ny; /* $E4A4 */
    t[4]            = (uint8_t)nx;
    c->xtxt[ny][nx] = (uint8_t)(slot + 0xC9); /* $E4CC */
    return 1;
}

/* ================================================================== *
 *  $E4DA  boatCanEnter -- may the boat move one step in `dir`?
 *
 *  Off the map counts as yes: $E388 makes its own bounds test and
 *  releases the slot there, so the two routines split the check between
 *  them rather than duplicating it.
 * ================================================================== */
static int boat_can_enter(City *c, int slot, int dir)
{
    uint8_t  *t = thing(c, slot);
    const int y = t[3] + STEP_DY[dir]; /* $E500 */
    const int x = t[4] + STEP_DX[dir]; /* $E510 */
    int       b;

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 1; /* $E52C */

    b = c->xbld[y][x];
    if (b == 0xF8) /* $E546, a marina: the boat has arrived */
    {
        /*  Released by hand rather than through $9D7E, and note it
         *  clears the boat's OWN tile, not the marina it entered. */
        t[0]                = 0; /* $E554 */
        c->xtxt[t[3]][t[4]] = 0; /* $E578 */
        return 0;
    }
    if (b == 0xDF) /* $E580, the near half of a bridge */
        return 0;
    if (c->xtxt[y][x]) /* $E598, something is already there */
        return 0;
    return (c->xbit[y][x] & XBIT_WATER) ? 1 : 0; /* $E5B4 */
}

/* ================================================================== *
 *  $E262  stepBoat -- the sailboat, thing type 9.
 *
 *  Four may be alive at once.  The counter is cleared by the driver
 *  every frame and rebuilt here, so the fifth boat reached in a pass is
 *  the one that gets released -- which boat that is depends on slot
 *  order, not on age.
 * ================================================================== */
static void step_boat(City *c, int slot)
{
    uint8_t *t = thing(c, slot);
    int      dir;

    c->boat_count++; /* $E26E */
    if (c->boat_count > 4)
    {
        free_thing(c, slot); /* $E27A */
        return;
    }

    if (t[2]) /* $E28E -- already sinking */
    {
        if (game_rand(5) == 0)
            free_thing(c, slot); /* $E2A4 */
        return;
    }

    if (game_rand(4) != 0) /* $E2B4 -- three steps in four it just sails */
    {
        if (boat_can_enter(c, slot, t[1]))
            advance_subtile(c, slot); /* $E2DE */
        return;
    }

    /*  The fourth step is the one where it looks around. */
    if (!(c->xbit[t[3]][t[4]] & XBIT_WATER))
    {
        free_thing(c, slot); /* $E316, aground */
        return;
    }
    if (game_rand(0xFA0) == 0) /* $E324, one step in four thousand */
    {
        t[2] = 1; /* $E338, it starts to sink */
        /*  $E342 reports it, which only makes a sound. */
    }
    /*  $E35A -- turn by -1, 0 or +1, from the Toolbox generator rather
     *  than the game's own.  This is the only draw in the routine that
     *  does not come from the shift register. */
    dir  = t[1] + (int)((uint16_t)Random() % 3) - 1;
    t[1] = (uint8_t)(dir & 3);
}

/*  $EA0E  shipWater -- is this tile navigable? */
static int ship_water(City *c, int y, int x)
{
    const int b = c->xbld[y][x];
    int       i;

    if (!(c->xbit[y][x] & XBIT_WATER))
        return 0; /* $EA2E */
    if (c->xter[y][x] < 0x10)
        return 0; /* $EA4E, too shallow */
    if (c->xter[y][x] >= 0x20)
        return 0; /* $EA6C */
    if (b == 0xF8)
        return 0; /* $EA8A, a marina */
    if (b == 0)
        return 1;           /* $EA96, open water */
    for (i = 0; i < 9; i++) /* $EA9C */
        if (b == SHIP_PASSABLE[i])
            return 1;
    return 0;
}

/*  $E978  shipCanEnter -- may the ship head that way?  Off the map
 *  counts as yes, and an XTXT of $C9 or more means another record is
 *  already there, so ships never stack. */
static int ship_can_enter(City *c, int slot, int dir)
{
    const uint8_t *t = thing(c, slot);
    const int      y = t[3] + SHIP_DY[dir]; /* $E99E */
    const int      x = t[4] + SHIP_DX[dir]; /* $E9AE */

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 1; /* $E9CA */
    if (!ship_water(c, y, x))
        return 0;                         /* $E9DE */
    return c->xtxt[y][x] >= 0xC9 ? 0 : 1; /* $E9F8 */
}

/*  $EAC2  shipAdvance -- the ship's version of $E388.  Two differences
 *  from the boat's: the bounds are $80 rather than $7F, and the tile it
 *  is standing on is saved in +$0A and put back when it leaves. */
static int ship_advance(City *c, int slot)
{
    uint8_t  *t   = thing(c, slot);
    const int dir = t[1];
    int       py  = t[6] + SHIP_SUB_DY[dir]; /* $EAEA */
    int       px  = t[7] + SHIP_SUB_DX[dir]; /* $EAF8 */
    int       cy = 0, cx = 0, carry = 0;
    int       oy, ox, ny, nx;

    if (py > 16)
    {
        cy = 1;
        py -= 16;
        carry = 1;
    } /* $EB04 */
    if (py < 0)
    {
        cy = -1;
        py += 16;
        carry = 1;
    } /* $EB14 */
    if (px > 16)
    {
        cx = 1;
        px -= 16;
        carry = 1;
    } /* $EB22 */
    if (px < 0)
    {
        cx = -1;
        px += 16;
        carry = 1;
    } /* $EB36 */

    t[6] = (uint8_t)py; /* $EB50 */
    t[7] = (uint8_t)px;
    if (!carry)
        return 1; /* $EB60 */

    oy              = t[3];
    ox              = t[4];
    ny              = oy + cy;
    nx              = ox + cx;
    c->xtxt[oy][ox] = t[0x0A]; /* $EBBA, put back what was underneath */

    if (ny < 0 || nx < 0 || ny >= MAP_H || nx >= MAP_W)
    {
        /*  $EBD8 -- and note $9D7E then zeroes the tile that was just
         *  restored, because the record still holds the old position. */
        free_thing(c, slot);
        return 0;
    }
    t[3]            = (uint8_t)ny; /* $EBEA */
    t[4]            = (uint8_t)nx;
    t[0x0A]         = c->xtxt[ny][nx];        /* $EC0E, pick the new one up */
    c->xtxt[ny][nx] = (uint8_t)(slot + 0xC9); /* $EC20 */
    return 1;
}

/*  $B4CC  bearing -- the compass direction from one tile to another,
 *  0..7 matching SHIP_DY/SHIP_DX.  A half-step of slack on each axis
 *  decides whether a heading counts as straight or diagonal. */
static int bearing(int y0, int x0, int y1, int x1)
{
    const int dy = y1 - y0, dx = x1 - x0;
    const int ay = dy < 0 ? -dy : dy;
    const int ax = dx < 0 ? -dx : dx;

    if ((ax + 1) / 2 > ay) /* $B50C */
        return dx < 0 ? 0 : 4;
    if (dy < 0) /* $B51A */
        return (ay + 1) / 2 > ax ? 6 : (dx < 0 ? 7 : 5);
    return (ay + 1) / 2 > ax ? 2 : (dx < 0 ? 1 : 3);
}

/*  $B3D8  turnToward -- one step round the compass toward `want`, the
 *  short way.  It ALWAYS moves: given cur == want it takes the second
 *  arm, finds a difference of zero, and still steps one.  $B46A below
 *  is the same routine with that case guarded. */
static int turn_toward(int cur, int want)
{
    if (want < cur)
        cur += (cur - want > 4) ? 1 : -1; /* $B3EA */
    else
        cur += (want - cur > 4) ? -1 : 1; /* $B3FC */
    return cur & 7;
}

/*  $B46A  steerToward -- bearing, then one step toward it, holding
 *  still when it is already right. */
static int steer_toward(int cur, int y0, int x0, int y1, int x1)
{
    const int want = bearing(y0, x0, y1, x1); /* $B486 */
    if (want == cur)
        return cur; /* $B492 */
    return turn_toward(cur, want);
}

/* ================================================================== *
 *  $E5C8  stepShip -- thing type 3.
 * ================================================================== */
static void step_ship(City *c, int slot)
{
    uint8_t  *t = thing(c, slot);
    const int y = t[3], x = t[4];   /* $E5FC/$E606, before it moves */
    const int ty = t[8], tx = t[9]; /* $E60C/$E616, where it is headed */
    int       i, d5         = 0, d6;

    c->ship_count++; /* $E5D4 */
    if ((Random() & 0xFF) == 0)
        ; /* $E5E8 sounds the horn, $205 */

    if (!(c->xbit[y][x] & XBIT_WATER)) /* $E630 */
    {
        /*  Aground.  The record is not released -- it turns into a
         *  type 6 where it stands. */
        t[0]    = 6; /* $E63E */
        t[2]    = 0; /* $E64C */
        t[0x0B] = 0; /* $E658 */
        t[1]    = 0; /* $E664 */
        return;
    }
    if (t[2] > 4)
        return; /* $E67E */

    switch (t[2]) /* $E688 */
    {
        case 0:                     /* $E696 -- under way */
            if (game_rand(10) == 0) /* $E69A */
            {
                d6 = steer_toward(t[1], y, x, ty, tx); /* $E6C2 */
                if (ship_can_enter(c, slot, d6))       /* $E6CC */
                    t[1] = (uint8_t)d6;
            }
            if (ship_can_enter(c, slot, t[1])) /* $E6F8 */
                ship_advance(c, slot);         /* $E706 */
            else
                t[2] = 1; /* $E718 */

            /*  $E71E -- either way, look two tiles out in four
             *  directions.  A wharf beside the ship sends it to the
             *  waiting state.  The scan uses the position the ship had
             *  when the routine started, not where it just moved to. */
            for (i = 0; i < 4; i++)
            {
                const int ny = y + DOCK_DY[i]; /* $E72C */
                const int nx = x + DOCK_DX[i]; /* $E738 */
                if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W)
                    continue;                /* $E73C */
                if (c->xbld[ny][nx] == 0xDF) /* $E768 */
                    t[2] = 3;
            }
            break;

        case 1:                                    /* $E788 -- lining up on the target */
            d6   = bearing(y, x, ty, tx);          /* $E794 */
            t[1] = (uint8_t)turn_toward(t[1], d6); /* $E7AC */
            if (d6 != t[1])
                break;                                  /* $E7CA, still coming round */
            t[2] = ship_can_enter(c, slot, d6) ? 0 : 2; /* $E7EC/$E7FC */
            break;

        case 2: /* $E806 -- boxed in, sweep for any way out */
            d6 = t[1];
            if (game_rand(2) != 0) /* $E818 */
            {
                for (i = 0; i < 9; i++) /* $E828 */
                {
                    d5 = (d6 + TURN_CW[i]) & 7;
                    if (ship_can_enter(c, slot, d5))
                        break;
                }
            }
            else
            {
                for (i = 0; i < 9; i++) /* $E856 */
                {
                    d5 = (d6 + TURN_CCW[i]) & 7;
                    if (ship_can_enter(c, slot, d5))
                        break;
                }
            }
            /*  $E87E tests for exactly 8, so the ship is released when
             *  the ninth heading is the one that worked -- but NOT when
             *  the sweep ran out with nothing, which leaves the loop at
             *  9.  The two writes below then land on a freed slot. */
            if (i == 8)
                free_thing(c, slot); /* $E886 */
            t[1] = (uint8_t)d5;      /* $E894 */
            t[2] = 0;                /* $E8A0 */
            break;

        case 3: /* $E8A8 -- tied up, one chance in thirty of leaving */
            if (game_rand(0x1E) != 0)
                break;
            t[2] = 4; /* $E8C2 */
            /*  $E8CC sounds the horn again. */
            /*  $E8DE/$E8EA take the LOW BYTE of the seaport position at
             *  A5+0x12F4 and 0x12F6 -- the odd addresses, not the even
             *  ones -- and make that the new target. */
            t[8] = (uint8_t)c->ship_y;
            t[9] = (uint8_t)c->ship_x;
            break;

        case 4:                                /* $E8F4 -- running for the seaport */
            if (ship_can_enter(c, slot, t[1])) /* $E906 */
            {
                ship_advance(c, slot); /* $E914 */
                break;
            }
            d6 = t[1];
            /*  $E930 -- the sweep starts at 0 or 1, so the ship
             *  sometimes skips its own heading before looking. */
            for (i = (int)game_rand(2); i < 9; i++)
            {
                d5 = (d6 + TURN_CW[i]) & 7;
                if (ship_can_enter(c, slot, d5))
                    break;
            }
            t[1] = (uint8_t)d5; /* $E96C */
            break;

        default:
            break;
    }
}

/*  $A6E4 -- Manhattan distance. */
static int manhattan(int y0, int x0, int y1, int x1)
{
    const int dy = y1 - y0, dx = x1 - x0;
    return (dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx);
}

/*  What _TickCount answers.  $C928 is the only place the simulation
 *  reads the clock, and the interpreter feeds it a counter rather than
 *  a real one, so the reconstruction does the same. */
static int32_t tick_count(City *c) { return (c->ticks += 64); }

/*  $CBFE  heliBlocked -- NOTE THE SENSE.  This returns NONZERO when the
 *  tile three steps out is blocked, the opposite of the ship's $E978.
 *  Off the map is not blocked, so a helicopter will fly off the edge. */
static int heli_blocked(City *c, int slot, int dir)
{
    const uint8_t *t = thing(c, slot);
    const int      y = t[3] + HELI_DY[dir]; /* $CC24 */
    const int      x = t[4] + HELI_DX[dir]; /* $CC34 */

    if (y < 0 || y >= MAP_H || x < 0 || x >= MAP_W)
        return 0;                         /* $CC50 */
    return c->xbld[y][x] >= 0xFB ? 1 : 0; /* $CC68 */
}

/*  $CB86 -- hold the heading while it is clear, otherwise take the
 *  first of seven alternatives that is. */
static void heli_avoid(City *c, int slot)
{
    uint8_t *t = thing(c, slot);
    int      i, d = t[1];

    if (!heli_blocked(c, slot, t[1]))
        return;             /* $CBAE */
    for (i = 0; i < 7; i++) /* $CBB4 */
    {
        d = (t[1] + HELI_SWEEP[i]) & 7;
        if (!heli_blocked(c, slot, d))
            break;
    }
    t[1] = (uint8_t)d; /* $CBF2, written even when nothing was clear */
}

/* ================================================================== *
 *  $B568  advanceSpeed -- the general sub-tile advance, scaled by a
 *  speed index, and the only one of the three that will walk more than
 *  one tile in a frame: landing on an occupied tile makes it step again
 *  in the same direction until it finds a free one or leaves the map.
 * ================================================================== */
static void advance_speed(City *c, int speed_idx, int slot, int dir)
{
    uint8_t  *t  = thing(c, slot);
    const int sp = MOVE_SPEED[speed_idx];     /* $B58C */
    int       py = t[6] + sp * STEP8_DY[dir]; /* $B59A */
    int       px = t[7] + sp * STEP8_DX[dir]; /* $B5AE */
    int       cy = 0, cx = 0, carry = 0;
    int       ny, nx;

    if (py > 16)
    {
        cy = 1;
        py -= 16;
        carry = 1;
    } /* $B5C0 */
    if (py < 0)
    {
        cy = -1;
        py += 16;
        carry = 1;
    } /* $B5D4 */
    if (px > 16)
    {
        cx = 1;
        px -= 16;
        carry = 1;
    } /* $B5E6 */
    if (px < 0)
    {
        cx = -1;
        px += 16;
        carry = 1;
    } /* $B5FA */

    t[6] = (uint8_t)py; /* $B614 */
    t[7] = (uint8_t)px;
    if (!carry)
        return; /* $B628 */

    ny                  = t[3] + cy;
    nx                  = t[4] + cx;
    c->xtxt[t[3]][t[4]] = t[0x0A]; /* $B686, put back what was underneath */

    for (;;)
    {
        /*  $B6E2 reads XTXT before testing the bounds, so the original
         *  can read one row past the table here.  The reconstruction
         *  cannot reproduce that read, and treats off-map as unoccupied,
         *  which takes the same branch the read almost always would. */
        const int occupied = (ny >= 0 && ny < MAP_H && nx >= 0 && nx < MAP_W)
                                 ? c->xtxt[ny][nx] >= 0xC9
                                 : 0;
        if (!occupied)
            break;
        /*  $B68C -- it moves onto the occupied tile anyway and tries
         *  again one further on. */
        t[3] = (uint8_t)ny;
        t[4] = (uint8_t)nx;
        ny   = t[3] + cy;
        nx   = t[4] + cx;
        if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W) /* $B6BE */
        {
            free_thing(c, slot); /* $B6D8 */
            return;
        }
    }
    if (ny < 0 || ny >= MAP_H || nx < 0 || nx >= MAP_W) /* $B704 */
    {
        free_thing(c, slot); /* $B71E */
        return;
    }
    t[3]            = (uint8_t)ny; /* $B72E */
    t[4]            = (uint8_t)nx;
    t[0x0A]         = c->xtxt[ny][nx];        /* $B74C */
    c->xtxt[ny][nx] = (uint8_t)(slot + 0xC9); /* $B762 */
}

/* ================================================================== *
 *  $C7A4  stepHeli -- thing type 2.
 * ================================================================== */
static void step_heli(City *c, int slot)
{
    uint8_t  *t   = thing(c, slot);
    const int dir = t[1];

    c->heli_count++; /* $C7B0 */

    if (c->xbld[t[3]][t[4]] >= 0xFB) /* $C7EA */
    {
        /*  Over something it cannot be over.  Like the ship running
         *  aground, it becomes a type 6 where it stands. */
        t[0]    = 6; /* $C7F8 */
        t[2]    = 5; /* $C806 */
        t[0x0B] = 0; /* $C814 */
        t[1]    = 0; /* $C820 */
        return;
    }
    if (t[2] > 5)
        return; /* $C82E */

    switch (t[2]) /* $C838 */
    {
        case 0: /* $C848 -- climbing away */
            t[1] = (uint8_t)((dir + 1) & 7);
            if (t[5] < 10)
                t[5]++; /* $C874 */
            else
                t[2] = 2; /* $C880 */
            break;

        case 1: /* $CB7E -- state 1 does nothing at all */
            break;

        case 2: /* $C88A -- cruising */
            {
                const int td = manhattan(t[3], t[4], t[8], t[9]);

                t[1] = (uint8_t)steer_toward(dir, t[3], t[4], t[8], t[9]); /* $C8B6 */
                heli_avoid(c, slot);                                       /* $C8CA */
                advance_speed(c, 2, slot, t[1]);                           /* $C8E6 */

                /*  $C91C -- the traffic report.  XTRF is at half the map's
                 *  resolution, and the report is rate-limited by the clock
                 *  rather than by the simulation: five thousand ticks. */
                if (c->xtrf[t[3] / 2][t[4] / 2] >= 0xAA &&
                    tick_count(c) > c->heli_timer)
                {
                    /*  $C936 plays it. */
                    c->heli_timer = tick_count(c) + 0x1388; /* $C948 */
                }

                if (td >= 2)
                    break; /* $C984, still on its way */

                /*  Arrived.  Pick somewhere new near where the player is
                 *  looking, redrawing either coordinate that lands off the
                 *  map.  Every one of these is a Toolbox draw. */
                {
                    int ny = c->view_y + (int)((uint16_t)Random() % 0x40) - 0x20;
                    int nx = c->view_x + (int)((uint16_t)Random() % 0x40) - 0x20;
                    if (ny < 0 || ny >= MAP_H)
                        ny = (int)((uint16_t)Random() % 0x40) + 0x20; /* $C9E2 */
                    if (nx < 0 || nx >= MAP_W)
                        nx = (int)((uint16_t)Random() % 0x40) + 0x20; /* $CA08 */
                    t[8] = (uint8_t)ny;                               /* $CA28 */
                    t[9] = (uint8_t)nx;
                }
                if (!(Random() & 1))
                    break; /* $CA42 */
                if (c->xbld[t[3]][t[4]] != 0)
                    break; /* $CA6E */
                if (c->xter[t[3]][t[4]] != 0)
                    break; /* $CA88 */
                t[2] = 3;  /* $CA8E, bare ground -- go down */
                break;
            }

        case 3: /* $CA98 -- descending */
            t[1] = (uint8_t)((dir + 1) & 7);
            if (t[5] > 2)
                t[5]--; /* $CAC4 */
            else
                t[2] = 4; /* $CAD0 */
            break;

        case 4: /* $CADA -- on the ground, one chance in twenty of going */
            if ((uint16_t)Random() % 0x14 != 0)
                break;
            t[2] = 0; /* $CAF8 */
            break;

        case 5: /* $CB00 -- going down hard */
            t[1] = (uint8_t)((dir + 2) & 7);
            if (t[5] == 4)
                ; /* $CB2C sounds the crash, $203 */
            if (t[5] > 2)
            {
                t[5]--; /* $CB42 */
                break;
            }
            t[0]    = 6;    /* $CB50 */
            t[2]    = 0x11; /* $CB5E */
            t[0x0B] = 1;    /* $CB6C */
            t[1]    = 0;    /* $CB7A */
            break;

        default:
            break;
    }
}

/*  $B418 -- the coarse bearing, decided by the sign of each delta alone
 *  rather than by $B4CC's half-step slack. */
static int bearing_coarse(int y0, int x0, int y1, int x1)
{
    const int dy = y1 - y0, dx = x1 - x0;

    if (dy < 0) /* $B42E */
        return dx < 0 ? 7 : (dx > 0 ? 5 : 6);
    if (dy > 0) /* $B446 */
        return dx < 0 ? 1 : (dx > 0 ? 3 : 2);
    return dx < 0 ? 0 : 4; /* $B45C */
}

/*  $B394 -- one chance in n of drifting a point either way. */
static int wander(int dir, int n)
{
    if ((uint16_t)Random() % (uint16_t)n != 0)
        return dir;                                       /* $B3B0 */
    return (dir + (int)((uint16_t)Random() % 3) - 1) & 7; /* $B3C2 */
}

/* ================================================================== *
 *  $C1E6  stepPlane -- thing type 1.
 * ================================================================== */
static void step_plane(City *c, int slot)
{
    uint8_t  *t     = thing(c, slot);
    const int state = t[2] & 0x0F; /* $C208 */
    const int b     = c->xbld[t[3]][t[4]];
    int       runway;

    c->plane_count++; /* $C1F2 */

    /*  $C23C -- anything over $70 is a real building, and an aeroplane
     *  low enough is going to hit it.  A zone nibble of exactly 8 is the
     *  airport, which it is allowed to be over. */
    if (b > 0x70 && (c->xzon[t[3]][t[4]] & 0x0F) != 8)
    {
        if (b >= 0xFB) /* $C262 */
        {
            t[0] = 6; /* $C270 */
            t[2] = 5; /* $C27E */
            /*  $C288 -- one crash in sixteen leaves the flag set. */
            t[0x0B] = (uint8_t)(game_rand(0x10) == 0 ? 1 : 0);
            t[1]    = 0; /* $C2B8 */
            return;
        }
        /*  $C2DA -- the building's shape height over three.  Flying
         *  lower than that is flying into it. */
        if (BUILDING[b].sprite_h / 3 > t[5])
        {
            t[0]    = 6; /* $C2EE */
            t[2]    = 5;
            t[0x0B] = 1;
            t[1]    = 0;
            return;
        }
    }
    if (state > 7)
        return; /* $C328 */

    switch (state) /* $C332 */
    {
        case 0:                              /* $C346 -- climbing out */
            advance_speed(c, 1, slot, t[1]); /* $C35C */
            if (t[5] == 0)
                ; /* $C378 sounds the engine, $206 */
            if (t[5] < 14)
                t[5]++; /* $C38E */
            else
                t[2] = 2; /* $C39E */
            break;

        case 1: /* $C3A8 -- on the ground, rolling out */
            advance_speed(c, 1, slot, t[1]);
            /*  $C3EC computes the distance to the target here and never
             *  looks at it. */
            (void)manhattan(t[3], t[4], t[8], t[9]);
            if (c->anim_phase & 1)
                t[5]--; /* $C408, so it sheds height on alternate frames */
            if (t[5] >= 1)
                break; /* $C41E */
            /*  $C426 sounds it, then the record goes back. */
            c->xtxt[t[3]][t[4]] = t[0x0A]; /* $C458 */
            free_thing(c, slot);           /* $C45E */
            break;

        case 2:                              /* $C468 -- cruising */
            t[1] = (uint8_t)wander(t[1], 5); /* $C47C */
            heli_avoid(c, slot);             /* $C490, shared with the helicopter */
            advance_speed(c, 1, slot, t[1]); /* $C4AC */
            break;

        case 3:                                                     /* $C4B8 -- turning onto the approach */
            t[1] = (uint8_t)bearing_coarse(t[3], t[4], t[8], t[9]); /* $C4E2 */
            heli_avoid(c, slot);                                    /* $C4F6 */
            advance_speed(c, 1, slot, t[1]);                        /* $C512 */
            if (manhattan(t[3], t[4], t[8], t[9]) >= 2)
                break; /* $C552 */

            runway = t[2] >> 4;                          /* $C564 */
            t[1]   = (uint8_t)turn_toward(t[1], runway); /* $C56A */
            t[2]   = (uint8_t)((runway << 4) + 4);       /* $C57C */
            /*  $C58E -- back the target off six tiles along the runway,
             *  so the aeroplane flies past the threshold and turns in. */
            switch (runway)
            {
                case 1:
                    t[9] = (uint8_t)(t[9] - 6);
                    break; /* $C5B0 */
                case 3:
                    t[8] = (uint8_t)(t[8] + 6);
                    break; /* $C5C2 */
                case 5:
                    t[9] = (uint8_t)(t[9] + 6);
                    break; /* $C5D4 */
                case 7:
                    t[8] = (uint8_t)(t[8] - 6);
                    break; /* $C5E6 */
                default:
                    break;
            }
            break;

        case 4:                                                         /* $C5EC -- flying the approach */
            t[1] = (uint8_t)steer_toward(t[1], t[3], t[4], t[8], t[9]); /* $C61E */
            advance_speed(c, 1, slot, t[1]);                            /* $C638 */
            if (manhattan(t[3], t[4], t[8], t[9]) >= 2)
                break; /* $C678 */

            runway = t[2] >> 4; /* $C690 */
            t[1]   = (uint8_t)runway;
            t[2]   = 1; /* $C69E, down to state 1 */
            t[6]   = 8; /* $C6AC, centre it in the tile */
            t[7]   = 8;
            /*  $C6C0 -- and snap the axis it is landing along. */
            if (runway == 1 || runway == 5)
                t[3] = t[8]; /* $C6E6 */
            else if (runway == 3 || runway == 7)
                t[4] = t[9]; /* $C700 */
            break;

        case 7: /* $C70A -- going down */
            if (t[5] == 0)
            {
                t[0]    = 6; /* $C76E */
                t[2]    = 5;
                t[0x0B] = 1;
                t[1]    = 0;
                break;
            }
            t[5]--; /* $C71C */
            if (t[5] == 8)
                ; /* $C734 sounds the crash, $203 */
            /*  $C752 turns the aeroplane as it falls, but moves it on the
             *  heading it had BEFORE the turn. */
            {
                const int old = t[1];
                t[1]          = (uint8_t)((old + 1) & 7);
                advance_speed(c, 1, slot, old); /* $C762 */
            }
            break;

        default: /* 5 and 6 fall straight through to $C79C */
            break;
    }
}

/* ================================================================== *
 *  $09E0A  stepThings -- one pass over the forty records.
 * ================================================================== */
void sim_step_things(City *c)
{
    int slot;

    c->anim_phase++; /* $9E24, A5+0x12F8 */
    if (c->anim_phase > 0x3FF)
        c->anim_phase = 0;

    /*  $9E56 -- the game does not trust an incremented count.  Every
     *  per-type total is cleared here and rebuilt by the steppers.
     *
     *  $9E76 -- two ambient sounds, a traffic one and a police siren.
     *  Neither changes any state, but each rolls a Toolbox random every
     *  frame it is eligible, and the steppers draw from that same stream.
     *  So the rolls have to happen even though the sound does not: skipping
     *  one shifts every later draw in the pass.  Both averages come from
     *  the statistics pass at $224DA, which is not reconstructed, so in
     *  practice neither gate opens yet. */
    if (c->graph[GRAPH_TRAFFIC][0] > 0x23 && (Random() & 0xFF) == 0)
        ;                       /* $9E90 sound $209 */
    if (c->census[0xD2] != 0 && /* a police station exists */
        c->graph[GRAPH_CRIME][0] > 0x28 && (Random() & 0xFF) == 0)
        ; /* $9EBC sound $1FA */

    c->plane_count = c->heli_count = c->ship_count = 0;
    c->count_12E6 = c->boat_count = c->monster_count = 0;
    c->road_count = c->tornado_count = 0;

    /*  A save without an XTHG segment has no slot table at all, and the
     *  original never met one: it always had the table its own new-city
     *  code laid out.  The guard is around the LOOP and not the function
     *  on purpose -- everything above draws from the Toolbox random
     *  stream, and skipping those rolls would shift every later draw in
     *  the pass. */
    if (!c->xthg)
        return;

    for (slot = 1; slot < THING_N; slot++) /* $A000 */
    {
        const int type = c->xthg[slot * THING_SZ];
        if (type == 0 || type > 17)
            continue; /* $9ED8 */
        if (!THING_ENABLED[type])
            continue; /* $9EE0 */

        switch (type) /* $9F1C */
        {
            case 10:
            case 12:
                c->road_count++; /* $9FB6, only this arm counts */
                step_train(c, slot);
                break;
            case 9:
                step_boat(c, slot); /* $9F5C */
                break;
            case 3:
                step_ship(c, slot); /* $9F58 */
                break;
            case 2:
                step_heli(c, slot); /* $9F4C */
                break;
            case 1:
                step_plane(c, slot); /* $9F40 */
                break;
            default:
                /*  types 5, 6, 15 and 16 have steppers that are not
                 *  reconstructed yet -- none of them occurs in any
                 *  shipped city.  7 and 8 reach empty ones. */
                break;
        }
    }
}

/* ================================================================== *
 *  $BA7A  spawnHelicopter and $B76E  spawnPlane.
 *
 *  Both end with a call to $A3E4.  That routine only projects the thing
 *  to screen coordinates and unions a redraw rectangle.  It makes no
 *  call and writes nothing outside its own stack frame, so leaving it
 *  out changes no simulation state.
 *
 *  g_disastersActive gates both.  While a disaster runs, nothing new
 *  takes to the air.
 * ================================================================== */
void spawn_helicopter(City *c, int y, int x)
{
    uint8_t *t;
    int      slot;

    if (c->xtxt[y][x] >= 0xC9)
        return; /* $BA96 */
    if (c->monster_count != 0)
        return; /* $BAAA */
    if (c->heli_count >= 1)
        return; /* $BAB6 */

    slot = alloc_thing(c);
    if (slot == 0)
        return; /* $BAC8 */

    t       = thing(c, slot);
    t[0]    = 2;          /* $BAD8, a helicopter */
    t[1]    = 2;          /* $BAEC */
    t[3]    = (uint8_t)y; /* $BB00 */
    t[4]    = (uint8_t)x;
    t[6]    = 8;
    t[7]    = 8;
    t[8]    = (uint8_t)((uint16_t)Random() % 128); /* $BB2A */
    t[9]    = (uint8_t)((uint16_t)Random() % 128);
    t[5]    = 0;
    t[0x0A] = c->xtxt[y][x];
    t[2]    = 0;

    c->heli_count++;                        /* $BB86 */
    c->heli_timer = 0;                      /* $BB8A, clears A5+0x12F0 */
    c->xtxt[y][x] = (uint8_t)(slot + 0xC9); /* $BB9C */
}

void spawn_plane(City *c, int y, int x, int kind)
{
    uint8_t *t;
    int      slot, ty, tx;

    if (c->xtxt[y][x] >= 0xC9)
        return; /* $B78E */
    if (c->monster_count != 0)
        return; /* $B7A2 */
    if (c->plane_count >= 2)
        return; /* $B7AE */

    slot = alloc_thing(c);
    if (slot == 0)
        return; /* $B7C0 */

    t    = thing(c, slot);
    t[0] = 1; /* $B7CC */
    t[6] = 8;
    t[7] = 8;

    if ((uint16_t)Random() % 10 >= 5) /* $B7F8, start at the airport */
    {
        ty   = y;
        tx   = x;
        t[8] = 0x14; /* $B9DC */
        t[9] = 0x14;
        t[1] = (uint8_t)kind;
        t[5] = 0;
        t[2] = 0;
    }
    else /* $B80C, fly in from one edge of the map */
    {
        int edge  = (int)(Random() & 3);
        int along = (int)((uint16_t)Random() % 100) + 10;
        switch (edge)
        {
            case 0:
                ty   = 0;
                tx   = along;
                t[1] = 3;
                break; /* $B822 */
            case 1:
                tx   = 0;
                ty   = along;
                t[1] = 5;
                break; /* $B860 */
            case 2:
                ty   = 0x7F;
                tx   = along;
                t[1] = 7;
                break; /* $B89C */
            default:
                tx   = 0x7F;
                ty   = along;
                t[1] = 1;
                break; /* $B8DA */
        }
        t[5] = 0x10; /* $B916 */
        t[2] = (uint8_t)((kind << 4) + 3);
        if (kind == 0)
        {
            t[8] = (uint8_t)y;
            t[9] = (uint8_t)(x + 0x10);
        }
        else
        {
            t[8] = (uint8_t)(y - 0x10);
            t[9] = (uint8_t)x;
        }
    }
    t[3] = (uint8_t)ty;
    t[4] = (uint8_t)tx;

    t[0x0A]         = c->xtxt[ty][tx]; /* $B97C */
    c->xtxt[ty][tx] = (uint8_t)(slot + 0xC9);
    c->plane_count++; /* $BA5E */
}

/* ================================================================== *
 *  $B4CC  headingToward -- an eight point compass from one tile to
 *  another.  A direction counts as diagonal only when the two spans
 *  are within a factor of two of each other.
 * ================================================================== */
static int heading_toward(int fy, int fx, int ty, int tx)
{
    int dy = ty - fy; /* $B4D6 */
    int dx = tx - fx; /* $B4E0 */
    int ay = dy < 0 ? -dy : dy;
    int ax = dx < 0 ? -dx : dx;

    if ((ax + 1) / 2 > ay)
        return dx < 0 ? 0 : 4; /* $B510, mostly sideways */
    if (dy < 0)                /* $B51A, upward */
    {
        if ((ay + 1) / 2 > ax)
            return 6;
        return dx < 0 ? 7 : 5;
    }
    if ((ay + 1) / 2 > ax)
        return 2; /* $B540, downward */
    return dx < 0 ? 1 : 3;
}

/* ================================================================== *
 *  $BBB0  seaportShip -- send a ship in from the edge of the map.
 *
 *  The dice choose one of the four edges, then the whole edge is
 *  scanned for water.  The LAST water tile found wins, because the scan
 *  does not stop at the first.  The ship starts there and heads for the
 *  seaport that called.
 * ================================================================== */
void seaport_ship(City *c, int y, int x)
{
    int      edge, i, sy = 0, sx = 0, found = 0, slot;
    uint8_t *t;

    if (c->ship_count >= 1)
        return; /* $BBB6, one ship at a time */

    edge = (int)(Random() & 3); /* $BBC6 */
    for (i = 0; i < MAP_W; i++)
    {
        int ty = (edge == 0) ? 2 : (edge == 1) ? 0x7E
                                               : i;
        int tx = (edge == 2) ? 2 : (edge == 3) ? 0x7E
                                               : i;
        if (c->xter[ty][tx] == 0x10) /* $BBEE, open water */
        {
            sy    = ty;
            sx    = tx;
            found = 1; /* keeps the LAST match */
        }
    }
    if (!found)
        return; /* $BC9A */
    if (c->xtxt[sy][sx] >= 0xC9)
        return; /* $BCAE */

    slot = alloc_thing(c);
    if (slot == 0)
        return; /* $BCC2 */

    t       = thing(c, slot);
    t[0]    = 3;                                     /* $BCCE, a ship */
    t[1]    = (uint8_t)heading_toward(sy, sx, y, x); /* $BCE4 */
    t[3]    = (uint8_t)sy;                           /* $BCF2 */
    t[4]    = (uint8_t)sx;
    t[6]    = 8;
    t[7]    = 8;
    t[5]    = 1;
    t[0x0A] = c->xtxt[sy][sx];
    t[2]    = 0;

    c->ship_y = (int16_t)sy; /* $BD46 */
    c->ship_x = (int16_t)sx;
    /*  $BD52 calls $3F636, which does nothing unless the machine
     *  reports more than $9C40 bytes free.  The reconstruction models
     *  no memory pressure, so that gate stays shut on both sides. */
    c->ship_count++;                          /* $BD62 */
    c->xtxt[sy][sx] = (uint8_t)(slot + 0xC9); /* $BD6E */
}
