//  tile_marks.glsl -- the tile edges in screen pixels, and the tiles the
//  player's tool marks.  terrain_body.frag and sprite_body.frag splice it
//  in.  The grid, the underground lattice and the marks' rim all measure
//  a tile's edge with grid_px, so a line is the same width in each.

//  How many screen pixels one unit of each grid coordinate spans, from
//  the true gradient of each coordinate.  Call it in uniform control
//  flow, before any branch, because it takes derivatives.
vec2 grid_px(vec2 g)
{
    vec2 gu = vec2(dFdx(g.x), dFdy(g.x));
    vec2 gv = vec2(dFdx(g.y), dFdy(g.y));
    return vec2(1.0) / max(vec2(length(gu), length(gv)), vec2(1e-4));
}

//  The distance in screen pixels from the grid point g to the nearest
//  tile edge.
float grid_edge_px(vec2 g, vec2 ppu)
{
    vec2 f = fract(g);
    vec2 e = min(f, 1.0 - f) * ppu;
    return min(e.x, e.y);
}

#ifdef TILE_MARKS
//  THE MARKS.  t_marks holds one texel a tile: the color of the mark,
//  and in alpha how far a marked tile goes to it, or 0 where the tile is
//  not marked.  The app picks the color from the one palette
//  (ui_mark_color), so a mark means the same thing in every view.  What
//  belongs to a marked tile takes a wash of its color, and what lies flat
//  on it takes a rim along the tile's own edges.  So each marked tile
//  shows as a tile, and an area as its tiles.
const float MARK_RIM    = 0.85; // how far the rim goes to its color
const float MARK_RIM_PX = 1.0;  // the rim's full width in screen pixels
const float MARK_FADE   = 1.5;  // the pixels over which the rim fades out

//  The mark on tile t, or nothing off the map.
vec4 mark_at(ivec2 t)
{
    ivec2 n = textureSize(t_marks, 0);
    if (t.x < 0 || t.y < 0 || t.x >= n.x || t.y >= n.y)
        return vec4(0.0);
    return texelFetch(t_marks, t, 0);
}

//  The distance in screen pixels from the grid point g, on tile t, to the
//  nearest edge of the tile.
float mark_rim_px(ivec2 t, vec2 g, vec2 ppu)
{
    vec2 lo = (g - vec2(t)) * ppu;
    vec2 hi = (vec2(t) + 1.0 - g) * ppu;
    return min(min(lo.x, lo.y), min(hi.x, hi.y));
}

//  The fragment lit by the mark on tile t.  The grid point g gives the
//  rim, and `rim` false gives the wash alone.  The alpha is a palette
//  index and stays as the body wrote it.
void light_marks(ivec2 t, vec2 g, vec2 ppu, bool rim)
{
    vec4 m = mark_at(t);
    if (m.a <= 0.0)
        return;
    float r   = rim ? 1.0 - smoothstep(MARK_RIM_PX, MARK_RIM_PX + MARK_FADE, mark_rim_px(t, g, ppu)) : 0.0;
    o_col.rgb = mix(o_col.rgb, m.rgb, mix(m.a, max(m.a, MARK_RIM), r));
}
#endif
