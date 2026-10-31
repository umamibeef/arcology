#version 450
/*  sprite_mark.frag -- sprite.frag, with the art on the tiles the
 *  player's tool marks lit (tile_marks.glsl).  Art that lies flat, the
 *  ground among it, takes the wash and the rim the mesh takes.  Art that
 *  stands takes the wash alone, and a car painted onto a road (a stencil)
 *  is not lit.  Drawn only while a tool marks a tile, so every other
 *  frame is the plain shader's to the pixel. */
#define TILE_MARKS 1
//#include "sprite_body.frag"

#undef main
void main()
{
    vec2 ppu = grid_px(v_grid.xy);
    paint();
    if (v_flags.x >= 0)
        return;
    /*  A flat sprite's grid point lies on the plane at its tile's level,
     *  and names the tile of its footprint under the pixel.  The footprint
     *  runs n tiles down the columns and up the rows from the tile the art
     *  is placed at.  Where a slope lifts the art off that plane, the
     *  point falls off the footprint.  There the art takes the mark of its
     *  own tile, and no rim. */
    ivec2 at = v_under.zw;
    ivec2 g  = ivec2(floor(v_grid.xy));
    int   n  = int(v_grid.w + 0.5);
    bool  on = v_grid.z < 0.5 && g.x <= at.x && g.x > at.x - n && g.y >= at.y && g.y < at.y + n;
    light_marks(on ? g : at, v_grid.xy, ppu, on);
}
