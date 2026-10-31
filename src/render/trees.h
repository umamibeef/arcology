/*  trees.h: the forest the simulation grows, drawn as trees of a species
 *  on each forest tile (trees.c). */
#ifndef R_TREES_H
#define R_TREES_H

#include <stdint.h>

#include "city.h"
#include "mesh/mesh.h"

/*  Whether a building byte is a forest stage: the original's 0x06, a
 *  sapling, to 0x0D, old growth. */
static inline int tree_byte(uint8_t xbld)
{
    return xbld >= 0x06 && xbld <= 0x0D;
}

/*  The trees of every forest tile the build wants.  Each tree is laid
 *  whole in its chunk's range of trees (MESH_RANGE_TREES).  Each tile's
 *  trees are laid again as simple crowns (MESH_RANGE_CROWNS).  Answers
 *  0, or -1 on a mesh error. */
int trees_draw(RMesh *m, const RCity *c, uint8_t mask_bit);

/*  The canopy the last build laid, two bytes a tile.  The first is the
 *  share of the tile the crowns cover, 0 to 255.  The second is the
 *  species of its largest tree.  The ground takes it where the trees are too small to draw. */
const uint8_t *trees_canopy(void);

/*  The species the leaf material can tell apart: its number is MAT_LEAF
 *  plus a sixteenth for each species. */
#define TREES_SPECIES_MAX 8 /* as GPU_LEAF_SPECIES */

/*  THE BIOMES (trees.c): how many, each one's name, the biome of a city
 *  by the path of its file, and the biome the build grows. */
int         trees_biomes(void);
const char *trees_biome_name(int i);
int         trees_biome_of_city(const char *path);
void        trees_set_biome(int i);

/*  Each species' two leaf colors and how ragged its edge is, for the
 *  leaf material (terrain_body.frag). */
void trees_leaf_colors(float out[TREES_SPECIES_MAX][2][4], float rag[TREES_SPECIES_MAX]);

#endif
