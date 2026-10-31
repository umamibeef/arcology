/*  sprite.frag -- paint one texel of a sprite.  The atlas is palette
 *  indices, as the game's art is, and the palette is a 256x1 texture the
 *  app rewrites when the animated runs turn.  Alpha carries the index so
 *  the resolve pass can apply $19B76's shadow rule, which reads the
 *  destination index. */
layout(location = 0) in vec2  v_tex;
layout(location = 1) in vec2  v_pos;
layout(location = 2) flat in ivec4 v_under;
layout(location = 3) flat in ivec4 v_flags; /* stencil, road w, road flip, road h */
#ifdef TILE_MARKS
layout(location = 4) in vec4 v_grid; /* the grid point in the sweep's quarter, 1 for art that stands, the footprint's edge (sprite.vert) */
#endif

layout(location = 0) out vec4 o_col;

layout(set = 2, binding = 0) uniform usampler2D t_atlas;
layout(set = 2, binding = 1) uniform sampler2D  t_pal;
#ifdef TILE_MARKS
/*  What the player's tool marks on each tile, one texel a tile, in the
 *  sweep's own quarter: the mark's color, or alpha 0 for no mark
 *  (tile_marks.glsl).  sprite_mark.frag lights the art on a marked tile
 *  with it. */
layout(set = 2, binding = 2) uniform sampler2D t_marks;
#endif
layout(set = 3, binding = 0) uniform Frag
{
    ivec4 p; /* transparent index, 0, 0, 0 */
} fr;

//#include "tile_marks.glsl"

#ifdef TILE_MARKS
/*  The body's painting becomes a function the marked variant calls
 *  before it lights the art. */
#define main paint
#endif
void main()
{
    uint v = texelFetch(t_atlas, ivec2(floor(v_tex)), 0).r;
    if (v == uint(fr.p.x))
        discard;
    if (v_flags.x >= 0)
    {
        /*  $19004: a car is painted only where the destination still holds
         *  the road surface, index 0x91.  The destination is not readable
         *  here, so the road sprite's own texel under this pixel stands in
         *  for it; outside the road's art there is no road to paint on. */
        ivec2 d = ivec2(floor(v_pos)) - v_under.zw;
        int   rx;
        if (d.x < 0 || d.y < 0 || d.x >= v_flags.y || d.y >= v_flags.w)
            discard;
        rx = (v_flags.z != 0) ? (v_flags.y - 1 - d.x) : d.x;
        if (texelFetch(t_atlas, v_under.xy + ivec2(rx, d.y), 0).r !=
            uint(v_flags.x))
            discard;
    }
    /*  Drawn smaller than its own size, a pixel covers more than one
     *  texel.  Its color is then the mean of the texels it covers, each
     *  through the palette, as a mip level of the art would hold it, so
     *  the art does not shimmer as the view moves.  Transparent texels
     *  are left out.  The index in alpha is the pixel's own texel's. */
    vec2 fw = fwidth(v_tex);
    int  n  = int(clamp(ceil(max(fw.x, fw.y) - 0.01), 1.0, 4.0));
    vec3 c  = texelFetch(t_pal, ivec2(int(v), 0), 0).rgb;
    if (n > 1)
    {
        vec3  sum  = vec3(0.0);
        float took = 0.0;
        vec2  o    = v_tex - 0.5 * fw;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                uint t = texelFetch(t_atlas, ivec2(floor(o + fw * (vec2(i, j) + 0.5) / float(n))), 0).r;
                if (t == uint(fr.p.x))
                    continue;
                sum += texelFetch(t_pal, ivec2(int(t), 0), 0).rgb;
                took += 1.0;
            }
        if (took > 0.0)
            c = sum / took;
    }
    o_col = vec4(c, float(v) / 255.0);
}
