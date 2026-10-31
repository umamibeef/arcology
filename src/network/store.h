/*  store.h: the road network kept by place.
 *
 *  A build derives the network from a window of chunks and the cells
 *  within net_halo of it.  That derivation, the state of every layer of
 *  the network and the copy of the city it was derived from, is kept as
 *  one NetStore.  Each chunk the build laid points at it.  A later build
 *  points its own chunks at its own NetStore, and the others stand.  A
 *  NetStore no chunk points at is freed.
 *
 *  A reader of a place loads the network of that place's chunk
 *  (net_store_use_at).  Every index the network answers belongs to the
 *  NetStore that is loaded.  The network is never read while a build
 *  runs. */
#ifndef NET_STORE_H
#define NET_STORE_H

#include <stdint.h>

#include "city.h"

typedef struct NetStore NetStore;

/*  Before a build derives the network: the layers start empty, and every
 *  kept NetStore stands untouched. */
void net_store_begin(void);

/*  A derivation that is not kept, such as the lane check's or the street
 *  layer's on import.  What the layers hold since net_store_begin is
 *  freed, and they start empty again. */
void net_store_discard(void);

/*  After the build: what the layers hold becomes one NetStore.  It keeps
 *  the city `city` it was derived from, which the store then owns, and
 *  that city's rcity_hash.  The
 *  layers keep it loaded.  The chunks `in` marks, one byte a chunk of
 *  the map, point at it from then on.  The NetStore of a chunk it takes
 *  over is freed once no chunk points at it.  0, or -1 where there is no
 *  memory, and then the city is freed. */
int net_store_keep(RCity *city, const uint8_t *in, uint64_t hash, int shaped);

/*  A network kept before, derived from a city whose rcity_hash is `hash`,
 *  loaded and taken by the chunks `in` marks.  `shaped` says whether its
 *  roads could be shaped.  1 where there is one, else 0, and nothing
 *  changes. */
int net_store_reuse(uint64_t hash, const uint8_t *in, int *shaped);

/*  Load the network of the chunk that tile `col`, `row` lies in.  0, or
 *  -1 where no build has laid that chunk. */
int net_store_use_at(int32_t col, int32_t row);

/*  Which NetStore is loaded, to tell one from another: NULL before any
 *  build. */
const NetStore *net_store_loaded(void);

#endif
