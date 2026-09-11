/*  net/internal.h: what one file in this directory lends another.
 *
 *  A name here is the DIRECTORY'S, not the pipeline's.  It is declared
 *  because two files in net/ answer one concern between them, and it is
 *  visible nowhere else.  What the rest of the renderer may call is in
 *  net/net.h and the headers under it.
 *
 *  A name earns a place here by being needed across a split, and loses
 *  it the moment one file answers for it again. */
#ifndef ARC_NET_INTERNAL_H
#define ARC_NET_INTERNAL_H

#include "net/types.h"

/*  band.c reads the map for band_walk.c: which cells make a band, the
 *  free air its slab may sweep over, and the tiles each walk covered. */
void                 net_bands_reset(void);
void                 net_band_record(const int32_t *own, int n);
void                 band_lane_stations(Sample *smp, int ns);
int                  band_is_incline(uint8_t b);
void                 band_free_air(const RCity *c, const RAtlasLevel *l);
const uint8_t       *band_corridor(const RCity *c, const int32_t *own, int n_own, int spurs);
extern int           gix_slab_ground_margin;

/*  How many spur tiles one map may carry. */
#define ORIENTS_MAX 4096

/*  spur.c reads the spur tiles for spur_build.c: the pose each was
 *  given, the slab and line beside it, and the count of what was built. */
/*  One spur's working state, handed to the stages below so each can be
 *  read on its own.
 *
 *      The spur tile.
 *      The slab beside it.
 *      The line it joins and how.
 *      The two poses.
 *      The pieces. */
typedef struct
{
    RMesh             *m;
    const RCity       *c;
    uint8_t            mask_bit;
    int                comp;
    int32_t            col, row;
    Piece             *pc;
    const BandSpur      *rp;
    const RAtlasLevel *l;
    int                line_port; /* the junction port the line end is, or -1 */
    float              taper;     /* the loft eases the lane down to the line over this much of its end: the line part and a little of the spur tile.  0 when the spur ends at a port */
    float              merge;     /* how far along the line the join slid, tiles.  -1 for the two legs through the foot */
    int                iref, side, sgn, band, fork, np, np1, np2, slab_lane, line_lane, flat, form, off, lane_off, how, lines, k, q, r;
    int                cut;  /* where the drive's pieces for the descent are */
    int                legs; /* ... and for the two legs through the foot */
    int                dside, rside, eside;
    float              ds, s_top, s_foot, total_len, rmin, z0, climb, total, best;
    V2                 rd, mdir, A, tA, F, tF, B, tB, along, toward;
} Spur;
int                  spur_orient(const RCity *c, int32_t col, int32_t row, int *dside, int *rside, int *eside, V2 *along, V2 *toward, int *off, int *lines);
int                  spur_find(Spur *x);
int                  spur_classify(Spur *x);
extern int           gix_lane_line;
extern int           s_spur_forms[6];

#endif /* ARC_NET_INTERNAL_H */
