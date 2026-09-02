/* ==================================================================== *
 *  .arco -- Arcology's own world format.
 *
 *  The 1995 save is an IFF file: big-endian chunks, a bespoke run-length
 *  codec, a 4800-byte block of unnamed longs, and a map that is 128 by
 *  128 because the code says so.  Arcology reads and writes it exactly,
 *  and will go on doing so -- it is the import path and the reference
 *  baseline.  It is not a format to build a bigger game on.
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
 *      world.json              dimensions, chunk size, the city list
 *      chunks/<cx>_<cy>/       the terrain and content layers
 *      cities/<id>.json        one city's treasury, budget, graphs, clock
 *      cities/<id>/mask.bin    which tiles that city owns
 *
 *  Three things the format allows that the original cannot express, and
 *  which are why it exists:
 *
 *    - **The world is chunked and unbounded.**  Chunk size is in the
 *      manifest, not in the code.  An imported 1995 save is a 128x128
 *      patch written into whatever chunks it spans; nothing about it has
 *      to stay 128 afterwards.
 *    - **A city is a mask, not a rectangle.**  `cities/<id>/mask.bin`
 *      gives an owner per tile, so a city's limits can follow its
 *      development instead of a square, and two cities can meet.
 *    - **Altitude is 16 bits and unclamped.**  The one-level-step rule
 *      is the original's, kept in the faithful mode and broken in the
 *      enhanced one.
 *
 *  Round-tripping is the constraint the format is held to: a 1995 save
 *  converted to .arco and back must come out byte-identical.  Anything
 *  the reconstruction has not named yet rides along in `legacy.misc`,
 *  so naming a field later is a change to the manifest, never a change
 *  to what a file can hold.
 * ==================================================================== */
#ifndef ARCO_H
#define ARCO_H

#include "sc2k.h"

/*  The version in world.json.  Bump only for a change a reader from the
 *  previous version could not survive; adding a file or a manifest key
 *  is not one of those. */
#define ARCO_VERSION 1

/*  Chunk edge, in tiles.  A multiple of 4 so the quarter-resolution
 *  layers divide, and of 8 so the growth scan's sixteen slices do.  A
 *  1995 save is exactly four chunks across. */
#define ARCO_CHUNK 32

/*  Write `c` as a .arco world with one city in it.  0 on success. */
int arco_save(const char *path, const City *c);

/*  Read a .arco world.  Only its first city is loaded into `c`, which
 *  is what the game does today; the container already holds more.
 *  0 on success. */
int arco_load(const char *path, City *c);

/*  Does this file start with a ZIP local header, i.e. is it .arco
 *  rather than the 1995 IFF?  Cheap enough to call before either. */
int arco_is_arco(const char *path);

#endif
