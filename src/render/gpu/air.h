/*  gpu/air.h: the air the camera looks through.  The medium of a
 *  physically based atmosphere, and the light that it scatters many
 *  times, which air.frag adds to the light it scatters once. */
#ifndef R_GPU_AIR_H
#define R_GPU_AIR_H

/*  The medium, as air.frag reads it.  Each coefficient is per meter at
 *  sea level, and each height is in meters. */
typedef struct
{
    float rayleigh[4]; /* the air's scattering, red, green, blue, and the height at which it thins by e */
    float mie[4];      /* the haze's scattering, its extinction, the height at which it thins by e, and its asymmetry */
    float ozone[4];    /* the ozone's absorption, red, green, blue, and the height of its peak */
} AirMedium;

/*  The medium of Hillaire (2020), with its haze made `haze` times as
 *  thick as clean air's. */
void air_medium(float haze, AirMedium *m);

/*  The light that the air scatters more than once, toward any way, at
 *  the ground, for a sun of one.  `sun` is the unit direction toward the
 *  sun, z up.  `albedo` is the share of the light that the ground gives
 *  back.  Air.frag multiplies `psi` by the scattering at each point.
 *  `sky` is the light of the whole sky on a level surface, for the same
 *  sun, which lights the shaded side of a cloud. */
void air_multiple(const AirMedium *m, const float sun[3], float albedo, float psi[3], float sky[3]);

/*  What reaches the ground from a sun of one through the whole air, by
 *  color.  `sun` is the unit direction toward the sun, z up.  Nothing
 *  reaches it once the sun is under the horizon. */
void air_sun(const AirMedium *m, const float sun[3], float out[3]);

#endif /* R_GPU_AIR_H */
