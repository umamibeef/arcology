#version 450
/*  sprite.frag -- paint one texel of a sprite.
 *
 *  The atlas is palette indices, as the game's art is, and the palette is
 *  a 256x1 texture the app rewrites when the animated runs turn.  Alpha
 *  carries the index so the resolve pass can apply $19B76's shadow rule,
 *  which reads the destination index.
 */
layout(location = 0) in vec2  v_tex;
layout(location = 1) in vec2  v_pos;
layout(location = 2) flat in ivec4 v_under;
layout(location = 3) flat in ivec4 v_flags; /* stencil, road w, road flip, road h */

layout(location = 0) out vec4 o_col;

layout(set = 2, binding = 0) uniform usampler2D t_atlas;
layout(set = 2, binding = 1) uniform sampler2D  t_pal;
layout(set = 3, binding = 0) uniform Frag
{
    ivec4 p; /* transparent index, 0, 0, 0 */
} fr;

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
    o_col = vec4(texelFetch(t_pal, ivec2(int(v), 0), 0).rgb,
                 float(v) / 255.0);
}
