/*  terrain.vert -- the terrain as geometry.  A vertex is a point on the
 *  grid: column, row and altitude in levels.  The projection is the
 *  original's, stated as numbers rather than derived: canvas x = ox + (row
 *  - col) * tw/2, canvas y = oy + (col + row) * th/2 minus alt_step per
 *  level, where alt_step is 0.75 of the tile height, the art's own choice.
 *  A tile corner therefore lands exactly where the sprite renderer put it,
 *  and the sprites can be superimposed as they are.  The depth is the
 *  camera's own: how far the point lies along the way it looks, the same
 *  measure sprite.vert gives the art, so the frame composes by where things
 *  are and not by which tile they were drawn from. */
#ifdef EDIT_SHOW
layout(location = 0) in vec3 a_xyz; /* col, row, alt */
layout(location = 3) in uint a_key; /* the piece and the way to its triangle's middle (show.h) */
#define a_pos vec4(a_xyz, 0.0)
#else
layout(location = 0) in vec4 a_pos; /* col, row, alt, and a word for the shader that moves pieces (show.h) */
#endif
layout(location = 1) in vec4 a_nrm; /* normal in world units, 0           */
layout(location = 2) in vec4 a_col; /* base colour, alpha = index / 255   */

layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec3 v_wpos;
layout(location = 3) out float v_curv;
#ifdef EDIT_SHOW
layout(location = 4) out float v_gone; /* how far an edit's piece has dissolved: 0 whole, 1 gone */

/*  Every piece's state as an edit shows it, by the piece's id: 0 still,
 *  1 going, 2 coming, 3 leaving, over 255.  1024 to a row. */
layout(set = 0, binding = 0) uniform sampler2D t_show;
#endif

layout(set = 1, binding = 0) uniform Cam
{
    vec4 view; /* scroll x, scroll y, 2*scale/target_w, 2*scale/target_h */
    vec4 proj; /* ox + tw/2, oy, tw/2, th/2                             */
    vec4 alt;  /* alt_step, depth range, cos, sin of the rotation        */
    vec4 rot;  /* free camera (1 or 0), pivot column, pivot row, pitch  */
    vec4 mode; /* the sweep's painter (1) or the camera's depth (0): the mesh is never drawn under the painter */
} cam;

#ifdef EDIT_SHOW
/*  How the pieces an edit reached move.  A piece breaks into fragments,
 *  and they go and come as a wave from the edited tiles: a fragment a
 *  tile further out moves a tile's time later. */
layout(set = 1, binding = 1) uniform Show
{
    vec4 clock; /* seconds since the edit, when the going began, when the coming began, when the leaving began */
    vec4 area;  /* the edited tiles: first column, first row, last column + 1, last row + 1 */
    vec4 pace;  /* tiles a second the going and the coming waves run, seconds a fragment takes to go and to come */
    vec4 wave;  /* how far out the coming wave starts, 1 where it runs in from there, 0, 1 while any moves */
    vec4 piece; /* how wide a fragment is, the most it waits past the wave, how far it drifts, how far it dissolves */
    vec4 lift;  /* levels a going fragment is thrown up and falls below its place, levels a coming one drops from */
} show;

/*  A number from 0 to 1 for each cell of the fragment grid, and another
 *  for each `salt`. */
float cell_rand(ivec2 c, uint salt)
{
    uint x = uint(c.x) * 747796405u + uint(c.y) * 2654435761u + salt * 2891336453u + 1u;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x & 0xffffffu) / 16777216.0;
}
#endif

void main()
{
#ifdef EDIT_SHOW
    v_gone = 0.0;
#endif
    /*  The free rotation, for inspection: the grid turns about the pivot,
     *  the point under the view's centre, before the projection, so the
     *  same oblique camera sees the terrain from any side and what the
     *  view looks at stays where it is. */
    vec2 cr = a_pos.xy - cam.rot.yz;
    vec2 rr = vec2(cr.x * cam.alt.z - cr.y * cam.alt.w,
                   cr.x * cam.alt.w + cr.y * cam.alt.z) + cam.rot.yz;
    /*  The camera's pitch, in radians: how far it has been raised from the
     *  original's own view towards the zenith.  The original is 30 degrees
     *  -- a tile's diamond is twice as wide as it is tall, so the ground's
     *  foreshortening is sin 30 -- and the map view is the same camera
     *  raised to 90: the ground stops being foreshortened and height stops
     *  shifting a point up the screen.  One camera, one projection: at 30
     *  every number below is the original's. */
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
    /*  The depth is the camera's: how far the point lies along the way the
     *  camera looks, the turned column plus row into the screen at cos
     *  pitch and the height toward it at sin pitch, a level being 0.866 of
     *  a tile's diagonal step.  Larger is nearer.  At 30 degrees it is the
     *  isometric view's own distance; straight down it is the height alone.
     *  The sprites carry the same measure and write it too, so everything
     *  composes by where it stands and not by the tile it was drawn from --
     *  a deck over a road, a lane descending beside a deck, a column, an
     *  arc cutting a corner.  The last term breaks the ties: everything on
     *  one diagonal is the same distance from an oblique camera and the
     *  sweep orders those by row; a four hundredth of a tile per step
     *  across the diagonal is the sweep's own order and nothing more. */
    float near  = (rr.x + rr.y) * cp + a_pos.z * 0.8660254 * sp + (rr.y - rr.x) * 0.0015;
    float depth = 1.0 - (near + 300.0) / cam.alt.y;
    /*  A road strip lies on its tile's ground, a lane on a deck, a marking
     *  on a lane: nearly coplanar, and a strip can lie a hair under the
     *  grass between the tile's corners, so they take a bias by material
     *  to win over what they lie on, a third of a tile at most, cars over
     *  roads over ground; the free camera's own numbers.  Less than a
     *  fiftieth of a tile and the grass takes a stretch of asphalt.  The
     *  seabed under a water body is meant to be seen through the surface:
     *  a hair further, since a canal's bed can lie at its table (mesh.c,
     *  rule 4: "a quarter step behind the tile"). */
    if (a_col.z > 3.5 && a_col.z < 4.5)
        depth += 0.05 / cam.alt.y;
    if (a_col.z > 6.5)
        depth -= (0.3 / cam.alt.y) * (a_col.z > 20.25 ? 0.0 : a_col.z > 19.15 ? 1.1 : a_col.z > 18.5 ? 1.0 : a_col.z > 17.5 ? 0.3 : a_col.z > 16.5 ? 1.0 : a_col.z > 15.5 ? 1.3 : a_col.z > 14.5 ? 2.0 : a_col.z > 13.5 ? 1.5 : a_col.z > 12.5 ? 0.5 : a_col.z > 10.5 && a_col.z < 11.5 ? 1.2 : 1.0); /* a railway a shade nearer than the map's zone tint (1.1): a turnout's curve over a zoned corner shows */
    /*  A zone tint belongs to the map view: looking obliquely it would
     *  lie over the world like a stain, so below 45 degrees it is thrown
     *  outside the frustum instead of drawn, and from there it fades in
     *  as the camera rises, so the map's colours arrive with the map
     *  rather than snapping on at the end of the move.  The fade rides in
     *  col.g, which a tint quad does not otherwise use. */
    if (a_col.z > 19.5 && a_col.z < 20.25)
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
#ifdef EDIT_SHOW
    /*  An edit's piece breaks into FRAGMENTS.  Each triangle belongs to
     *  the cell of an uneven grid on the ground its middle lies in, and
     *  the triangles of one cell are a fragment.  Going, each is thrown up
     *  and falls away below its place.  Coming, each drops in from above
     *  and lands in it.  The cell's own seed says when and which way, so
     *  every vertex of a fragment moves together and the fragment keeps
     *  its shape.  The move is added to the finished position.  So a
     *  still piece is placed by exactly the arithmetic above. */
    uint id = a_key & 0x3FFFFu;
    if (show.wave.w > 0.5 && id != 0x3FFFFu)
    {
        float st = texelFetch(t_show, ivec2(int(id & 1023u), int(id >> 10)), 0).r * 255.0;
        if (st > 0.5)
        {
            bool  going = st < 1.5 || st > 2.5;
            vec2  mid   = (floor(a_xyz.xy * 16.0 + 0.5) + vec2(int((a_key >> 18) & 0x7Fu) - 64, int((a_key >> 25) & 0x7Fu) - 64)) / 16.0;
            /*  The nearest seed: one a cell, set off within it. */
            vec2  g     = mid / show.piece.x;
            ivec2 base  = ivec2(floor(g));
            ivec2 cell  = base;
            vec2  seed  = vec2(0.0);
            float best  = 1e9;
            for (int cy = -1; cy <= 1; ++cy)
                for (int cx = -1; cx <= 1; ++cx)
                {
                    ivec2 c  = base + ivec2(cx, cy);
                    vec2  s2 = vec2(c) + vec2(cell_rand(c, 1u), cell_rand(c, 2u));
                    float dd = dot(g - s2, g - s2);
                    if (dd < best)
                    {
                        best = dd;
                        cell = c;
                        seed = s2;
                    }
                }
            seed *= show.piece.x;
            /*  What goes, goes from the edit outward.  What comes may come
             *  in from the far edge, so the edit is the last thing built. */
            vec2  q     = clamp(seed, show.area.xy, show.area.zw);
            float d     = length(seed - q);
            float start = st < 1.5 ? show.clock.y : st > 2.5 ? show.clock.w : show.clock.z;
            float delay = !going && show.wave.y > 0.5 ? max(show.wave.x - d, 0.0) : d;
            float from  = start + delay / (going ? show.pace.x : show.pace.y) + cell_rand(cell, 3u) * show.piece.y;
            float k     = clamp((show.clock.x - from) / (going ? show.pace.z : show.pace.w), 0.0, 1.0);
            float turn  = 6.2831853 * cell_rand(cell, 4u);
            float u     = going ? k : 1.0 - k;
            vec2  dxy   = vec2(cos(turn), sin(turn)) * show.piece.z * u;
            float dz    = going ? show.lift.x * 4.0 * k * (1.0 - k) - show.lift.y * k * k * k : show.lift.z * u * u;
            /*  The move through the projection above: turned with the
             *  grid, then onto the canvas, the screen and the depth. */
            vec2  dr2   = vec2(dxy.x * cam.alt.z - dxy.y * cam.alt.w, dxy.x * cam.alt.w + dxy.y * cam.alt.z);
            vec2  dcan  = vec2((dr2.y - dr2.x) * cam.proj.z,
                               (dr2.x + dr2.y) * cam.proj.w * (sp / 0.5) - dz * cam.alt.x * (cp / 0.8660254));
            float dnear = (dr2.x + dr2.y) * cp + dz * 0.8660254 * sp + (dr2.y - dr2.x) * 0.0015;
            gl_Position.x += dcan.x * cam.view.z;
            gl_Position.y -= dcan.y * cam.view.w;
            gl_Position.z -= dnear / cam.alt.y;
            /*  A fragment gone is not drawn at all, nor one still to come.
             *  On the way it may dissolve as well. */
            v_gone = (going ? k >= 1.0 : k <= 0.0) ? 1.0 : u * show.piece.w;
        }
    }
#endif
}
