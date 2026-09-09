#version 450
/*  shadow.frag -- the silhouette of a flying thing, into the shadow mask.
 *
 *  $19B76 walks the sprite's silhouette and darkens what is already on the
 *  screen under it.  The mask records where the silhouette fell, depth
 *  tested against the terrain like any sprite; the resolve pass does the
 *  darkening where the destination index allows it.
 */
layout(location = 0) in vec2  v_tex;
layout(location = 1) in vec2  v_pos;
layout(location = 2) flat in ivec4 v_under;
layout(location = 3) flat in ivec4 v_flags;

layout(location = 0) out vec4 o_mask;

layout(set = 2, binding = 0) uniform usampler2D t_atlas;
layout(set = 3, binding = 0) uniform Frag
{
    ivec4 p; /* transparent index, 0, 0, 0 */
} fr;

void main()
{
    uint v = texelFetch(t_atlas, ivec2(floor(v_tex)), 0).r;
    if (v == uint(fr.p.x))
        discard;
    o_mask = vec4(1.0);
}
