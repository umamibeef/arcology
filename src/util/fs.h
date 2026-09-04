/*  fs.h -- the two filesystem questions this program asks.
 *
 *  Neither knows anything about cities, art or drawing, which is why they
 *  are here and not beside the code that looks for those.
 */
#ifndef ARC_FS_H
#define ARC_FS_H

int is_dir(const char *p);
int is_file(const char *p);

#endif
