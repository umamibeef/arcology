/*  incr.h: THE BUILD BY WINDOWS.
 *
 *  Every build lays a window of chunks.  A whole build is the window that
 *  is every chunk.  A window laid into the mesh standing replaces its
 *  chunks whole, and an edit is the window round the tiles it changed.
 *
 *  It sits above mesh/ and net/ because both ask it whether a tile lies
 *  in what this build lays.  The rest of it is the build's own, and only
 *  build.c calls that. */
#ifndef ARC_INCR_H
#define ARC_INCR_H

#include <stddef.h>

#include "mesh/mesh.h"

/*  What a build is, as incr_begin answers it. */
enum
{
    INCR_STANDS = -1, /* the same city under the same key: the mesh stands */
    INCR_WHOLE  = 0,  /* every chunk, or the window named, laid afresh */
    INCR_WINDOW = 1,  /* the window named, laid into the mesh standing */
    INCR_EDIT   = 2   /* the chunks an edit can reach, laid into the mesh standing */
};

/*  The box round the tiles an edit changed: the least column and row,
 *  then the greatest.  Nonzero when no tile changed. */
int incr_dirty_box(int32_t box[4]);

/*  The window this build lays, as boxes of whole chunks, each in tiles:
 *  first column, first row, last column + 1, last row + 1.  A block of
 *  the world is one box, and an edit is a box round each group of the
 *  tiles it changed.  Answers how many, 0 where none is named.  The
 *  build hands it to the mesh (mesh_window_set). */
#define INCR_BOXES MESH_WINDOW_BOXES
int incr_window(int32_t box[INCR_BOXES][4]);
/*  Name the window in tiles, either corner first, or NULL for the whole
 *  map.  And step on to the next window --build-window lists: 0 where
 *  it lists no more. */
void incr_window_set(const int32_t tiles[4]);
int  incr_window_next(void);
/*  A build of a window into the mesh standing: whether one is under way,
 *  and its end, where the window's chunks replace the mesh's own.  0 on
 *  success. */
int incr_window_building(void);
/*  Whether the window under way is an edit's that the show plays: the
 *  chunks within the halo of the tiles it changed.  A build the clock
 *  asks for (incr_quiet_next) is none. */
int incr_window_edit(void);
int incr_window_end(RMesh *m);
/*  And the build's own.  Begin one, and take the city it saw for the
 *  next to diff against.  Give the mesh back where a window's build
 *  failed. */
int incr_begin(RMesh *m, const RCity *c, const void *key, size_t keylen);
/*  How a tile has changed since the last build.  A NEAR change is the
 *  forest's alone (bare land, rubble, a forest stage): it reaches its own
 *  chunk.  Any other reaches net_halo (geo.c). */
enum
{
    INCR_CHANGE_NONE = 0,
    INCR_CHANGE_NEAR,
    INCR_CHANGE_FAR
};
/*  The furthest reaching change of the city c since the last build. */
int incr_pending(const RMesh *m, const RCity *c);
/*  The next build is the clock's, not the player's: it shows nothing. */
void incr_quiet_next(void);
int  incr_snapshot(RMesh *m, const RCity *c, const void *key, size_t keylen);
void incr_abort(RMesh *m);
/*  Free the city the last build saw, with its layers. */
void incr_free(RMesh *m);

/*  A whole build's lists packed and cut into levels, by chunk, for the
 *  frame's culling. */
int mesh_partition(RMesh *m);
#endif /* ARC_INCR_H */
