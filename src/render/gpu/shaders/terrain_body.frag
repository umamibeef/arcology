/*  terrain.frag -- a lit material for the terrain mesh. */
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec4 v_col;
layout(location = 2) in vec3 v_wpos;
layout(location = 3) in float v_curv;
layout(location = 5) in float v_lean; /* how far a cliff's lean moved the ground (terrain.vert) */
#ifdef EDIT_SHOW
layout(location = 4) in float v_gone; /* how far an edit's piece has dissolved (terrain.vert) */
#endif
#ifdef TILE_MARKS
layout(location = 9) flat in uint v_tile; /* 1 + the tile the triangle belongs to, or 0 (terrain_mark.vert) */
#endif
#ifdef LAND_SETTLE
layout(location = 7) in float v_dim; /* how much darker the land still being built is drawn (terrain_settle.vert) */
layout(location = 8) in float v_settled; /* how far the point has settled (terrain_settle.vert) */
#endif

layout(set = 2, binding = 0) uniform sampler2D t_field; /* b: tiles to water / 16 */
#ifdef TILE_MARKS
/*  What the player's tool marks on each tile, one texel a tile: the
 *  mark's color, or alpha 0 for no mark (tile_marks.glsl).
 *  terrain_mark.frag lights the tile with it. */
layout(set = 2, binding = 1) uniform sampler2D t_marks;
#define SUN_SB 2
#else
#define SUN_SB 1
#endif
/*  The sun's shadow map: how far from the sun the mesh's nearest
 *  surface lies, over the place the sun's frame gives (fr.sunm). */
layout(set = 2, binding = SUN_SB) uniform sampler2DShadow t_sun;
/*  A repeating noise, the one the sky's field is (gpu/clouds.h).  The
 *  ground's large bumps are read from it. */
layout(set = 2, binding = SUN_SB + 1) uniform sampler2D t_bumps;
/*  What the build lays over the ground, LAMP_TEXELS a tile: the street
 *  lamps' light and its height (draw/lamps.h), the trees' canopy and the
 *  tile's main species (trees.h). */
layout(set = 2, binding = SUN_SB + 2) uniform sampler2D t_laid;

layout(location = 0) out vec4 o_col;

//#include "tile_marks.glsl"

/*  THE MATERIALS, from the one place they are written.  A material is a
 *  number the mesh puts in a vertex colour, and this reads it by the
 *  half step around that number.  A second list here would drift from
 *  the mesh's, and the two would paint different things.
 *
 *  A material named at run time is numbered from MAT_RUNTIME_BASE up, and
 *  is shaded from its parameters rather than from a branch here: the
 *  shaders are built with the program and cannot grow a branch for
 *  something named after it. */
//#include "materials.glsl"

layout(set = 3, binding = 0) uniform Frag
{
    vec4 sun;    /* direction xyz (toward the sun), ambient            */
    vec4 params; /* diffuse, time, grid half-width in canvas px (0 off),
                  * underground view                                     */
    vec4 mats[MAT_RUNTIME_MAX]; /* one named at run time: rgb the color, w the roughness */
    vec4 strata[MAT_STRATA_MAX]; /* the tones of a cut's layers (STRATA, frame.c) */
    vec4 strata_n;               /* x: how many */
    vec4 sunm[3];                /* the sun's frame: a grid point to the map's place and its depth (frame.c, sun_frame) */
    vec4 sunsh;                  /* 1 while the map holds, the share of the sun a shadow takes, the map's texel in tiles, the tiles a level stands for */
    vec4 ground;                 /* the large bumps' steepest slope, their width in tiles, the noise's cells a side, the water's clarity */
    vec4 eye;                    /* toward the camera, in the normals' metric, and 0 */
    vec4 wave;                   /* the water's swells: their steepest slope, their length in tiles, and the wind in tiles a second */
    vec4 cover;                  /* the land's cover: the tiles a cell of its noise spans, the noise's mean and spread, its strength */
    vec4 amb;                    /* the ambient light's color at this hour (frame.c, sun_clock) */
    vec4 suncol;                 /* the sun's light's color at this hour, at its full strength on a face square to it */
    vec4 lamp;                   /* the street lamps' light at full, by how dark the hour is (frame.c) */
    vec4 fine[MAT_RUNTIME_MAX / 4]; /* each run-time material's thinnest feature in tiles, four to a vector */
    vec4 leaf[16];                  /* each species' two leaf colors, the light and the dark (trees.h) */
    vec4 rag[2];                    /* each species' raggedness, four to a vector */
    vec4 trees;                     /* 1 while a draw of trees, the dither from which it keeps them and to which, and the far cover's share */
} fr;

/*  How much of the sun reaches this point (sun_seen). */
float g_sun = 1.0;

/*  The street lamps' light here (draw/lamps.h): two texels a tile, the
 *  light and the height in levels it falls at, times four.  A surface
 *  more than a level from that height takes none, so a deck's lamps do
 *  not light the ground under the deck. */
vec3 lamp_light()
{
    vec2  l = texture(t_laid, v_wpos.xy * 2.0 / vec2(textureSize(t_laid, 0))).rg;
    float h = l.y * 255.0 / 4.0;
    return fr.lamp.rgb * l.x * (1.0 - smoothstep(0.75, 1.5, abs(v_wpos.z - h)));
}

/*  The light on a face whose cosine to the sun is d: the ambient's
 *  color and the sun's, as the hour gives them, the sun's shaded by its
 *  shadow, and the street lamps' at night. */
vec3 lit3(float d)
{
    return fr.amb.rgb + fr.suncol.rgb * d * g_sun + lamp_light();
}

/*  The tiles one pixel of the frame spans here. */
float g_tpp = 1.0;

/*  THE GROUND'S LARGE BUMPS: rises and swells a few tiles wide, which
 *  the heights cannot hold, since a height is one a tile corner in whole
 *  levels.  They tilt the light only, so nothing the game reads moves.
 *  Two widths, the second 0.4 of the first and turned, so the noise does
 *  not repeat in a view.  A width fades out as it spans fewer than ten
 *  pixels, so the bumps never shimmer at a distance. */
vec2 bump_slope(vec2 p, float wide, mat2 turn)
{
    float fade = smoothstep(4.0, 10.0, wide / g_tpp);
    if (fade <= 0.0)
        return vec2(0.0);
    vec2  uv = (turn * p) / (wide * fr.ground.z);
    float e  = 1.0 / float(textureSize(t_bumps, 0).x);
    vec2  dh = vec2(texture(t_bumps, uv + vec2(e, 0.0)).r - texture(t_bumps, uv - vec2(e, 0.0)).r,
                    texture(t_bumps, uv + vec2(0.0, e)).r - texture(t_bumps, uv - vec2(0.0, e)).r) /
              (2.0 * e);
    /*  A height of the width over a cell of the noise, so the slope is
     *  the same at every width. */
    return fade * (transpose(turn) * dh) / fr.ground.z;
}

vec3 ground_bumps(vec2 p, vec3 nl, float open)
{
    if (fr.ground.x <= 0.0 || open <= 0.0)
        return nl;
    vec2 g = bump_slope(p, fr.ground.y, mat2(1.0)) +
             0.5 * bump_slope(p, 0.4 * fr.ground.y, mat2(0.8480, 0.5299, -0.5299, 0.8480));
    return normalize(nl + vec3(-g * fr.ground.x * open, 0.0));
}

/*  The repeating noise at a point, evened to a mean of nought and a
 *  spread of one by the bake's own measure (clouds_field). */
float cover_noise(vec2 p, mat2 turn, float cells)
{
    return (texture(t_bumps, (turn * p) / (cells * fr.ground.z)).r - fr.cover.y) / max(fr.cover.z, 1e-3);
}

/*  THE LAND'S COVER, as it reads from above: a mosaic of meadow, scrub,
 *  woodland and bare earth, each patch tens of tiles across with soft
 *  edges.  The noise says where a patch would lie.  The land says what
 *  it can hold.  Woodland gathers in damp hollows and on gentle slopes,
 *  and thins with height and steepness.  Bare earth comes out on dry,
 *  high ridges that face the sun.  Scrub lies between the woods and the
 *  open grass.  Inside a wood the crowns show as a mottle about a tile
 *  wide, faded out when they span too few pixels. */
vec3 land_cover(vec3 base, vec2 p, float slope, float high, float damp, float hollow, float ridge, float aspect)
{
    if (fr.cover.w <= 0.0)
        return base;
    float n1    = cover_noise(p, mat2(1.0), fr.cover.x);
    float n2    = cover_noise(p, mat2(0.7071, 0.7071, -0.7071, 0.7071), 0.45 * fr.cover.x);
    float n     = 0.75 * n1 + 0.45 * n2;
    float steep = smoothstep(0.35, 0.75, slope);
    float wood  = smoothstep(0.30, 0.90, n + 0.6 * damp + 0.5 * hollow - 1.0 * steep - 0.8 * high);
    float scrub = smoothstep(0.55, 1.10, -0.8 * n + 0.4 * ridge + 0.4 * aspect - 0.6 * damp) * (1.0 - wood);
    float bare  = smoothstep(1.30, 2.00, -n + 0.8 * ridge + 0.6 * aspect + 0.8 * high - damp) * (1.0 - wood);
    float fade  = smoothstep(4.0, 10.0, 1.2 / g_tpp);
    float crown = 1.0 + fade * 0.22 * (texture(t_bumps, p / (1.2 * fr.ground.z)).r - fr.cover.y) / max(fr.cover.z, 1e-3) * 0.5;
    vec3  c     = mix(base, vec3(0.35, 0.42, 0.19), scrub * 0.7);
    c           = mix(c, vec3(0.56, 0.48, 0.31), bare * 0.8);
    c           = mix(c, vec3(0.20, 0.36, 0.15) * crown, wood * 0.7);
    return mix(base, c, fr.cover.w);
}

/*  THE FOREST SEEN FROM FAR: the canopy the trees lay (trees.h) over
 *  the ground.  Under the trees the floor is darker at every distance.
 *  Where the trees are too small to draw, the ground takes the main
 *  species' leaves instead, mottled a crown wide and darker between the
 *  crowns. */
vec3 forest_floor(vec3 base, vec2 p)
{
    vec2  ca  = texture(t_laid, p * 2.0 / vec2(textureSize(t_laid, 0))).ba;
    float cov = ca.x;
    if (cov <= 0.0)
        return base;
    int   sp  = clamp(int(ca.y * 255.0 + 0.5), 0, 7);
    float mot = texture(t_bumps, p / (0.45 * fr.ground.z)).r;
    float gap = smoothstep(0.35, 0.65, texture(t_bumps, p / (0.9 * fr.ground.z) + vec2(0.37, 0.71)).r);
    vec3  can = mix(fr.leaf[2 * sp + 1].rgb, fr.leaf[2 * sp].rgb, smoothstep(0.3, 0.7, mot)) * (0.7 + 0.3 * gap);
    base      = mix(base, base * vec3(0.72, 0.78, 0.70), cov * 0.6);
    return mix(base, can, clamp(cov * 1.3, 0.0, 1.0) * fr.trees.w);
}

/*  How much of the sun reaches this point, from 0 to 1.  The point moves
 *  out along its normal by a texel and a half, so a face does not shade
 *  itself.  Four taps of the map, each filtered by the device, give a
 *  soft edge two texels wide. */
float sun_seen(vec3 n)
{
    if (fr.sunsh.x < 0.5)
        return 1.0;
    float k = 1.5 * fr.sunsh.z;
    vec4  w = vec4(v_wpos + vec3(n.xy * k, n.z * k / fr.sunsh.w), 1.0);
    vec3  q = vec3(dot(fr.sunm[0], w), dot(fr.sunm[1], w), dot(fr.sunm[2], w));
    vec2  uv = vec2(q.x * 0.5 + 0.5, 0.5 - q.y * 0.5);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || q.z > 1.0)
        return 1.0;
    vec2  t = 0.5 / vec2(textureSize(t_sun, 0));
    float s = texture(t_sun, vec3(uv + vec2(-t.x, -t.y), q.z)) + texture(t_sun, vec3(uv + vec2(t.x, -t.y), q.z)) +
              texture(t_sun, vec3(uv + vec2(-t.x, t.y), q.z)) + texture(t_sun, vec3(uv + vec2(t.x, t.y), q.z));
    return 1.0 - fr.sunsh.y * (1.0 - 0.25 * s);
}

float hash2(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float vnoise(vec2 p)
{
    vec2  i = floor(p);
    vec2  f = fract(p);
    float a = hash2(i);
    float b = hash2(i + vec2(1.0, 0.0));
    float c = hash2(i + vec2(0.0, 1.0));
    float d = hash2(i + vec2(1.0, 1.0));
    f       = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        v += a * vnoise(p);
        p  = p * 2.03 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

/*  How much water lies behind a point of the glass: from that point's
 *  depth down to the bed. */
float bed_thickness(float surf, float bed, float zw)
{
    return (surf - bed) - (surf - zw);
}

/*  The sky's color that a ray from the water toward the sky meets:
 *  pale at the horizon, bluer overhead. */
vec3 sky_toward(vec3 r)
{
    return mix(vec3(0.78, 0.85, 0.92), vec3(0.32, 0.52, 0.82), sqrt(clamp(r.z, 0.0, 1.0)));
}

/*  THE WATER'S SURFACE, as it reads from high above.  What lies under a
 *  tile is too small to see from there, so nothing here is.
 *
 *  The body: the sun's light goes down through the water to the bed and
 *  comes back to the camera.  Each level of water takes a share of each
 *  color, red the most, so shallows show the sand as turquoise and the
 *  deep shows only the light the water itself scatters, a dark blue.
 *  The depth is the tile's own, from the shore field.
 *
 *  The top: swells several tiles long that the wind carries, read from
 *  the repeating noise as the ground's bumps are, and faded out as they
 *  grow too small to see.  Fresnel weighs the sky they reflect against
 *  the body, from the camera's real way.  A broad glint follows the sun.
 *  Bands of surf run in toward the coast, broken in patches along it,
 *  with a steady line at the water's edge.  Calm water, a marina or a
 *  stream (calm 1), has no swell and no surf. */
vec3 water_surface(vec2 p, float t, float calm)
{
    vec2  fld   = texture(t_field, p / vec2(textureSize(t_field, 0))).rg * 16.0;
    float dist  = fld.x;
    float depth = fld.y + min(dist, 6.0) * 0.50; /* in levels, as the mesh lowers the bed */

    vec2  drift = fr.wave.zw * t;
    vec2  g     = bump_slope(p - drift, fr.wave.y, mat2(1.0)) +
                  0.5 * bump_slope(p - 0.6 * drift, 0.37 * fr.wave.y, mat2(0.9135, 0.4067, -0.4067, 0.9135));
    vec3  n     = normalize(vec3(-g * fr.wave.x * (1.0 - calm), 1.0));
    vec3  V     = normalize(fr.eye.xyz);
    vec3  L     = normalize(fr.sun.xyz);
    float nv    = max(dot(n, V), 0.0);
    float F     = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
    /*  Down to the bed and back up toward the camera. */
    float path  = depth * (1.0 + 1.0 / max(nv, 0.25));
    vec3  T     = exp(-vec3(0.45, 0.16, 0.09) * path / fr.ground.w);
    vec3  light = lit3(max(L.z, 0.0));
    vec3  body  = (vec3(0.60, 0.55, 0.40) * T + vec3(0.04, 0.20, 0.36) * (1.0 - T)) * light;
    vec3  c     = mix(body, sky_toward(reflect(-V, n)) * fr.amb.rgb / max(fr.sun.w, 1e-3), F);
    c          += vec3(1.0, 0.97, 0.90) * 0.5 * pow(max(dot(n, normalize(L + V)), 0.0), 80.0) * g_sun * fr.suncol.rgb / max(fr.params.x, 1e-3);
    /*  The surf.  A band's crest keeps its phase as the distance falls
     *  and the time grows, so it runs in to the shore.  Bands too fine to
     *  see fade to their mean. */
    float near  = 1.0 - smoothstep(0.5, 1.6, dist);
    float bands = smoothstep(4.0, 10.0, 0.7 / g_tpp);
    float crest = smoothstep(0.75, 0.95, 0.5 + 0.5 * sin(dist * 8.976 + t * 1.2));
    float clump = smoothstep(0.35, 0.65, texture(t_bumps, p / (3.0 * fr.ground.z)).r);
    float foam  = near * clump * mix(0.08, crest, bands) + (1.0 - smoothstep(0.5, 0.75, dist)) * mix(0.15, 0.5, bands);
    return mix(c, vec3(0.90, 0.94, 0.97) * light, clamp(foam, 0.0, 1.0) * 0.8 * (1.0 - calm));
}

/*  The brown of earth: the ground where it slopes, and the topsoil a cut
 *  shows under the grass. */
const vec3 EARTH = vec3(0.50, 0.42, 0.24);

/*  The green of lush grass. */
const vec3 GRASS = vec3(0.30, 0.50, 0.18);

/*  Value noise in three dimensions.  Noise laid on the ground from
 *  above stretches into streaks down a slope.  Noise of the point
 *  itself, height included, has no way to stretch on any face. */
float hash3(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

float vnoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f      = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1.0, 0.0, 0.0)), f.x),
                   mix(hash3(i + vec3(0.0, 1.0, 0.0)), hash3(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0.0, 0.0, 1.0)), hash3(i + vec3(1.0, 0.0, 1.0)), f.x),
                   mix(hash3(i + vec3(0.0, 1.0, 1.0)), hash3(i + vec3(1.0, 1.0, 1.0)), f.x), f.y),
               f.z);
}

/*  Four octaves of it. */
float fbm3(vec3 p)
{
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i)
    {
        v += a * vnoise3(p);
        p  = p * 2.03 + vec3(1.7, 9.2, 4.1);
        a *= 0.5;
    }
    return v;
}

/*  The fine noise of the ground at `xyz`, the point in tiles with its
 *  height, at scale s. */
float ground_noise(vec3 xyz, float s, vec2 off)
{
    return vnoise3(xyz * s + vec3(off, 0.0));
}

/*  How deep the topsoil is under the surface, in levels. */
const float SOIL = 0.2;

/*  Loose stones: the scree at a cliff's foot, on the ground and in a
 *  cut through it.  `pz` is the point in tiles with its height. */
vec3 scree_at(vec3 pz)
{
    float stones = smoothstep(0.62, 0.80, ground_noise(pz, 26.0, vec2(2.0, 9.0)));
    return mix(vec3(0.50, 0.46, 0.40), vec3(0.36, 0.33, 0.29), stones) *
           (0.88 + 0.24 * ground_noise(pz, 41.0, vec2(0.0)));
}

/*  THE LAND AS A LAYER CAKE, wherever a cut shows it: a cliff's face, the
 *  ground of its lip, the map's own cut.  Every cut shows the same cake,
 *  so they meet with no seam.  The strata are laid down level, so each
 *  band is a run of HEIGHT, thick and thin by turns, with a dark seam
 *  between each and a little grain.  The frame gives the tones
 *  (STRATA in frame.c), and each band is dealt one by a hash.  None given
 *  draws the layers black.  The topsoil is the top layer: SOIL deep under
 *  the surface, where `soil` is one.  Above the surface lies what a
 *  cliff's foot heaped on it: scree.  `u` runs along the cut, `zw` is the
 *  height and `below` the depth under the surface, both in tiles, and
 *  `pz` the point. */
vec3 land_cake(float u, float zw, float below, float soil, vec3 pz)
{
    if (below < 0.0)
        return scree_at(pz);
    float warp = (fbm(vec2(u * 2.0, zw * 1.5)) - 0.5) * 0.02;
    float bc   = (warp - zw) * 6.0;
    bc        += 0.40 * sin(bc * 0.9); /* thick and thin bands, by band */
    float k    = floor(bc);
    float f    = fract(bc);
    float h    = hash2(vec2(k, 3.0));
    int   nt   = int(fr.strata_n.x + 0.5);
    vec3  tone = nt > 0 ? fr.strata[clamp(int(h * float(nt)), 0, nt - 1)].rgb : vec3(0.0);
    float seam = smoothstep(0.0, 0.08, f) * (1.0 - smoothstep(0.92, 1.0, f));
    vec3  c    = tone * (0.72 + 0.28 * seam) * (0.90 + 0.20 * vnoise(vec2(u * 25.0, zw * 40.0)));
    float top  = soil * SOIL * 0.5;
    if (top > 0.0)
    {
        /*  Topsoil: the ground's own brown, a shade darker, mottled, with
         *  dark crumbs. */
        float m     = fbm(vec2(u * 6.0, zw * 6.0));
        float crumb = smoothstep(0.72, 0.86, vnoise(vec2(u * 28.0, zw * 28.0)));
        vec3  dirt  = EARTH * (0.70 + 0.30 * m) * (1.0 - 0.30 * crumb);
        c           = mix(c, dirt, 1.0 - smoothstep(top * 0.75, top, below));
    }
    return c;
}

/*  The walls.  The colour slot's third component names the material,
 *  constant over a face: 1 a retaining wall of coursed blocks, 2 the map
 *  edge's cut through layers of sediment, 3 the water column in that
 *  cut.  Along a wall one of x and y is constant, so their sum runs
 *  along it; z is in levels, half a tile each. */
vec3 wall_colour(float mat, vec3 n)
{
    float u  = v_wpos.x + v_wpos.y;
    float zw = v_wpos.z * 0.5;
    float t  = fr.params.y;
    vec3  c;
    if (MAT_UPTO(mat, MAT_ENG_WALL))
    {
        /*  Blocks a quarter tile long and a quarter level high in
         *  running bond, each its own shade, mortar between. */
        float course = floor(zw / 0.125);
        float off    = mod(course, 2.0) * 0.125;
        vec2  cell   = vec2(floor((u + off) / 0.25), course);
        vec2  f      = vec2(fract((u + off) / 0.25), fract(zw / 0.125));
        float mortar = (f.x < 0.07 || f.y < 0.14) ? 1.0 : 0.0;
        float tint   = 0.82 + 0.36 * hash2(cell);
        vec3  block  = vec3(0.62, 0.58, 0.52) * tint *
                       (0.94 + 0.12 * vnoise(vec2(u * 30.0, zw * 60.0)));
        c = mix(block, vec3(0.42, 0.40, 0.37), mortar);
    }
    else if (MAT_UPTO(mat, MAT_SEDIMENT))
    {
        /*  A cut through the land: the layer cake (land_cake).  v_col.r
         *  is the surface the cut passes under at this point, and v_col.g
         *  is one where topsoil lies under it, nought where none does. */
        c = land_cake(u, zw, v_col.r * 0.5 - zw, v_col.g, vec3(v_wpos.xy, zw));
    }
    else if (MAT_UPTO(mat, MAT_WATER))
    {
        /*  The aquarium's glass.  Seen from the side, a point on the
         *  face looks through the water behind it to the floor: from the
         *  surface the eye crosses the whole depth of the body before
         *  reaching the sand, at the floor's edge none of it.  So the
         *  water's colour is by that path, long at the top of the face
         *  and gone at the bottom (the table is v_col.r, the bed v_col.g,
         *  the view about 27 degrees up), with light rays in the
         *  shallows and a bright line at the surface. */
        float surf  = v_col.r * 0.5;
        float bed   = v_col.g * 0.5;
        float depth = max(surf - zw, 0.0);
        float thick = max(bed_thickness(surf, bed, zw), 0.0);
        float path  = thick / 0.45;
        float body  = 1.0 - exp(-1.2 * path);
        float rays  = vnoise(vec2(u * 4.0 + t * 0.15, zw * 0.5 - t * 0.05));
        c = mix(vec3(0.30, 0.66, 0.80), vec3(0.04, 0.20, 0.46), body);
        c += vec3(0.10, 0.16, 0.16) * rays * (1.0 - smoothstep(0.0, 1.2, depth));
        return c; /* lit by the water, not the sun */
    }
    else if (MAT_ABOVE(mat, MAT_SEABED))
    {
        /*  The earth wedge under a network piece on a slope: bare earth
         *  with a fine grain, in the tone of the slope sprites' wedges. */
        float grain = 0.86 + 0.28 * vnoise(vec2(u * 28.0, zw * 44.0));
        c = vec3(0.44, 0.31, 0.18) * grain;
        float d = max(dot(n, normalize(fr.sun.xyz)), 0.0);
        return c * lit3(d);
    }
    else
    {
        /*  The seabed: rippled sand, pebbles, caustic webs in the
         *  shallows, and the light going out of it with the depth below
         *  the table in v_col.r, so it reads as a floor receding into the
         *  deep. */
        vec2  p      = v_wpos.xy;
        float surf   = v_col.r * 0.5;
        float depth  = max(surf - zw, 0.0);
        float ripple = 0.5 + 0.5 * sin((p.x + p.y) * 22.0 + 3.0 * fbm(p * 2.0));
        float grain  = 0.88 + 0.24 * vnoise(p * 17.0);
        float pebble = smoothstep(0.80, 0.90, vnoise(p * 31.0 + vec2(7.0, 3.0)));
        float caus   = vnoise(p * 9.0 + vec2(t * 0.30, t * 0.20)) *
                       vnoise(p * 11.0 - vec2(t * 0.25, -t * 0.35));
        vec3  sand   = mix(vec3(0.62, 0.55, 0.38), vec3(0.82, 0.76, 0.56), ripple) * grain;
        sand        *= vec3(0.80, 0.88, 0.96); /* under water, seen through it */
        sand         = mix(sand, vec3(0.36, 0.33, 0.28), pebble * 0.8);
        sand        += vec3(0.40, 0.46, 0.44) * smoothstep(0.28, 0.58, caus) *
                       (1.0 - smoothstep(0.0, 2.0, depth));
        c = mix(sand, vec3(0.03, 0.10, 0.20), smoothstep(0.0, 3.5, depth) * 0.9);
    }
    float d = max(dot(n, normalize(fr.sun.xyz)), 0.0);
    return c * lit3(d);
}

/*  An ordered threshold for each pixel of a four by four tile, so a
 *  piece that dissolves loses its pixels in a fixed pattern and keeps an
 *  even screen of what is left.  Nothing is blended, since the alpha
 *  this writes is a palette index.  The trees' levels of detail cross
 *  over by it too: one level keeps the pixels the other discards. */
float dither4(vec2 p)
{
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 q = ivec2(p) & 3;
    return (m[q.x + 4 * q.y] + 0.5) / 16.0;
}

#ifdef TILE_MARKS
/*  The mark of the tile the triangle belongs to (shape_tile_of), not of
 *  the tile under the pixel.  So the ground, the water, a street, a tree
 *  or a lamp lights with its own tile, and a deck over a marked tile does
 *  not.  What lies flat over its own tile also takes the tile's edge.
 *  The variant calls it after the shading, with the grid's scale. */
void light_own_tile(vec2 ppu)
{
    if (v_tile == 0u)
        return;
    int   side = textureSize(t_marks, 0).x;
    int   i    = int(v_tile - 1u);
    ivec2 t    = ivec2(i % side, i / side);
    bool  rim  = ivec2(floor(v_wpos.xy)) == t && normalize(v_nrm).z > 0.7;
    light_marks(t, v_wpos.xy, ppu, rim);
}
#endif

#if defined(TILE_MARKS) || defined(TRI_COLOURS) || defined(LAND_SETTLE)
/*  The body's shading becomes a function the variant calls before it
 *  adds its own. */
#define main shade
#endif
void main()
{
#ifdef EDIT_SHOW
    if (v_gone > 0.0 && dither4(gl_FragCoord.xy) < v_gone)
        discard;
#endif
    if (fr.trees.x > 0.5)
    {
        float k = dither4(gl_FragCoord.xy);
        if (k < fr.trees.y || k >= fr.trees.z)
            discard;
    }
    vec3  n = normalize(v_nrm);
    float d = max(dot(n, normalize(fr.sun.xyz)), 0.0);
    g_sun   = d > 0.0 ? sun_seen(n) : 1.0;
#ifdef LAND_SETTLE
    /*  The sun's map holds the land at its own height.  So a point takes
     *  its shadow as it settles onto that height, and the shadows come in
     *  with the land rather than all at once when the settle ends. */
    g_sun = mix(1.0, g_sun, v_settled);
#endif
    g_tpp   = max(fwidth(v_wpos.x), fwidth(v_wpos.y));
    if (fr.params.w > 0.5)
    {
        /*  The underground view: the original's lattice, a hairline here
         *  at any scale (the half-width in fr.params.z is half a device
         *  pixel), and nothing else.  The original's underground ground
         *  is a wireframe that hides nothing: the pipes and subways of the
         *  tiles behind show through it, and a lattice line in front
         *  paints over them.  So only the line is drawn, with its depth;
         *  the ground between the lines is the white backdrop, which never
         *  covers a sprite. */
        float dist = grid_edge_px(v_wpos.xy, grid_px(v_wpos.xy));
        if (dist >= fr.params.z)
            discard;
        o_col = vec4(vec3(155.0, 135.0, 71.0) / 255.0, v_col.a);
        return;
    }
    if (MAT_FROM(v_col.b, MAT_RUNTIME_BASE))
    {
        /*  A material named at run time: its own color, and a
         *  roughness that decides how much of the sun it takes as a
         *  sheen.  No branch of its own, because it was named after the
         *  shaders were built.  It is tested before every material the
         *  branches below name as a run upward. */
        int   k     = int(v_col.b - MAT_RUNTIME_BASE + 0.5);
        vec4  m     = fr.mats[clamp(k, 0, MAT_RUNTIME_MAX - 1)];
        /*  A feature thinner than a pixel is left out, through a dither
         *  laid on the world so it holds still as the view moves.  It
         *  goes over between one pixel and a third of one. */
        int   kc    = clamp(k, 0, MAT_RUNTIME_MAX - 1);
        float fine  = fr.fine[kc / 4][kc % 4];
        if (fine > 0.0)
        {
            float px   = fine / max(g_tpp, 1e-6);
            float keep = smoothstep(0.35, 1.0, px);
            if (keep < hash2(floor(v_wpos.xy * 64.0) + floor(v_wpos.z * 8.0)))
                discard;
        }
        float sheen = (1.0 - m.w) * pow(max(d, 0.0), 8.0) * 0.35;
        o_col       = vec4(clamp(m.rgb * lit3(d) + sheen, 0.0, 1.0), v_col.a);
        return;
    }
    if (MAT_FROM(v_col.b, MAT_ZONE))
    {
        /*  A zone, as the original's map view paints them: residential
         *  green, commercial blue, industrial yellow, the darker shade
         *  for the dense zone, and the rest as the game's own map has
         *  them.  col.r carries the zone; 10 and 11 are not zones but
         *  the structures the player placed -- a power plant burning
         *  orange, a park in parkland green, everything else civic
         *  white -- and they are painted
         *  more solidly than a zone, because on a map they are the
         *  landmarks a person looks for. */
        int  zn = int(v_col.r + 0.5);
        vec3 c  = zn == 1    ? vec3(0.36, 0.78, 0.36)
                  : zn == 2  ? vec3(0.16, 0.55, 0.20)
                  : zn == 3  ? vec3(0.40, 0.62, 0.95)
                  : zn == 4  ? vec3(0.16, 0.32, 0.78)
                  : zn == 5  ? vec3(0.92, 0.85, 0.35)
                  : zn == 6  ? vec3(0.78, 0.62, 0.12)
                  : zn == 7  ? vec3(0.66, 0.30, 0.30)
                  : zn == 8  ? vec3(0.62, 0.62, 0.68)
                  : zn == 9  ? vec3(0.30, 0.62, 0.66)
                  : zn == 11 ? vec3(0.95, 0.45, 0.18)
                  : zn == 12 ? vec3(0.22, 0.42, 0.20)
                             : vec3(0.94, 0.93, 0.90);
        /*  The grid runs over the tint as it runs over the ground: the
         *  tint is a lid on the tile, so it has to carry the tile's edges
         *  or the map loses its grid exactly where it is most read. */
        if (fr.params.z > 0.0)
        {
            if (grid_edge_px(v_wpos.xy, grid_px(v_wpos.xy)) < fr.params.z)
                c *= 0.72;
        }
        /*  A lot's border (4 and more in col.g) is the zone's own colour
         *  a shade deeper, as solid as col.g less 4 says. */
        if (v_col.g > 3.5)
        {
            o_col = vec4(c * 0.85, clamp(v_col.g - 4.0, 0.0, 1.0));
            return;
        }
        /*  A lot's fill (2 and more) is an even wash over the ground, as
         *  solid as col.g less 2 says.  Its colour is mixed toward white
         *  as far as col.r's fraction says, 0.4 being white, so zoned land
         *  reads lighter than the ground round it. */
        if (v_col.g > 1.5)
        {
            float lt = clamp((v_col.r - float(zn)) / 0.4, 0.0, 1.0);
            o_col    = vec4(mix(c, vec3(1.0), lt), clamp(v_col.g - 2.0, 0.0, 1.0));
            return;
        }
        o_col   = vec4(c, (zn >= 10 ? 0.78 : 0.45) * clamp(v_col.g, 0.0, 1.0));
        return;
    }
    if (MAT_IS(v_col.b, MAT_LEAF))
    {
        /*  A tree's foliage (trees.c).  col.r is how far out from the trunk
         *  the point lies, col.g how far down its tier's skirt: 0 at the top,
         *  1 at a drooped tip, more under it.  col.r carries the tree's
         *  shade too, two a step, 0 to 9 (trees.c).  The normal is the crown's
         *  envelope, so the crown is lit as one mass, softly, much of it by
         *  the light the sky and the leaves round it give.  Deeper in the
         *  crown and under a tier it is darker.  Two colors mottle it at the
         *  scale of a clump of needles, faded to their mean when a clump is
         *  smaller than a pixel.  The skirt's edge is cut ragged, and the
         *  cut fades out the same way, so it never shimmers. */
        int   sp    = clamp(int((v_col.b - MAT_LEAF) * 16.0 + 0.5), 0, 7);
        float shade = floor(v_col.r * 0.5);
        float reach = v_col.r - 2.0 * shade;   /* how far out from the trunk, 0 to 1 */
        float tone  = shade / 9.0 - 0.5;       /* the tree's own shade, -0.5 to 0.5 */
        if (v_col.g > 2.5)
        {
            /*  A broadleaf's card or cap, or a palm's frond (trees.c): col.r
             *  is how far across it, col.g its kind (3 a card, 5 a cap, 7 a
             *  frond) plus how far up or along it.  A card or a cap is cut
             *  to a round crown with a ragged edge and gaps between its
             *  leaves.  A frond is cut to a fringe of leaflets that narrows
             *  to the tip.  Each cut that is finer than a pixel fades out. */
            float kind = floor(v_col.g * 0.5) * 2.0 + 1.0;
            vec2  uv   = vec2(reach, v_col.g - kind);
            vec3  wp2  = vec3(v_wpos.xy, v_wpos.z * 0.5);
            float fn   = smoothstep(0.5, 2.0, 0.05 / max(g_tpp, 1e-6));
            float rag2 = fr.rag[sp / 4][sp % 4];
            float lit_up;
            if (kind > 6.0)
            {
                float across = abs(uv.x - 0.5) * 2.0, along = uv.y;
                float fringe = (1.0 - along * 0.85) * mix(1.0, 0.55 + 0.45 * abs(sin(along * 46.0)), fn * rag2);
                if (across > fringe || (across < 0.08 && along > 0.95))
                    discard;
                lit_up = 0.85;
            }
            else
            {
                vec2  q    = uv * 2.0 - 1.0;
                float disc = 1.0 - dot(q, q);
                float edge = (vnoise3(wp2 * 11.0) - 0.5) * rag2 * 1.6 + (vnoise3(wp2 * 31.0) - 0.5) * rag2 * 0.8 * fn;
                if (disc + edge < 0.0 || (fn > 0.0 && vnoise3(wp2 * 36.0) < 0.16 * fn))
                    discard;
                lit_up = kind > 4.0 ? 1.0 : mix(0.55, 1.0, uv.y);
            }
            float mot2  = mix(0.5, vnoise3(wp2 * 24.0) * 0.6 + vnoise3(wp2 * 61.0) * 0.4, fn);
            vec3  base2 = mix(fr.leaf[2 * sp + 1].rgb, fr.leaf[2 * sp].rgb, smoothstep(0.25, 0.75, mot2));
            base2 *= 1.0 + 0.28 * tone;
            base2  = mix(base2, base2 * vec3(1.12, 1.04, 0.80), max(tone, 0.0));
            float sun2 = d * g_sun;
            vec3  lit2 = fr.amb.rgb * 0.85 * lit_up + fr.suncol.rgb * sun2 * (0.6 + 0.4 * lit_up) * 1.3 + lamp_light();
            o_col      = vec4(clamp(base2 * lit2 * mix(vec3(1.0), vec3(1.0, 0.95, 0.75), sun2 * 0.6), 0.0, 1.0), v_col.a);
            return;
        }
        vec3  wp    = vec3(v_wpos.xy, v_wpos.z * 0.5);
        float clump = 0.06;
        float fine  = smoothstep(0.5, 2.0, clump / max(g_tpp, 1e-6));
        float rag   = fr.rag[sp / 4][sp % 4];
        if (fine > 0.0 && v_col.g > 0.55 && v_col.g < 1.05)
        {
            float edge = smoothstep(0.55, 1.0, v_col.g) * rag * fine;
            if (vnoise3(wp * 38.0) < edge * 0.9)
                discard;
        }
        float mot  = mix(0.5, vnoise3(wp * 22.0) * 0.65 + vnoise3(wp * 57.0) * 0.35, fine);
        vec3  base = mix(fr.leaf[2 * sp + 1].rgb, fr.leaf[2 * sp].rgb, smoothstep(0.25, 0.75, mot));
        /*  Each tree its own shade: lighter or darker, yellower or bluer. */
        base *= 1.0 + 0.28 * tone;
        base  = mix(base, base * vec3(1.12, 1.04, 0.80), max(tone, 0.0));
        /*  The light.  The sky's share is held back, since a crown shades
         *  most of itself, and the sun's is the envelope's, so a crown
         *  turned to the sun is bright and warm and the far side falls
         *  into shade.  Deeper in the crown and under a tier is darker. */
        float self = mix(0.6, 1.0, clamp(reach, 0.0, 1.0)) * (v_col.g > 1.05 ? 0.5 : mix(1.0, 0.78, smoothstep(0.3, 1.0, v_col.g)));
        float sun  = d * g_sun;
        vec3  lit  = fr.amb.rgb * 0.85 * self + fr.suncol.rgb * sun * (0.6 + 0.4 * self) * 1.3 + lamp_light();
        vec3  warm = vec3(1.0, 0.95, 0.75);
        o_col      = vec4(clamp(base * lit * mix(vec3(1.0), warm, sun * 0.6) + base * vec3(0.18, 0.22, 0.04) * pow(sun, 3.0) * 0.6, 0.0, 1.0), v_col.a);
        return;
    }
    if (MAT_FROM(v_col.b, MAT_LAMP))
    {
        /*  A signal lamp.  col.r is the junction's phase, col.g the lamp:
         *  0..2 red, amber, green on a north-south arm, 3..5 on an
         *  east-west arm.  Twelve seconds round: one group runs green,
         *  then amber, then red while the other waits, then they swap. */
        float t    = fract(fr.params.y / 20.0 + v_col.r); /* twenty seconds round: green, amber, all-red, then the swap */
        int   code = int(v_col.g + 0.01); /* a sign's face carries its v in the fraction */
        int   grp  = code / 3, lamp = code - grp * 3;
        if (code == 0 && v_col.r == 0.0)
        {
            /* a crossbuck's white arms (phase 0, lamp 0) */
            o_col = vec4(vec3(0.92) * lit3(d), v_col.a);
            return;
        }
        if (code >= 6)
        {
            /*  A rail signal's steady aspect, 6 green and 7 red; a
             *  crossing's flasher pair, 8 and 9, alternating each second,
             *  or 12 dark; 10 a yellow sign face; 11 the gate arm's red
             *  and white stripes (spec 3.15, 5.6); 14 a street lamp's
             *  head. */
            if (code == 14)
            {
                /*  A street lamp's head: a grey housing by day, lit warm as
                 *  the hour's sun fades (frame.c, sun_clock). */
                float night = 1.0 - smoothstep(0.05, 0.35, max(fr.suncol.r, max(fr.suncol.g, fr.suncol.b)));
                o_col       = vec4(mix(vec3(0.62) * lit3(d), vec3(1.0, 0.86, 0.62), night), v_col.a);
                return;
            }
            if (code == 10)
            {
                o_col = vec4(vec3(0.95, 0.78, 0.10) * lit3(d), v_col.a);
                return;
            }
            if (code == 13)
            {
                /* a STOP sign: a red octagon with a white border, the face's corners cut */
                vec2  q  = abs(vec2(v_col.r, fract(v_col.g) * 2.0) - 0.5) * 2.0;
                float oc = q.x + q.y;
                if (oc > 1.41)
                    discard;
                bool border = q.x > 0.86 || q.y > 0.86 || oc > 1.27;
                o_col       = vec4((border ? vec3(0.95) : vec3(0.78, 0.08, 0.10)) * lit3(d), v_col.a);
                return;
            }
            if (code == 11)
            {
                float st = fract((v_wpos.x + v_wpos.y + v_wpos.z * 0.35) * 8.0);
                o_col    = vec4((st < 0.5 ? vec3(0.90, 0.12, 0.10) : vec3(0.95)) * lit3(d), v_col.a);
                return;
            }
            bool  on  = code == 6 || code == 7 || (code != 12 && (fract(fr.params.y + v_col.r) < 0.5) == (code == 8));
            vec3  col = code == 6 ? vec3(0.15, 0.95, 0.30) : vec3(0.95, 0.12, 0.10);
            o_col     = vec4(on ? col : col * 0.18, v_col.a);
            return;
        }
        float tt   = grp == 1 ? fract(t + 0.5) : t;
        int   lit  = tt < 0.30 ? 2 : (tt < 0.40 ? 1 : 0); /* six seconds green, two amber, two all-red */
        vec3  col  = lamp == 0 ? vec3(0.95, 0.12, 0.10)
                   : lamp == 1 ? vec3(0.98, 0.76, 0.12) : vec3(0.15, 0.90, 0.30);
        o_col = vec4(lamp == lit ? col : col * 0.18, v_col.a);
        return;
    }
    if (MAT_FROM(v_col.b, MAT_PROP))
    {
        /*  Street furniture: painted steel, lit flat. */
        o_col = vec4(vec3(0.22, 0.22, 0.24) * lit3(d), v_col.a);
        return;
    }
    if (MAT_FROM(v_col.b, MAT_PIER))
    {
        /*  A highway's column: concrete, lit flat. */
        o_col = vec4(vec3(0.55, 0.58, 0.59) * lit3(d), v_col.a);
        return;
    }
    if (MAT_FROM(v_col.b, MAT_SURFACE))
    {
        o_col = vec4(clamp(water_surface(v_wpos.xy, fr.params.y, v_col.g), 0.0, 1.0), v_col.a);
        return;
    }
    if (MAT_ABOVE(v_col.b, MAT_GROUND))
    {
        /*  The water column is blended: its cover grows with the depth
         *  below the table, so the floor shows through the shallows and
         *  the deep is water. */
        float a = v_col.a;
        if (MAT_IS(v_col.b, MAT_WATER))
        {
            /* the glass covers by the water behind it, see wall_colour */
            float thick = max(bed_thickness(v_col.r * 0.5, v_col.g * 0.5, v_wpos.z * 0.5), 0.0);
            a = 0.22 + 0.76 * (1.0 - exp(-1.6 * thick / 0.45));
        }
        o_col = vec4(clamp(wall_colour(v_col.b, n), 0.0, 1.0), a);
        return;
    }
    /*  The ground's material, in world coordinates so it does not swim
     *  with the camera: meadows of green over the sprite's tan, laid out
     *  by a slow noise, bare and rockier where the slope steepens, with a
     *  fine grain on top. */
    vec2  p      = v_wpos.xy;
    /*  The ground reads its own topology.  Slope from the face, aspect from
     *  the face against the sun, altitude from the vertex, curvature from
     *  the height field, and the distance to water from the field texture:
     *  lush grass low, damp and in hollows; drier grass on ridges, sun-
     *  facing slopes and with altitude; earth and then rock as the slope
     *  steepens; sand on level ground at the waterline; a fine two-octave
     *  grain over all of it.  The material reads the SMOOTHED surface, the
     *  height field's gradient carried per vertex, so it grades across the
     *  facets instead of stepping at each one. */
    vec3  ns     = normalize(vec3(-v_col.xy, 1.0));
    float slope  = clamp((1.0 - ns.z) * 3.0 * 0.7 + (1.0 - n.z) * 3.0 * 0.3, 0.0, 1.0);
    vec2  nh     = ns.xy;
    float aspect = length(nh) > 1e-3 ? max(dot(normalize(nh), normalize(fr.sun.xy)), 0.0) * clamp((1.0 - ns.z) * 3.0, 0.0, 1.0) : 0.0;
    float high   = smoothstep(8.0, 16.0, v_wpos.z);
    float hollow = smoothstep(0.0, 0.35, v_curv);
    float ridge  = smoothstep(0.0, 0.35, -v_curv);
    float towater = texture(t_field, p / vec2(textureSize(t_field, 0))).b * 16.0; /* texel centre = tile centre */
    float damp   = 1.0 - smoothstep(0.3, 3.0, towater);
    /* the land side of a tile edge is 0.5 in the field; the beach is the near half of the tile */
    float beach  = (1.0 - smoothstep(0.5, 1.0, towater)) * (1.0 - slope);
    float drift  = fbm(p * 0.35 + vec2(3.1, 7.7)) - 0.5;
    vec3  pz     = vec3(p, v_wpos.z * 0.5); /* the point in tiles, for noise laid from the sides */
    /*  The grain is finer than a tile, so it fades out as it spans too
     *  few pixels to see. */
    float grain  = 1.0 + smoothstep(4.0, 10.0, (1.0 / 9.0) / g_tpp) *
                             (0.14 * (ground_noise(pz, 9.0, vec2(0.0)) - 0.5) + 0.10 * (ground_noise(pz, 23.0, vec2(5.0, 2.0)) - 0.5)) -
                   0.10;

    vec3  lush   = GRASS + drift * vec3(0.08, 0.07, 0.02);
    vec3  dry    = vec3(0.52, 0.55, 0.26);
    vec3  earth  = EARTH;
    vec3  rock   = vec3(0.47, 0.45, 0.41);
    vec3  sand   = vec3(0.76, 0.70, 0.50);
    vec3  wet    = vec3(0.24, 0.38, 0.15);
    float dryness = clamp(0.35 * high + 0.30 * ridge + 0.30 * aspect - 0.5 * damp - 0.4 * hollow, 0.0, 1.0);
    vec3  base   = mix(lush, dry, dryness);
    base         = mix(base, wet, damp * hollow * 0.5);
    base         = land_cover(base, p, slope, high, damp, hollow, ridge, aspect);
    base         = forest_floor(base, p);
    base         = mix(base, earth, smoothstep(0.15, 0.55, slope));
    base         = mix(base, rock, clamp(smoothstep(0.55, 1.0, slope) + high * slope * 0.3, 0.0, 1.0));
    base         = mix(base, sand, beach);
    /*  Scree where a cliff's lean filled the foot (v_lean above
     *  nought): loose stones in torn patches, thickest against the face,
     *  with the grass between them. */
    float fill   = max(v_lean, 0.0);
    float pile   = smoothstep(0.45, 0.65, fill * 1.4 + 0.9 * (fbm3(pz * 2.5) - 0.5) +
                                              0.25 * (ground_noise(pz, 14.0, vec2(0.0)) - 0.5)) * step(0.001, fill);
    base         = mix(base, scree_at(pz), pile);
    /*  The light.  On gentle ground it falls on the smoothed surface, so a
     *  hill is lit as one rounded form.  A face as steep as a cliff keeps
     *  its own plane, so its edge stays crisp.  The large bumps tilt the
     *  light on open ground.  The sky's light is the share of the sky the
     *  tile sees past the land round it (shore.c), so a valley is darker
     *  only by what stands over it. */
    float crisp  = smoothstep(0.35, 0.6, 1.0 - n.z);
    vec3  nl     = ground_bumps(p, normalize(mix(ns, n, crisp)), 1.0 - slope);
    float dl     = max(dot(nl, normalize(fr.sun.xyz)), 0.0);
    float sky    = texture(t_field, p / vec2(textureSize(t_field, 0))).a;
    vec3  lit    = fr.amb.rgb * sky + fr.suncol.rgb * dl * g_sun + lamp_light();
    vec3  c      = base * grain * lit;
    /*  Where a cliff's lean cut the lip away (v_lean below nought, how
     *  deep in levels), the ground shows the cut: the layer cake, the
     *  same as the face below it and the map's cut beside it. */
    float cut    = max(-v_lean, 0.0);
    if (cut > 0.0)
    {
        vec3 cake = land_cake(p.x + p.y, v_wpos.z * 0.5, cut * 0.5, 1.0, pz);
        c         = mix(c, cake * lit, smoothstep(0.01, 0.06, cut));
    }
    if (fr.params.z > 0.0)
    {
        /*  The grid the sprites carry: every ground diamond has a darker
         *  stair along its four edges, two pixels per row in the flat
         *  tile (index 107 on 104) and a subtler one on the slopes (103 on
         *  102).  The band is measured in screen pixels from the tile's
         *  edges in grid units, so it stays a pixel wide at every zoom.
         *  The sprites' stair is a one-pixel line, and neighbouring tiles
         *  overpaint the same edge pixels.  So the line is the pixels the
         *  edge passes through: half a pixel to either side of it. */
        float dist = grid_edge_px(v_wpos.xy, grid_px(v_wpos.xy));
        if (dist < fr.params.z)
        {
            float slope  = clamp((1.0 - n.z) * 4.0, 0.0, 1.0);
            vec3  strong = vec3(0.79, 0.70, 0.61);
            vec3  subtle = vec3(0.95, 0.92, 0.87);
            c *= mix(strong, subtle, slope);
        }
    }
    o_col = vec4(clamp(c, 0.0, 1.0), v_col.a);
}
