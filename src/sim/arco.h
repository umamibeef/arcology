/* ==================================================================== *
 *  .arco: Arcology's own world format.
 *
 *  The 1995 save is an IFF file: big-endian chunks, a bespoke run-length
 *  codec, a 4800-byte block of unnamed longs.  A map that is 128 by 128
 *  because the code says so.  Arcology reads it exactly and never writes
 *  it.  It is the import path and the reference baseline.  It is not a
 *  format to build a bigger game on.
 *
 *  .arco is that format.  It is a ZIP archive.  That one decision buys
 *  most of what "modern and intelligible" means here:
 *
 *    - "unzip -l world.arco" tells you what is in it.
 *    - The manifest is JSON.  You can read it, diff it, and hand-edit it.
 *    - Every language can already open it.
 *    - A new feature adds a FILE, and a reader that does not know the
 *      file ignores it.  There is no chunk registry to coordinate.
 *    - Grids stay binary, because a 512x512 array of bytes has no
 *      business being ASCII, and deflate does better on them than the
 *      original's RLE did.
 *
 *  Inside:
 *
 *      world.json   the chunk size, the chunks there are, the city list
 *      chunks/<cx>_<cy>/  every layer of those tiles, and their owner
 *      cities/<id>.json  one city's treasury, budget, graphs, clock
 *      cities/<id>/      that city's own records
 *
 *  Three things the format allows that the original cannot express, and
 *  which are why it exists:
 *
 *    - **The world is chunked and unbounded.**  A chunk is written only
 *      where the map has one, so the world is as large as its chunks and
 *      grows with them.  An imported 1995 save is one city whose window,
 *      128 by 128 tiles, lies on whatever chunks it spans.
 *    - **A world holds any number of cities.**  Each tile's owner is in
 *      chunks/<cx>_<cy>/owner.bin, so a city's limits follow what it
 *      zoned and built instead of a square, and two cities can meet.
 *    - **Altitude is 16 bits and unclamped.**  The one-level-step rule
 *      is the original's, kept in the faithful mode and broken in the
 *      enhanced one.
 *
 *  A 1995 save converted to .arco loses nothing: MISC goes whole into
 *  cities/<id>/misc.bin, and the chunks the reconstruction has not taken
 *  apart go in verbatim.  So naming a field later is a change to the
 *  manifest, never a change to what a file can hold.
 * ==================================================================== */
#ifndef ARCO_H
#define ARCO_H

#include "sim.h"

/*  The version in world.json.  Bump only for a change a reader from the
 *  previous version could not survive.  Adding a file or a manifest key
 *  is not one of those. */
#define ARCO_VERSION 2

struct World;

/*  Write `c` as a .arco world with one city in it, its window at the
 *  origin.  0 on success. */
int arco_save(const char *path, const City *c);

/*  A file the program around the simulation keeps in the world, under a
 *  name of its own (`render/...`).  The format carries it and never
 *  reads it. */
typedef struct
{
    const char *name;
    const void *data;
    size_t      len;
} ArcoFile;

/*  Write a whole world: every chunk it holds, every city on it, and the
 *  caller's files.  0 on success. */
int arco_save_world(const char *path, const struct World *w, const ArcoFile *extra, int n_extra);

/*  Read a whole world into `w`, which need not be set up first: every
 *  chunk, every city at its origin, each city's window filled.  A world
 *  written at version 1 reads as one city at the origin.  0 on success,
 *  and on a failure `w` holds nothing. */
int arco_load_world(const char *path, struct World *w);

/*  Read a .arco world's first city into `c`, its window as the city
 *  sees it.  0 on success. */
int arco_load(const char *path, City *c);

/*  Does this file start with a ZIP local header, i.e. is it .arco
 *  rather than the 1995 IFF?  Cheap enough to call before either. */
int arco_is_arco(const char *path);

#endif
