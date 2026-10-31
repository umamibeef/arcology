/*  air.frag: the air between the camera and what the frame shows.  The
 *  pass reads the frame's color and depth and gives back the color as
 *  the camera sees it through the air: dimmed by the air in front of it,
 *  with the sunlight that the same air scatters toward the camera added.
 *
 *  Each pixel is a ray of the world.  The shader runs the camera's
 *  projection backwards from the pixel and its depth to the world point,
 *  and the frame gives the ray's direction and the camera's distance.  So
 *  the medium is integrated along a real ray at any camera.
 *
 *  The medium is the physically based atmosphere of Hillaire (2020): air
 *  that scatters by Rayleigh's law, and haze that scatters by Mie's, each
 *  thinning with height.  Two more media share the ray.  The smog is as
 *  thick as the city's pollution under it, and it absorbs more blue than
 *  red.  The clouds are a layer marched as Nubis marches one (Schneider,
 *  2015), lit by the sun through themselves (Hillaire, 2016).  Their place
 *  and size come from the sky's field (gpu/clouds.h), as Nubis takes them
 *  from its weather map.  They also shade the ground and the smog under
 *  them.  With the air off, the pass draws the clouds, their shadow and the occlusion alone.  Distances are in meters,
 *  and the sun's light is in the frame's own units, where a white surface
 *  that faces the sun is one. */
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_col;

layout(set = 2, binding = 0) uniform sampler2D t_col;
layout(set = 2, binding = 1) uniform sampler2D t_depth;
layout(set = 2, binding = 2) uniform sampler3D t_cloud;
layout(set = 2, binding = 3) uniform sampler2D t_smog;
layout(set = 2, binding = 4) uniform sampler2D t_sky; /* the sky's field, repeating (gpu/clouds.h) */
#if !defined(CLOUD_MARCH) && !defined(AO_PASS)
/*  The clouds, as the cloud pass marched them at a lower resolution: the
 *  light they send toward the camera, and in alpha what they let through. */
layout(set = 2, binding = 5) uniform sampler2D t_clouds;
/*  The sky's light that reaches each point past what stands round it, as
 *  the occlusion pass found it at a lower resolution (air_ao.frag), and
 *  in g the depth of the pixel it read. */
layout(set = 2, binding = 6) uniform sampler2D t_ao;
#endif
layout(set = 3, binding = 0) uniform Air
{
    vec4 proj;  /* the tile projection, as the camera's own */
    vec4 view;  /* scroll x, scroll y, canvas pixels to target pixels, level step in pixels */
    vec4 turn;  /* depth range, cos and sin of the turn, pitch in radians */
    vec4 grid;  /* pivot column, pivot row, meters a tile, meters a level */
    vec4 ray;   /* the ray's direction in meters, away from the camera, and the camera's distance from the pivot */
    vec4 sun;   /* the direction toward the sun, and its light */
    vec4 steps; /* the steps along each ray, the pivot's height in meters, 1 with the air on, the clouds' light without it */
    vec4 rayleigh; /* the air's scattering per meter at sea level, and the height at which it thins by e */
    vec4 mie;      /* the haze's scattering and extinction, the height at which it thins by e, its asymmetry */
    vec4 ozone;    /* the ozone's absorption, and the height of its peak */
    vec4 multi;    /* the light scattered more than once, for a sun of one (gpu/air.c) */
    vec4 sky;      /* the sky's light on a level surface for a sun of one, and the ground's albedo */
    vec4 shade;    /* the frame's own lighting: its ambient, and the sun's share on a level surface */
    vec4 cloud;    /* the clouds' base and top in meters, their coverage, their extinction per meter */
    vec4 cloudk;   /* the meters over which the shape and the detail noise repeat, 0, and how hard the detail wears the body */
    vec4 cloudp;   /* the forward and the back lobe's asymmetry, the back lobe's share, each octave's share of the one before */
    vec4 cloudq;   /* the octaves, the share of the sky's light at the bottom of the layer, how much of the clouds the zoom shows, the share of their shadow the ground takes */
    vec4 smog;     /* the smog's extinction per meter at full pollution, the height at which it thins by e, the map's tiles a side, the share it scatters */
    vec4 smogabs;  /* the smog's absorption, red, green and blue, as a share of its extinction, and its asymmetry */
    vec4 skyf;     /* the sky's field: the tiles its lowest octave spans, the field's mean and spread, and the tiles a texture of it spans */
    vec4 skyw;     /* how far the wind has carried the field's two layers, in tiles */
    vec4 skym;     /* the meters the clouds part over round the pointer, the map's side, and the tiles past it the clouds thin out over, from and to */
    vec4 skyc;     /* the pointer, in this pass's pixels, the share of the field past the clouds' cover their wisps reach, and the density's gain over the cut */
    vec4 lowres;   /* the frame's pixels to one of the cloud pass's, 1 while the clouds draw, the frame's pixels to one of the occlusion pass's, 1 while it runs */
    vec4 ao;       /* the occlusion's reach in tiles, its strength, the share of a tap's way it ignores, the frame's pixels a tile spans */
    vec4 daysun;   /* the sun's light at this hour against its light at the art's own hour, by color (frame.c, sun_clock) */
    vec4 dayamb;   /* the ambient light at this hour against the art's, by color, and the exposure */
    vec4 tint;     /* the art's color at this hour against its own, and 1 while it differs */
} air;

const float PI = 3.14159265;

/*  The point of the clouds' layer under the pointer, in meters, which the
 *  clouds part round.  main sets it before the march. */
vec2 g_part = vec2(-1e9);

/*  The world point under a pixel, in columns, rows and levels.  The
 *  inverse of terrain_body.vert's projection: the canvas from the pixel,
 *  then the column less the row from the canvas's x, then the sum and the
 *  height from its y and the depth together. */
vec3 world_at(vec2 frag, float d)
{
    float sp = sin(air.turn.w), cp = cos(air.turn.w);
    vec2  canvas = air.view.xy + frag / air.view.z;
    float u      = (canvas.x - air.proj.x) / air.proj.z;
    float A      = air.turn.x;
    float near   = (1.0 - d) * A - 0.375 * A;
    float a1 = air.proj.w * (sp / 0.5), b1 = -air.view.w * (cp / 0.8660254);
    float a2 = cp, b2 = 0.8660254 * sp;
    float c1 = canvas.y - air.proj.y, c2 = near - u * 0.0015;
    float det = a1 * b2 - a2 * b1;
    float s   = (c1 * b2 - b1 * c2) / det;
    float z   = (a1 * c2 - a2 * c1) / det;
    vec2  rr  = vec2(s - u, s + u) * 0.5 - air.grid.xy;
    vec2  cr  = vec2(rr.x * air.turn.y + rr.y * air.turn.z,
                     -rr.x * air.turn.z + rr.y * air.turn.y);
    return vec3(cr + air.grid.xy, z);
}

/*  How much of the air and the haze lies at a height, against sea level. */
vec2 density(float h)
{
    h = max(h, 0.0);
    return vec2(exp(-h / air.rayleigh.w), exp(-h / air.mie.z));
}

/*  The ozone's share at a height: a tent round its peak, fifteen
 *  kilometers to each side. */
float ozone(float h)
{
    return max(0.0, 1.0 - abs(h - air.ozone.w) / 15000.0);
}

/*  What reaches a height from the sun.  The air above a point is a flat
 *  column, so the path is the column over the sine of the sun's height.
 *  That holds while the sun stands well over the horizon. */
vec3 sun_through(float h)
{
    float up  = max(air.sun.z, 0.05);
    vec2  rho = density(h);
    vec3  tau = air.rayleigh.rgb * air.rayleigh.w * rho.x + vec3(air.mie.y * air.mie.z * rho.y) +
               air.ozone.rgb * 15000.0;
    /*  Under the horizon the sun reaches nothing. */
    return exp(-tau / up) * smoothstep(-0.01, 0.03, air.sun.z);
}

float phase_rayleigh(float mu)
{
    return 3.0 / (16.0 * PI) * (1.0 + mu * mu);
}

/*  Cornette and Shanks, the form Hillaire uses. */
float phase_mie(float mu)
{
    float g = air.mie.w, gg = g * g;
    return 3.0 / (8.0 * PI) * ((1.0 - gg) * (1.0 + mu * mu)) /
           ((2.0 + gg) * pow(1.0 + gg - 2.0 * g * mu, 1.5));
}

/*  Henyey and Greenstein's phase, for the smog and the clouds. */
float phase_hg(float mu, float g)
{
    float gg = g * g;
    return (1.0 - gg) / (4.0 * PI * pow(1.0 + gg - 2.0 * g * mu, 1.5));
}

float remap(float v, float lo, float hi, float nlo, float nhi)
{
    return nlo + (v - lo) * (nhi - nlo) / (hi - lo);
}

/*  How deep into a cloud the tile point `t` lies: 0 at the outer reach of
 *  its wisps and outside it, 1 where the field is highest.  The field is the mean of
 *  two layers of the sky's field, the second smaller, each carried by the
 *  wind its own way.  It is equalized, so a coverage of c puts that share
 *  of the map under cloud.  Its outline lies skyc.z of that share
 *  further out, where only thin wisps are, so the share of the map the
 *  clouds are seen to cover is the cover itself. */
float cloud_depth(vec2 t)
{
    float n = 0.5 * (texture(t_sky, (t - air.skyw.xy) / air.skyf.w).r +
                     texture(t_sky, (t - air.skyw.zw) / (0.6 * air.skyf.w) + vec2(0.37, 0.71)).r);
    float u = 1.0 / (1.0 + exp(-1.702 * (n - air.skyf.y) / air.skyf.z));
    float c = air.cloud.z * (1.0 + air.skyc.z);
    return clamp((u - (1.0 - c)) / max(c, 1e-4), 0.0, 1.0);
}

/*  The noise volume read so it never shows its repeat.  It is read twice,
 *  at the scale `m` meters and at 1.618 times it, each turned by an angle
 *  that lines up with nothing.  The two repeats then never meet within
 *  any view.  `c` is the channel, as a mask. */
float cloud_noise(vec3 w, float m, vec4 c)
{
    const mat2 R0 = mat2(0.848, 0.530, -0.530, 0.848);  /* 32 degrees */
    const mat2 R1 = mat2(0.956, -0.292, 0.292, 0.956);  /* -17 degrees */
    vec3       a  = vec3(R0 * w.xy, w.z) / m;
    vec3       b  = vec3(R1 * w.xy, w.z + 71.0) / (1.618 * m) + vec3(0.31, 0.57, 0.13);
    return dot(mix(texture(t_cloud, a), texture(t_cloud, b), 0.4), c);
}

/*  How thick the cloud is at a point, per meter, as Nubis shapes one
 *  (Schneider, 2015).  The sky's field says where the clouds are and how
 *  far into one a point lies.  The shape noise gives each cloud its body
 *  in three dimensions.  It is cut at one less that depth, so out at a
 *  cloud's reach only its strongest lumps are left: the wisps.  Their
 *  density grows with how far the shape clears the cut, so they fade out
 *  as they reach.  The profile
 *  rounds the base and the top, and the cloud stands taller toward its
 *  middle.  The detail wears the body.  Low in the cloud it is turned
 *  over, so the base frays and the top billows.  The clouds thin out past
 *  the map's edge, and with `detail` they keep clear of the point under
 *  the pointer. */
float cloud_at(vec3 q, bool detail)
{
    float h = (q.z - air.cloud.x) / (air.cloud.y - air.cloud.x);
    if (h <= 0.0 || h >= 1.0)
        return 0.0;
    vec2  t   = q.xy / air.grid.z;
    /*  Near the pointer a point lies less deep in its cloud, so the cloud
     *  gives way where its own shape is thinnest: a gap that follows the
     *  lumps, not a circle. */
    float cov = cloud_depth(t) * (detail ? smoothstep(0.3 * air.skym.x, air.skym.x, distance(q.xy, g_part)) : 1.0);
    if (cov <= 0.0)
        return 0.0;
    float top     = 0.35 + 0.65 * cov;
    float profile = smoothstep(0.0, 0.1, h) * (1.0 - smoothstep(top - 0.3, top, h));
    /*  The body travels with its cloud: the noise is carried by the wind
     *  that carries the field's first layer, in meters. */
    vec3  w       = vec3(q.xy - air.skyw.xy * air.grid.z, q.z);
    float shape   = cloud_noise(w, air.cloudk.x, vec4(1.0, 0.0, 0.0, 0.0)) * profile;
    /*  The density is how far the shape rises over the cut, by the gain
     *  skyc.w, squared.  A cloud is seen as opaque once its
     *  density is a few hundredths of the whole, so the density has to
     *  fall far faster than the distance for the cloud to fade evenly as
     *  it reaches.  A lump that only just clears the cut is a haze. */
    float d       = pow(clamp((shape - (1.0 - cov)) * air.skyc.w, 0.0, 1.0), 2.0);
    vec2  past    = max(max(-t, t - air.skym.y), 0.0);
    d *= 1.0 - smoothstep(air.skym.z, air.skym.w, length(past));
    if (detail && d > 0.0)
    {
        float det = cloud_noise(w, air.cloudk.y, vec4(0.0, 0.0, 1.0, 0.0));
        det       = mix(1.0 - det, det, clamp(h * 4.0, 0.0, 1.0));
        d         = clamp(remap(d, det * air.cloudk.w, 1.0, 0.0, 1.0), 0.0, 1.0);
    }
    return d * air.cloud.w;
}

/*  What the clouds let through from the sun to a point under them. */
float cloud_shadow(vec3 p)
{
    float up = max(air.sun.z, 0.05);
    float s0 = max((air.cloud.x - p.z) / up, 0.0), s1 = max((air.cloud.y - p.z) / up, 0.0);
    float ds = (s1 - s0) / 4.0, tau = 0.0;
    for (int i = 0; i < 4; ++i)
        tau += cloud_at(p + air.sun.xyz * (s0 + (float(i) + 0.5) * ds), false) * ds;
    return exp(-tau);
}

/*  The color the smog's particles scatter: a dirty yellow. */
const vec3 SMOG_TINT = vec3(1.0, 0.9, 0.3);

/*  The light a thick haze scatters many times, over what one scattering gives. */
const float SMOG_MULTI = 3.0;

/*  The pollution over a point `q`, nought to one, and nought off the
 *  map.  It thins with the height over `ground`, the ground under the
 *  ray. */
float smog_at(vec3 q, float ground)
{
    vec2 uv = q.xy / air.grid.z / air.smog.z;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 0.0;
    /*  Light pollution is the air's own haze.  The smog shows where the
     *  pollution is heavy, and it thins with height. */
    return smoothstep(0.2, 0.7, texture(t_smog, uv).r) * exp(-max(q.z - ground, 0.0) / air.smog.y);
}

/*  The clouds along a ray, from `o` along `dir` for `len`, where it
 *  crosses their layer: the light they send toward the camera, and what
 *  they let through of what lies behind them.  `show` fades them as the
 *  view closes in: what they send and what they hide both, so a dense
 *  cloud fades as evenly as a thin one. */
vec4 clouds_on(vec3 o, vec3 dir, float len, float show, float mu, vec2 frag)
{
    float dz = min(dir.z, -1e-4);
    float t0 = max((air.cloud.y - o.z) / dz, 0.0), t1 = min((air.cloud.x - o.z) / dz, len);
    vec3  Lc = vec3(0.0);
    float Tc = 1.0;
    if (t1 <= t0 || show <= 0.0)
        return vec4(Lc, Tc);
    /*  The steps start a fraction of a step in, a different one at each
     *  pixel, so the layer shows no bands.  The fraction is a hash of the
     *  pixel, with no pattern of its own to show. */
    uvec2 hp  = uvec2(frag);
    uint  hh  = hp.x * 1664525u + hp.y * 1013904223u;
    hh        = (hh ^ (hh >> 16)) * 0x7feb352du;
    hh        = (hh ^ (hh >> 15)) * 0x846ca68bu;
    float jit = float((hh ^ (hh >> 16)) & 0xffffffu) / 16777216.0;
    float cdt = (t1 - t0) / 32.0;
    float up  = max(air.sun.z, 0.05);
    /*  The hour's sun color carries the exposure once (dayamb.w), and the
     *  sun's strength carries it too, so it is taken out of that.  The
     *  sky's light is already the hour's own (air_multiple). */
    float sw  = air.sun.w / max(air.dayamb.w, 1e-3);
    /*  The light from every way: the sky's from above, the ground's from
     *  below, each as bright in every way. */
    vec3 amb = air.sun.w * (0.5 * air.sky.rgb + 0.5 * air.sky.w * (air.shade.y * up + air.sky.rgb)) / PI;
    for (int i = 0; i < 32 && Tc > 0.01; ++i)
    {
        vec3  q = o + dir * (t0 + (float(i) + jit) * cdt);
        float s = cloud_at(q, true);
        if (s <= 0.0)
            continue;
        float tl = 0.0, ls = (air.cloud.y - q.z) / up / 3.0;
        for (int k = 0; k < 3; ++k)
            tl += cloud_at(q + air.sun.xyz * ls * (float(k) + 0.5), false) * ls;
        /*  The light scattered many times inside the cloud, as octaves
         *  (Wrenninge, 2013).  Each has a share of the scattering and the
         *  same share of the extinction of the one before, so no octave
         *  scatters more than it takes away.  Its phase spreads by half, so
         *  the light inside grows even.  The phase is two lobes, forward
         *  and back (Hillaire, 2016). */
        float oct = 0.0, share = 1.0, spread = 1.0;
        for (int k = 0; k < int(air.cloudq.x); ++k)
        {
            oct += share * mix(phase_hg(mu, air.cloudp.x * spread), phase_hg(mu, air.cloudp.y * spread), air.cloudp.z) * exp(-tl * share);
            share *= air.cloudp.w;
            spread *= 0.5;
        }
        float h  = (q.z - air.cloud.x) / (air.cloud.y - air.cloud.x);
        /*  The sun on the cloud is the hour's sun (frame.c, sun_clock), by
         *  steps.w, which stands for the light the octaves leave out of a
         *  thick cloud.  So a cloud is white at the fitted hour, warm as the
         *  sun sinks and dark at night, with the air on or off. */
        vec3  sl = air.steps.w * air.daysun.rgb;
        vec3  Sc = s * (sw * sl * oct + amb * mix(air.cloudq.y, 1.0, h));
        float tx = exp(-s * cdt);
        Lc += Tc * (Sc - Sc * tx) / s;
        Tc *= tx;
    }
    return vec4(Lc * show, 1.0 - show * (1.0 - Tc));
}

/*  The palette index the mesh writes in alpha, as terrain.vert's
 *  LAND_INDEX.  Only the mesh takes the occlusion: the art carries its
 *  own shading. */
const float LAND_A = 105.0 / 255.0;

/*  A pixel's point in the normals' metric: columns, rows, and a level as
 *  half a tile. */
vec3 tile_at(ivec2 q)
{
    vec3 w = world_at(vec2(q) + 0.5, texelFetch(t_depth, q, 0).r);
    return vec3(w.xy, w.z * 0.5);
}

#ifdef AO_PASS
/*  THE OCCLUSION, as Alchemy finds it (McGuire et al., 2011).  Taps on
 *  a spiral round the pixel, out to ao.x tiles, each a point of the
 *  frame from its depth.  A tap above the pixel's plane hides part of
 *  the sky from it, the more the nearer and the steeper it stands.
 *  Whatever drew depth hides the sky: the ground, a deck, a column, a
 *  building's art.  The normal comes from the frame's own depth, from the
 *  nearer neighbor on each axis, so an edge does not bend it. */
float occlusion(ivec2 q, ivec2 at)
{
    vec4 c = texelFetch(t_col, q, 0);
    float d = texelFetch(t_depth, q, 0).r;
    float rpx = air.ao.x * air.ao.w;
    if (d >= 1.0 || abs(c.a - LAND_A) > 0.5 / 255.0 || rpx < 3.0)
        return 1.0;
    vec3 P  = tile_at(q);
    vec3 ex = tile_at(q + ivec2(1, 0)) - P, wx = P - tile_at(q - ivec2(1, 0));
    vec3 ey = tile_at(q + ivec2(0, 1)) - P, wy = P - tile_at(q - ivec2(0, 1));
    vec3 N  = normalize(cross(dot(ex, ex) < dot(wx, wx) ? ex : wx, dot(ey, ey) < dot(wy, wy) ? ey : wy));
    if (N.z < 0.0)
        N = -N;
    /*  Each pixel of a four by four block of this pass turns the spiral
     *  by its own angle and starts it at its own step.  So the sixteen
     *  together cover the disc evenly, and the filter up, four by four
     *  as well, evens them out. */
    int   k    = ((at.x & 3) << 2) | (at.y & 3);
    float turn = 6.2831853 * fract(float(k) * 0.6180340);
    float jit  = fract(float(k) * 0.7548777);
    float sum  = 0.0;
    const int TAPS = 16;
    for (int i = 0; i < TAPS; ++i)
    {
        float f  = (float(i) + jit) / float(TAPS);
        float a  = turn + f * 15.707963; /* two and a half turns out to the reach */
        ivec2 o  = ivec2(vec2(cos(a), sin(a)) * rpx * f);
        ivec2 t  = clamp(q + o, ivec2(0), textureSize(t_depth, 0) - 1);
        if (texelFetch(t_depth, t, 0).r >= 1.0)
            continue;
        vec3  v  = tile_at(t) - P;
        float vv = dot(v, v);
        float r2 = air.ao.x * air.ao.x;
        sum += max(dot(v, N) - air.ao.z * sqrt(vv), 0.0) / (vv + 0.01 * r2) * max(1.0 - vv / r2, 0.0);
    }
    return clamp(1.0 - air.ao.y * 2.0 * air.ao.x * sum / float(TAPS), 0.0, 1.0);
}
#endif

#if !defined(CLOUD_MARCH) && !defined(AO_PASS)
/*  The occlusion pass's answer at this pixel: the four by four block of
 *  its pixels round this one, each counted by how near it lies and by how
 *  near its own depth is to this pixel's.  So the block's sixteen turns
 *  of the spiral even out, and the occlusion of a deck does not bleed
 *  onto the ground beside it. */
float occlusion_here(vec2 frag, float d)
{
    if (air.lowres.w < 0.5)
        return 1.0;
    vec2  g   = frag / air.lowres.z - 0.5;
    ivec2 b   = ivec2(floor(g)) - 1;
    ivec2 n   = textureSize(t_ao, 0) - 1;
    float sum = 0.0, wsum = 1e-6;
    for (int k = 0; k < 16; ++k)
    {
        ivec2 t  = clamp(b + ivec2(k & 3, k >> 2), ivec2(0), n);
        vec2  a  = texelFetch(t_ao, t, 0).rg;
        vec2  dv = vec2(t) - g;
        float w  = exp(-0.35 * dot(dv, dv)) / (1e-3 + abs(a.y - d) * 4000.0);
        sum     += w * a.x;
        wsum    += w;
    }
    return sum / wsum;
}

void shade_frame()
{
    vec2 frag = gl_FragCoord.xy;
    vec3 dir  = air.ray.xyz;
    float mu  = dot(dir, air.sun.xyz);
    vec3 piv  = vec3(air.grid.xy * air.grid.z, air.steps.y);
    vec3 cam0 = piv - dir * air.ray.w;
    /*  The clouds as the cloud pass marched them, filtered up to this
     *  pixel.  Their ray does not read the frame, so a coarser march of
     *  them needs no care at the edges of what lies under them.  The
     *  cloud pass's pixel i covers this pass's pixels lowres.x i on. */
    vec4 cl = air.lowres.y > 0.5 ? texture(t_clouds, frag / (air.lowres.x * vec2(textureSize(t_clouds, 0)))) : vec4(0.0, 0.0, 0.0, 1.0);
    vec4  c  = texelFetch(t_col, ivec2(frag), 0);
    float d  = texelFetch(t_depth, ivec2(frag), 0).r;
    /*  The art carries the light of its own hour, so at another hour it
     *  takes the tint of that hour's light.  The mesh is lit by the hour
     *  itself. */
    if (air.tint.w > 0.5 && d < 1.0 && abs(c.a - LAND_A) > 0.5 / 255.0)
        c.rgb *= air.tint.rgb;

    if (d >= 1.0)
    {
        /*  The background lies under no surface: only the clouds lie over
         *  it. */
        o_col = vec4(cl.rgb + cl.a * c.rgb, c.a);
        return;
    }

    vec3  w   = world_at(frag, d);
    vec3  p   = vec3(w.xy * air.grid.z, w.z * air.grid.w);
    float len = max(dot(p - cam0, dir), 0.0);
    float pr = phase_rayleigh(mu), pm = phase_mie(mu), ps = phase_hg(mu, air.smogabs.w);
    /*  The clouds' shadow on the point takes the sun's share of the
     *  frame's light away.  The occlusion takes the sky's share away. */
    float sh  = air.lowres.y > 0.5 ? mix(1.0, cloud_shadow(p), air.cloudq.w) : 1.0;
    float ao  = occlusion_here(frag, d);
    float lit = air.shade.x + air.shade.y;
    c.rgb *= (air.shade.x * ao + air.shade.y * sh) / lit;
    /*  With the air off there is no air to see through: the clouds and their
     *  shadow alone. */
    if (air.steps.z < 0.5)
    {
        o_col = vec4(cl.rgb + cl.a * c.rgb, c.a);
        return;
    }
    /*  THE SMOG lies low over the polluted tiles, so it is marched on its
     *  own, in eight steps through the part of the ray under its top, out
     *  from the point toward the camera.  The light from the point passes
     *  it first and the air above it after. */
    {
        float top = p.z + 4.0 * air.smog.y;
        float lsm = min(len, max(top - p.z, 0.0) / max(-dir.z, 0.05));
        if (air.smog.x > 0.0 && lsm > 0.0)
        {
            vec3  Tm = vec3(1.0), Lm = vec3(0.0);
            float ds = lsm / 8.0;
            for (int k = 0; k < 8; ++k)
            {
                vec3  q   = p - dir * ((float(k) + 0.5) * ds);
                float pol = smog_at(q, p.z) * air.smog.x;
                if (pol <= 0.0)
                    continue;
                float ss  = pol * air.smog.w;
                vec3  ext = vec3(ss) + pol * air.smogabs.rgb;
                vec3  Ts  = sun_through(q.z) * sh;
                /*  The haze is lit by the sun and by the whole sky over it.
                 *  Light scatters many times inside a thick haze, as in a
                 *  cloud, so SMOG_MULTI stands for the light a single
                 *  scattering leaves out. */
                vec3  S   = air.sun.w * ss * SMOG_TINT * SMOG_MULTI * (Ts * ps + air.sky.rgb / (4.0 * PI));
                vec3  Tx  = exp(-ext * ds);
                Lm        = Lm * Tx + (S - S * Tx) / max(ext, vec3(1e-12));
                Tm *= Tx;
            }
            c.rgb = c.rgb * Tm + Lm;
        }
    }
    int   n  = int(air.steps.x);
    float dt = len / float(n);
    vec3  T  = vec3(1.0);
    vec3  L  = vec3(0.0);
    for (int i = 0; i < n; ++i)
    {
        float t   = (float(i) + 0.5) * dt;
        vec3  q   = p - dir * (len - t);
        vec2  rho = density(q.z);
        vec3  sr  = air.rayleigh.rgb * rho.x;
        float sm  = air.mie.x * rho.y;
        vec3  ext = sr + vec3(air.mie.y * rho.y) + air.ozone.rgb * ozone(q.z);
        /*  The sun under the clouds is the sun through them.  The air and
         *  the smog lie low, so the shadow on the point holds for them. */
        vec3 Ts = sun_through(q.z) * (q.z < air.cloud.x ? sh : 1.0);
        /*  The light scattered toward the camera: the sun's, once, by
         *  each phase, and the light that the air has scattered many
         *  times, which comes from every way at once. */
        vec3 S  = air.sun.w * (Ts * (sr * pr + sm * pm) + (sr + sm) * air.multi.rgb);
        vec3 Tx = exp(-ext * dt);
        /*  Hillaire's integral over the step, exact for a step of one
         *  density. */
        L += T * (S - S * Tx) / max(ext, vec3(1e-12));
        T *= Tx;
    }
    vec3 seen = c.rgb * T + L;
    o_col     = vec4(cl.rgb + cl.a * seen, c.a);
}
#endif

void main()
{
#ifdef AO_PASS
    /*  One pixel of the occlusion pass covers lowres.z of the frame's a
     *  side, and reads the frame's pixel at its middle. */
    ivec2 q = clamp(ivec2((floor(gl_FragCoord.xy) + 0.5) * air.lowres.z), ivec2(0), textureSize(t_depth, 0) - 1);
    o_col   = vec4(occlusion(q, ivec2(gl_FragCoord.xy)), texelFetch(t_depth, q, 0).r, 0.0, 1.0);
    return;
#else
#ifdef CLOUD_MARCH
    /*  One pixel of the cloud pass covers lowres.x of the frame's pixels
     *  a side.  Its ray goes through its middle. */
    vec2 frag = (floor(gl_FragCoord.xy) + 0.5) * air.lowres.x;
#else
    vec2 frag = gl_FragCoord.xy;
#endif
    vec3  dir  = air.ray.xyz;
    float mu   = dot(dir, air.sun.xyz);
    /*  The camera stands its distance back from the pivot along the ray,
     *  and every pixel's ray starts on the plane through it, square to the
     *  ray.  The pivot's height is where the camera looks. */
    vec3 piv  = vec3(air.grid.xy * air.grid.z, air.steps.y);
    vec3 cam0 = piv - dir * air.ray.w;
    /*  The clouds clear as the view closes in.  Their shadows stay. */
    float show = air.cloudq.z;
#ifdef CLOUD_MARCH
    /*  The pixel's ray through the clouds starts on the camera's plane, at
     *  a place that the pixel alone gives.  So nothing under the pixel
     *  moves its clouds, not the land as it settles and not the edge of
     *  the world.  The clouds lie over all of the land, so the ray goes on
     *  to their base. */
    vec3 w0 = world_at(frag, 0.5);
    vec3 p0 = vec3(w0.xy * air.grid.z, w0.z * air.grid.w);
    /*  The clouds part round the pointer.  Its pixel's ray is cast as every
     *  pixel's is, and where it crosses the middle of the layer is the
     *  point they thin toward (cloud_at). */
    {
        vec3  c0 = world_at(air.skyc.xy, 0.5);
        vec3  cp = vec3(c0.xy * air.grid.z, c0.z * air.grid.w);
        vec3  co = cp - dir * dot(cp - cam0, dir);
        float zc = 0.5 * (air.cloud.x + air.cloud.y);
        g_part   = (co + dir * ((zc - co.z) / min(dir.z, -1e-4))).xy;
    }
#endif
#ifdef CLOUD_MARCH
    o_col = clouds_on(p0 - dir * dot(p0 - cam0, dir), dir, 1e9, show, mu, frag);
#else
    shade_frame();
#endif
#endif
}
