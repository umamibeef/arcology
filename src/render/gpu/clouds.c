/*  gpu/clouds.c: the noise that the clouds are shaped from.
 *
 *  Each noise repeats over the volume.  Perlin noise wraps its lattice
 *  at its period.  Worley noise wraps its cells, so a feature point near
 *  one face of the volume is also near the opposite face.  So the
 *  volume tiles with no seam at every frequency. */
#include "gpu/clouds.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*  A hash of a lattice point, the same for every point that wraps to it. */
static uint32_t clouds_hash(int x, int y, int z, int period, uint32_t seed)
{
    uint32_t h;
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    z = ((z % period) + period) % period;
    h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ seed * 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

static float clouds_unit(uint32_t h)
{
    return (float)(h & 0xffffffu) / 16777216.0f;
}

static float clouds_fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/*  One of the twelve edge directions of a cube, by the hash. */
static float clouds_grad(uint32_t h, float x, float y, float z)
{
    switch (h % 12u)
    {
        case 0:
            return x + y;
        case 1:
            return -x + y;
        case 2:
            return x - y;
        case 3:
            return -x - y;
        case 4:
            return x + z;
        case 5:
            return -x + z;
        case 6:
            return x - z;
        case 7:
            return -x - z;
        case 8:
            return y + z;
        case 9:
            return -y + z;
        case 10:
            return y - z;
        default:
            return -y - z;
    }
}

/*  Perlin noise at a point of the unit volume, `period` cells a side,
 *  near nought on average and within one. */
static float clouds_perlin(float px, float py, float pz, int period, uint32_t seed)
{
    float x = px * (float)period, y = py * (float)period, z = pz * (float)period;
    int   ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - (float)ix, fy = y - (float)iy, fz = z - (float)iz;
    float u = clouds_fade(fx), v = clouds_fade(fy), w = clouds_fade(fz);
    float c[8];
    int   k;
    for (k = 0; k < 8; k++)
    {
        int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
        c[k] = clouds_grad(clouds_hash(ix + dx, iy + dy, iz + dz, period, seed), fx - (float)dx, fy - (float)dy, fz - (float)dz);
    }
    {
        float x00 = c[0] + u * (c[1] - c[0]), x10 = c[2] + u * (c[3] - c[2]);
        float x01 = c[4] + u * (c[5] - c[4]), x11 = c[6] + u * (c[7] - c[6]);
        float y0 = x00 + v * (x10 - x00), y1 = x01 + v * (x11 - x01);
        return y0 + w * (y1 - y0);
    }
}

/*  Worley noise at a point of the unit volume, `cells` a side: one less
 *  the distance to the nearest feature point, in cells.  So it is one at
 *  a feature point and falls toward nought between them. */
static float clouds_worley(float px, float py, float pz, int cells, uint32_t seed)
{
    float x = px * (float)cells, y = py * (float)cells, z = pz * (float)cells;
    int   ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float best = 9.0f;
    int   dx, dy, dz;
    for (dz = -1; dz <= 1; dz++)
        for (dy = -1; dy <= 1; dy++)
            for (dx = -1; dx <= 1; dx++)
            {
                uint32_t h  = clouds_hash(ix + dx, iy + dy, iz + dz, cells, seed);
                float    fx = (float)(ix + dx) + clouds_unit(h) - x;
                float    fy = (float)(iy + dy) + clouds_unit(h * 747796405u + 1u) - y;
                float    fz = (float)(iz + dz) + clouds_unit(h * 2891336453u + 7u) - z;
                float    d  = fx * fx + fy * fy + fz * fz;
                best        = d < best ? d : best;
            }
    best = 1.0f - sqrtf(best);
    return best > 0.0f ? best : 0.0f;
}

/*  Three octaves of Worley noise, each twice the frequency of the one
 *  before, in the weights the Nubis clouds use. */
static float clouds_worley3(float x, float y, float z, int cells, uint32_t seed)
{
    return 0.625f * clouds_worley(x, y, z, cells, seed) + 0.25f * clouds_worley(x, y, z, cells * 2, seed + 1u) +
           0.125f * clouds_worley(x, y, z, cells * 4, seed + 2u);
}

/*  Octaves of Perlin noise, from `period`, put into nought to one. */
static float clouds_perlin_fbm(float x, float y, float z, int period, int octaves, uint32_t seed)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int   o;
    for (o = 0; o < octaves; o++)
    {
        sum += amp * clouds_perlin(x, y, z, period << o, seed + (uint32_t)o);
        norm += amp;
        amp *= 0.5f;
    }
    sum = sum / norm * 0.5f + 0.5f;
    return sum < 0.0f ? 0.0f : sum > 1.0f ? 1.0f
                                          : sum;
}

static uint8_t clouds_byte(float v)
{
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f
                                   : v;
    return (uint8_t)(v * 255.0f + 0.5f);
}

/*  Spread a channel evenly over nought to one: each value becomes the
 *  share of the volume that lies below it.  So a cut at one less the
 *  coverage keeps that share of the volume, whatever shape the noise's
 *  own spread has. */
#define CLOUDS_BINS 4096
static void clouds_even(uint8_t *rgba, const float *v, int channel, size_t count)
{
    static uint32_t hist[CLOUDS_BINS];
    float           below[CLOUDS_BINS];
    uint32_t        run = 0;
    size_t          i;
    int             b;
    memset(hist, 0, sizeof hist);
    for (i = 0; i < count; i++)
        hist[(int)(v[i] * (CLOUDS_BINS - 1))]++;
    for (b = 0; b < CLOUDS_BINS; b++)
    {
        below[b] = ((float)run + 0.5f * (float)hist[b]) / (float)count;
        run += hist[b];
    }
    for (i = 0; i < count; i++)
        rgba[4 * i + (size_t)channel] = clouds_byte(below[(int)(v[i] * (CLOUDS_BINS - 1))]);
}

void clouds_noise(uint8_t *rgba)
{
    const int    n     = CLOUD_NOISE_N;
    const size_t count = (size_t)n * (size_t)n * (size_t)n;
    float       *shape = (float *)malloc(count * sizeof *shape);
    float       *wx    = (float *)malloc(count * sizeof *wx);
    int          x, y, z;
    if (!shape || !wx)
    {
        free(shape);
        free(wx);
        memset(rgba, 0, count * 4u);
        return;
    }
    for (z = 0; z < n; z++)
        for (y = 0; y < n; y++)
            for (x = 0; x < n; x++)
            {
                float  px = ((float)x + 0.5f) / (float)n, py = ((float)y + 0.5f) / (float)n, pz = ((float)z + 0.5f) / (float)n;
                float  perlin = clouds_perlin_fbm(px, py, pz, 4, 3, 11u);
                float  floor_ = clouds_worley3(px, py, pz, 4, 21u);
                size_t i      = ((size_t)z * (size_t)n + (size_t)y) * (size_t)n + (size_t)x;
                /*  Perlin noise worn by Worley noise: the Worley noise
                 *  is the floor that the Perlin noise lifts toward one. */
                shape[i]        = floor_ + perlin * (1.0f - floor_);
                wx[i]           = clouds_perlin_fbm(px, py, pz, 2, 2, 51u);
                rgba[4 * i + 1] = clouds_byte(clouds_worley3(px, py, pz, 2, 31u));
                rgba[4 * i + 2] = clouds_byte(clouds_worley3(px, py, pz, 8, 41u));
            }
    clouds_even(rgba, shape, 0, count);
    clouds_even(rgba, wx, 3, count);
    free(shape);
    free(wx);
}

/*  The value of the lattice point (x, y) of an octave of `period` cells,
 *  the same for every point that wraps to it. */
static float field_lattice(int x, int y, int period)
{
    return clouds_unit(clouds_hash(x, y, 0, period, 0x5ce7u + (uint32_t)period));
}

/*  Value noise at (x, y) in cells, smoothed between the four lattice
 *  points round it, wrapping at `period` cells. */
static float field_value(float x, float y, int period)
{
    int   ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy, a, b, c, d;
    fx = fx * fx * (3.0f - 2.0f * fx), fy = fy * fy * (3.0f - 2.0f * fy);
    a = field_lattice(ix, iy, period), b = field_lattice(ix + 1, iy, period);
    c = field_lattice(ix, iy + 1, period), d = field_lattice(ix + 1, iy + 1, period);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

/*  The field at the texel (x, y), wrapping, filtered as a sampler does. */
static float field_at(const float *v, float x, float y)
{
    int   n = SKY_FIELD_N, ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    int   x0 = ((ix % n) + n) % n, y0 = ((iy % n) + n) % n, x1 = (x0 + 1) % n, y1 = (y0 + 1) % n;
    float a = v[y0 * n + x0], b = v[y0 * n + x1], c = v[y1 * n + x0], d = v[y1 * n + x1];
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

void clouds_field(float *v, float stats[2])
{
    int      n   = SKY_FIELD_N, x, y, o, k;
    double   sum = 0.0, sq = 0.0;
    uint32_t r = 12345u;
    for (y = 0; y < n; ++y)
        for (x = 0; x < n; ++x)
        {
            float s = 0.0f, a = 0.5f;
            for (o = 0; o < 4; ++o)
            {
                int period = SKY_FIELD_CELLS << o;
                s += a * field_value((float)x * (float)period / (float)n, (float)y * (float)period / (float)n, period);
                a *= 0.5f;
            }
            v[y * n + x] = s;
        }
    /*  The clouds take the mean of the field at one scale and at 1/0.6 of
     *  it (air.frag).  Its mean and spread, from points spread over the
     *  plane. */
    for (k = 0; k < 40000; ++k)
    {
        float px, py, m;
        r = r * 1664525u + 1013904223u, px = (float)(r >> 8) / 65536.0f;
        r = r * 1664525u + 1013904223u, py = (float)(r >> 8) / 65536.0f;
        m = 0.5f * (field_at(v, px, py) + field_at(v, px / 0.6f + 189.0f, py / 0.6f + 363.0f));
        sum += m, sq += (double)m * m;
    }
    stats[0] = (float)(sum / 40000.0);
    stats[1] = (float)sqrt(sq / 40000.0 - (sum / 40000.0) * (sum / 40000.0));
}
