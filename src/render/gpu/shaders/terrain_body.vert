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
#if defined(EDIT_SHOW) || defined(MESH_PULLED)
/*  The show reads the mesh itself, not through attributes: a vertex is
 *  shared by the triangles that meet at it, and a piece moves by its
 *  triangle.  The index buffer gives a draw's vertices in triangles, and
 *  each triangle has its piece in the keys.  A vertex is eight words
 *  (mesh/vert.h).  The marks read the mesh so too, for each triangle's
 *  tile.  Metal numbers these after the uniform buffers, and Vulkan after
 *  the samplers: two of each while the land settles. */
#if defined(LAND_SETTLE) || defined(ARC_MSL)
#define MESH_SB 2
#else
#define MESH_SB 1
#endif
layout(std430, set = 0, binding = MESH_SB) readonly buffer MeshVerts { uint mv[]; };
layout(std430, set = 0, binding = MESH_SB + 1) readonly buffer MeshIndex { uint mi[]; };
layout(std430, set = 0, binding = MESH_SB + 2) readonly buffer MeshKeys { uint mk[]; };
#ifdef TILE_MARKS
/*  1 + the tile each triangle belongs to, or 0 for none (gpu.c). */
layout(std430, set = 0, binding = MESH_SB + 3) readonly buffer MeshTiles { uint mt[]; };
#endif

vec3 mesh_xyz(uint i)
{
    return uintBitsToFloat(uvec3(mv[8u * i], mv[8u * i + 1u], mv[8u * i + 2u]));
}
#ifdef LAND_SETTLE
vec4 a_pos; /* the settle moves it */
#else
#define a_pos vec4(a_xyz, uintBitsToFloat(mv[8u * at + 3u]))
#endif
#elif defined(LAND_SHEET)
/*  The sheet has no vertex buffer.  Each instance is one chunk, and each
 *  six vertices are one tile of it, so main makes every attribute. */
vec4 a_pos;
vec2 a_oct;
vec2 a_paint;
vec2 a_aux;
#elif defined(LAND_SETTLE)
layout(location = 0) in vec4 a_pos_in; /* as a_pos below.  The settle moves a copy of it */
layout(location = 1) in vec2 a_oct;
layout(location = 2) in vec2 a_paint;
layout(location = 4) in vec2 a_aux;
vec4 a_pos;
#else
layout(location = 0) in vec4 a_pos;   /* col, row, alt, and how far a cliff's lean moved it (mesh/vert.h) */
layout(location = 1) in vec2 a_oct;   /* the normal in world units, octahedral (mesh/vert.h) */
layout(location = 2) in vec2 a_paint; /* across and along the paint, a wall's reference heights, or the gradient */
layout(location = 4) in vec2 a_aux;   /* the ground's curvature, and the material */
#endif

/*  The alpha channel carries a palette index for the resolve pass's
 *  shadow rule.  It is a dirt-spur index, so a flying thing darkens the
 *  ground as $19B76 does.  Every vertex of the mesh has it. */
#define LAND_INDEX (105.0 / 255.0)

vec3 oct_normal(vec2 e)
{
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

layout(location = 0) out vec3 v_nrm;
layout(location = 1) out vec4 v_col;
layout(location = 2) out vec3 v_wpos;
layout(location = 3) out float v_curv;
layout(location = 5) out float v_lean; /* how far a cliff's lean moved the ground, in levels */
#ifdef LAND_SETTLE
layout(location = 7) out float v_dim; /* how much darker the land is drawn while it is still being built */
layout(location = 8) out float v_settled; /* how far the point has settled onto its own height, 0 to 1; the sheet 0 */
#endif
#ifdef TRI_COLOURS
layout(location = 6) flat out uint v_tri; /* the triangle's place in the mesh, for the triangle view */
#endif
#ifdef TILE_MARKS
layout(location = 9) flat out uint v_tile; /* 1 + the tile the triangle belongs to, or 0 */
#endif
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
    vec4 wind; /* the way the wind blows, the seconds, and how far a crown sways in tiles (0 still) */
} cam;

//#include "materials.glsl"

/*  How far the wind moves a point of a tree's leaves (trees.c lays
 *  them).  The crown leans downwind in gusts that run across the map
 *  along the wind, each tree a little out of step by its shade.  A tip
 *  moves more than the middle, and a palm's frond more than a crown.
 *  The trunk does not move. */
vec2 tree_sway(vec4 col, vec2 p)
{
    if (cam.wind.w <= 0.0 || !MAT_IS(col.z, MAT_LEAF))
        return vec2(0.0);
    float g     = col.y;
    float reach = g < 2.5 ? 0.6 + 0.4 * fract(col.x) : g < 4.5 ? 0.6 + 0.4 * fract(g) : g < 6.5 ? 1.0 : 2.5 * fract(g);
    float phase = dot(p, cam.wind.xy) * 0.8 + floor(col.x * 0.5) * 0.7;
    float t     = cam.wind.z;
    float gust  = 0.6 * sin(t * 1.7 - phase) + 0.4 * sin(t * 2.9 - phase * 1.3);
    return cam.wind.xy * cam.wind.w * reach * (0.5 + 0.5 * gust);
}

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

#ifdef LAND_SETTLE
/*  THE LAND SETTLING.  Until a chunk is built, a sheet with waves running
 *  over it stands where its land will be (terrain_sheet.vert).  A sweep
 *  runs out from the point under the view's center.  Each point of a
 *  built chunk eases from the waves to its own height once the sweep
 *  reaches it.  So the land settles point by point, never a chunk at a
 *  time.  Whether a chunk is built is read from the chunks round the
 *  point, so the ground and the sheet meet with no step. */
layout(set = 0, binding = 0) uniform sampler2D t_settle; /* by chunk: r the second it came onto the device, g 1 once it is built */
layout(set = 0, binding = 1) uniform sampler2D t_land;   /* by tile: its corners' heights in levels, at (0,0), (1,0), (1,1) and (0,1) from it */
layout(set = 1, binding = 1) uniform Settle
{
    vec4 wave;     /* the sheet's height in levels, the waves' height in levels, their length in tiles, tiles a second */
    vec4 clock;    /* seconds, tiles a chunk, chunks a side, tiles a side */
    vec4 sweep;    /* the point the sweep runs out from, its tiles a second, the seconds a point takes to settle */
    vec4 start;    /* the second the sweep starts: when the first of the world came onto the device; how much darker the land still being built is; the seconds that takes to come in */
    vec4 train[5]; /* each train of waves: the way it runs, its length as a share of the length, its share of the height */
} settle;

/*  The waves' height over the sheet in levels, and their slope in levels
 *  a tile.  Five trains of waves cross the whole map from five ways, each
 *  of its own length (frame.c, TRAIN).  A longer wave runs faster, as on
 *  deep water, so the crests gather and part all over the map. */
vec3 wave_at(vec2 p)
{
    float h = 0.0;
    vec2  g = vec2(0.0);
    for (int i = 0; i < 5; ++i)
    {
        vec2  d = normalize(settle.train[i].xy);
        float k = 6.2831853 / (settle.wave.z * settle.train[i].z);
        float v = settle.wave.w * sqrt(settle.train[i].z);
        float a = (dot(p, d) - settle.clock.x * v) * k + 1.7 * float(i);
        h += settle.train[i].w * sin(a);
        g += settle.train[i].w * k * cos(a) * d;
    }
    return vec3(settle.wave.y * h, settle.wave.y * g);
}

/*  The second the sweep reaches a point.  It starts when the first of the
 *  world comes onto the device, so the art does not all come down at once
 *  when a long build ends. */
float sweep_at(vec2 p)
{
    return settle.start.x + (settle.sweep.z > 0.0 ? length(p - settle.sweep.xy) / settle.sweep.z : 0.0);
}

/*  How far a point has settled, as chunk `c` has it: nought until the
 *  chunk is built and the sweep has reached the point. */
float settled_in(vec2 p, ivec2 c)
{
    vec2  st   = texelFetch(t_settle, c, 0).rg;
    float from = max(st.r, sweep_at(p));
    return st.g < 0.5 ? 0.0 : clamp((settle.clock.x - from) / max(settle.sweep.w, 1e-3), 0.0, 1.0);
}

/*  How far the point has settled, eased: 0 on the sheet, 1 at its own
 *  height.  It is blended from the four chunks whose middles are round
 *  the point, so it changes smoothly where one chunk is built and the
 *  next is not. */
float settled(vec2 p)
{
    int   per = int(settle.clock.z);
    vec2  u   = p / settle.clock.y - 0.5;
    ivec2 i0  = ivec2(floor(u));
    vec2  f   = u - vec2(i0);
    ivec2 hi  = ivec2(per - 1);
    float a   = settled_in(p, clamp(i0, ivec2(0), hi));
    float b   = settled_in(p, clamp(i0 + ivec2(1, 0), ivec2(0), hi));
    float c   = settled_in(p, clamp(i0 + ivec2(0, 1), ivec2(0), hi));
    float d   = settled_in(p, clamp(i0 + ivec2(1, 1), ivec2(0), hi));
    float k   = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    return k * k * (3.0 - 2.0 * k);
}

/*  The point moved from its own height and slope to the sheet's, by how
 *  far it has settled.  The ground's slope rides in the paint, and only
 *  the ground carries it there. */
void settle_move(inout vec4 nrm, inout vec4 col)
{
    float e = settled(a_pos.xy);
    vec3  w = wave_at(a_pos.xy);
    a_pos.z = mix(settle.wave.x + w.x, a_pos.z, e);
    nrm.xyz = normalize(mix(normalize(vec3(-0.5 * w.yz, 1.0)), nrm.xyz, e));
    if (col.z < 0.5)
        col.xy = mix(0.5 * w.yz, col.xy, e);
}
#endif

#ifdef LAND_SHEET
/*  Whether every point of a chunk has settled: it is built, and the
 *  sweep reached its farthest corner settle.sweep.w seconds ago. */
bool chunk_settled(ivec2 c)
{
    vec2  st  = texelFetch(t_settle, c, 0).rg;
    vec2  lo  = vec2(c) * settle.clock.y, hi = lo + settle.clock.y;
    vec2  far = max(abs(lo - settle.sweep.xy), abs(hi - settle.sweep.xy));
    float at  = max(st.r, settle.start.x + (settle.sweep.z > 0.0 ? length(far) / settle.sweep.z : 0.0));
    return st.g > 0.5 && settle.clock.x >= at + settle.sweep.w;
}

/*  Whether a chunk and the eight round it have all settled. */
bool settled_round(ivec2 ch, int per)
{
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (!chunk_settled(clamp(ch + ivec2(dx, dy), ivec2(0), ivec2(per - 1))))
                return false;
    return true;
}

/*  Whether this tile of the sheet lies under a chunk that is built.  If
 *  it does, it sinks as the chunk settles, so it stays under the ground
 *  where that is cut below the land.  It sinks less near a chunk not yet
 *  built, and not at all on the edge between them, so the sheet over
 *  that chunk meets it with no step. */
bool sheet_under()
{
    int   per  = int(settle.clock.z);
    float side = settle.clock.y;
    ivec2 ch   = ivec2(gl_InstanceIndex % per, gl_InstanceIndex / per);
    if (texelFetch(t_settle, ch, 0).g < 0.5)
        return false;
    vec2  f    = a_pos.xy / side - vec2(ch);
    float d    = 1.0;
    if (ch.x > 0 && texelFetch(t_settle, ch + ivec2(-1, 0), 0).g < 0.5)
        d = min(d, f.x);
    if (ch.x < per - 1 && texelFetch(t_settle, ch + ivec2(1, 0), 0).g < 0.5)
        d = min(d, 1.0 - f.x);
    if (ch.y > 0 && texelFetch(t_settle, ch + ivec2(0, -1), 0).g < 0.5)
        d = min(d, f.y);
    if (ch.y < per - 1 && texelFetch(t_settle, ch + ivec2(0, 1), 0).g < 0.5)
        d = min(d, 1.0 - f.y);
    a_pos.z -= 3.0 * settled(a_pos.xy) * clamp(d * side / 4.0, 0.0, 1.0);
    return true;
}

/*  One tile of the sheet, on the land's own heights before the settle
 *  moves it.  Under a chunk that is built, the sheet stays just below
 *  its ground and sinks as it settles.  The wave bends the ground's
 *  long edges less than the points along them, so cracks open while a
 *  chunk settles, and the sheet fills them.  Once a chunk and the chunks
 *  round it have settled, there is no sheet, and there is none past the
 *  map's edge.  False where there is none. */
bool sheet_place()
{
    int   per  = int(settle.clock.z), side = int(settle.clock.y), n = int(settle.clock.w);
    ivec2 ch   = ivec2(gl_InstanceIndex % per, gl_InstanceIndex / per);
    int   q    = gl_VertexIndex / 6, k = gl_VertexIndex % 6;
    ivec2 tile = ch * side + ivec2(q % side, q / side);
    if (tile.x >= n || tile.y >= n || settled_round(ch, per))
        return false;
    vec2 f  = vec2((k == 1 || k == 2 || k == 4) ? 1.0 : 0.0, (k == 2 || k == 4 || k == 5) ? 1.0 : 0.0);
    vec4 h  = texelFetch(t_land, tile, 0);
    float z = mix(mix(h.x, h.y, f.x), mix(h.w, h.z, f.x), f.y);
    vec2 g  = vec2(mix(h.y - h.x, h.z - h.w, f.y), mix(h.w - h.x, h.z - h.y, f.x));
    a_pos   = vec4(vec2(tile) + f, z, 0.0);
    a_paint = 0.5 * g;
    a_aux   = vec2(0.0, 0.0);
    a_oct   = vec2(0.0, 0.0);
    return true;
}
#endif

void main()
{
#ifdef LAND_SHEET
    if (!sheet_place())
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
#elif defined(LAND_SETTLE) && !defined(MESH_PULLED)
    a_pos = a_pos_in;
#endif
#if defined(EDIT_SHOW) || defined(MESH_PULLED)
    uint vi      = uint(gl_VertexIndex);
    uint at      = mi[vi];
    vec3 a_xyz   = mesh_xyz(at);
    vec2 a_paint = uintBitsToFloat(uvec2(mv[8u * at + 4u], mv[8u * at + 5u]));
    vec2 a_oct   = unpackSnorm2x16(mv[8u * at + 6u]);
    vec2 a_aux   = unpackHalf2x16(mv[8u * at + 7u]);
    uint a_key   = mk[vi / 3u];
#ifdef LAND_SETTLE
    a_pos = vec4(a_xyz, uintBitsToFloat(mv[8u * at + 3u]));
#endif
#endif
    vec4 a_nrm = vec4(oct_normal(a_oct), a_aux.x);
    vec4 a_col = vec4(a_paint, a_aux.y, LAND_INDEX);
#ifdef LAND_SHEET
    a_nrm.xyz = normalize(vec3(-a_paint, 1.0));
#endif
#ifdef LAND_SETTLE
    settle_move(a_nrm, a_col);
#endif
#ifdef LAND_SETTLE
    /*  The land still being built is darker.  The darkening comes in as
     *  the settle begins, and a point loses it as it settles. */
    v_dim = settle.start.y * clamp(settle.clock.x / max(settle.start.z, 1e-3), 0.0, 1.0) * (1.0 - settled(a_pos.xy));
#ifdef LAND_SHEET
    v_settled = 0.0;
#else
    v_settled = settled(a_pos.xy);
#endif
#endif
#ifdef LAND_SHEET
    bool under = sheet_under();
#endif
#ifdef EDIT_SHOW
    v_gone = 0.0;
#endif
#ifdef TILE_MARKS
    v_tile = mt[vi / 3u];
#endif
#ifdef TRI_COLOURS
    v_tri = vi / 3u;
#endif
    /*  The free rotation, for inspection: the grid turns about the pivot,
     *  the point under the view's centre, before the projection, so the
     *  same oblique camera sees the terrain from any side and what the
     *  view looks at stays where it is. */
    vec2 cr = a_pos.xy + tree_sway(a_col, a_pos.xy) - cam.rot.yz;
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
    v_lean = a_pos.w;
    vec2 ndc    = (canvas - cam.view.xy) * cam.view.zw - 1.0;
    /*  The depth is the camera's: how far the point lies along the way the
     *  camera looks, the turned column plus row into the screen at cos
     *  pitch and the height toward it at sin pitch, a level being 0.866 of
     *  a tile's diagonal step.  Larger is nearer.  At 30 degrees it is the
     *  isometric view's own distance; straight down it is the height alone.
     *  The sprites carry the same measure and write it too, so everything
     *  composes by where it stands and not by the tile it was drawn from --
     *  a pylon on a slope, a wire spanning a valley, a mark that cuts a
     *  corner.  The last term breaks the ties: everything on
     *  one diagonal is the same distance from an oblique camera and the
     *  sweep orders those by row; a four hundredth of a tile per step
     *  across the diagonal is the sweep's own order and nothing more. */
    float near  = (rr.x + rr.y) * cp + a_pos.z * 0.8660254 * sp + (rr.y - rr.x) * 0.0015;
    float depth = 1.0 - (near + 0.375 * cam.alt.y) / cam.alt.y;
    /*  A prop stands on its tile's ground and a zone tint lies just over
     *  it: nearly coplanar, so each takes a bias by material to win over
     *  what it lies on, a third of a tile at most; the free camera's own
     *  numbers.  A material named at run time takes none, and is ordered by
     *  its painter's slot.  The seabed under a water body is meant to be
     *  seen through the surface: a hair further, since a canal's bed can
     *  lie at its table (mesh.c, rule 4: "a quarter step behind the
     *  tile"). */
    if (a_col.z > 3.5 && a_col.z < 4.5)
        depth += 0.05 / cam.alt.y;
    if (a_col.z > 6.5)
        depth -= (0.3 / cam.alt.y) * (a_col.z > 20.25 ? 0.0 : a_col.z > 19.15 ? 1.1 : a_col.z > 9.5 && a_col.z < 10.5 ? 0.0 : 1.0); /* a prop a step nearer than its ground, a zone tint a shade nearer still, a highway's column and a run-time material at their own depth */
    /*  A zone tint belongs to the map view: looking obliquely it would
     *  lie over the world like a stain, so below 45 degrees it is thrown
     *  outside the frustum instead of drawn, and from there it fades in
     *  as the camera rises, so the map's colours arrive with the map
     *  rather than snapping on at the end of the move.  The fade rides in
     *  col.g, which a tint quad does not otherwise use. */
    if (a_col.z > 19.5 && a_col.z < 20.25 && a_col.y > 0.5)
    {
        /*  A LOT compose.c draws in place of the lot picture: at every
         *  pitch.  col.g carries 2 plus how solid a fill is, or 4 plus
         *  how solid a border is. */
        v_col.g = 1.0 + a_col.y;
    }
    else if (a_col.z > 19.5 && a_col.z < 20.25)
    {
        float fade = smoothstep(0.7071, 0.9700, sp);
        if (fade <= 0.0)
        {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
            return;
        }
        v_col.g = fade;
    }
#ifdef LAND_SHEET
    /*  Under a built chunk the sheet is a hair further than the ground, as
     *  the seabed is under the water, so the ground wins where they meet
     *  and the sheet shows only through a crack. */
    if (under)
        depth += 0.05 / cam.alt.y;
#endif
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
    if (show.wave.w > 0.5)
    {
        uint  t0  = vi - vi % 3u;
        vec2  mid = (mesh_xyz(mi[t0]).xy + mesh_xyz(mi[t0 + 1u]).xy + mesh_xyz(mi[t0 + 2u]).xy) / 3.0;
        float st  = id != 0x3FFFFu ? texelFetch(t_show, ivec2(int(id & 1023u), int(id >> 10)), 0).r * 255.0 : 0.0;
        /*  While the last world's triangles go, the ones keyed as gone go,
         *  whatever piece they belong to, and nothing else does. */
        if (show.wave.z > 0.5)
            st = id == 0x3FFFEu ? 1.0 : 0.0;
        if (st > 0.5)
        {
            bool  going = st < 1.5 || st > 2.5;
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
