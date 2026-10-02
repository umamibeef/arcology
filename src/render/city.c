/*  city.c: what the renderer knows of a city on its own.  The reader
 *  is the simulation's, and adapt.c fills the view from it.  So this
 *  holds only what depends on nothing but the view: the corner mask a
 *  rotation turns the corner nibble under. */
#include "city.h"

uint8_t city_corner_mask(int32_t rotation)
{
    /*  ROT_CORNER_MASK.  The array turns under the corner nibble.
     *  Rotating a city moves the bytes but leaves XZON's high nibble
     *  alone.  So the bit that means "nearest the viewer" changes with
     *  the rotation. */
    static const uint8_t mask[4] = {0x80, 0x10, 0x20, 0x40};
    return mask[rotation & 3];
}
