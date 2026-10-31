/*  shape.h: the shapes the mesh is made of.
 *
 *  Every land triangle belongs to a SHAPE, and a shape knows what it is
 *  and where it came from.  A producer opens one, says what it is, draws
 *  into it, and closes it: the triangles emitted meanwhile are its own
 *  by construction.  Nothing works out where one shape ends and the next
 *  begins.  A rule over the call site, the
 *  material and how near the last triangle was can only do that badly.
 *  It merges neighbors into one object and leaves a thing's own
 *  triangles outside it.
 *
 *  Shapes NEST.  A tile's ground opens a shape, and what is drawn as
 *  part of it opens its own inside.  So the inspector can name both the
 *  piece under the pointer and the thing it is part of.
 *
 *  A triangle emitted with no shape open is UNCLAIMED.  The mesh check
 *  counts those.  It names the call sites that made them, and that count
 *  is meant to reach zero. */
#ifndef R_SHAPE_H
#define R_SHAPE_H

#include <stddef.h>
#include <stdint.h>

#include "city.h"

#ifndef R_STR
    #define R_STR2(x) #x
    #define R_STR(x)  R_STR2(x)
#endif

typedef uint32_t ShapeId;
#define SHAPE_NONE 0xFFFFFFFFu

/*  Open a shape and say what it is: the name is what the inspector calls
 *  it and what an outline is labeled with.  So it names the thing ("power
 *  line at 62,55"), not the routine.  Opens inside whatever shape
 *  is already open, which becomes its parent. */
ShapeId shape_open_at(const char *where, const char *who, const char *fmt, ...);
#define shape_open(...) shape_open_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
/*  Close it.  The id is passed back so a mismatch is caught rather than
 *  silently unbalancing the stack. */
void shape_close(ShapeId id);
/*  File what is emitted next under a shape opened and closed before, until
 *  shape_close names it again.  So one primitive can lay the parts of one
 *  cross-section, each under its own shape. */
void shape_enter(ShapeId id);
/*  A line of the open shape's own account of itself: key, TAB, value.
 *  The inspector shows these as rows. */
void shape_note(const char *fmt, ...);
/*  The record the open shape draws, as its producer names it: a kind and
 *  a key.  The table keeps the two and reads nothing into them.  A key
 *  that names a record by its place stays the same across builds, and a
 *  shape's index does not. */
void shape_ref(int kind, uint64_t key);
/*  The record a shape draws: its own, or the nearest one up its parents.
 *  The shape that holds it, or SHAPE_NONE where no shape up the tree has
 *  one. */
ShapeId shape_ref_of(ShapeId id, int *kind, uint64_t *key);
/*  The tiles the open shape stands on: `n` cells (row * side + col),
 *  each the first cell of a unit `span` cells a side.  A triangle the
 *  shape claims belongs to the tile of these nearest its middle.  A
 *  shape with none takes its parent's. */
void shape_tiles(const int32_t *cells, int n, int span);
/*  The open shape draws network `net` (RNET_*).  A triangle it claims
 *  belongs to the nearest cell that carries a piece of that network.  A
 *  shape with neither tiles nor a network takes its parent's. */
void shape_net(int net);
/*  The open shape belongs to no tile, so no mark lights it.  The lane
 *  graph is one: it is drawn over the marks on the tiles under it. */
void shape_unmarked(void);
/*  The city a build lays, which shape_net reads its cells from. */
void shape_city(const RCity *c);
/*  The tile the triangle `p` of shape `id` belongs to, by its shape's
 *  tiles or network, or the nearest such up its parents.  -1 where no
 *  shape up the tree has either, or no cell is near.  SHAPE_TILE_NONE
 *  where the shape belongs to no tile. */
#define SHAPE_TILE_NONE (-2)
int32_t shape_tile_of(ShapeId id, const float p[3][3]);

/*  A build starts the table again.  Recording is on only while one
 *  runs, so nothing drawn between builds adds to it. */
void shape_reset(void);
void shape_record(int on);
/*  The triangle about to be written belongs to the open shape.  Returns
 *  the shape it was filed under, or SHAPE_NONE when none was open, which
 *  is counted against the call site. */
ShapeId shape_claim(const char *where, const char *who, const float p[3][3], float mat);

int         shape_count(void);
const char *shape_name(ShapeId id);
const char *shape_note_of(ShapeId id);
const char *shape_where(ShapeId id);
const char *shape_who(ShapeId id);
ShapeId     shape_parent(ShapeId id);
/*  Its material (-1 when it draws in more than one), how many triangles
 *  it holds, and the box round them.  Nonzero when the id is not one. */
int shape_get(ShapeId id, float *mat, uint32_t *tris, float box[6]);
/*  The same over a shape AND everything under it, which is what a parent
 *  is worth highlighting as. */
int shape_get_deep(ShapeId id, uint32_t *tris, float box[6]);
int shape_is_ancestor(ShapeId anc, ShapeId id);
/*  Something built on the ground rather than the ground, its banks, its
 *  water or a tint laid over it. */
int shape_built(ShapeId id);
/*  The check's line: how many triangles no shape claimed, and the call
 *  sites that made them. */
void shape_unclaimed_report(void);

#endif /* R_SHAPE_H */
