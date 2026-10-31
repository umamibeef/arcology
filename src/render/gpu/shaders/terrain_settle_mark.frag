#version 450
/*  terrain_settle_mark.frag -- terrain_settle.frag, with the marked tiles
 *  lit (light_own_tile). */
#define LAND_SETTLE 1
#define TILE_MARKS 1
//#include "terrain_body.frag"

#undef main
void main()
{
    vec2 ppu = grid_px(v_wpos.xy);
    shade();
    o_col.rgb *= 1.0 - v_dim;
    light_own_tile(ppu);
}
