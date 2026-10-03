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
 *  sinks and dissolves the piece by it.  Which pieces move is the
 *  script's: the drive hands arc.rules.edit_show the candidates of each
 *  phase, and it marks the ones that move.  How they move is the script's numbers,
 *  arc.numbers("edit_show").
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

/*  THE KEY the device's copy of a vertex carries in pos.w, a word read
 *  as an unsigned integer by the shader that moves pieces.  The low 18
 *  bits are the piece's id, all ones for none.  The next two sevens are
 *  the way from the vertex to its triangle's middle, in sixteenths of a
 *  tile and offset by 64.  The shader rounds the vertex to sixteenths and
 *  adds them, so every vertex of a triangle lands on the same middle.
 *  The middle picks the fragment the triangle goes and comes with. */
#define SHOW_KEY_NONE 0x3FFFFu
/*  The keys of one triangle's three vertices, written into their pos.w. */
void show_key_tri(RMeshVert v[3], uint32_t id);

/*  Whether anything is shown at all: on where a window draws the world. */
void show_enable(int on);

/*  The candidates of one phase, offered to the drive, which hands them
 *  to arc.rules.edit_show (scripts/compose/world.lua and frame.lua).
 *  GOING is asked as an edit's grading pass begins: the pieces drawn on
 *  the edited tiles.  LEAVING and COMING are asked once the passes are
 *  done.  They are the other pieces the build changed, then the pieces it
 *  made with those that went and still stand.  Answers how many
 *  candidates, or -1 where there is no edit being shown.  The new world
 *  waits for the last piece to go.  While show_holds answers yes, the
 *  device keeps the last world, and show_arrive says the new one is up. */
int show_offer(int phase, const RMesh *m);
/*  One chunk's network triangles, the last build's and this one's, as
 *  the meet joins them.  A new triangle with no twin among the old ones
 *  marks its piece as changed.  An old one with no twin among the new
 *  marks its piece as lost.  A twin of a piece that went marks the new
 *  piece as coming back. */
void show_meet(const RMeshVert *old, const uint32_t *old_ids, uint32_t n_old, const RMeshVert *now, const uint32_t *now_ids, uint32_t n_now);
int  show_holds(void);
void show_arrive(void);
/*  A whole build, or an edit's that failed: every piece still.  A whole
 *  build numbers the pieces again from nought. */
void show_reset(void);

/*  The candidates the rule is handed.  `dist` is how far the piece lies
 *  from the edited tiles.  `same` says its triangles are the last
 *  build's.  `back` says some of them belong to a piece that went.
 *  `kept` says it is a piece that went and still stands. */
int  show_phase(void); /* SHOW_GOING, SHOW_LEAVING or SHOW_COMING */
int  show_count(void);
int  show_area(float area[4]);
int  show_candidate(int i, float *dist, int *same, int *back, int *kept);
void show_mark(int i);

/*  Whether pieces are moving or about to: an edit's build is under way,
 *  or its pieces are still coming.  And, once a frame, the end of the
 *  last move, which sets every piece still again. */
int  show_active(void);
void show_tick(void);
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
