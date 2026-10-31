/*  load.c: a city from a file of either format, .arco or the 1995 .sc2.
 *  Which one a file is, is a question the file answers. */
#include "arco.h"
#include "sim.h"

int city_load(const char *path, City *c)
{
    /*  The two readers report differently and always will.  City_load
     *  answers 1 for success because its callers read it as a predicate.
     *  Arco_load answers 0 because its callers read it as an errno.
     *  Converting here is the whole of the reconciliation. */
    if (arco_is_arco(path))
        return arco_load(path, c) == 0;
    return city_load_sc2(path, c);
}
