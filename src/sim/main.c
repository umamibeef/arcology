/*  main.c -- the verification driver.
 *
 *  The point of the reconstruction is that it can be checked, so this
 *  runs every check there is and prints one table.  Three kinds of
 *  evidence, in increasing order of strength:
 *
 *    format     re-serialise a city and compare the bytes
 *    aggregate  recompute a total and compare it to the file's own
 *    per-cell   recompute a whole layer and diff it cell by cell
 *
 *  Nothing here is fitted.  Where a check falls short of 100% the
 *  reason is recorded next to it, and in every case so far the reason
 *  is that a save records a city mid-cycle: values written by an
 *  earlier phase describe a map that later phases have already changed.
 */
#include "advisor.h"
#include "arco.h"
#include "sc2k.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers ----------------------------------------------- */
static int32_t sum_bytes(const uint8_t *p, size_t n)
{
    int32_t s = 0;
    size_t  i;
    for (i = 0; i < n; i++)
        s += p[i];
    return s;
}

typedef void (*each_fn)(const char *path, City *c, void *ctx);

static int for_each_city(const char *dir, each_fn fn, void *ctx)
{
    DIR           *d = opendir(dir);
    struct dirent *e;
    static City    c;
    char           path[1024];
    int            n = 0;

    if (!d)
    {
        fprintf(stderr, "cannot open %s\n", dir);
        return 0;
    }
    while ((e = readdir(d)))
    {
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (!city_load(path, &c))
            continue;
        n++;
        fn(path, &c, ctx);
        city_free(&c);
    }
    closedir(d);
    return n;
}

/* ---- the tally --------------------------------------------------- */
typedef struct
{
    int    cities;
    int    agg_ok[5]; /* pollution, value, crime, traffic, pop */
    long   pow_bad, wat_bad, tiles;
    int    pow_det, pow_det_ok;
    long   crm_cells, crm_ok, pop_cells, pop_ok, val_cells, val_ok; /* isolated */
    long   ccrm_ok, cval_ok, ccrm_live, cval_live;                  /* chained  */
    long   crm_live, crm_live_ok, val_live, val_live_ok;            /* non-zero */
    long   pop_live, pop_live_ok;
    long   crm_err, val_err, pop_err_abs;
    int    crm_perfect, pop_perfect;
    long   plc_live, plc_live_ok, fir_live, fir_live_ok; /* coverage */
    long   plc_err, fir_err, cov_cells;
    int    plc_perfect, fir_perfect;
    long   bud_ok, bud_cells;
    int    bud_perfect; /* budget amounts */
    int    census_exact;
    double pop_err;
    int    pop_err_n;
} Tally;

static void check_city(const char *path, City *c, void *ctx)
{
    Tally *t = (Tally *)ctx;
    (void)path;
    static uint8_t xbit0[MAP_H][MAP_W], xcrm0[HALF_H][HALF_W];
    static uint8_t xpop0[QTR_H][QTR_W], xval0[HALF_H][HALF_W];
    static uint8_t xplc0[QTR_H][QTR_W], xfir0[QTR_H][QTR_W];
    static uint8_t xplt0[HALF_H][HALF_W];
    int32_t        plt, val, crm, trf, pop;
    int            y, x, random_plant = 0;
    long           bad;

    /* --- aggregates, straight off the loaded layers --------------- */
    plt = sum_bytes(&c->xplt[0][0], sizeof c->xplt);
    val = sum_bytes(&c->xval[0][0], sizeof c->xval);
    crm = sum_bytes(&c->xcrm[0][0], sizeof c->xcrm);
    trf = sum_bytes(&c->xtrf[0][0], sizeof c->xtrf);
    pop = sim_map_population(c);

    t->cities++;
    t->agg_ok[0] += (plt == c->pollution_tot);
    t->agg_ok[1] += (val == c->land_value_tot);
    t->agg_ok[2] += (crm == c->crime_tot);
    t->agg_ok[3] += (trf >= c->traffic_tot);
    if (c->population)
    {
        double e = (double)(pop - c->population) / c->population;
        t->agg_ok[4] += (fabs(e) < 0.02);
        t->pop_err += e;
        t->pop_err_n++;
    }
    else
        t->agg_ok[4]++;

    /* --- per-cell: crime, density, land value --------------------- */
    memcpy(xcrm0, c->xcrm, sizeof xcrm0);
    memcpy(xpop0, c->xpop, sizeof xpop0);
    memcpy(xval0, c->xval, sizeof xval0);
    memcpy(xplt0, c->xplt, sizeof xplt0);

    /*  Two different questions, so two different runs.
     *
     *  ISOLATED gives each stage the inputs the file recorded and asks
     *  whether the transcription of that one stage is right.
     *
     *  CHAINED runs them in the order $2317E does -- land value is
     *  stage 5, density stage 7, crime stage 9 -- with each stage
     *  feeding the next, and asks whether the whole pass reproduces.
     *  It cannot, because two of the inputs are destroyed before the
     *  save is written, and the point of showing it is to see how far
     *  that propagates.
     */
    sim_crime(c); /* isolated */
    bad = 0;
    for (y = 0; y < HALF_H; y++)
        for (x = 0; x < HALF_W; x++)
        {
            if (c->xcrm[y][x] || xcrm0[y][x])
            {
                t->crm_live++;
                if (c->xcrm[y][x] == xcrm0[y][x])
                    t->crm_live_ok++;
            }
            if (c->xcrm[y][x] == xcrm0[y][x])
                t->crm_ok++;
            else
                bad++;
            t->crm_err += abs((int)c->xcrm[y][x] - (int)xcrm0[y][x]);
        }
    t->crm_cells += HALF_H * HALF_W;
    if (!bad)
        t->crm_perfect++;
    memcpy(c->xcrm, xcrm0, sizeof xcrm0);

    sim_pollution(c);  /* stages 1-2 first */
    sim_land_value(c); /* isolated */
    for (y = 0; y < HALF_H; y++)
        for (x = 0; x < HALF_W; x++)
        {
            if (c->xval[y][x] || xval0[y][x])
            {
                t->val_live++;
                if (c->xval[y][x] == xval0[y][x])
                    t->val_live_ok++;
            }
            if (c->xval[y][x] == xval0[y][x])
                t->val_ok++;
            t->val_err += abs((int)c->xval[y][x] - (int)xval0[y][x]);
        }
    t->val_cells += HALF_H * HALF_W;
    memcpy(c->xval, xval0, sizeof xval0);

    /*  Police and fire coverage, and the budget amounts they scale
     *  with.  All three are new here: coverage was blocked until the
     *  budget block was found in MISC, and the budget amounts can now
     *  be recomputed from the census and checked against what the file
     *  recorded for them. */
    memcpy(xplc0, c->xplc, sizeof xplc0);
    memcpy(xfir0, c->xfir, sizeof xfir0);
    {
        static Dept saved[N_DEPT];
        static City rebuilt;
        int32_t     funds0 = c->funds;
        int         i, same = 1;

        memcpy(saved, c->dept, sizeof saved);
        sim_budget(c);
        for (i = 0; i < N_DEPT; i++)
        {
            t->bud_cells++;
            if (c->dept[i].amount == saved[i].amount)
                t->bud_ok++;
            else
                same = 0;
        }
        t->bud_perfect += same;
        memcpy(c->dept, saved, sizeof saved);
        c->funds = funds0;

        /*  Does a census rebuilt from the map match the one the game
         *  has been maintaining incrementally?  Where it does not, the
         *  budget cannot be expected to either. */
        memcpy(rebuilt.xbld, c->xbld, sizeof rebuilt.xbld);
        sim_rebuild_census(&rebuilt);
        t->census_exact += !memcmp(rebuilt.census, c->census, sizeof rebuilt.census);
    }

    sim_density(c); /* map only: both */
    for (y = 0; y < QTR_H; y++)
        for (x = 0; x < QTR_W; x++)
        {
            t->cov_cells++;
            if (c->xplc[y][x] || xplc0[y][x])
            {
                t->plc_live++;
                if (c->xplc[y][x] == xplc0[y][x])
                    t->plc_live_ok++;
            }
            if (c->xfir[y][x] || xfir0[y][x])
            {
                t->fir_live++;
                if (c->xfir[y][x] == xfir0[y][x])
                    t->fir_live_ok++;
            }
            t->plc_err += abs((int)c->xplc[y][x] - (int)xplc0[y][x]);
            t->fir_err += abs((int)c->xfir[y][x] - (int)xfir0[y][x]);
        }
    t->plc_perfect += !memcmp(c->xplc, xplc0, sizeof xplc0);
    t->fir_perfect += !memcmp(c->xfir, xfir0, sizeof xfir0);
    memcpy(c->xplc, xplc0, sizeof xplc0);
    memcpy(c->xfir, xfir0, sizeof xfir0);

    bad = 0;
    for (y = 0; y < QTR_H; y++)
        for (x = 0; x < QTR_W; x++)
        {
            if (c->xpop[y][x] || xpop0[y][x])
            {
                t->pop_live++;
                if (c->xpop[y][x] == xpop0[y][x])
                    t->pop_live_ok++;
            }
            if (c->xpop[y][x] == xpop0[y][x])
                t->pop_ok++;
            else
                bad++;
            t->pop_err_abs += abs((int)c->xpop[y][x] - (int)xpop0[y][x]);
        }
    t->pop_cells += QTR_H * QTR_W;
    if (!bad)
        t->pop_perfect++;

    /*  Chained: put every layer back to what the file recorded and then
     *  run the pass the way $2317E runs it, stage 1 through stage 9,
     *  each stage feeding the next.  Starting part way in would not be
     *  the same experiment -- the stages share one scratch plane that
     *  stage 1 is responsible for filling. */
    memcpy(c->xpop, xpop0, sizeof xpop0);
    memcpy(c->xval, xval0, sizeof xval0);
    memcpy(c->xcrm, xcrm0, sizeof xcrm0);
    memcpy(c->xplt, xplt0, sizeof xplt0);
    sim_pollution(c);
    sim_land_value(c);
    for (y = 0; y < HALF_H; y++)
        for (x = 0; x < HALF_W; x++)
            if (c->xval[y][x] || xval0[y][x])
            {
                t->cval_live++;
                if (c->xval[y][x] == xval0[y][x])
                    t->cval_ok++;
            }
    sim_density(c);
    sim_crime(c);
    for (y = 0; y < HALF_H; y++)
        for (x = 0; x < HALF_W; x++)
            if (c->xcrm[y][x] || xcrm0[y][x])
            {
                t->ccrm_live++;
                if (c->xcrm[y][x] == xcrm0[y][x])
                    t->ccrm_ok++;
            }

    /* --- per-tile: the two flood fills ---------------------------- */
    memcpy(xbit0, c->xbit, sizeof xbit0);
    for (y = 0; y < MAP_H && !random_plant; y++)
        for (x = 0; x < MAP_W; x++)
        {
            uint8_t b = c->xbld[y][x];
            if (b == 0xC8 || b == 0xCC)
            {
                random_plant = 1;
                break;
            } /* wind, solar */
        }
    sim_power_grid(c);
    sim_water_grid(c);
    bad = 0;
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
        {
            if ((xbit0[y][x] ^ c->xbit[y][x]) & XBIT_POWERED)
            {
                t->pow_bad++;
                bad++;
            }
            if ((xbit0[y][x] ^ c->xbit[y][x]) & XBIT_WATERED)
                t->wat_bad++;
        }
    t->tiles += MAP_W * MAP_H;
    if (!random_plant)
    {
        t->pow_det++;
        if (!bad)
            t->pow_det_ok++;
    }
}

/* ---- codec round-trip -------------------------------------------- */
static void roundtrip(const char *dir, int *files, int *lossless, int *exact)
{
    DIR           *d = opendir(dir);
    struct dirent *e;
    static City    a, b;
    char           path[1024];
    const char    *tmp = "/tmp/sc2k_roundtrip.tmp";

    *files = *lossless = *exact = 0;
    if (!d)
        return;
    while ((e = readdir(d)))
    {
        long     n1, n2;
        uint8_t *o1, *o2;
        FILE    *f;
        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (!city_load(path, &a))
            continue;
        (*files)++;
        if (!city_save(tmp, &a))
        {
            city_free(&a);
            continue;
        }
        if (city_load(tmp, &b))
        {
            if (!memcmp(a.xbld, b.xbld, sizeof a.xbld) &&
                !memcmp(a.xzon, b.xzon, sizeof a.xzon) &&
                !memcmp(a.altm, b.altm, sizeof a.altm) &&
                !memcmp(a.misc, b.misc, sizeof a.misc))
                (*lossless)++;
            city_free(&b);
        }
        f = fopen(path, "rb");
        fseek(f, 0, SEEK_END);
        n1 = ftell(f);
        rewind(f);
        o1 = (uint8_t *)malloc((size_t)n1);
        if (fread(o1, 1, (size_t)n1, f) != (size_t)n1)
            n1 = 0;
        fclose(f);
        f = fopen(tmp, "rb");
        fseek(f, 0, SEEK_END);
        n2 = ftell(f);
        rewind(f);
        o2 = (uint8_t *)malloc((size_t)n2);
        if (fread(o2, 1, (size_t)n2, f) != (size_t)n2)
            n2 = -1;
        fclose(f);
        if (n1 && n1 == n2 && !memcmp(o1, o2, (size_t)n1))
            (*exact)++;
        free(o1);
        free(o2);
        city_free(&a);
    }
    closedir(d);
}

static void pct(const char *label, long ok, long total, const char *note)
{
    printf("    %-34s %7ld / %-8ld %6.2f%%   %s\n",
           label,
           ok,
           total,
           total ? 100.0 * (double)ok / (double)total : 0.0,
           note);
}

/*  Same, plus how far out the misses are.  A layer can be 6%% exact and
 *  still be broadly right; the mean error says which. */
static void pct_err(const char *label, long ok, long total, long err, long cells, const char *note)
{
    printf("    %-34s %7ld / %-8ld %6.2f%%   mean err %5.1f / 255  %s\n",
           label,
           ok,
           total,
           total ? 100.0 * (double)ok / (double)total : 0.0,
           cells ? (double)err / (double)cells : 0.0,
           note);
}

/*  The developer modes of `arcology`.  a_main.c dispatches here when
 *  argv[1] names one of them; see ARC_DEV_MODES there. */
int sc2k_dev_main(int argc, char **argv)
{
    static Tally t;
    int          files = 0, lossless = 0, exact = 0, n;

    if (argc < 2)
    {
        fprintf(stderr,
                "usage: arcology --verify <dir> [<dir>...]  full report\n"
                "       arcology --<mode> ...              one developer mode\n");
        return 2;
    }
    /*  --convert: a 1995 save becomes a .arco world, or back again.
     *  The direction is read off the OUTPUT name, because that is what
     *  the person typing it is thinking about. */
    /*  --micro: run the year-end microsim pass and dump what it wrote.
     *  tools/micro_check.py drives $101AC against this. */
    if (argc >= 3 && !strcmp(argv[1], "--micro"))
    {
        City *c = (City *)calloc(1, sizeof *c);
        FILE *f;
        char  p[1024];
        if (!c)
            return 1;
        if (!city_load(argv[2], c))
        {
            fprintf(stderr, "not a city\n");
            free(c);
            return 1;
        }
        rng_seed(1, 1);
        sim_microsim(c);
        if (argc >= 4)
        {
            snprintf(p, sizeof p, "%s/xmic", argv[3]);
            f = fopen(p, "wb");
            if (f)
            {
                if (c->xmic)
                    fwrite(c->xmic, 1, c->xmic_len, f);
                fclose(f);
            }
            snprintf(p, sizeof p, "%s/scalars", argv[3]);
            f = fopen(p, "w");
            if (f)
            {
                fprintf(f, "police_term %d\n", (int)c->police_term);
                fprintf(f, "arco_pop %d\n", (int)c->misc[MISC_ARCO_POP]);
                fprintf(f, "police_load %d\n", (int)c->misc[MISC_POLICE_LOAD]);
                fprintf(f, "transit_bus %d\n", (int)c->transit_bus);
                fprintf(f, "transit_rail %d\n", (int)c->transit_rail);
                fprintf(f, "transit_subway %d\n", (int)c->transit_subway);
                fclose(f);
            }
        }
        city_free(c);
        free(c);
        return 0;
    }

    if (argc >= 4 && !strcmp(argv[1], "--convert"))
    {
        City  *c = (City *)calloc(1, sizeof *c);
        int    ok;
        size_t olen    = strlen(argv[3]);
        int    to_arco = olen > 5 && !strcmp(argv[3] + olen - 5, ".arco");
        if (!c)
            return 1;
        if (!city_load(argv[2], c))
        {
            fprintf(stderr, "cannot read %s\n", argv[2]);
            free(c);
            return 1;
        }
        /*  city_save answers 1 for success, arco_save answers 0 -- see
         *  the note in city_load.  Normalise here, once. */
        ok = to_arco ? arco_save(argv[3], c) == 0 : city_save(argv[3], c) != 0;
        printf("%s  %s -> %s\n", ok ? "wrote" : "FAILED", argv[2], argv[3]);
        city_free(c);
        free(c);
        return ok ? 0 : 1;
    }

    if (argc >= 5 && !strcmp(argv[1], "--riot"))
    {
        /*  --riot <city> <h> <v> [storm|flood]: run that disaster from
         *  the given point and print every tile it marked, so the
         *  result can be diffed against the original doing the same.
         *  $FD and $FE are fire, $FC is water. */
        static City c;
        int         ry, rx, hits = 0;
        const char *kind = (argc >= 6) ? argv[5] : "riot";
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        rng_log_start();
        c.disaster_h = (int16_t)atoi(argv[3]);
        c.disaster_v = (int16_t)atoi(argv[4]);
        c.view_y     = c.disaster_h;
        c.view_x     = c.disaster_v;
        if (!strcmp(kind, "storm"))
            sim_disaster_firestorm(&c);
        else if (!strcmp(kind, "flood"))
            sim_disaster_flood(&c);
        else if (!strcmp(kind, "chem"))
            sim_disaster_chemical(&c);
        else if (!strcmp(kind, "poll"))
            sim_disaster_pollution(&c);
        else if (!strcmp(kind, "crash"))
            sim_disaster_air_crash(&c);
        else if (!strcmp(kind, "fire"))
            sim_disaster_fire(&c);
        else if (!strcmp(kind, "tornado"))
            sim_disaster_tornado(&c);
        else if (!strcmp(kind, "monster"))
            sim_disaster_monster(&c);
        else if (!strcmp(kind, "micro"))
            sim_disaster_microwave(&c);
        else if (!strcmp(kind, "volcano"))
            sim_disaster_volcano(&c);
        else if (!strcmp(kind, "quake"))
            sim_disaster_earthquake(&c);
        else if (!strcmp(kind, "melt"))
            sim_disaster_meltdown(&c);
        else if (!strcmp(kind, "hurricane"))
            sim_disaster_hurricane(&c);
        else
            sim_disaster_riot(&c);
        if (argc >= 7 && !strcmp(argv[6], "rng"))
        {
            int i, drawn = rng_log_count();
            for (i = 0; i < drawn; i++)
            {
                int32_t v;
                int     kk = rng_log_entry(i, &v);
                printf("%c %ld\n", kk, (long)v);
            }
            city_free(&c);
            return 0;
        }
        for (ry = 0; ry < MAP_H; ry++)
            for (rx = 0; rx < MAP_W; rx++)
                if (c.xtxt[ry][rx])
                    printf("x %d %d %02X\n", ry, rx, c.xtxt[ry][rx]), hits++;
        for (ry = 0; ry < MAP_H; ry++)
            for (rx = 0; rx < MAP_W; rx++)
            {
                printf("XTER %d %d %02X\n", ry, rx, c.xter[ry][rx]);
                printf("XBIT %d %d %02X\n", ry, rx, c.xbit[ry][rx]);
                printf("XZON %d %d %02X\n", ry, rx, c.xzon[ry][rx]);
                printf("XBLD %d %d %02X\n", ry, rx, c.xbld[ry][rx]);
                printf("ALTM %d %d %04X\n", ry, rx, c.altm[ry][rx]);
            }
        printf("funds 0 0 %08X\n", (unsigned)c.funds);
        for (ry = 1; ry < 40; ry++)
        {
            printf("t %d", ry);
            for (rx = 0; rx < 12; rx++)
                printf(" %02X", c.xthg ? c.xthg[ry * 12 + rx] : 0);
            printf("\n");
        }
        printf("total %d\n", hits);
        city_free(&c);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--demolish1"))
    {
        /*  --demolish1 <city> <y> <x>: one demolition, seeded, so a
         *  single call can be lined up against the original. */
        static City c;
        int         y, x, r, q;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        rng_log_start();
        y = atoi(argv[3]);
        x = atoi(argv[4]);
        sim_demolish_and_place(&c, y, x, 0xFF);
        printf("draws %d\n", rng_log_count());
        for (r = y - 1; r <= y + 3; r++)
        {
            for (q = x - 2; q <= x + 2; q++)
                printf("%02X ", c.xbld[r][q]);
            printf("\n");
        }
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--averages"))
    {
        /*  --averages <city>: run the city scan, then the four map-view
         *  averages, and print them for tools/avg_check.py. */
        static City c;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        /*  the same order $2317E runs its stages in, so the totals and
         *  the developed count are the ones the averages divide */
        sim_pollution(&c);
        sim_land_value(&c);
        sim_density(&c);
        sim_crime(&c);
        /*  $2530E is phase 19, not part of the scan.  Running it here
         *  would decay the layer and change the total the averages are
         *  supposed to divide. */
        sim_overlay_averages(&c);
        printf("traffic %d\npollution %d\nland_value %d\ncrime %d\n"
               "developed %d\n",
               c.graph[GRAPH_TRAFFIC][0],
               c.graph[GRAPH_POLLUTION][0],
               c.graph[GRAPH_VALUE][0],
               c.graph[GRAPH_CRIME][0],
               c.developed);
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--advisor"))
    {
        /*  --advisor <city> [ticks]: run the clock and print what the
         *  board would have said, at the points where the original
         *  itself decides it has something to say. */
        static City c, before;
        int         nticks = (argc >= 4) ? atoi(argv[3]) : 300;
        int         ai, ak, said, last_topic = -1;
        AdvisorMsg  msg[4];
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        /*  --advisor <city> [ticks] [plain]  -- the joke board is on
         *  unless you ask for the plain one. */
        if (argc >= 5 && !strcmp(argv[4], "plain"))
            advisor_set(ADVISOR_PLAIN);
        rng_seed(1, 1);
        for (ai = 0; ai < nticks; ai++)
        {
            before = c;
            sim_tick(&c);
            /*  the original picks its story in phase 24, once a month,
             *  so that is when the board gets to speak */
            if (c.date % 25 != 24)
                continue;
            said = advisor_poll(&c, &before, msg, 4);
            for (ak = 0; ak < said; ak++)
            {
                /*  a standing problem is mentioned when it starts, not
                 *  every month until it is fixed.  Disasters and the
                 *  ordinance that passes itself always get said. */
                if (msg[ak].kind == ADV_STORY)
                {
                    if (msg[ak].id == last_topic)
                        continue;
                    last_topic = msg[ak].id;
                }
                printf("  month %-5d  %-14s %s\n", (int)(c.date / 25), msg[ak].who, msg[ak].text);
            }
            if (said == 0)
                last_topic = -1;
        }
        city_free(&c);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--clock"))
    {
        /*  --clock <city> <ticks> <dir>: run the 25-phase clock for
         *  <ticks> phases and write every layer and every scalar out,
         *  so tools/clock_check.py can diff the lot against $21EDE
         *  driven the same number of times.
         *
         *  Passes checked one at a time hide anything that travels
         *  between phases.  This is the test that does not. */
        static City c;
        char        p[1024];
        FILE       *f;
        int         ticks = atoi(argv[3]), i, k;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        rng_log_start();
        for (i = 0; i < ticks; i++)
            sim_tick(&c);

        /*  The dice matter as much as the state: two engines can reach
         *  the same totals having rolled different numbers, and the
         *  next tick will then part company.  Wind and solar output are
         *  drawn, so power_capacity is one of the figures that moves
         *  when a stream drifts. */
        {
            FILE *df;
            snprintf(p, sizeof p, "%s/dice", argv[4]);
            df = fopen(p, "w");
            if (df)
            {
                for (k = 0; k < rng_log_count(); k++)
                {
                    int32_t v;
                    int     kind = rng_log_entry(k, &v);
                    fprintf(df, "%c %d\n", kind, v);
                }
                fclose(df);
            }
        }

#define DUMPL(name, buf)                           \
    snprintf(p, sizeof p, "%s/%s", argv[4], name); \
    f = fopen(p, "wb");                            \
    if (f)                                         \
    {                                              \
        fwrite(buf, 1, sizeof buf, f);             \
        fclose(f);                                 \
    }
        DUMPL("ALTM", c.altm)
        DUMPL("XBLD", c.xbld)
        DUMPL("XZON", c.xzon)
        DUMPL("XTER", c.xter)
        DUMPL("XUND", c.xund)
        DUMPL("XBIT", c.xbit)
        DUMPL("XTXT", c.xtxt)
        DUMPL("XTRF", c.xtrf)
        DUMPL("XPLT", c.xplt)
        DUMPL("XVAL", c.xval)
        DUMPL("XCRM", c.xcrm)
        DUMPL("XPLC", c.xplc)
        DUMPL("XFIR", c.xfir)
        DUMPL("XPOP", c.xpop)
        DUMPL("XROG", c.xrog)
#undef DUMPL

        snprintf(p, sizeof p, "%s/scalars", argv[4]);
        f = fopen(p, "w");
        if (!f)
            return 1;
#define S(name, v) fprintf(f, "%s %d\n", name, (int)(v))
        S("date", c.date);
        S("funds", c.funds);
        S("bonds", c.bonds);
        S("month", c.month);
        S("years", c.years);
        S("land_value_tot", c.land_value_tot);
        S("crime_tot", c.crime_tot);
        S("traffic_tot", c.traffic_tot);
        S("pollution_tot", c.pollution_tot);
        S("power_pct", c.power_pct);
        S("water_pct", c.water_pct);
        S("power_capacity", c.power_capacity);
        S("water_capacity", c.water_capacity);
        S("population", c.population);
        S("pop_increase", c.pop_increase);
        S("pop_decrease", c.pop_decrease);
        S("developed", c.developed);
        S("ordinances", c.ordinances);
        S("unemployment", c.unemployment);
        S("treatment", c.misc[1043]);
        S("temperature", c.temperature);
        S("weather1", c.weather1);
        S("weather2", c.weather2);
        S("weather_state", c.weather_state);
        S("disaster_kind", c.disaster_kind);
        S("disaster_v", c.disaster_v);
        S("disaster_h", c.disaster_h);
        S("centre_y", c.centre_y);
        S("centre_x", c.centre_x);
        S("age_w65", c.misc[MISC_AGE_W65]);
        S("age_w90", c.misc[MISC_AGE_W90]);
        S("nat_index", c.misc[MISC_NAT_INDEX]);
        S("nat_index2", c.misc[MISC_NAT_INDEX2]);
        S("nat_mood", c.misc[MISC_NAT_MOOD]);
        for (i = 0; i < 3; i++)
            fprintf(f, "rci_demand%d %d\n", i, (int)c.rci_demand[i]);
        for (i = 0; i < 8; i++)
            fprintf(f, "accum8_%d %d\n", i, (int)c.accum8[i]);
        for (i = 0; i < 3; i++)
            fprintf(f, "rci_pop%d %d\n", i, (int)c.rci_pop[i]);
        /*  The eleven industry levels.  Read the LIVE array, not the
         *  MISC copy: the economy updates industry_level[] and MISC is
         *  only refreshed on save, so dumping MISC here reports last
         *  save's numbers and invents a divergence. */
        for (i = 0; i < 11; i++)
            fprintf(f, "ind_level%d %d\n", i, (int)c.industry_level[i]);
        /*  The age pyramid.  MISC interleaves the three series three longs
         *  to a bracket; the game keeps them as three separate arrays, so
         *  they are dumped the way the game holds them. */
        for (i = 0; i < 20; i++)
        {
            fprintf(f, "heads%d %d\n", i, (int)c.misc[MISC_HIST_BASE + 3 * i]);
            fprintf(f, "eduq%d %d\n", i, (int)c.misc[MISC_HIST_BASE + 3 * i + 1]);
            fprintf(f, "life%d %d\n", i, (int)c.misc[MISC_HIST_BASE + 3 * i + 2]);
        }
        for (i = 0; i < 16; i++)
            fprintf(f, "infra%d %d\n", i, (int)c.infra[i]);
        for (i = 0; i < N_DEPT && i < 16; i++)
        {
            fprintf(f, "dept%d_amount %d\n", i, (int)c.dept[i].amount);
            fprintf(f, "dept%d_funding %d\n", i, (int)c.dept[i].funding);
            fprintf(f, "dept%d_accrued %d\n", i, (int)c.dept[i].accrued);
        }
        for (i = 0; i < N_GRAPH; i++)
        {
            fprintf(f, "gmax%d %d\n", i, (int)c.graph_max[i]);
            for (k = 0; k < GRAPH_SAMPLES; k++)
                fprintf(f, "g%d_%d %d\n", i, k, (int)c.graph[i][k]);
        }
#undef S
        fclose(f);
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--graph"))
    {
        /*  --graph <city> [month] [years]: run the city scan and then a
         *  whole month of graph history, and print every sample and
         *  every scale for tools/graph_check.py.  The month and year
         *  override let one city exercise all three bands. */
        static City c;
        int         i, k;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        if (argc >= 4)
            c.month = (int16_t)atoi(argv[3]);
        if (argc >= 5)
            c.years = atoi(argv[4]);
        rng_seed(1, 1);
        /*  the order $2317E runs its stages in.  Power and water come
         *  first: series 8 and 9 are 100 minus what they leave behind,
         *  and water_pct can pass 100, which is how a sample goes
         *  negative and why the scale compares unsigned. */
        sim_power_grid(&c);
        sim_water_grid(&c);
        sim_pollution(&c);
        sim_land_value(&c);
        sim_density(&c);
        sim_crime(&c);
        sim_graph_pass(&c);
        for (i = 0; i < N_GRAPH; i++)
        {
            printf("max %d %d\n", i, c.graph_max[i]);
            for (k = 0; k < GRAPH_SAMPLES; k++)
                printf("g %d %d %d\n", i, k, c.graph[i][k]);
        }
        printf("unemployment %d\n", c.unemployment);
        printf("# power_pct %d cap %d water_pct %d cap %d\n", c.power_pct, c.power_capacity, c.water_pct, c.water_capacity);
        printf("# developed %d pollution_tot %d land_tot %d "
               "crime_tot %d traffic_tot %d\n",
               c.developed,
               c.pollution_tot,
               c.land_value_tot,
               c.crime_tot,
               c.traffic_tot);
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--things"))
    {
        /*  --things <city> [passes]: run the moving-object stepper and
         *  print every record, the XTXT it touches, and the counters. */
        static City c;
        int         i, k, passes = (argc >= 4) ? atoi(argv[3]) : 1;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        rng_log_start();
        for (k = 0; k < passes; k++)
            sim_step_things(&c);
        for (i = 0; i < rng_log_count(); i++)
        {
            int32_t v;
            int     kind = rng_log_entry(i, &v);
            printf("d %c %d\n", kind, v);
        }
        for (i = 0; i < 40; i++)
        {
            int j;
            printf("t %d ", i);
            for (j = 0; j < 12; j++)
                printf("%02x", c.xthg ? c.xthg[i * 12 + j] : 0);
            printf("\n");
        }
        for (i = 0; i < MAP_H * MAP_W; i++)
            if (((const uint8_t *)c.xtxt)[i])
                printf("x %d %02X\n", i, ((const uint8_t *)c.xtxt)[i]);
        printf("c 12E0 %d\nc 12E2 %d\nc 12E4 %d\nc 12E6 %d\n"
               "c 12E8 %d\nc 12EA %d\nc 12EC %d\nc 12EE %d\n",
               c.plane_count,
               c.heli_count,
               c.ship_count,
               c.count_12E6,
               c.boat_count,
               c.monster_count,
               c.road_count,
               c.tornado_count);
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--demolish"))
    {
        /*  --demolish <city>: knock down the first building of every
         *  distinct id and print every layer it could touch, plus how
         *  many dice it used. */
        static City c;
        int         y, x, i, seen[256];
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        rng_log_start();
        memset(seen, 0, sizeof seen);
        for (y = 3; y < 124; y++)
            for (x = 3; x < 124; x++)
            {
                int b = c.xbld[y][x];
                if (b >= 6 && !seen[b])
                {
                    seen[b] = 1;
                    sim_demolish_and_place(&c, y, x, 0);
                }
            }
        for (i = 0; i < MAP_H * MAP_W; i++)
            printf("d %d %02X %02X %02X %02X %04X\n", i, ((const uint8_t *)c.xbld)[i], ((const uint8_t *)c.xzon)[i], ((const uint8_t *)c.xbit)[i], ((const uint8_t *)c.xtxt)[i], ((const uint16_t *)c.altm)[i]);
        printf("n 0 %d\n", rng_log_count());
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--terrain"))
    {
        /*  --terrain <city>: run $128DE over a spread of tiles and
         *  print every layer it can touch.  $5FAA is stubbed on both
         *  sides, so this checks everything except the demolition. */
        static City c;
        int         k, i;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        if (argc >= 4 && !strcmp(argv[3], "nb"))
            for (k = 0; k < 2000; k++)
                sim_fix_neighbourhood(&c, (k * 31) % 128, (k * 17) % 128);
        else
            for (k = 0; k < 2000; k++)
                sim_fix_terrain(&c, (k * 31) % 128, (k * 17) % 128);
        for (i = 0; i < MAP_H * MAP_W; i++)
            printf("t %d %04X %02X %02X %02X %02X\n", i, ((const uint16_t *)c.altm)[i], ((const uint8_t *)c.xter)[i], ((const uint8_t *)c.xbit)[i], ((const uint8_t *)c.xzon)[i], ((const uint8_t *)c.xbld)[i]);
        for (i = 0; i < MAP_H * MAP_W; i++)
            if (((const uint8_t *)c.xund)[i])
                printf("u %d %02X\n", i, ((const uint8_t *)c.xund)[i]);
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--raise"))
    {
        /*  --raise <city>: test then raise a spread of tiles, printing
         *  ALTM, XBIT, XZON and the funds, so $8758 and $896C can be
         *  diffed together the way the volcano uses them. */
        static City c;
        int         k, i;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        c.raise_cost = 25;
        c.funds      = 25000;
        for (k = 0; k < 300; k++)
        {
            int y = (k * 29) % 128, x = (k * 41) % 128;
            if (sim_can_raise(&c, y, x))
                sim_raise_tile(&c, y, x);
        }
        for (i = 0; i < MAP_H * MAP_W; i++)
            printf("a %d %04X %02X %02X %02X %02X\n", i, ((const uint16_t *)c.altm)[i], ((const uint8_t *)c.xbit)[i], ((const uint8_t *)c.xzon)[i], ((const uint8_t *)c.xter)[i], ((const uint8_t *)c.xbld)[i]);
        printf("m 0 %08X\n", (unsigned)c.funds);
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--settile"))
    {
        /*  --settile <city>: run a fixed sequence of setTile calls and
         *  print what moved, so $4110 can be diffed whole -- including
         *  the military branch, which no shipped city exercises on its
         *  own. */
        static City     c;
        static uint16_t cen0[256];
        static int16_t  inf0[16];
        int             k, i;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        memcpy(cen0, c.census, sizeof cen0);
        memcpy(inf0, c.infra, sizeof inf0);
        for (k = 0; k < 4000; k++)
            sim_set_tile(&c, (k * 37) % 128, (k * 53) % 128, (uint8_t)((k * 11) & 0xFF));
        for (i = 0; i < MAP_H * MAP_W; i++)
            printf("b %d %02X\n", i, ((const uint8_t *)c.xbld)[i]);
        for (i = 0; i < 256; i++)
            printf("c %d %04X\n", i, (unsigned)(uint16_t)(c.census[i] - cen0[i]));
        for (i = 0; i < 16; i++)
            printf("i %d %04X\n", i, (unsigned)(uint16_t)(c.infra[i] - inf0[i]));
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--footprint"))
    {
        /*  --footprint <city>: ask $763A about every tile, so the whole
         *  map can be diffed against the original in one run. */
        static City c;
        int         y, x;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
            {
                int oy = y, ox = x;
                int fn = sim_footprint_origin(&c, &oy, &ox, c.xbld[y][x]);
                printf("f %d %d %d %d %d\n", y, x, fn, oy, ox);
            }
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--scenario"))
    {
        static City c;
        int         r, before;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        /*  --scenario <city> [zero|hard] perturbs the goals so the
         *  win path and each comparison can be exercised.  No shipped
         *  save meets its goals, so without this only the "not won"
         *  branch is ever tested. */
        if (argc >= 4 && !strcmp(argv[3], "zero"))
        {
            int k;
            for (k = MISC_GOAL_POP; k <= MISC_TILES_TWO; k++)
                c.misc[k] = 0;
            c.misc[MISC_SCEN_ACTIVE] = (int32_t)0xFF;
        }
        else if (argc >= 4 && !strcmp(argv[3], "hard"))
        {
            int k;
            for (k = MISC_GOAL_POP; k <= MISC_GOAL_EDU; k++)
                c.misc[k] = 0x7FFFFFF;
            c.misc[MISC_SCEN_ACTIVE] = (int32_t)0xFF;
        }
        before = (int)c.misc[MISC_SCEN_MONTHS];
        r      = sim_scenario_check(&c);
        printf("active %d  verdict %d  months %d -> %d\n",
               (int)(uint8_t)c.misc[MISC_SCEN_ACTIVE],
               r,
               before,
               (int)c.misc[MISC_SCEN_MONTHS]);
        city_free(&c);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "--dump-growth-all"))
    {
        /*  All sixteen phases in sequence, the way the clock runs them,
         *  then the layers.  A single phase from a fresh load never
         *  reaches the automatic builds, so this is the only mode that
         *  exercises $B058. */
        static City c;
        char        pp[1024];
        FILE       *f;
        int         yy, xx;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        if (argc >= 5 && !strcmp(argv[4], "--rng"))
            rng_log_start();
        {
            /*  Repeat the whole cycle when asked.  One cycle gives the
             *  aircraft path only about forty chances at a one in
             *  thirty roll, so it almost never fires.  More cycles
             *  reach it. */
            int reps = 1, r;
            if (argc >= 6 && !strcmp(argv[4], "--cycles"))
                reps = atoi(argv[5]);
            for (r = 0; r < reps; r++)
                for (yy = 0; yy < 4; yy++)
                    for (xx = 0; xx < 4; xx++)
                        sim_growth_scan(&c, yy, xx);
        }
        if (rng_log_count())
        {
            int i, drawn = rng_log_count();
            for (i = 0; i < drawn; i++)
            {
                int32_t v;
                int     k = rng_log_entry(i, &v);
                printf("%c %d\n", k, v);
            }
        }
#define GDUMP(name, buf)                             \
    snprintf(pp, sizeof pp, "%s/%s", argv[3], name); \
    f = fopen(pp, "wb");                             \
    if (f)                                           \
    {                                                \
        fwrite(buf, 1, sizeof buf, f);               \
        fclose(f);                                   \
    }
        GDUMP("XBLD", c.xbld)
        GDUMP("XZON", c.xzon)
        GDUMP("XBIT", c.xbit)
        GDUMP("XTRF", c.xtrf)
        GDUMP("XTXT", c.xtxt)
#undef GDUMP
        /*  XTHG is a raw block, not a row-pointer layer. */
        snprintf(pp, sizeof pp, "%s/XTHG", argv[3]);
        f = fopen(pp, "wb");
        if (f && c.xthg)
        {
            fwrite(c.xthg, 1, c.xthg_len, f);
            fclose(f);
        }
        city_free(&c);
        return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "--dump-growth"))
    {
        /*  One growth phase, then the layers it touched, so they can be
         *  compared with the original running the same phase. */
        static City c;
        char        pp[1024];
        FILE       *f;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        trip_mark_log = 1;
        sim_growth_scan(&c, atoi(argv[3]), atoi(argv[4]));
        {
            int q;
            for (q = 0; q < 8; q++)
                printf("ACC %d %d\n", q, (int)c.accum8[q]);
        }
#define GDUMP(name, buf)                             \
    snprintf(pp, sizeof pp, "%s/%s", argv[5], name); \
    f = fopen(pp, "wb");                             \
    if (f)                                           \
    {                                                \
        fwrite(buf, 1, sizeof buf, f);               \
        fclose(f);                                   \
    }
        GDUMP("XBLD", c.xbld)
        GDUMP("XZON", c.xzon)
        GDUMP("XBIT", c.xbit)
        GDUMP("XTRF", c.xtrf)
#undef GDUMP
        city_free(&c);
        return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "--trace-growth"))
    {
        /*  One growth phase, logging every random number drawn.  The
         *  original's stream is captured the same way under the
         *  interpreter; the first entry that differs is the first place
         *  the reconstruction took a different branch. */
        static City c;
        int         i, drawn;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        rng_log_start();
        sim_growth_scan(&c, atoi(argv[3]), atoi(argv[4]));
        drawn = rng_log_count();
        for (i = 0; i < drawn; i++)
        {
            int32_t v;
            int     k = rng_log_entry(i, &v);
            printf("%c %d\n", k, v);
        }
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--growth"))
    {
        /*  Run the sixteen growth phases the way the clock does and
         *  print the population accumulators, so they can be diffed
         *  against the original doing the same. */
        static City c;
        int         i, yy, xx;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        for (i = 1; i < 8; i++)
            c.accum8[i] = 0; /* phase 0, $21F88 */
        for (yy = 0; yy < 4; yy++)
            for (xx = 0; xx < 4; xx++)
                sim_growth_scan(&c, yy, xx);
        for (i = 0; i < 8; i++)
            printf("%d %d\n", i, c.accum8[i]);
        printf("todo %d\n", sim_growth_unimplemented());
        for (i = 1; i < 8; i++)
            if (sim_growth_stub(i))
                printf("stub%d %d\n", i, sim_growth_stub(i));
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--economy"))
    {
        /*  Run the economy and print the globals it touches, so they can
         *  be diffed against the original running under the interpreter. */
        static City c;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        rng_seed(1, 1);
        sim_economy(&c);
        printf("nat %d\n", c.misc[MISC_NAT_INDEX]);
        printf("nat2 %d\n", c.misc[MISC_NAT_INDEX2]);
        printf("mood %d\n", c.misc[MISC_NAT_MOOD]);
        printf("cyc %d\n", c.misc[MISC_NAT_CYCLE]);
        {
            int k;
            for (k = 0; k < 11; k++)
                printf("mix%d %d\n", k, c.industry_mix[k]);
            for (k = 0; k < 11; k++)
                printf("lvl%d %d\n", k, c.industry_level[k]);
            for (k = 0; k < 11; k++)
                printf("wrk%d %d\n", k, c.misc[MISC_IND_WORKERS + 3 * k]);
        }
        {
            int k;
            for (k = 0; k < 20; k++)
            {
                printf("hd%d %d\n", k, c.misc[MISC_HIST_BASE + 3 * k]);
                printf("eq%d %d\n", k, c.misc[MISC_HIST_BASE + 3 * k + 1]);
                printf("le%d %d\n", k, c.misc[MISC_HIST_BASE + 3 * k + 2]);
            }
        }
        printf("pinc %d\n", c.pop_increase);
        printf("pdec %d\n", c.pop_decrease);
        printf("taxa %d\n", c.misc[MISC_AGE_HEAD]);
        printf("taxb %d\n", c.misc[MISC_AGE_W65]);
        printf("taxc %d\n", c.misc[MISC_AGE_W90]);
        {
            int k;
            for (k = 0; k < 4; k++)
                printf("ind%d %d\n", k, c.misc[MISC_IND4_BASE + 4 * k]);
            for (k = 0; k < 4; k++)
                printf("mate%d %d\n", k, c.misc[MISC_IND4_B + 4 * k]);
        }
        city_free(&c);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--budget"))
    {
        /*  Recompute the budget from the tile census and print what each
         *  department comes to, so it can be diffed against the original
         *  running the same city under the interpreter. */
        static City c;
        int         i;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        sim_budget(&c);
        for (i = 0; i < N_DEPT; i++)
            printf("%d %d\n", i, c.dept[i].amount);
        city_free(&c);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "--dump"))
    {
        /*  Run the stages in $2317E's order and write the layers out, so
         *  they can be diffed against the interpreter running the
         *  original's own code from the same starting state.  That
         *  comparison tests the transcription with the save file's
         *  snapshot skew taken out of the picture entirely. */
        static City c;
        char        p[1024];
        FILE       *f;
        if (!city_load(argv[2], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        /*  --power runs the two grid phases first, which is the order
         *  the clock uses: $21FA6 is powerGridReset and $21FB0 the
         *  scan, one tick apart. */
        /*  argv[4] names the phases to run before the scan, which is
         *  how the clock's real ordering gets tested: $21FA6 is
         *  powerGridReset and $21FB0 the scan, one tick apart.
         *      --pre=p   the power grid
         *      --pre=w   the water grid
         *      --pre=pw  both
         *      --pre=p!  the power grid ALONE, no scan
         *      --pre=w!  the water grid alone */
        {
            const char *pre = (argc >= 5 && !strncmp(argv[4], "--pre=", 6))
                                  ? argv[4] + 6
                                  : "";
            if (strchr(pre, 'p'))
                sim_power_grid(&c);
            if (strchr(pre, 'w'))
                sim_water_grid(&c);
            if (!strchr(pre, '!'))
            {
                sim_pollution(&c);
                sim_land_value(&c);
                sim_density(&c);
                sim_crime(&c);
            }
        }
#define DUMP(name, buf)                            \
    snprintf(p, sizeof p, "%s/%s", argv[3], name); \
    f = fopen(p, "wb");                            \
    if (f)                                         \
    {                                              \
        fwrite(buf, 1, sizeof buf, f);             \
        fclose(f);                                 \
    }
        DUMP("XVAL", c.xval)
        DUMP("XCRM", c.xcrm)
        DUMP("XPOP", c.xpop)
        DUMP("XPLT", c.xplt)
        DUMP("XPLC", c.xplc)
        DUMP("XFIR", c.xfir)
        DUMP("XBIT", c.xbit)
#undef DUMP
        city_free(&c);
        return 0;
    }
    if (strcmp(argv[1], "--verify"))
    { /* single city */
        static City c;
        if (!city_load(argv[1], &c))
        {
            fprintf(stderr, "not a city\n");
            return 1;
        }
        sim_rebuild_census(&c);
        printf("%s\n  founded %d  day %d  funds $%d  rotation %d  population %d\n",
               argv[1],
               c.year_founded,
               c.date,
               c.funds,
               c.rotation,
               c.population);
        city_free(&c);
        return 0;
    }

    printf("\nSimCity 2000 simulation, reconstructed from the 68k binary\n");
    for (n = 2; n < argc; n++)
    {
        int f, l, x;
        roundtrip(argv[n], &f, &l, &x);
        files += f;
        lossless += l;
        exact += x;
        for_each_city(argv[n], check_city, &t);
    }
    printf("verified against %d city files\n\n", t.cities);

    printf("  file format\n");
    pct("codec round-trip, lossless", lossless, files, "");
    pct("byte-exact vs the shipped file", exact, files, "two encoders in the corpus");

    printf("\n  totals recomputed from the map\n");
    pct("sum(XPLT) == MISC[13]  pollution", t.agg_ok[0], t.cities, "");
    pct("sum(XVAL) == MISC[10]  land value", t.agg_ok[1], t.cities, "");
    pct("sum(XCRM) == MISC[11]  crime", t.agg_ok[2], t.cities, "");
    pct("sum(XTRF) >= MISC[12]  traffic", t.agg_ok[3], t.cities, "phase-19 snapshot");
    pct("population within 2%", t.agg_ok[4], t.cities, "phase-21 snapshot");

    printf("\n  each stage on its own, given the inputs the file recorded\n");
    printf("    (counting only cells that are non-zero in one or both, so that\n"
           "     empty countryside does not flatter the result)\n");
    pct_err("XCRM crime", t.crm_live_ok, t.crm_live, t.crm_err, t.crm_cells, "same-pass inputs");
    pct_err("XPOP density", t.pop_live_ok, t.pop_live, t.pop_err_abs, t.pop_cells, "map moved on");
    pct_err("XVAL land value", t.val_live_ok, t.val_live, t.val_err, t.val_cells, "scratch data lost");
    pct_err("XPLC police", t.plc_live_ok, t.plc_live, t.plc_err, t.cov_cells, "funding from the budget");
    pct_err("XFIR fire", t.fir_live_ok, t.fir_live, t.fir_err, t.cov_cells, "funding from the budget");
    pct("XBIT powered", t.tiles - t.pow_bad, t.tiles, "");
    pct("XBIT watered", t.tiles - t.wat_bad, t.tiles, "weather moved on at phase 21");

    printf("\n  the whole pass chained, stage 5 -> 7 -> 9\n");
    pct("XVAL land value", t.cval_ok, t.cval_live, "same as isolated: stage 5 runs first");
    pct("XCRM crime", t.ccrm_ok, t.ccrm_live, "now fed my land value, not the file's");

    printf("\n  the budget pass $263C8, recomputed from the tile census\n");
    pct("department amounts", t.bud_ok, t.bud_cells, "16 departments per city");
    pct("cities with all 16 right", t.bud_perfect, t.cities, "");
    pct("census matches a rebuild", t.census_exact, t.cities, "the game maintains it incrementally");

    printf("\n  cities reproduced perfectly\n");
    pct("crime, every cell", t.crm_perfect, t.cities, "");
    pct("density, every cell", t.pop_perfect, t.cities, "");
    pct("police, every cell", t.plc_perfect, t.cities, "");
    pct("fire, every cell", t.fir_perfect, t.cities, "");
    pct("power, every tile", t.pow_det_ok, t.pow_det, "excludes wind/solar cities");
    if (t.pop_err_n)
        printf("\n  mean population error %+.4f%%\n", 100.0 * t.pop_err / t.pop_err_n);
    printf("\n");
    return 0;
}
