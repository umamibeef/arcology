/*  net/band.h: what band.c answers for.
 *
 *  the raised band: which cells make one, the free air a slab may sweep, and the spurs beside it.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_BAND_H
#define ARC_NET_BAND_H

#include "net/types.h"

extern BandSpur s_band_spurs[MAX_SPURS];
extern int    s_band_nspurs;
/*  The band tiles, as arc.rules.band_tiles names them. */
int net_band_spur(uint8_t b);
/*  The slab's reading of the map (net/band.c): which cells make a band,
 *  which are free air, and how far beside a band the corridor reaches. */
void                 band_spur_lost_is(int32_t col, int32_t row, int why);
/*  The band's bands, as the script discovered them (net/network.c),
 *  and the five planes it reads them off (net/band.c). */
int  net_band_replaying(void); /* 1 where this build finds its bands rather than replaying them */
/*  A band's chain of fit points: which of its cells turn, which
 *  are pinned by a spur, and the points the chain is given. */
int   band_stair_turn(const StairFan *s, int i);
int   band_stair_pinned(const StairFan *s, int i);
void  band_stair_point(StairFan *s, int i);
void  band_stair_centre(StairFan *s, int i, int j);
/*  A band strip's elevation, station by station: what the script
 *  reads and writes, and the easing curve a lane drop follows. */
void  band_prof_at(const ProfFan *p, int i, float *s_at, float *z, float *ground);
void  band_prof_pose(const ProfFan *p, int i, float *x, float *y, float *dx, float *dy);
void  band_prof_set(ProfFan *p, int i, float z);
/*  The lane a spur drops from a slab: the stations and the spurs, and
 *  the width each station is left with. */
int   band_drop_station(const DropFan *d, int i, float *at, V2 *pos, V2 *dir);
int   band_drop_spur(const DropFan *d, int r, V2 *c0, V2 *tile, V2 *along, int *len, int *off);
void  band_drop_clear(DropFan *d);
void  band_drop_width(DropFan *d, int i, int side, float w);
void  band_drop_gore(DropFan *d, int i, int side);
int build_bands(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
int  build_band_next(void);
int  build_band_chained(void);
StairFan *net_band_chain(void);
int  build_band_fitted(void);
int  build_band_cut(void);
int  net_band_fits(void);
int  net_band_fit_begin(int w);
void net_band_fit_done(int w);
const char *net_band_fit_choice(const int **free_, const int **held);
void net_band_fit_choice_is(int keep_free);
int  build_band_done(void);
int  build_band_spurs(RMesh *m, const RCity *c, const RAtlasLevel *l, uint8_t mask_bit, int comp);
void *build_band_links(RMesh *m, const RCity *c, uint8_t mask_bit);
int   build_band_links_done(void);
const char *band_spur_lost(int32_t col, int32_t row);             /* band.c: why the on-spur at a tile was not built this pass, or NULL */
int  band_count(void);                               /* band.c: the bands' own tiles, -1 past its table */
int  band_get(int i, const int32_t **tiles, int *n); /* ... as row * R_MAP + col */
/*  THE LANES WITHIN REACH OF A POINT (net/lane.c), measured and offered
 *  for the script to pick one.  Which lane a spur's end fastens to is
 *  arc.rules.spur_lane's.  One candidate per piece of every lane within
 *  reach.  The pick is an index into them, or -1 for none. */
void net_spur_snap_what(const char **what, int *band);
void net_spur_snap_is(const char *what, int band); /* which snap the drive is being asked about */

#endif /* ARC_NET_BAND_H */
