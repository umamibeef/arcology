/*  dev/area_report.h: the debug report of an area.  It says what the city and the networks hold on a
 *  rectangle of tiles, as text a bug report can carry.  The map view's
 *  Shift-drag makes one.  --area C0,R0-C1,R1 makes the same one
 *  headless. */
#ifndef ARC_DEV_AREA_REPORT_H
#define ARC_DEV_AREA_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "city.h"

/*  The report for the tiles from (c0,r0) to (c1,r1), either corner first,
 *  as malloc'd text the caller frees.  The networks must be built. */
int net_area_report(const RCity *c, int32_t c0, int32_t r0, int32_t c1, int32_t r1, char **out);

#endif /* ARC_DEV_AREA_REPORT_H */
