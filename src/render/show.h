/*  show.h: what an edit's build shows going and coming.
 *
 *  An edit rebuilds the pieces it reaches, and that takes a moment.  The
 *  pieces drawn on the edited tiles are shown GOING at once, while the
 *  build runs.  Once it is done, the other pieces it changed are shown
 *  LEAVING the same way.  When they have gone, the pieces it made are
 *  shown COMING in their place.  Every wave runs out from the edited
 *  tiles.  The GPU holds the last
 *  build's world until the new one is uploaded.  So the pieces that go
 *  are the very triangles on the screen, and only the ones that change.
 *
 *  A piece is a shape (mesh/shape.h).  Each has a state: still, going,
 *  leaving or coming.  The vertex shader reads the state by the piece's id and
 *  sinks and dissolves the piece by it.  The composition (compose.c)
 *  reads the candidates of each phase and marks the ones that move.  How
 *  they move is the geo store's edit_show numbers.
 *
 *  Only a window shows it.  A headless run and every check build the
 *  same mesh with nothing marked, and the frames they read are still. */
#ifndef ARC_SHOW_H
#define ARC_SHOW_H
#include <stdint.h>

#include "mesh/mesh.h"
#include "mesh/vert.h"

/*  The state table's size: one entry a piece id, laid out as a texture
 *  SHOW_W wide.  An id past the table never moves. */
#define SHOW_W      1024
#define SHOW_H      256
#define SHOW_PIECES (SHOW_W * SHOW_H)

enum
{
    SHOW_STILL   = 0,
    SHOW_GOING   = 1,
    SHOW_COMING  = 2,
    SHOW_LEAVING = 3
};

/*  THE KEY of a triangle in the device's key buffer, read by the shader
 *  that moves pieces.  The low 18 bits are the piece's id, all ones for
 *  none.  The shader takes the triangle's middle from its corners, and
 *  the middle picks the fragment the triangle goes and comes with. */
#define SHOW_KEY_NONE 0x3FFFFu
/*  Two ids no piece takes.  GONE is an old triangle the build did not
 *  make again, and it goes while the build's new world waits.  COME is a new triangle with no twin in the last world and no piece of its own.  It comes with the new world. */
#define SHOW_KEY_GONE 0x3FFFEu
#define SHOW_KEY_COME 0x3FFFDu
/*  The most land ranges an edit's build can draw again, MESH_RANGES a
 *  chunk. */
#define SHOW_SLOTS (MESH_RANGES * MESH_CHUNKS_CAP)

/*  Whether anything is shown at all: on where a window draws the world. */
void show_enable(int on);

/*  The candidates of one phase, offered to compose.c, which marks the
 *  ones that move.
 *  GOING is asked as an edit's build begins, and offers nothing.  LEAVING
 *  and COMING are asked once the world is composed.  They are the other pieces the build changed, then the pieces it
 *  made with those that went and still stand.  The triangles of the last
 *  world with no twin in the new one go once the build is done, whatever
 *  piece they belong to.  Answers how many
 *  candidates, or -1 where there is no edit being shown.  `edit` is the
 *  box round the tiles the edit changed, first column and row, then last,
 *  or NULL where the build is no edit's.  The new world
 *  waits for the last piece to go.  While show_holds answers yes, the
 *  device keeps the last world, and show_arrive says the new one is up. */
int show_offer(int phase, const RMesh *m, const int32_t edit[4]);
/*  One network slot an edit's window replaced: the last build's list and
 *  this one's, both kept until the settle.  `base` is the number of the
 *  new list's first triangle.  Then THE SETTLE, once every slot is in.
 *  A piece stands where both worlds hold a piece of its name with
 *  exactly its triangles there.  Every other piece of the last world
 *  goes and every other piece of the new one comes. */
void show_chunk(int slot, const RMeshList *old, const RMeshList *now, uint32_t base);
void show_settle(void);
/*  The key the triangle at place `t` of the land list goes up with, its
 *  piece being `id`.  A triangle with no twin in the last world names its
 *  piece, or SHOW_KEY_COME where it has none.  Every other triangle names
 *  no piece and stands, and so does all of a world no edit is shown in. */
uint32_t show_tri_key(uint32_t t, uint32_t id);
/*  The key each of the last world's triangles in the slots the build
 *  drew again is to carry, once the build is done.  The device's copy of
 *  those slots is keyed again with them, so the triangles that go are the
 *  ones the build did not make again.  Answers whether there are any. */
int  show_rekey(const uint32_t **keys, uint32_t first[SHOW_SLOTS], uint32_t n[SHOW_SLOTS]);
int  show_holds(void);
void show_arrive(void);
/*  A whole build, or an edit's that failed: every piece still.  A whole
 *  build numbers the pieces again from nought. */
void show_reset(void);

int  show_count(void);
int  show_candidate(int i, float *dist, int *same, int *back, int *kept);
void show_mark(int i);

/*  Whether pieces are moving or about to: an edit's build is under way,
 *  or its pieces are still coming.  And, once a frame, the end of the
 *  last move, which sets every piece still again: 1 on the frame it
 *  does. */
int show_active(void);
int show_tick(void);
/*  The state of every piece, copied into `out` (SHOW_PIECES of them)
 *  when it changed since this was last asked.  Answers 0 when it did not,
 *  or when a build's phase holds it this frame. */
int show_states(uint8_t *out);
/*  The shader's twenty-four numbers now.  They give the clocks, the
 *  edited tiles, and how fast and how long each wave runs.  They also
 *  give where the coming wave starts, and how a piece breaks up and its
 *  fragments move.  All nought when nothing moves. */
void show_uniform(float u[24]);

#endif /* ARC_SHOW_H */
