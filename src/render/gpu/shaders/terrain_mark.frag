#version 450
/*  terrain_mark.frag -- terrain_show.frag, with the marked tiles lit
 *  (light_own_tile).  Drawn only while a tool marks a tile, so every
 *  other frame is the plain shader's to the pixel. */
#define EDIT_SHOW 1
#define TILE_MARKS 1
//#include "terrain_body.frag"

#undef main
void main()
{
    vec2 ppu = grid_px(v_wpos.xy);
    shade();
    light_own_tile(ppu);
}
