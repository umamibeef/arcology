#version 450
/*  terrain.vert -- the terrain as geometry.
 *
 *  A vertex is a point on the grid: column, row and altitude in levels.
 *  The projection is the original's, stated as numbers rather than derived:
 *  canvas x = ox + (row - col) * tw/2, canvas y = oy + (col + row) * th/2
 *  minus alt_step per level, where alt_step is 0.75 of the tile height, the
 *  art's own choice.  A tile corner therefore lands exactly where the
 *  sprite renderer put it, and the sprites can be superimposed as they are.
 *  The depth is the sweep's painter's index, per tile, so the mesh composes
 *  with the sprites exactly as the software's terrain pass does.
 */
layout(location = 0) in vec4 a_pos; /* col, row, alt, depth               */
layout(location = 1) in vec4 a_nrm; /* normal in world units, 0           */
layout(location = 2) in vec4 a_col; /* base colour, alpha = index / 255   */

layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec3 v_wpos;
layout(location = 3) out float v_curv;

layout(set = 1, binding = 0) uniform Cam
{
    vec4 view; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    vec4 proj; /* ox + tw/2, oy, tw/2, th/2                             */
    vec4 alt;  /* alt_step, depth divisor, cos, sin of the rotation     */
    vec4 rot;  /* rotating (1 or 0), pivot column, pivot row, 0         */
} cam;

void main()
{
    /*  The free rotation, for inspection: the grid turns about the pivot,
     *  the point under the view's centre, before the projection, so the
     *  same oblique camera sees the terrain from any side and what the
     *  view looks at stays where it is. */
    vec2 cr = a_pos.xy - cam.rot.yz;
    vec2 rr = vec2(cr.x * cam.alt.z - cr.y * cam.alt.w,
                   cr.x * cam.alt.w + cr.y * cam.alt.z) + cam.rot.yz;
    vec2 canvas = vec2(cam.proj.x + (rr.y - rr.x) * cam.proj.z,
                       cam.proj.y + (rr.x + rr.y) * cam.proj.w -
                           a_pos.z * cam.alt.x);
    v_nrm  = a_nrm.xyz;
    v_col  = a_col;
    v_wpos = a_pos.xyz;
    v_curv = a_nrm.w;
    vec2 ndc    = (canvas - cam.view.xy) * cam.view.zw - 1.0;
    /*  a_pos.w is the tile's painter's index.  The mesh sits in the middle
     *  of its tile's depth slot, below the sprites of the tile at 0.999;
     *  tiles order each other by the slot, which is the painter's order
     *  the sweep uses, and within a tile the water resolves against the
     *  ground in the fragment shader, not in the depth buffer. */
    float depth = 1.0 - (a_pos.w + 0.5) / cam.alt.y;
    /*  Rotated, the painter's index no longer orders the tiles; the
     *  oblique camera's depth is the rotated row plus column. */
    if (cam.rot.x > 0.5)
        depth = 1.0 - (rr.x + rr.y + 300.0) / 800.0;
    /*  A road strip lies on its tile's ground: a fifth of a depth slot
     *  nearer, so it wins over the ground it lies on and stays under the
     *  sprites of its tile.  A viaduct's bent takes less than the road
     *  it carries, so the deck draws over the columns beneath it while
     *  they still stand in front of the ground (the user: "the support
     *  columns are in front of the road"). */
    if (a_col.z > 6.5)
        depth -= ((cam.rot.x > 0.5) ? 0.3 / 800.0 : 0.2 / cam.alt.y) * (a_col.z > 17.5 ? 0.3 : a_col.z > 16.5 ? 1.0 : a_col.z > 15.5 ? 1.3 : a_col.z > 14.5 ? 2.0 : a_col.z > 13.5 ? 1.5 : a_col.z > 12.5 ? 0.5 : 1.0);
    /*  A vehicle's depth within its tile's slot follows its place along
     *  the tile's diagonal toward the viewer, a tenth of a slot across
     *  the tile, so two cars overlapping in one tile, on the inside of a
     *  tight bend, resolve by distance and not by draw order (the user:
     *  "z fighting between the train cars"); the tile's column plus row
     *  comes back out of the painter's index. */
    if (cam.rot.x <= 0.5 && a_col.z > 14.5 && a_col.z < 15.5)
    {
        float cr = floor((floor(a_pos.w) - 1.0) / 128.0);
        depth -= clamp(a_pos.x + a_pos.y - cr, 0.0, 2.0) * 0.05 / cam.alt.y;
    }
    gl_Position = vec4(ndc.x, -ndc.y, depth, 1.0);
}
