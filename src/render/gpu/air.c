/*  gpu/air.c: the medium of the atmosphere, and the light that it
 *  scatters many times.
 *
 *  The multiple scattering is the method of Hillaire (2020).  Light that
 *  is scattered twice reaches a point from every way.  The second order
 *  is integrated over the sphere of ways, each way a ray to the top of
 *  the air or to the ground.  The ground gives back its share of the
 *  sunlight that reaches it.  Each further order is the one before it
 *  times the share of light that the air round the point scatters back
 *  to it.  So the sum of all the orders is the second order over one
 *  less that share.  The phase is the same in every way. */
#include "gpu/air.h"

#include <math.h>
#include <string.h>

#define AIR_PI         3.14159265358979
#define AIR_RADIUS     6360.0e3 /* the ground's radius, in meters */
#define AIR_TOP        6460.0e3 /* the radius at the top of the air */
#define AIR_OZONE_HALF 15.0e3   /* the half width of the ozone's tent, in meters */

/*  The ways round the point, and the steps along each way and toward
 *  the sun. */
#define AIR_RINGS     16
#define AIR_SPOKES    32
#define AIR_STEPS     40
#define AIR_SUN_STEPS 24

void air_medium(float haze, AirMedium *m)
{
    memset(m, 0, sizeof *m);
    m->rayleigh[0] = 5.802e-6f, m->rayleigh[1] = 13.558e-6f, m->rayleigh[2] = 33.1e-6f, m->rayleigh[3] = 8000.0f;
    m->mie[0] = 3.996e-6f * haze, m->mie[1] = 4.40e-6f * haze, m->mie[2] = 1200.0f, m->mie[3] = 0.8f;
    m->ozone[0] = 0.650e-6f, m->ozone[1] = 1.881e-6f, m->ozone[2] = 0.085e-6f, m->ozone[3] = 25.0e3f;
}

/*  The scattering and the extinction at a height. */
static void air_at(const AirMedium *m, double h, double scat[3], double ext[3])
{
    double r, mie, oz;
    int    k;
    if (h < 0.0)
        h = 0.0;
    r   = exp(-h / m->rayleigh[3]);
    mie = exp(-h / m->mie[2]);
    oz  = 1.0 - fabs(h - m->ozone[3]) / AIR_OZONE_HALF;
    oz  = oz > 0.0 ? oz : 0.0;
    for (k = 0; k < 3; k++)
    {
        scat[k] = m->rayleigh[k] * r + m->mie[0] * mie;
        ext[k]  = m->rayleigh[k] * r + m->mie[1] * mie + m->ozone[k] * oz;
    }
}

/*  Where a ray from `o` along `d` leaves the sphere of radius `rad`, the
 *  farther root.  Answers -1 when it misses. */
static double air_exit(const double o[3], const double d[3], double rad)
{
    double b = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
    double c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - rad * rad;
    double q = b * b - c;
    return q < 0.0 ? -1.0 : -b + sqrt(q);
}

/*  Where a ray from `o` along `d` meets the ground, the nearer root.
 *  Answers -1 when it does not. */
static double air_ground(const double o[3], const double d[3])
{
    double b = o[0] * d[0] + o[1] * d[1] + o[2] * d[2];
    double c = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - AIR_RADIUS * AIR_RADIUS;
    double q = b * b - c, t;
    if (q < 0.0)
        return -1.0;
    t = -b - sqrt(q);
    return t > 0.0 ? t : -1.0;
}

static double air_height(const double p[3])
{
    return sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]) - AIR_RADIUS;
}

/*  What reaches the point `p` from a sun of one.  Nothing reaches it
 *  when the ground lies between. */
static void air_sunlit(const AirMedium *m, const double p[3], const double sun[3], double out[3])
{
    double tau[3] = {0.0, 0.0, 0.0}, len, dt, q[3], scat[3], ext[3];
    int    i, k;
    if (air_ground(p, sun) > 0.0)
    {
        out[0] = out[1] = out[2] = 0.0;
        return;
    }
    len = air_exit(p, sun, AIR_TOP);
    dt  = len / AIR_SUN_STEPS;
    for (i = 0; i < AIR_SUN_STEPS; i++)
    {
        double t = (i + 0.5) * dt;
        for (k = 0; k < 3; k++)
            q[k] = p[k] + sun[k] * t;
        air_at(m, air_height(q), scat, ext);
        for (k = 0; k < 3; k++)
            tau[k] += ext[k] * dt;
    }
    for (k = 0; k < 3; k++)
        out[k] = exp(-tau[k]);
}

void air_multiple(const AirMedium *m, const float sun_f[3], float albedo, float psi[3], float sky[3])
{
    static double way_l[AIR_RINGS * AIR_SPOKES][3], way_f[AIR_RINGS * AIR_SPOKES][3], way_up[AIR_RINGS * AIR_SPOKES];
    const double  pu    = 1.0 / (4.0 * AIR_PI);
    double        x0[3] = {0.0, 0.0, AIR_RADIUS + 1.0}, sun[3];
    double        l2[3] = {0.0, 0.0, 0.0}, fms[3] = {0.0, 0.0, 0.0};
    int           a, b, i, k, n = 0;
    for (k = 0; k < 3; k++)
        sun[k] = sun_f[k];
    for (a = 0; a < AIR_RINGS; a++)
        for (b = 0; b < AIR_SPOKES; b++)
        {
            /*  Ways spread evenly over the sphere: even in the cosine
             *  of the height, and even round it. */
            double cz = 1.0 - 2.0 * (a + 0.5) / AIR_RINGS, sz = sqrt(1.0 - cz * cz);
            double az = 2.0 * AIR_PI * (b + 0.5) / AIR_SPOKES;
            double d[3], tg, len, dt, T[3] = {1.0, 1.0, 1.0}, lw[3] = {0.0, 0.0, 0.0}, fw[3] = {0.0, 0.0, 0.0};
            d[0] = sz * cos(az), d[1] = sz * sin(az), d[2] = cz;
            tg  = air_ground(x0, d);
            len = tg > 0.0 ? tg : air_exit(x0, d, AIR_TOP);
            dt  = len / AIR_STEPS;
            for (i = 0; i < AIR_STEPS; i++)
            {
                double t = (i + 0.5) * dt, q[3], scat[3], ext[3], lit[3];
                for (k = 0; k < 3; k++)
                    q[k] = x0[k] + d[k] * t;
                air_at(m, air_height(q), scat, ext);
                air_sunlit(m, q, sun, lit);
                for (k = 0; k < 3; k++)
                {
                    /*  The step's own integral, exact for one density. */
                    double tx   = exp(-ext[k] * dt);
                    double gain = ext[k] > 1e-30 ? (1.0 - tx) / ext[k] : dt;
                    lw[k] += T[k] * scat[k] * lit[k] * pu * gain;
                    fw[k] += T[k] * scat[k] * gain;
                    T[k] *= tx;
                }
            }
            if (tg > 0.0)
            {
                /*  The ground at the end of the way gives back its share
                 *  of the sunlight on it, the same in every way. */
                double g[3], up[3], lit[3], c, r = 0.0;
                for (k = 0; k < 3; k++)
                    g[k] = x0[k] + d[k] * tg, r += g[k] * g[k];
                r = sqrt(r);
                for (k = 0; k < 3; k++)
                    up[k] = g[k] / r;
                c = up[0] * sun[0] + up[1] * sun[1] + up[2] * sun[2];
                air_sunlit(m, g, sun, lit);
                for (k = 0; k < 3; k++)
                    lw[k] += T[k] * lit[k] * (c > 0.0 ? c : 0.0) * albedo / AIR_PI;
            }
            for (k = 0; k < 3; k++)
                l2[k] += lw[k], fms[k] += fw[k], way_l[n][k] = lw[k], way_f[n][k] = fw[k];
            way_up[n] = tg > 0.0 ? 0.0 : cz;
            n++;
        }
    /*  The mean over the ways is the integral times the phase, since the
     *  phase is one over the sphere's whole angle. */
    for (k = 0; k < 3; k++)
    {
        double second = l2[k] / n, share = fms[k] / n;
        psi[k] = (float)(second / (1.0 - share));
    }
    /*  The sky's light on a level surface.  Each way that reaches the top
     *  of the air gives its light, by the cosine of its height, over the
     *  angle the way holds.  The light from a way is what it scatters
     *  once, and what its scattering adds of the light scattered many
     *  times. */
    for (k = 0; k < 3; k++)
    {
        double e = 0.0;
        for (i = 0; i < n; i++)
            if (way_up[i] > 0.0)
                e += (way_l[i][k] + way_f[i][k] * psi[k]) * way_up[i];
        sky[k] = (float)(e * 4.0 * AIR_PI / n);
    }
}

void air_sun(const AirMedium *m, const float sun_f[3], float out[3])
{
    const double p[3] = {0.0, 0.0, AIR_RADIUS + 1.0};
    double       d[3], len, o[3];
    int          k;
    len = sqrt((double)sun_f[0] * sun_f[0] + (double)sun_f[1] * sun_f[1] + (double)sun_f[2] * sun_f[2]);
    for (k = 0; k < 3; k++)
        d[k] = len > 0.0 ? sun_f[k] / len : 0.0;
    air_sunlit(m, p, d, o);
    for (k = 0; k < 3; k++)
        out[k] = (float)o[k];
}
