/*  gpu/clouds.h: the noise that the clouds are shaped from. */
#ifndef R_GPU_CLOUDS_H
#define R_GPU_CLOUDS_H

#include <stdint.h>

/*  The edge of the noise volume, in texels.  The volume repeats, so a
 *  cloud field of any size reads it with no seam. */
#define CLOUD_NOISE_N 64

/*  Fill `rgba`, CLOUD_NOISE_N cubed texels of four bytes, with the noise
 *  of the Nubis clouds (Schneider, 2015).  Red is Perlin noise worn by
 *  Worley noise, the shape of a cloud.  Green and blue are Worley noise
 *  at a low and a high frequency, the coverage and the detail that wears
 *  an edge.  Alpha is Perlin noise at the lowest frequency, the weather
 *  that moves the coverage from place to place. */
void clouds_noise(uint8_t *rgba);

/*  The edge of the sky's field, in texels, and the lattice cells of its
 *  lowest octave across that edge. */
#define SKY_FIELD_N     512
#define SKY_FIELD_CELLS 8

/*  Fill `v`, SKY_FIELD_N squared floats, with the sky's field: value noise
 *  of four octaves that repeats over the texture with no seam.  The clouds
 *  read it at two scales and take the mean of the two.  `stats` gets that
 *  mean's own mean and spread over the plane, which equalize it. */
void clouds_field(float *v, float stats[2]);

#endif /* R_GPU_CLOUDS_H */
