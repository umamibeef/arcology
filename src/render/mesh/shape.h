/*  shape.h -- the shapes the mesh is made of.
 *
 *  Every land triangle belongs to a SHAPE, and a shape knows what it is
 *  and where it came from.  A producer opens one, says what it is, draws
 *  into it, and closes it: the triangles emitted meanwhile are its own by
 *  construction.  Nothing works out where one shape ends and the next
 *  begins.  A rule over the call site, the material and how near the last
 *  triangle was can only do that badly -- it merges a whole city's
 *  footways into one object, and leaves a strip's own triangles outside
 *  the strip.
 *
 *  Shapes NEST.  A junction opens a shape, and its asphalt, its kerb
 *  returns, its footways and its markings open theirs inside it, so the
 *  inspector can name both the piece under the pointer and the thing it
 *  is part of.
 *
 *  A triangle emitted with no shape open is UNCLAIMED.  The mesh check
 *  counts those and names the call sites that made them, and that count
 *  is meant to reach zero.
 */
#ifndef R_SHAPE_H
#define R_SHAPE_H

#include <stddef.h>
#include <stdint.h>

#ifndef R_STR
    #define R_STR2(x) #x
    #define R_STR(x)  R_STR2(x)
#endif

typedef uint32_t ShapeId;
#define SHAPE_NONE 0xFFFFFFFFu

/*  Open a shape and say what it is: the name is what the inspector calls
 *  it and what an outline is labelled with, so it names the thing ("road
 *  strip 62,55 to 67,55"), not the routine.  Opens inside whatever shape
 *  is already open, which becomes its parent. */
ShapeId shape_open_at(const char *where, const char *who, const char *fmt, ...);
#define shape_open(...) shape_open_at(__FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
/*  The same, under a parent named outright.  What a thing BELONGS to and
 *  when it happens to be drawn are two different things: a junction's
 *  footway is laid in a later pass, and says whose it is here. */
ShapeId shape_open_under_at(ShapeId parent, const char *where, const char *who, const char *fmt, ...);
#define shape_open_under(p, ...) shape_open_under_at((p), __FILE__ ":" R_STR(__LINE__), __func__, __VA_ARGS__)
/*  Close it.  The id is passed back so a mismatch is caught rather than
 *  silently unbalancing the stack. */
void shape_use(ShapeId id);
void shape_close(ShapeId id);
/*  A line of the open shape's own account of itself: key, TAB, value.
 *  The inspector shows these as rows. */
void    shape_note(const char *fmt, ...);
ShapeId shape_current(void);

/*  A build starts the table again; recording is on only while one runs,
 *  so the traffic's per-frame prisms add nothing to it. */
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
