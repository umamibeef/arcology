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
    vec4 rot;  /* free camera (1 or 0), pivot column, pivot row, pitch  */
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
    /*  The camera's pitch, in radians: how far it has been raised from
     *  the original's own view towards the zenith.  The original is 30
     *  degrees -- a tile's diamond is twice as wide as it is tall, so the
     *  ground's foreshortening is sin 30 -- and the map view (the user:
     *  "I think it's best that the inspect be baked into the main
     *  renderer, as a top down view -- think of OG sim city", then "for
     *  the map view, I'd prefer you simply changed the camera") is the
     *  same camera raised to 90: the ground stops being foreshortened and
     *  height stops shifting a point up the screen.  One camera, one
     *  projection: at 30 every number below is the original's. */
    float sp     = sin(cam.rot.w);
    float cp     = cos(cam.rot.w);
    vec2  canvas = vec2(cam.proj.x + (rr.y - rr.x) * cam.proj.z,
                        cam.proj.y + (rr.x + rr.y) * cam.proj.w * (sp / 0.5) -
                            a_pos.z * cam.alt.x * (cp / 0.8660254));
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
    /*  Turned or raised, the painter's index no longer orders the tiles
     *  and the camera's own depth takes over: how far a point lies along
     *  the direction the camera looks, the rotated row plus column into
     *  the screen and the height towards it.  Both terms are measured
     *  against the original's own pitch, so at 30 degrees the first is
     *  the rotated view's depth unchanged and the second is nothing: the
     *  sprites drawn beside the mesh carry no height of their own, and
     *  they would sink into their own hillside if the mesh counted a
     *  height they cannot.  The height enters as the camera rises, which
     *  is exactly where the sprites stop being drawn, and straight down
     *  it is the whole of the depth -- the one view where the painter's
     *  slot cannot help and does not have to.
     *
     *  The last term breaks the ties.  Everything on one diagonal is the
     *  same distance from an oblique camera, and the sweep orders those
     *  by row -- a tie left to the depth test resolves at random, which
     *  is what made a turned forest, and a turned row of buildings,
     *  overlap differently every time.  A four hundredth of a tile per
     *  step across the diagonal is far too small to disturb the real
     *  ordering and enough to be the sweep's own. */
    if (cam.rot.x > 0.5)
        depth = 1.0 - ((rr.x + rr.y) * (cp / 0.8660254) +
                       a_pos.z * (sp - 0.5) + (rr.y - rr.x) * 0.0015 +
                       300.0) / 800.0;
    /*  A road strip lies on its tile's ground: a fifth of a depth slot
     *  nearer, so it wins over the ground it lies on and stays under the
     *  sprites of its tile.  A viaduct's bent takes less than the road
     *  it carries, so the deck draws over the columns beneath it while
     *  they still stand in front of the ground (the user: "the support
     *  columns are in front of the road"). */
    if (a_col.z > 6.5)
        depth -= ((cam.rot.x > 0.5) ? 0.3 / 800.0 : 0.2 / cam.alt.y) * (a_col.z > 18.5 ? 1.0 : a_col.z > 17.5 ? 0.3 : a_col.z > 16.5 ? 1.0 : a_col.z > 15.5 ? 1.3 : a_col.z > 14.5 ? 2.0 : a_col.z > 13.5 ? 1.5 : a_col.z > 12.5 ? 0.5 : 1.0);
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
    /*  A zone tint belongs to the map view: looking obliquely it would
     *  lie over the world like a stain, so below 45 degrees it is thrown
     *  outside the frustum instead of drawn, and from there it fades in
     *  as the camera rises, so the map's colours arrive with the map
     *  rather than snapping on at the end of the move.  The fade rides in
     *  col.g, which a tint quad does not otherwise use. */
    if (a_col.z > 19.5)
    {
        float fade = smoothstep(0.7071, 0.9700, sp);
        if (fade <= 0.0)
        {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        v_col.g = fade;
    }
    gl_Position = vec4(ndc.x, -ndc.y, depth, 1.0);
}
