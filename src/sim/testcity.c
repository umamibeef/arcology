/*  testcity.c -- a city built to exercise the network renderer.
 *
 *      arcology --testcity <template city> <out.sc2>
 *
 *  The shipped cities are places, not test cases: they have thousands of
 *  ordinary straights and a handful of the corners that actually break
 *  the fit, and finding those means hunting.  This lays out every case
 *  the road and rail fitter has to handle, once each, in labelled blocks
 *  on flat ground, and prints where each one is (the user: "create a
 *  test city that includes all of the possible road types we may
 *  encounter (as well as their crossings) so we can test this bitch
 *  out").
 *
 *  A template city is loaded and its tile layers replaced, so the file
 *  that comes out has the chunk order, the scalars and the MISC block of
 *  a real save; nothing here has to know the container.
 *
 *  It lives on the simulation's side of the fence because it AUTHORS a
 *  City.  The renderer may only read one, through adapt.c -- "the one
 *  file that includes both sides" (adapt.h) -- so a generator that builds
 *  a save cannot live in src/render, however much it is used for looking
 *  at the renderer's work.
 *
 *  The piece ids follow the layout every family shares -- power at 0x0E,
 *  roads at 0x1D, rails at 0x2C -- read off the shipped cities the same
 *  way the crossings were:
 *
 *      +0  east-west straight      +8  north-east corner
 *      +1  north-south straight    +9  north-west corner
 *      +2..+5 straights on a slope +10 north-south-west tee
 *      +6  south-west corner       +11 east-south-west tee
 *      +7  east-south corner       +12 north-east-south tee
 *                                  +13 north-east-west tee
 *                                  +14 crossroads
 */
#include "sc2k.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TW 128
#define TH 128

enum
{
    N_NONE  = 0,
    N_ROAD  = 1,
    N_RAIL  = 2,
    N_POWER = 3
};

static uint8_t fam[TH][TW];  /* what runs through a tile          */
static uint8_t fam2[TH][TW]; /* and what crosses it, if anything  */

static void put(int c, int r, int f)
{
    if (c < 0 || r < 0 || c >= TW || r >= TH)
        return;
    if (fam[r][c] && fam[r][c] != f)
        fam2[r][c] = (uint8_t)f;
    else
        fam[r][c] = (uint8_t)f;
}

static void hrun(int c0, int c1, int r, int f)
{
    int c;
    for (c = c0; c <= c1; ++c)
        put(c, r, f);
}

static void vrun(int c, int r0, int r1, int f)
{
    int r;
    for (r = r0; r <= r1; ++r)
        put(c, r, f);
}

/*  A staircase: `runs` legs of `leg` tiles, stepping down one row after
 *  each.  leg 1 is the 45 degree case, leg 2 the 2:1, and so on -- the
 *  shapes the fit has to turn into one smooth diagonal. */
static void staircase(int c0, int r0, int leg, int runs, int f)
{
    int i, c = c0, r = r0;
    for (i = 0; i < runs; ++i)
    {
        hrun(c, c + leg - 1, r, f);
        c += leg - 1;
        put(c, r + 1, f);
        ++r;
    }
}

static int links_of(int c, int r, int f)
{
    int m = 0;
    if (r > 0 && (fam[r - 1][c] == f || fam2[r - 1][c] == f))
        m |= 1;
    if (c + 1 < TW && (fam[r][c + 1] == f || fam2[r][c + 1] == f))
        m |= 2;
    if (r + 1 < TH && (fam[r + 1][c] == f || fam2[r + 1][c] == f))
        m |= 4;
    if (c > 0 && (fam[r][c - 1] == f || fam2[r][c - 1] == f))
        m |= 8;
    return m;
}

static int piece_for(int mask)
{
    switch (mask)
    {
        case 10:
            return 0; /* east-west            */
        case 5:
            return 1; /* north-south          */
        case 12:
            return 6; /* south-west           */
        case 6:
            return 7; /* east-south           */
        case 3:
            return 8; /* north-east           */
        case 9:
            return 9; /* north-west           */
        case 13:
            return 10; /* north-south-west     */
        case 14:
            return 11; /* east-south-west      */
        case 7:
            return 12; /* north-east-south     */
        case 11:
            return 13; /* north-east-west      */
        case 15:
            return 14; /* crossroads           */
        case 1:
        case 4:
            return 1; /* a stub keeps its axis */
        default:
            return 0;
    }
}

static uint8_t base_of(int f)
{
    return (uint8_t)(f == N_ROAD ? 0x1D : f == N_RAIL ? 0x2C
                                                      : 0x0E);
}

/*  Where two families meet, the six ids the crossings actually use. */
static uint8_t crossing_id(int a, int b, int a_mask)
{
    const int ew = (a_mask & 10) != 0 && (a_mask & 5) == 0;
    if ((a == N_ROAD && b == N_RAIL) || (a == N_RAIL && b == N_ROAD))
    {
        int road_ew = (a == N_ROAD) ? ew : !ew;
        return (uint8_t)(road_ew ? 0x45 : 0x46);
    }
    if ((a == N_ROAD && b == N_POWER) || (a == N_POWER && b == N_ROAD))
    {
        int road_ew = (a == N_ROAD) ? ew : !ew;
        return (uint8_t)(road_ew ? 0x43 : 0x44);
    }
    if ((a == N_RAIL && b == N_POWER) || (a == N_POWER && b == N_RAIL))
    {
        int rail_ew = (a == N_RAIL) ? ew : !ew;
        return (uint8_t)(rail_ew ? 0x47 : 0x48);
    }
    return 0;
}

struct Block
{
    int         c, r;
    const char *what;
};
static struct Block block[64];
static int          nblock;

static void note(int c, int r, const char *what)
{
    if (nblock < 64)
    {
        block[nblock].c    = c;
        block[nblock].r    = r;
        block[nblock].what = what;
        ++nblock;
    }
}

/*  One block of the catalogue: sixteen tiles square, laid out at (c,r). */
static void lay_family(int c, int r, int f, const char *fname)
{
    static char label[16][48];
    static int  nlabel;
    int         k = nlabel++ % 16;

    /* a crossroads, four stubs and the two straights that make it */
    snprintf(label[k], sizeof label[k], "%s: cross, tees, stubs", fname);
    hrun(c + 1, c + 13, r + 7, f);
    vrun(c + 7, r + 1, r + 13, f);
    vrun(c + 3, r + 5, r + 7, f);  /* a tee from the north  */
    vrun(c + 11, r + 7, r + 9, f); /* a tee to the south    */
    hrun(c + 1, c + 3, r + 11, f); /* a stub, dead ended    */
    note(c, r, label[k]);
}

static void lay_corners(int c, int r, int f, const char *fname)
{
    static char label[8][48];
    static int  n;
    int         k = n++ % 8;
    snprintf(label[k], sizeof label[k], "%s: the four 90 degree corners", fname);
    /* a closed ring: every corner appears once */
    hrun(c + 2, c + 12, r + 2, f);
    hrun(c + 2, c + 12, r + 12, f);
    vrun(c + 2, r + 2, r + 12, f);
    vrun(c + 12, r + 2, r + 12, f);
    note(c, r, label[k]);
}

static void lay_diagonals(int c, int r, int f, const char *fname)
{
    static char label[8][48];
    static int  n;
    int         k = n++ % 8;
    snprintf(label[k], sizeof label[k], "%s: staircases 45, 2:1 and 3:1", fname);
    staircase(c + 1, r + 1, 1, 12, f); /* 45 degrees        */
    staircase(c + 1, r + 6, 2, 6, f);  /* two across, one down */
    staircase(c + 1, r + 11, 3, 4, f); /* three across      */
    note(c, r, label[k]);
}

static void lay_jogs(int c, int r, int f, const char *fname)
{
    static char label[8][48];
    static int  n;
    int         k = n++ % 8;
    snprintf(label[k], sizeof label[k], "%s: jogs, a hairpin, parallel runs", fname);
    hrun(c + 1, c + 6, r + 2, f);  /* out ...            */
    vrun(c + 6, r + 2, r + 5, f);  /* ... over ...       */
    hrun(c + 6, c + 13, r + 5, f); /* ... and on again   */
    hrun(c + 1, c + 10, r + 9, f); /* a hairpin          */
    vrun(c + 10, r + 9, r + 11, f);
    hrun(c + 1, c + 10, r + 11, f);
    hrun(c + 1, c + 13, r + 14, f); /* parallel, one apart */
    note(c, r, label[k]);
}

/*  All six crossing ids, which needs each pair BOTH ways round: the id
 *  says which axis the road or the rail runs on, so a power line has to
 *  cross an east-west road as well as a north-south one. */
static void lay_crossings(int c, int r)
{
    hrun(c + 1, c + 14, r + 2, N_ROAD);   /* east-west road ...            */
    vrun(c + 4, r + 1, r + 14, N_RAIL);   /* ... crossed by rail   -> 0x45 */
    vrun(c + 8, r + 1, r + 14, N_POWER);  /* ... and by power      -> 0x43 */
    hrun(c + 1, c + 14, r + 6, N_RAIL);   /* east-west rail ...            */
    vrun(c + 12, r + 1, r + 14, N_POWER); /* ... crossed by power  -> 0x47 */
    vrun(c + 2, r + 1, r + 14, N_ROAD);   /* north-south road ...          */
    hrun(c + 1, c + 14, r + 10, N_RAIL);  /* ... crossed by rail   -> 0x46 */
    hrun(c + 1, c + 14, r + 13, N_POWER); /* ... and by power      -> 0x44 */
    vrun(c + 6, r + 1, r + 14, N_RAIL);   /* north-south rail under it -> 0x48 */
    note(c, r, "crossings: all six ids, each pair both ways");
}

static void lay_comb(int c, int r)
{
    int i;
    hrun(c + 1, c + 14, r + 7, N_ROAD);
    for (i = 0; i < 7; ++i)
    {
        vrun(c + 1 + i * 2, r + 3, r + 7, N_ROAD);
        vrun(c + 2 + i * 2, r + 7, r + 12, N_ROAD);
    }
    note(c, r, "junction stress: a tee every other tile");
}

static void lay_grid(int c, int r)
{
    int i;
    for (i = 0; i <= 12; i += 4)
    {
        hrun(c + 1, c + 13, r + 1 + i, N_ROAD);
        vrun(c + 1 + i, r + 1, r + 13, N_ROAD);
    }
    note(c, r, "a dense block grid, four tiles to a side");
}

int testcity_main(int argc, char **argv)
{
    City   *c;
    int     col, row, i;
    uint8_t alt;

    if (argc < 3)
    {
        printf("usage: arcology --testcity <template city> <out.sc2>\n"
               "\n"
               "  Lays out every network case the fit has to handle on flat\n"
               "  ground and writes it as a save.  The template supplies the\n"
               "  chunk order and the scalars; its tiles are replaced.\n");
        return 2;
    }
    c = (City *)calloc(1, sizeof *c);
    if (!c)
        return 1;
    if (!city_load(argv[1], c))
    {
        fprintf(stderr, "testcity: cannot read %s\n", argv[1]);
        free(c);
        return 1;
    }

    /*  Flat ground at a middling altitude: the fit is the subject here,
     *  not the grading, and every slope case has its own city to come. */
    alt = 6;
    for (row = 0; row < TH; ++row)
        for (col = 0; col < TW; ++col)
        {
            c->altm[row][col] = (uint16_t)((alt << 5) | 0);
            c->xter[row][col] = 0;
            c->xbld[row][col] = 0;
            c->xzon[row][col] = 0;
            c->xund[row][col] = 0;
            c->xtxt[row][col] = 0;
            c->xbit[row][col] = 0;
        }
    memset(fam, 0, sizeof fam);
    memset(fam2, 0, sizeof fam2);

    /*  The catalogue, in sixteen-tile blocks with a tile of air between. */
    lay_family(4, 4, N_ROAD, "road");
    lay_corners(24, 4, N_ROAD, "road");
    lay_diagonals(44, 4, N_ROAD, "road");
    lay_jogs(64, 4, N_ROAD, "road");

    lay_family(4, 24, N_RAIL, "rail");
    lay_corners(24, 24, N_RAIL, "rail");
    lay_diagonals(44, 24, N_RAIL, "rail");
    lay_jogs(64, 24, N_RAIL, "rail");

    lay_family(4, 44, N_POWER, "power");
    lay_corners(24, 44, N_POWER, "power");
    lay_diagonals(44, 44, N_POWER, "power");
    lay_jogs(64, 44, N_POWER, "power");

    lay_crossings(4, 64);
    lay_comb(24, 64);
    lay_grid(44, 64);

    /*  Ids last, so every tile sees its finished neighbourhood. */
    for (row = 0; row < TH; ++row)
        for (col = 0; col < TW; ++col)
        {
            int f = fam[row][col], g = fam2[row][col], mask;
            if (!f)
                continue;
            mask = links_of(col, row, f);
            if (g)
            {
                uint8_t x = crossing_id(f, g, mask);
                if (x)
                {
                    c->xbld[row][col] = x;
                    continue;
                }
            }
            c->xbld[row][col] = (uint8_t)(base_of(f) + piece_for(mask));
        }

    if (!city_save(argv[2], c))
    {
        fprintf(stderr, "testcity: cannot write %s\n", argv[2]);
        free(c);
        return 1;
    }
    printf("testcity: wrote %s\n", argv[2]);
    for (i = 0; i < nblock; ++i)
        printf("  c%-3d r%-3d  %s\n", block[i].c, block[i].r, block[i].what);
    free(c);
    return 0;
}
