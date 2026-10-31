#version 450
/*  terrain_settle.frag -- terrain.frag while the land settles, for the
 *  sheet over the chunks not yet built and for the ground settling onto
 *  its heights (terrain_settle.vert, terrain_sheet.vert).  The land still
 *  being built is drawn darker, by how much the vertex says. */
#define LAND_SETTLE 1
//#include "terrain_body.frag"

#undef main
void main()
{
    shade();
    o_col.rgb *= 1.0 - v_dim;
}
