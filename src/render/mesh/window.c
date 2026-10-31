/*  mesh/window.c: the window a build lays, as the mesh holds it.  The
 *  build names it once, before anything is laid (mesh_window_set).  Every
 *  question the mesh asks about a tile is answered from this copy, so
 *  the mesh asks the build nothing. */
#include "mesh/mesh.h"

#include <string.h>

static int32_t s_box[MESH_WINDOW_BOXES][4];
static int     s_n;

void mesh_window_set(const int32_t box[][4], int n)
{
    s_n = n < 0 ? 0 : n > MESH_WINDOW_BOXES ? MESH_WINDOW_BOXES
                                            : n;
    if (s_n)
        memcpy(s_box, box, (size_t)s_n * sizeof s_box[0]);
}

int mesh_window(int32_t box[MESH_WINDOW_BOXES][4])
{
    if (s_n)
        memcpy(box, s_box, (size_t)s_n * sizeof s_box[0]);
    return s_n;
}

/*  Whether a tile lies in one of the boxes, grown by `out` tiles on every
 *  side.  Every tile where there are none. */
static int in_boxes(int32_t col, int32_t row, int out)
{
    int k;
    if (!s_n)
        return 1;
    for (k = 0; k < s_n; ++k)
        if (col >= s_box[k][0] - out && row >= s_box[k][1] - out && col < s_box[k][2] + out && row < s_box[k][3] + out)
            return 1;
    return 0;
}

int mesh_window_has(int32_t col, int32_t row)
{
    /*  A tile off the map is filed in the chunk at the map's edge
     *  (mesh_chunk_of), so it is in the window that chunk is in. */
    col = col < 0 ? 0 : col >= r_map ? r_map - 1
                                     : col;
    row = row < 0 ? 0 : row >= r_map ? r_map - 1
                                     : row;
    return in_boxes(col, row, 0);
}

int mesh_wants_tile(int32_t col, int32_t row)
{
    return in_boxes(col, row, 1);
}
