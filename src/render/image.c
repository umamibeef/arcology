/*  image.c -- the finished picture, as a file.  Freeing it, hashing it, and
 *  the four PNG writers: the plain one, the two that keep the GAME's
 *  palette indices rather than letting lodepng renumber them, and the
 *  provenance one. */
#include "soft/soft.h"
#include "project.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lodepng.h"
#include "tables.h"

void image_free(RImage *im)
{
    free(im->rgb);
    free(im->idx);
    free(im->prov);
    im->rgb  = NULL;
    im->idx  = NULL;
    im->prov = NULL;
    im->w = im->h = 0;
}

uint32_t image_crc(const RImage *im)
{
    return lodepng_crc32(im->rgb, (size_t)im->w * (size_t)im->h * 3u);
}

/*  Write the picture as a palette PNG carrying the GAME's palette indices.
 *  This is not the same as letting lodepng fold the RGB back to a palette:
 *  auto_convert builds a fresh table from the colours present and renumbers
 *  everything, which is fine to look at and useless for anything that cares
 *  which index a pixel is.  SC2K's animation is a rotation of indices
 *  155..203 and 224..238, so an export that renumbers them turns the
 *  animation into a no-op -- which is exactly what it did. */
int image_write_png_indices(const RImage *im, const RAtlas *a, const char *path)
{
    LodePNGState   st;
    unsigned char *png = NULL;
    size_t         n   = 0;
    unsigned       err;
    int            i;

    lodepng_state_init(&st);
    st.info_raw.colortype       = LCT_PALETTE;
    st.info_raw.bitdepth        = 8;
    st.info_png.color.colortype = LCT_PALETTE;
    st.info_png.color.bitdepth  = 8;
    st.encoder.auto_convert     = 0;
    for (i = 0; i < 256; ++i)
    {
        lodepng_palette_add(&st.info_raw, a->palette[i][0], a->palette[i][1], a->palette[i][2], 255);
        lodepng_palette_add(&st.info_png.color, a->palette[i][0], a->palette[i][1], a->palette[i][2], 255);
    }
    err = lodepng_encode(&png, &n, im->idx, (unsigned)im->w, (unsigned)im->h, &st);
    if (!err)
        err = lodepng_save_file(png, n, path);
    free(png);
    lodepng_state_cleanup(&st);
    return err ? -1 : 0;
}

int image_write_png_indexed(const RImage *im, const char *path)
{
    LodePNGState   st;
    unsigned char *png = NULL;
    size_t         n   = 0;
    unsigned       err;

    lodepng_state_init(&st);
    st.info_raw.colortype       = LCT_RGB;
    st.info_raw.bitdepth        = 8;
    st.info_png.color.colortype = LCT_RGB;
    st.info_png.color.bitdepth  = 8;
    st.encoder.auto_convert     = 1; /* palette when it fits, RGB when not */
    err                         = lodepng_encode(&png, &n, im->rgb, (unsigned)im->w, (unsigned)im->h, &st);
    if (!err)
        err = lodepng_save_file(png, n, path);
    free(png);
    lodepng_state_cleanup(&st);
    return err ? -1 : 0;
}

int image_write_png_provenance(const RImage *im, const char *path)
{
    /*  16-bit greyscale, big-endian samples as PNG stores them.  Nothing
     *  here is a picture; it is a plane of ids that a checker reads back. */
    LodePNGState   st;
    unsigned char *png = NULL, *raw;
    size_t         n = 0, npx = (size_t)im->w * (size_t)im->h, k;
    unsigned       err;

    raw = (unsigned char *)malloc(npx * 2u);
    if (!raw)
        return -1;
    for (k = 0; k < npx; ++k)
    {
        raw[k * 2u]      = (unsigned char)(im->prov[k] >> 8);
        raw[k * 2u + 1u] = (unsigned char)(im->prov[k] & 0xFFu);
    }
    lodepng_state_init(&st);
    st.info_raw.colortype       = LCT_GREY;
    st.info_raw.bitdepth        = 16;
    st.info_png.color.colortype = LCT_GREY;
    st.info_png.color.bitdepth  = 16;
    st.encoder.auto_convert     = 0;
    err                         = lodepng_encode(&png, &n, raw, (unsigned)im->w, (unsigned)im->h, &st);
    if (!err)
        err = lodepng_save_file(png, n, path);
    free(png);
    free(raw);
    lodepng_state_cleanup(&st);
    return err ? -1 : 0;
}

int image_write_png(const RImage *im, const char *path)
{
    /*  auto_convert off, on purpose.  Left on, lodepng notices the image
     *  uses few colours and writes a palette PNG -- correct and smaller,
     *  but the format then depends on the picture.  Tools that read this
     *  back want it predictable, so pin it to 8-bit RGB; --indexed opts
     *  into the smaller form explicitly. */
    LodePNGState   st;
    unsigned char *png = NULL;
    size_t         n   = 0;
    unsigned       err;

    lodepng_state_init(&st);
    st.info_raw.colortype       = LCT_RGB;
    st.info_raw.bitdepth        = 8;
    st.info_png.color.colortype = LCT_RGB;
    st.info_png.color.bitdepth  = 8;
    st.encoder.auto_convert     = 0;
    err                         = lodepng_encode(&png, &n, im->rgb, (unsigned)im->w, (unsigned)im->h, &st);
    if (!err)
        err = lodepng_save_file(png, n, path);
    free(png);
    lodepng_state_cleanup(&st);
    return err ? -1 : 0;
}
