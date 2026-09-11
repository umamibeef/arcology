/*  net/drive.h: what drive.c answers for.
 *
 *  the one door: where C hands a turn of the world to a script.
 *
 *  Nothing declared here decides anything. */
#ifndef ARC_NET_DRIVE_H
#define ARC_NET_DRIVE_H

#include "net/types.h"

int  net_drive_build(void);
int  net_drive_move(void);
int  net_drive_what(void);

#endif /* ARC_NET_DRIVE_H */
