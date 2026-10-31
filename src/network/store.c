/*  store.c: the road network kept by place (store.h).  Each NetStore is
 *  the saved state of the four layers, one after another, and the city
 *  they were derived from.  A table by chunk points at the NetStore of
 *  each chunk.  A count of the chunks that point at a NetStore frees it
 *  when it falls to nought. */
#include "network/store.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "network/bank.h"
#include "network/network.h"
#include "network/shape.h"
#include "network/topology.h"

struct NetStore
{
    unsigned char *bytes;
    RCity         *city;
    uint64_t       hash;   /* rcity_hash of the city it was derived from */
    int            shaped; /* 1 where its roads could be shaped */
    int            refs;
};

/*  The layers in the order their state is laid out. */
typedef struct
{
    size_t (*size)(void);
    void (*save)(unsigned char *p);
    void (*load)(const unsigned char *p);
    void (*release)(void);
} Layer;

static const Layer LAYERS[] = {
    {topology_state_size, topology_state_save, topology_state_load, topology_release},
    {network_state_size,  network_state_save,  network_state_load,  network_release },
    {shape_state_size,    shape_state_save,    shape_state_load,    shape_release   },
    {bank_state_size,     bank_state_save,     bank_state_load,     bank_release    },
};
#define N_LAYERS ((int)(sizeof LAYERS / sizeof LAYERS[0]))

static unsigned char *s_empty;  /* the layers as the program starts, before any build */
static NetStore      *s_loaded; /* the NetStore the layers hold */
static NetStore     **s_at;     /* per chunk, its NetStore */
static int            s_nat;    /* the chunks s_at holds */
static int            s_nstore; /* the NetStores that stand */

static size_t state_size(void)
{
    size_t n = 0;
    int    k;
    for (k = 0; k < N_LAYERS; ++k)
        n += LAYERS[k].size();
    return n;
}

static void save(unsigned char *p)
{
    int k;
    for (k = 0; k < N_LAYERS; ++k)
        LAYERS[k].save(p), p += LAYERS[k].size();
}

static void load(const unsigned char *p)
{
    int k;
    for (k = 0; k < N_LAYERS; ++k)
        LAYERS[k].load(p), p += LAYERS[k].size();
}

/*  What the layers hold goes back into the NetStore they hold.  A
 *  reader may have grown an array of it since it was loaded. */
static void write_back(void)
{
    if (s_loaded)
        save(s_loaded->bytes);
}

/*  The layers empty, holding no NetStore. */
static void load_empty(void)
{
    load(s_empty);
    s_loaded = NULL;
}

static void store_free(NetStore *st)
{
    int k;
    write_back();
    load(st->bytes);
    for (k = 0; k < N_LAYERS; ++k)
        LAYERS[k].release();
    if (s_loaded && s_loaded != st)
        load(s_loaded->bytes);
    else
        load_empty();
    free(st->bytes);
    s_nstore--;
    rcity_free(st->city);
    free(st->city);
    free(st);
}

static void net_store_reset(void);

static void unref(NetStore *st)
{
    if (st && --st->refs == 0)
        store_free(st);
}

void net_store_begin(void)
{
    if (!s_empty)
    {
        s_empty = (unsigned char *)malloc(state_size());
        if (!s_empty)
            return;
        save(s_empty);
    }
    write_back();
    load_empty();
}

void net_store_discard(void)
{
    int k;
    if (s_loaded)
        return;
    for (k = 0; k < N_LAYERS; ++k)
        LAYERS[k].release();
    load_empty();
}

/*  Point the chunks `in` marks at `st`.  A NetStore no chunk points at
 *  any more is freed, `st` counted first. */
static void point(NetStore *st, const uint8_t *in, int n)
{
    int ch;
    st->refs++;
    for (ch = 0; ch < n; ++ch)
        if (in[ch] && s_at[ch] != st)
        {
            NetStore *was = s_at[ch];
            s_at[ch]      = st;
            st->refs++;
            unref(was);
        }
    unref(st);
}

int net_store_reuse(uint64_t hash, const uint8_t *in, int *shaped)
{
    int ch, n = (r_map / MESH_CHUNK) * (r_map / MESH_CHUNK);
    if (n != s_nat)
        return 0;
    for (ch = 0; ch < s_nat; ++ch)
        if (s_at[ch] && s_at[ch]->hash == hash)
        {
            NetStore *st = s_at[ch];
            if (st != s_loaded)
            {
                write_back();
                load(st->bytes);
                s_loaded = st;
            }
            *shaped = st->shaped;
            point(st, in, n);
            return 1;
        }
    return 0;
}

int net_store_keep(RCity *city, const uint8_t *in, uint64_t hash, int shaped)
{
    int       n = (r_map / MESH_CHUNK) * (r_map / MESH_CHUNK);
    NetStore *st;
    if (n != s_nat)
    {
        net_store_reset();
        s_at = (NetStore **)calloc((size_t)n, sizeof *s_at);
        if (!s_at)
            return -1;
        s_nat = n;
    }
    st = (NetStore *)calloc(1, sizeof *st);
    if (st)
        st->bytes = (unsigned char *)malloc(state_size());
    if (!st || !st->bytes)
    {
        free(st);
        rcity_free(city);
        free(city);
        R_ERR("net", "no memory to keep the road network");
        return -1;
    }
    save(st->bytes);
    st->city   = city;
    st->hash   = hash;
    st->shaped = shaped;
    s_loaded   = st;
    s_nstore++;
    point(st, in, n);
    R_DBG("net", "a network kept: %d networks stand", s_nstore);
    return 0;
}

int net_store_use_at(int32_t col, int32_t row)
{
    int per = r_map / MESH_CHUNK, ch;
    if (col < 0 || row < 0 || col >= r_map || row >= r_map || !s_at)
        return -1;
    ch = row / MESH_CHUNK * per + col / MESH_CHUNK;
    if (ch >= s_nat || !s_at[ch])
        return -1;
    if (s_at[ch] != s_loaded)
    {
        write_back();
        load(s_at[ch]->bytes);
        s_loaded = s_at[ch];
    }
    return 0;
}

const NetStore *net_store_loaded(void)
{
    return s_loaded;
}

static void net_store_reset(void)
{
    int ch;
    for (ch = 0; ch < s_nat; ++ch)
    {
        NetStore *st = s_at[ch];
        s_at[ch]     = NULL;
        unref(st);
    }
    free(s_at);
    s_at  = NULL;
    s_nat = 0;
}
