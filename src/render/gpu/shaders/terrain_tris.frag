#version 450
/*  terrain_tris.frag -- terrain.frag, with each triangle of the mesh in a
 *  color of its own, so the way the mesh is cut can be seen.  A debug
 *  view: drawn only while it is on, so every other frame is the plain
 *  shader's to the pixel. */
#define TRI_COLOURS 1
//#include "terrain_body.frag"

layout(location = 6) flat in uint v_tri; /* the triangle's place in the mesh (terrain_tris.vert) */

/*  A color from a triangle's place in its draw, lit by the sun as the
 *  ground is, so the slopes still read. */
vec3 tri_color(uint id)
{
    uint x = id * 747796405u + 2891336453u;
    x      = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x      = (x >> 22u) ^ x;
    return vec3(float(x & 255u), float((x >> 8u) & 255u), float((x >> 16u) & 255u)) / 255.0 * 0.75 + 0.25;
}

#undef main
void main()
{
    shade();
    float d   = max(dot(normalize(v_nrm), normalize(fr.sun.xyz)), 0.0);
    o_col.rgb = tri_color(v_tri) * (0.55 + 0.45 * d);
}
