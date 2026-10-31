/*  mesh.h: the terrain as geometry, replicating the sprites.  Each land
 *  tile becomes the faces its sprite draws.  The top has its four
 *  corners at ALTM's ground, plus the lift of the slope code's corner
 *  bits.  It is cut along the diagonal the sprite is cut along,
 *  flat-shaded.  A corner's height is shared with the tiles meeting
 *  there, which is what the grid says on 97% of the shipped cities'
 *  corners.  Where two tiles disagree, the mean joins them.  So the dark
 *  wedge of wall the higher tile's sprite shows above a lower neighbor
 *  does not appear.  The result is one connected surface with the
 *  sprites' faces and no walls but the map's edge.  Water tiles are flat
 *  ground at their table.  Their sprites' water pixels cover it, and
 *  with the mesh on the sprites drop their sand rim so the ground runs
 *  up to the water.  The mesh has no state of its own.  It is rebuilt
 *  when the grid changes. */
#ifndef R_MESH_H
#define R_MESH_H

#include <stddef.h>
#include <stdint.h>

#include "atlas/atlas.h"
#include "city.h"
#include "mesh/shape.h"
#include "mesh/vert.h"

#define MESH_CHUNKS     ((r_map / MESH_CHUNK) * (r_map / MESH_CHUNK)) /* chunks on the map         */
#define MESH_CHUNKS_CAP 4096                                          /* at most: 2048 tiles a side */
/*  The chunk a tile is in.  A tile off the map counts as the nearest tile
 *  on it. */
int mesh_chunk_of(int32_t col, int32_t row);

/*  The land ranges of one chunk, in the order the frame draws them.  The
 *  ground, then what stands on it.  Then the trees twice: whole, and each
 *  as a simple crown.  The frame draws the trees at one of the two
 *  levels, or at both across a band (trees.c). */
enum
{
    MESH_RANGE_GROUND,
    MESH_RANGE_STANDS,
    MESH_RANGE_TREES,
    MESH_RANGE_CROWNS,
    MESH_RANGES
};

/*  The most boxes of chunks a build's window is made of. */
#define MESH_WINDOW_BOXES 16

/*  THE WINDOW this build lays (mesh/window.c): boxes of whole chunks, each
 *  in tiles, first column, first row, last column + 1, last row + 1.  The
 *  build names it before it lays anything, and n 0 is the whole map.
 *  mesh_window copies it out and answers how many boxes.  A tile lies in
 *  it (mesh_window_has), or the build visits it, the window's tiles and
 *  one round them (mesh_wants_tile).  Every tile, in a whole build. */
void mesh_window_set(const int32_t box[][4], int n);
int  mesh_window(int32_t box[MESH_WINDOW_BOXES][4]);
int  mesh_window_has(int32_t col, int32_t row);
int  mesh_wants_tile(int32_t col, int32_t row);

/*  World units per altitude level, for the normals and for the sun's
 *  shadow.  The projection draws a level as 0.75 of a tile height
 *  whatever this says. */
#define MESH_LEVEL_H    0.5f
#define MESH_LOD_LEVELS 6
#define MESH_LOD_ERR0   (1.0f / 32.0f)

/*  One list of triangles.  A build LAYS it three vertices a triangle,
 *  and PACKS it when it is done (mesh_list_pack).  A packed list holds
 *  each distinct vertex once, and three corners a triangle name them.  So a packed list is
 *  what the device draws.  Per triangle it holds the shape that made it,
 *  the piece being drawn when it was laid, and its painter's order.  The
 *  inspector points at a triangle and asks what drew it.  An edit keeps
 *  the triangles of every piece it does not draw again. */
typedef struct
{
    RMeshVert *v;
    uint32_t   nv, cap; /* vertices: n of them as laid, nv packed */
    uint32_t  *idx;     /* packed: n corners, places in v.  NULL while laid */
    uint32_t   n;       /* corners, three a triangle */
    uint32_t  *comp;    /* SHAPE_NONE in a list of water */
    uint32_t  *piece;   /* as mesh_piece_key makes it, or 0 for none */
    float     *order;   /* the painter's order the checks read */
    int32_t   *tile;    /* the tile each triangle belongs to (shape_tile_of), which a mark on it lights */
    uint32_t   tcap;    /* triangles `comp`, `piece`, `order` and `tile` hold room for */
    /*  Packed, its LEVELS OF DETAIL.  Level 0 is `idx` itself.  Level L
     *  after it holds the corners of the clusters drawn where a pixel
     *  hides an error of MESH_LOD_ERR0 times 2^(L-1) tiles.  The cut is
     *  mesh_list_lod's.  NULL where it is level 0's. */
    uint32_t *lod[MESH_LOD_LEVELS];
    uint32_t  lod_n[MESH_LOD_LEVELS];
} RMeshList;

typedef struct
{
    /*  The mesh by CHUNK, one list a range.  A chunk is MESH_CHUNK tiles
     *  a side.  Land range MESH_RANGES k + r is chunk k's range r, and
     *  water range k is chunk k's water.  So the frame draws only the
     *  chunks in view.  A triangle goes to the chunk its first corner
     *  lies in. */
    RMeshList *rl, *wl;
    int        n_rl, n_wl;
    RMeshList *rl_old, *wl_old; /* the last build's, while an edit builds */
    /*  A triangle's NUMBER is its place in the ranges laid end to end,
     *  land then water.  These are where each range starts and how much
     *  it holds, in vertices, set after a build. */
    uint32_t range_start[MESH_RANGES * MESH_CHUNKS_CAP], range_count[MESH_RANGES * MESH_CHUNKS_CAP];
    uint32_t wrange_start[MESH_CHUNKS_CAP], wrange_count[MESH_CHUNKS_CAP];
    uint32_t n_land, n_water; /* vertices in all the ranges */
    int      range;           /* the chunk's land range a triangle goes to: MESH_RANGE_GROUND until the ground is done */
    int      ranged;
    uint8_t  chunk_changed[MESH_CHUNKS_CAP]; /* which chunks this build changed: every one after a full build, the wanted ones after an edit's: the GPU uploads those */
    uint8_t  chunk_laid[MESH_CHUNKS_CAP];    /* which chunks hold what a build laid there: every one after a whole build, a window's once it is built */
    /*  The city the last build saw, and the key it was built under
     *  (incr.c).  The next build diffs against them.  For a small
     *  edit under the same key, rebuilds only the chunks it touched.
     *  The snapshot is one RCity, allocated once with its layers, and
     *  incr_free frees it. */
    void    *snap;
    int      snap_ok;
    uint8_t  key[256];
    size_t   key_len;
    int      to_water; /* while set, triangles go to the water lists */
    uint32_t n_walls;  /* retaining walls emitted, for the report */
} RMesh;

void mesh_free(RMesh *m);
/*  The lists (mesh/list.c).
 *
 *      Begin: `n_rl` land ranges and `n_wl` water ranges, each empty.
 *      A list keeps its room for the next build.
 *      Room: space in a list for one more triangle.
 *      Reserve: space in a list for `verts` vertices in all.
 *      Pack: every list packed (mesh_list_pack).
 *      Set ranges: every range's place in the numbering, and the counts.
 *
 *  0 on success for each that can fail. */
int  mesh_lists_begin(RMesh *m, int n_rl, int n_wl);
void mesh_lists_free(RMeshList *l, int n);
int  mesh_list_room(RMeshList *l);
void mesh_lists_pack(RMesh *m);
void mesh_ranges_set(RMesh *m);
/*  Corner `i` of a list, laid or packed. */
static inline const RMeshVert *mesh_list_corner(const RMeshList *l, uint32_t i)
{
    return &l->v[l->idx ? l->idx[i] : i];
}
/*  Triangle `t` of a list, copied out. */
void mesh_list_tri(const RMeshList *l, uint32_t t, RMeshVert out[3]);
/*  Triangle `t` by its number.  Copy gives its three vertices, 0 on
 *  success.  The others give the shape that made it, the piece it
 *  belongs to, its painter's order and its material.  A number past the land is the
 *  water's.  -1, SHAPE_NONE and 0 past the end. */
int      mesh_tri_copy(const RMesh *m, uint32_t t, RMeshVert out[3]);
uint32_t mesh_tri_comp(const RMesh *m, uint32_t t);
float    mesh_tri_order(const RMesh *m, uint32_t t);
float    mesh_tri_mat(const RMesh *m, uint32_t t);
/*  The emitter's own scratch: the triangle index by tile and the
 *  coplanar check's hashes, kept between builds. */
void mesh_emit_free(void);

/*  What the mesh made of a tile, for the query tool.  It gives its kind,
 *  the corners of the face it draws, and the shared terrain around it.
 *  Valid after a build, which fills the fields it reads. */
int mesh_query(const RCity *c, int32_t col, int32_t row, char *buf, size_t n);
/*  Who drew the mesh on a tile.  It gives the name of each function that
 *  emitted a land triangle there.  It also gives the material it drew
 *  in, and how many triangles it made, one to a line.  The inspector
 *  shows it.  Returns how many entries the tile has, -1 if the tile is
 *  off the map. */
int mesh_origins(int32_t col, int32_t row, char *out, size_t n);

/*  Pointing at the mesh: the land triangles whose middle lies in a tile,
 *  and one triangle's corners and the shape that holds it. */
int  mesh_tris_at(const RMesh *m, int32_t col, int32_t row, uint32_t *out, int max);
void mesh_tri_get(const RMesh *m, uint32_t t, float p[3][3], uint32_t *comp);
/*  Every land triangle whose shape `grp` gives a group, in one pass: at
 *  most `max` of them into `out`, and each one's group into `gout`.
 *  `grp` holds one group a shape, `ngrp` of them, nought for none.  The
 *  count. */
int mesh_tris_by(const RMesh *m, const uint16_t *grp, uint32_t ngrp, float (*out)[3][3], uint16_t *gout, int max);
/*  The heights of the four corners of the face the mesh draws for a
 *  tile, NW, NE, SE, SW, for the query highlight.  With `underground`
 *  the ground the underground view draws there, the seabed under water. */
int mesh_tile_corners(const RCity *c, int32_t col, int32_t row, int underground, float z[4]);

/*  The watertight check: every edge of every triangle must belong to at
 *  least one other triangle, so the surface has no crack.  Returns the
 *  number of free edges found (0 when the mesh is closed), -1 when out
 *  of memory.  With `verbose` it prints the first forty and a summary by
 *  material.  Meant for a build with `rotated` set, which cuts all four
 *  map edges.  The base of the cut is the floor and counts as closed. */
int mesh_check(const RMesh *m, int verbose);
/*  The shape table kept to the shapes the mesh's lists claim, one to each
 *  name under one parent, and the ids in the lists renumbered.  It does
 *  nothing while a shape is open.  Nonzero when out of memory
 *  (mesh/shape.c). */
int shape_compact(RMesh *m);
/*  Two faces of DIFFERENT shapes covering the same ground at the same
 *  height.  Nothing separates such a pair but the painter's slot each
 *  carries, and where the slots agree too, nothing at all does.  Answers
 *  how many pairs share a slot as well. */
int mesh_check_overlap(const RMesh *m, int verbose);
/*  Every face of the mesh, or of one tile ("C,R"), to the dump sink: its
 *  tile, shape, slot and corners byte for byte (--tile-dump). */
void mesh_tile_dump(const RMesh *m, const char *which);
/*  Two faces of different shapes that pass THROUGH one another: geometry
 *  no painter's order can separate.  Answers how many pairs. */
int mesh_check_collide(const RMesh *m, int verbose);
/*  Every surface over one point of the map, highest first: its height
 *  there, its painter's slot, its material and the shape it belongs to.
 *  What to reach for when a surface is hidden or two of them flicker. */
void mesh_probe(const RMesh *m, float px, float py);

/* ---- mesh.c: the build's clock, read by the stages' timing lines */
double tms(void);

#endif /* R_MESH_H */
