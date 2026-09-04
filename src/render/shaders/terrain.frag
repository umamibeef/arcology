#version 450
/*  terrain.frag -- a lit material for the terrain mesh. */
layout(location = 0) in vec3 v_nrm;
layout(location = 1) in vec4 v_col;
layout(location = 2) in vec3 v_wpos;
layout(location = 3) in float v_curv;

layout(set = 2, binding = 0) uniform sampler2D t_field; /* b: tiles to water / 16 */

layout(location = 0) out vec4 o_col;

layout(set = 3, binding = 0) uniform Frag
{
    vec4 sun;    /* direction xyz (toward the sun), ambient            */
    vec4 params; /* diffuse, time, grid half-width in canvas px (0 off),
                  * underground view                                     */
} fr;

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

/*  The water surface on a cut edge, shaded as sprite_water.frag shades
 *  the decal: the same field, the same noise normals, Fresnel, foam and
 *  caustics, in world coordinates, so it is continuous with the decals
 *  beside it. */
vec3 water_surface(vec2 p, float t, float calm)
{
    vec2  fld   = texture(t_field, p / 128.0).rg * 16.0;
    float dist  = fld.x;
    float depth = fld.y + min(dist, 6.0) * 0.50; /* as the mesh lowers the bed */
    float shallow = 1.0 - smoothstep(0.0, 1.2, depth); /* 1 over a bed at the surface */

    /*  The surface: a noise height field at three scales, two of them
     *  drifting, differentiated for the normal; the ripples are calmer in
     *  the shallows.  No sinusoids: a handful of them interferes into a
     *  lattice, and water does not. */
    vec2  d1 = vec2(t * 0.06, t * 0.045), d2 = vec2(-t * 0.10, t * 0.07);
    float e  = 0.02;
#define HF(q) (fbm((q) * 1.5) * 0.5 + fbm((q) * 4.5 + d1) * 0.35 + fbm((q) * 12.0 + d2) * 0.15)
    float f0 = HF(p);
    float fx = HF(p + vec2(e, 0.0));
    float fy = HF(p + vec2(0.0, e));
#undef HF
    vec2  slope = vec2(fx - f0, fy - f0) / e * mix(0.10, 0.06, shallow);
    vec3  n     = normalize(vec3(-slope, 1.0));

    /*  The camera of a 2:1 isometric view sits about 27 degrees above
     *  the horizon, toward the south-east; the sun is over its shoulder. */
    vec3  L    = normalize(fr.sun.xyz);
    vec3  V    = normalize(vec3(0.60, 0.60, 0.45));
    vec3  H    = normalize(L + V);
    float nv   = max(dot(n, V), 0.0);
    float ndl  = max(dot(n, L), 0.0);
    float sheen   = pow(max(dot(n, H), 0.0), 16.0);
    float glitter = pow(max(dot(n, H), 0.0), 400.0);
    float F = 0.08 + 0.92 * pow(1.0 - nv, 4.0); /* Schlick, a little exaggerated */

    /*  Colour by depth: a turquoise over the shallows where the bed shows
     *  through, a deep blue-green offshore; the sky reflected on top by
     *  Fresnel, a broad sheen and sparse sun glitter. */
    vec3  deep    = vec3(0.02, 0.16, 0.36);
    vec3  mid     = vec3(0.07, 0.34, 0.54);
    vec3  shoal   = vec3(0.22, 0.60, 0.66);
    vec3  sky     = vec3(0.66, 0.80, 0.93);
    /*  The coast line lies at distance 0.5 in the field, so a bed one
     *  level down at the first water tile reads about 1.3 here and the
     *  open sea two levels down, well offshore, about 3.8. */
    vec3  base    = mix(shoal, mid, smoothstep(0.25, 2.2, depth));
    base          = mix(base, deep, smoothstep(2.2, 4.5, depth));
    base          = mix(base, vec3(0.16, 0.32, 0.80), 0.08);
    vec3  c       = base * (0.80 + 0.30 * ndl);
    /*  Caustics in the shallows: two drifting noise webs, brightest where
     *  they cross, fading with depth. */
    float caus = vnoise(p * 9.0 + vec2(t * 0.30, t * 0.20)) *
                 vnoise(p * 11.0 - vec2(t * 0.25, -t * 0.35));
    c            += vec3(0.35, 0.45, 0.45) * smoothstep(0.30, 0.60, caus) * shallow * 0.6 * (1.0 - calm);
    c             = mix(c, sky, F * 0.8);
    c            += vec3(1.0, 0.98, 0.92) * (sheen * 0.08 + glitter * 0.6);
    /*  Foam: a breaking line just off the land, torn by noise and
     *  breathing with time, and a fainter second line behind it. */
    float edge  = 1.0 - smoothstep(0.5, 0.95, dist); /* the coast line is at 0.5 */
    float foamn = vnoise(p * 6.0 + vec2(t * 0.5, -t * 0.4)) * 0.6 +
                  vnoise(p * 15.0 - vec2(t * 0.3, t * 0.6)) * 0.4;
    float foam  = edge * smoothstep(0.35, 0.75, foamn + 0.25 * sin(t * 1.3 + dist * 6.0));
    float foam2 = (1.0 - smoothstep(0.9, 1.7, dist)) * smoothstep(0.62, 0.85, foamn);
    c            = mix(c, vec3(0.92, 0.96, 1.0), clamp(foam * 0.8 + foam2 * 0.35, 0.0, 1.0) * (1.0 - calm));
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
    if (mat < 1.5)
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
    else if (mat < 2.5)
    {
        /*  Strata.  They are layers, so they are offsets of the surface
         *  the cut passes through: v_col.r is the mesh's own edge profile
         *  at this point, and every band lies parallel to it, thick and
         *  thin by turns by band, six tones dealt by band with a dark seam
         *  between each, a little grain in the seams.  Topsoil is the
         *  first hand. */
        float surf  = v_col.r * 0.5;
        float below = surf - zw;
        float warp  = (fbm(vec2(u * 2.0, below * 1.5)) - 0.5) * 0.02;
        float bc    = (below + warp) * 6.0;
        bc         += 0.40 * sin(bc * 0.9); /* thick and thin bands, by band */
        float k     = floor(bc);
        float f     = fract(bc);
        float h     = hash2(vec2(k, 3.0));
        vec3  tone  = h < 0.17 ? vec3(0.74, 0.60, 0.38)   /* ochre      */
                    : h < 0.34 ? vec3(0.40, 0.29, 0.20)   /* umber      */
                    : h < 0.51 ? vec3(0.62, 0.58, 0.50)   /* grey tan   */
                    : h < 0.68 ? vec3(0.57, 0.36, 0.26)   /* red-brown  */
                    : h < 0.85 ? vec3(0.80, 0.72, 0.54)   /* pale sand  */
                               : vec3(0.50, 0.44, 0.36);  /* dun        */
        float seam  = smoothstep(0.0, 0.08, f) * (1.0 - smoothstep(0.92, 1.0, f));
        c = tone * (0.72 + 0.28 * seam) *
            (0.90 + 0.20 * vnoise(vec2(u * 25.0, below * 40.0)));
        c = mix(vec3(0.33, 0.24, 0.15), c, smoothstep(0.04, 0.14, below)); /* topsoil */
    }
    else if (mat < 3.5)
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
    else if (mat > 4.5)
    {
        /*  The earth wedge under a network piece on a slope: bare earth
         *  with a fine grain, in the tone of the slope sprites' wedges. */
        float grain = 0.86 + 0.28 * vnoise(vec2(u * 28.0, zw * 44.0));
        c = vec3(0.44, 0.31, 0.18) * grain;
        float d = max(dot(n, normalize(fr.sun.xyz)), 0.0);
        return c * (fr.sun.w + fr.params.x * d);
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
    return c * (fr.sun.w + fr.params.x * d);
}

void main()
{
    vec3  n = normalize(v_nrm);
    float d = max(dot(n, normalize(fr.sun.xyz)), 0.0);
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
        vec2  f    = fract(v_wpos.xy);
        vec2  edge = min(f, 1.0 - f);
        vec2  gu   = vec2(dFdx(v_wpos.x), dFdy(v_wpos.x));
        vec2  gv   = vec2(dFdx(v_wpos.y), dFdy(v_wpos.y));
        vec2  px   = edge / max(vec2(length(gu), length(gv)), vec2(1e-4));
        float dist = min(px.x, px.y);
        if (dist >= fr.params.z)
            discard;
        o_col = vec4(vec3(155.0, 135.0, 71.0) / 255.0, v_col.a);
        return;
    }
    if (v_col.b > 15.5 && v_col.b < 16.5)
    {
        /*  A level crossing's surface: dark rubber panels across both
         *  tracks, seamed every panel width, over the asphalt. */
        vec2  px = fwidth(v_wpos.xy);
        vec2  fj = fract(v_wpos.xy * 8.0);
        vec3  c  = vec3(0.24, 0.23, 0.22) * (0.96 + 0.08 * vnoise(v_wpos.xy * 11.0));
        if (fj.x < px.x * 8.0 * 1.5 || fj.y < px.y * 8.0 * 1.5)
            c *= 0.78;
        o_col = vec4(clamp(c * (1.0 + 0.35 * ((fr.sun.w + fr.params.x * d) / max(fr.sun.w + fr.params.x * max(normalize(fr.sun.xyz).z, 0.0), 1e-3) - 1.0)), 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 14.5 && v_col.b < 15.5)
    {
        /*  A vehicle: a train car or a road car, a painted box.  col.r
         *  picks the paint: 0 the engine, 1 to 3 freight cars, 4 to 8
         *  road cars; the top a shade lighter than the sides, which the
         *  normal says. */
        int  paint = int(v_col.r + 0.5);
        vec3 c     = paint == 0 ? vec3(0.26, 0.28, 0.34)
                     : paint == 1 ? vec3(0.58, 0.30, 0.20)
                     : paint == 2 ? vec3(0.52, 0.54, 0.52)
                     : paint == 3 ? vec3(0.64, 0.50, 0.20)
                     : paint == 4 ? vec3(0.85, 0.85, 0.85)
                     : paint == 5 ? vec3(0.65, 0.12, 0.10)
                     : paint == 6 ? vec3(0.15, 0.25, 0.60)
                     : paint == 7 ? vec3(0.70, 0.70, 0.72)
                                  : vec3(0.15, 0.15, 0.17);
        float top  = clamp(v_nrm.z, 0.0, 1.0);
        c          = c * (0.72 + 0.45 * top);
        o_col      = vec4(clamp(c, 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 13.5 && v_col.b < 14.5)
    {
        /*  A rail across a road: the two rails alone, flush in the
         *  crossing surface, the ties and ballast under the asphalt. */
        float x  = abs(v_col.r);
        float u  = abs(x - 0.43);
        float px = fwidth(v_col.r);
        float rw = clamp(max(px * 0.55, 0.04), 0.04, 0.08);
        if (!(u > 0.155 - rw && u < 0.155 + rw))
            discard;
        o_col = vec4(vec3(0.78, 0.78, 0.80), v_col.a);
        return;
    }
    if (v_col.b > 12.5 && v_col.b < 13.5)
    {
        /*  A sidewalk: a road tile paved to its edges, concrete slabs
         *  scored every half tile both ways, under the pavement band. */
        vec2  jw  = min(fwidth(v_wpos.xy) * 0.6, vec2(0.06));
        vec2  fj  = fract(v_wpos.xy * 2.0);
        vec3  c   = vec3(0.60, 0.58, 0.54);
        float lit = (fr.sun.w + fr.params.x * d) /
                    max(fr.sun.w + fr.params.x * max(normalize(fr.sun.xyz).z, 0.0), 1e-3);
        /*  Paving keeps its colour on a slope, as the sprites' does: only
         *  a third of the shading follows the tilt. */
        lit = 1.0 + 0.35 * (lit - 1.0);
        if (fj.x < jw.x * 2.0 || fj.y < jw.y * 2.0)
            c *= 0.84;
        o_col = vec4(clamp(c * lit, 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 17.5 && v_col.b < 18.5)
    {
        /*  A viaduct's structure: plain cast concrete, no pattern at all
         *  (the user, three times: "I also asked you not to use the
         *  texture", "the engineered wall texture").  col.g set means
         *  the girder's face, which lies in the deck's own shadow. */
        vec3 c = vec3(0.62, 0.61, 0.59);
        if (v_col.g > 0.5)
            c *= 0.52;
        o_col = vec4(clamp(c * (fr.sun.w + fr.params.x * d), 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 11.5 && v_col.b < 12.5)
    {
        /*  A raised road's embankment: the coursed blocks of a wall. */
        o_col = vec4(clamp(wall_colour(1.0, n), 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 10.5 && v_col.b < 11.5)
    {
        /*  A railway, one track, sized against the road as the road is
         *  sized to the tile (spec 5.9's order, bottom up): ballast the
         *  band's width, ties across the middle three fifths, sixteen
         *  to the tile, and the two rails on top a third of the way out
         *  from the centre each side, a device pixel at least and never
         *  wider than a tenth of the band.  The user: "two separate
         *  lines for railways", and not four. */
        /*  The rails are the light element, their polished heads seen
         *  from above, on mid-brown ties over light ballast; dark rails
         *  on dark ties had merged into one line at the base zoom (the
         *  user: "I'm not seeing two rails").  Each rail is a device
         *  pixel at least. */
        /*  Two tracks (the user: "there needs to be two tracks"): their
         *  centres at 0.61 of the half band each side of the centreline,
         *  and on each, from its own centre, ties to 0.34 and the two
         *  rails at 0.22. */
        /*  Spec 6.1 at the road's scale, fifteen metres to the tile: track
         *  centres 4.0 m apart, gauge 1435 mm, ties 2.6 m long at 600 mm. */
        float x    = abs(v_col.r);
        float u    = abs(x - 0.43);
        float px   = fwidth(v_col.r);
        float rw   = clamp(max(px * 0.55, 0.04), 0.04, 0.08);
        float tie  = fract(v_col.g * 25.0);
        float tw   = fwidth(v_col.g) * 25.0;
        float tieA = smoothstep(0.55 + tw, 0.55 - tw, tie) * (1.0 - smoothstep(0.5, 1.5, tw));
        vec3  bal  = vec3(0.64, 0.61, 0.57) * (0.96 + 0.08 * vnoise(v_wpos.xy * 9.0));
        vec3  wood = vec3(0.42, 0.31, 0.21);
        vec3  rail = vec3(0.78, 0.78, 0.80);
        vec3  c    = bal;
        if (u < 0.28)
            c = mix(bal, wood, tieA);
        if (u > 0.155 - rw && u < 0.155 + rw)
            c = rail;
        o_col = vec4(clamp(c * (1.0 + 0.35 * ((fr.sun.w + fr.params.x * d) /
                                                   max(fr.sun.w + fr.params.x * max(normalize(fr.sun.xyz).z, 0.0), 1e-3) - 1.0)), 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 19.5)
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
        /*  The grid runs over the tint as it runs over the ground (the
         *  user: "grids aren't visible in map view over the zones and
         *  buildings"): the tint is a lid on the tile, so it has to
         *  carry the tile's edges or the map loses its grid exactly
         *  where it is most read. */
        if (fr.params.z > 0.0)
        {
            vec2  f    = fract(v_wpos.xy);
            vec2  edge = min(f, 1.0 - f);
            vec2  gu   = vec2(dFdx(v_wpos.x), dFdy(v_wpos.x));
            vec2  gv   = vec2(dFdx(v_wpos.y), dFdy(v_wpos.y));
            vec2  px   = edge / max(vec2(length(gu), length(gv)), vec2(1e-4));
            if (min(px.x, px.y) < fr.params.z)
                c *= 0.72;
        }
        o_col   = vec4(c, (zn >= 10 ? 0.78 : 0.45) * clamp(v_col.g, 0.0, 1.0));
        return;
    }
    if (v_col.b > 18.5)
    {
        /*  A freeway deck (spec 7.1), two carriageways either side of a
         *  median barrier, three lanes each: across the band, -1 to 1,
         *  the barrier to 0.06, a yellow edge line against it, white
         *  dashes between the lanes and a white edge line at the
         *  shoulder.  It had worn a surface road's class and drawn a
         *  double yellow line down the middle of the carriageway. */
        float x    = abs(v_col.r);
        float px   = fwidth(v_col.r);
        float line = clamp(px, 0.012f, 0.05f);
        float lit  = (fr.sun.w + fr.params.x * d) /
                    max(fr.sun.w + fr.params.x * max(normalize(fr.sun.xyz).z, 0.0), 1e-3);
        lit        = 1.0 + 0.35 * (lit - 1.0);
        /*  Flat asphalt, no grain (the user, twice: "I also asked you not
         *  to use the texture"): the markings are markings, the surface
         *  is one colour. */
        vec3 c     = vec3(143.0, 143.0, 143.0) / 255.0;
        bool dash  = fract(v_col.g * 4.0) < 0.5;
        if (x < 0.11)
            c = vec3(0.70, 0.69, 0.67); /* the median barrier, as the art has it: no yellow on the deck */
        else if (abs(x - 0.40) < 0.5 * line && dash)
            c = vec3(0.92);
        else if (abs(x - 0.70) < 0.5 * line && dash)
            c = vec3(0.92);
        else if (x > 0.94 - line && x < 0.94)
            c = vec3(0.92);
        o_col = vec4(clamp(c * lit, 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 6.5 && v_col.b < 7.5 || v_col.b > 9.5 && v_col.b < 10.5 || (v_col.b > 16.5 && v_col.b < 17.5))
    {
        /*  Approaching a level crossing (17): v_col.g is the distance
         *  to the crossing; the centre and lane lines are solid, and a
         *  large X is stencilled in each lane a tile out (spec 3.15). */
        bool  xapp = v_col.b > 16.5 && v_col.b < 17.5;
        /*  A road strip (v_col.b 7) or a crosswalk band (10).  Across the
         *  strip, -1 to 1 in v_col.r: the asphalt (the sprites' palette
         *  0x91) to 0.78, a curb, then a sidewalk of concrete with a
         *  joint every half tile (the user: "the roads are missing
         *  sidewalks, curbs, boulevards").  Down the crown, by class in
         *  v_curv: a road has the sprites' light dashes (0x8B), nine to
         *  the tile; an avenue a double yellow line with a dashed lane
         *  line each side; a boulevard a planted median between curbs.
         *  Every line is the art's width, one pixel at the base zoom, and
         *  never thinner than a device pixel (the user: "the stripes are
         *  much too wide").  v_col.g runs along the strip in tiles; a
         *  negative v_col.g is a junction, no dashes.  The crosswalk is
         *  white bars across the asphalt, six to the width, with the
         *  solid stop line at its outer end. */
        float x       = abs(v_col.r);
        float px      = fwidth(v_col.r);
        float line    = clamp(px, 0.02, 0.08); /* capped: fwidth swells in sliver triangles */
        float cb      = clamp(px, 0.04, 0.12);
        float cls     = mod(v_curv, 4.0); /* 4 and up flags a cut for the clipping check */
        /*  Lit so that a flat strip is exactly the sprite's colour: the
         *  sun's product on level ground divides out, and only a slope
         *  shades away from it. */
        float lit     = (fr.sun.w + fr.params.x * d) /
                        max(fr.sun.w + fr.params.x * max(normalize(fr.sun.xyz).z, 0.0), 1e-3);
        /*  Asphalt keeps its colour on a slope, as the sprites' slope
         *  pieces do: only a third of the shading follows the tilt, so a
         *  ramp is a road, not a black face. */
        lit           = 1.0 + 0.35 * (lit - 1.0);
        vec3  asphalt = vec3(143.0, 143.0, 143.0) / 255.0 * (0.97 + 0.06 * vnoise(v_wpos.xy * 7.0));
        vec3  dash    = vec3(227.0, 227.0, 227.0) / 255.0;
        vec3  yellow  = vec3(0.85, 0.72, 0.15);
        vec3  curb    = vec3(0.36, 0.36, 0.37);
        vec3  walk    = vec3(0.60, 0.58, 0.54);
        vec3  grass   = vec3(0.30, 0.50, 0.18);
        vec3  c       = asphalt;
        bool  dashed  = xapp ? true : (v_col.g >= 0.0 && fract(v_col.g * 9.0) < 0.5);
        /*  The sidewalk is the band's outer fifth each side, inside the
         *  0.71 the road tiles inscribe (spec 3.9): concrete slabs scored
         *  every half tile, behind a curb; beyond the band the tile's own
         *  ground shows, as the sprites' grass margins do. */
        float sk      = 0.80;
        if (x > sk)
        {
            float jw = min(fwidth(v_col.g) * 1.2, 0.08);
            c        = walk;
            if (fract(v_col.g * 2.0) < jw || x > sk + (1.0 - sk) * 0.5 - 0.5 * line && x < sk + (1.0 - sk) * 0.5 + 0.5 * line)
                c *= 0.84;
            if (x < sk + cb)
                c = curb;
        }
        else if (v_col.b > 9.5 && v_col.b < 10.5)
        {
            float t = v_col.g * 5.0; /* 0 at the junction, 1 at the band's outer end */
            if (t > 0.78 && t < 0.92)
                c = vec3(0.92);
            else if (t < 0.72 && fract(v_col.r * 3.0 + 0.5) < 0.5)
                c = vec3(0.92);
        }
        else if (cls > 2.5)
        {
            /*  A freeway deck, the spec's 7.1 bake: two tiles wide with
             *  three lanes each way.  Across the half deck, in its own
             *  14.9 m: half the median barrier 0.3, inner shoulder 0.6,
             *  three 3.7 m lanes, a 2.4 m outer shoulder and the 0.5 m
             *  parapet.  Normalised, that is every number below.
             *
             *  Yellow on the LEFT against the median and white on the
             *  right is the freeway cue -- it is the pair that says at a
             *  glance which way the traffic runs. */
            if (x < 0.020)
                c = vec3(0.72);                      /* median barrier   */
            else if (abs(x - 0.060) < 0.5 * line)
                c = yellow;                          /* left edge line   */
            else if (abs(x - 0.309) < 0.5 * line && dashed)
                c = dash;                            /* lane 1 / lane 2  */
            else if (abs(x - 0.557) < 0.5 * line && dashed)
                c = dash;                            /* lane 2 / lane 3  */
            else if (abs(x - 0.805) < 0.5 * line)
                c = dash;                            /* right edge line  */
            else if (x > 0.966)
                c = vec3(0.70);                      /* parapet          */
        }
        else if (cls > 1.5)
        {
            if (x < 0.14)
                c = grass;
            else if (x < 0.14 + cb)
                c = curb;
            else if (abs(x - 0.47) < 0.5 * line && dashed)
                c = dash;
        }
        else if (cls > 0.5)
        {
            if (x < 0.5 * line || (x > 0.035 && x < 0.035 + line))
                c = yellow;
            else if (abs(x - 0.42) < 0.5 * line && dashed)
                c = dash;
        }
        else if (x < 0.5 * line && dashed)
            c = dash;
        if (xapp && x < sk)
        {
            /* the X of the RXR stencil, one per lane, its arms a line wide, between 1.0 and 1.3 tiles out */
            float lx = (x - 0.42) / 0.30;       /* across the lane, -1..1 */
            float ly = (v_col.g - 1.15) / 0.15; /* along, -1..1 */
            float ww = max(line, 0.05) / 0.30;
            if (abs(lx) < 1.0 && abs(ly) < 1.0 && (abs(lx - ly) < ww || abs(lx + ly) < ww))
                c = vec3(0.92);
            /* the solid centre line, plain white on a local road */
            if (cls < 0.5 && x < 0.5 * line)
                c = dash;
        }
        /* the crown: the middle a shade lighter than the gutters */
        if (x <= sk)
            c *= 1.0 + 0.04 * (1.0 - x);
        o_col = vec4(clamp(c * lit, 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 8.5)
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
            o_col = vec4(vec3(0.92) * (fr.sun.w + fr.params.x * d), v_col.a);
            return;
        }
        if (code >= 6)
        {
            /*  A rail signal's steady aspect, 6 green and 7 red; a
             *  crossing's flasher pair, 8 and 9, alternating each second,
             *  or 12 dark; 10 a yellow sign face; 11 the gate arm's red
             *  and white stripes (spec 3.15, 5.6). */
            if (code == 10)
            {
                o_col = vec4(vec3(0.95, 0.78, 0.10) * (fr.sun.w + fr.params.x * d), v_col.a);
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
                o_col       = vec4((border ? vec3(0.95) : vec3(0.78, 0.08, 0.10)) * (fr.sun.w + fr.params.x * d), v_col.a);
                return;
            }
            if (code == 11)
            {
                float st = fract((v_wpos.x + v_wpos.y + v_wpos.z * 0.35) * 8.0);
                o_col    = vec4((st < 0.5 ? vec3(0.90, 0.12, 0.10) : vec3(0.95)) * (fr.sun.w + fr.params.x * d), v_col.a);
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
    if (v_col.b > 7.5)
    {
        /*  Street furniture: painted steel, lit flat. */
        o_col = vec4(vec3(0.22, 0.22, 0.24) * (fr.sun.w + fr.params.x * d), v_col.a);
        return;
    }
    if (v_col.b > 5.5)
    {
        o_col = vec4(clamp(water_surface(v_wpos.xy, fr.params.y, v_col.g), 0.0, 1.0), v_col.a);
        return;
    }
    if (v_col.b > 0.5)
    {
        /*  The water column is blended: its cover grows with the depth
         *  below the table, so the floor shows through the shallows and
         *  the deep is water. */
        float a = v_col.a;
        if (v_col.b > 2.5 && v_col.b < 3.5)
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
    /*  The ground reads its own topology.  Slope from the face, aspect
     *  from the face against the sun, altitude from the vertex, curvature
     *  from the height field, and the distance to water from the field
     *  texture: lush grass low, damp and in hollows; drier grass on ridges,
     *  sun-facing slopes and with altitude; earth and then rock as the
     *  slope steepens; sand on level ground at the waterline; a fine
     *  two-octave grain over all of it. */
    /*  The material reads the SMOOTHED surface, the height field's
     *  gradient carried per vertex, so it grades across the facets instead
     *  of stepping at each one; the flat face normal lights it. */
    vec3  ns     = normalize(vec3(-v_col.xy, 1.0));
    float slope  = clamp((1.0 - ns.z) * 3.0 * 0.7 + (1.0 - n.z) * 3.0 * 0.3, 0.0, 1.0);
    vec2  nh     = ns.xy;
    float aspect = length(nh) > 1e-3 ? max(dot(normalize(nh), normalize(fr.sun.xy)), 0.0) * clamp((1.0 - ns.z) * 3.0, 0.0, 1.0) : 0.0;
    float high   = smoothstep(8.0, 16.0, v_wpos.z);
    float hollow = smoothstep(0.0, 0.35, v_curv);
    float ridge  = smoothstep(0.0, 0.35, -v_curv);
    float towater = texture(t_field, p / 128.0).b * 16.0; /* texel centre = tile centre */
    float damp   = 1.0 - smoothstep(0.3, 3.0, towater);
    /* the land side of a tile edge is 0.5 in the field; the beach is the near half of the tile */
    float beach  = (1.0 - smoothstep(0.5, 1.0, towater)) * (1.0 - slope);
    float drift  = fbm(p * 0.35 + vec2(3.1, 7.7)) - 0.5;
    float grain  = 0.90 + 0.14 * (vnoise(p * 9.0) - 0.5) +
                   0.10 * (vnoise(p * 23.0 + vec2(5.0, 2.0)) - 0.5);

    vec3  lush   = vec3(0.30, 0.50, 0.18) + drift * vec3(0.08, 0.07, 0.02);
    vec3  dry    = vec3(0.52, 0.55, 0.26);
    vec3  earth  = vec3(0.50, 0.42, 0.24);
    vec3  rock   = vec3(0.47, 0.45, 0.41);
    vec3  sand   = vec3(0.76, 0.70, 0.50);
    vec3  wet    = vec3(0.24, 0.38, 0.15);
    float dryness = clamp(0.35 * high + 0.30 * ridge + 0.30 * aspect - 0.5 * damp - 0.4 * hollow, 0.0, 1.0);
    vec3  base   = mix(lush, dry, dryness);
    base         = mix(base, wet, damp * hollow * 0.5);
    base         = mix(base, earth, smoothstep(0.15, 0.55, slope));
    base         = mix(base, rock, clamp(smoothstep(0.55, 1.0, slope) + high * slope * 0.3, 0.0, 1.0));
    base         = mix(base, sand, beach);
    float occl   = 1.0 - 0.12 * hollow;
    vec3  c      = base * grain * occl * (fr.sun.w + fr.params.x * d);
    if (fr.params.z > 0.0)
    {
        /*  The grid the sprites carry: every ground diamond has a darker
         *  stair along its four edges, two pixels per row in the flat
         *  tile (index 107 on 104) and a subtler one on the slopes (103 on
         *  102).  The band is measured in screen pixels from the tile's
         *  edges in grid units, so it stays a pixel wide at every zoom. */
        vec2  f    = fract(v_wpos.xy);
        vec2  edge = min(f, 1.0 - f);
        /*  Perpendicular distance in screen pixels from the nearest edge,
         *  by the true gradient of each grid coordinate.  The sprites'
         *  stair is a one-pixel line -- two pixels per row is as thin as
         *  a 2:1 line gets -- and neighbouring tiles overpaint the same
         *  edge pixels, so the line is the pixels the edge passes through:
         *  half a pixel to either side of it. */
        vec2  gu   = vec2(dFdx(v_wpos.x), dFdy(v_wpos.x));
        vec2  gv   = vec2(dFdx(v_wpos.y), dFdy(v_wpos.y));
        vec2  px   = edge / max(vec2(length(gu), length(gv)), vec2(1e-4));
        float dist = min(px.x, px.y);
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
