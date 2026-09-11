/*  drive.c: THE ONE DOOR.
 *
 *  This is the only place in the renderer where C calls up into a
 *  script.  Everything the pipeline hands over goes through
 *  `arc.rules.frame`: the build's passes, and the world that moves.
 *
 *  The reason for one door rather than several is not tidiness.  A pass
 *  that enters a script for itself is a pass that decides, in C, when
 *  the script gets a say.  With one door the script is handed the turn
 *  and decides what a turn of the world does.  Build it, move it, draw
 *  it, or none of those.
 *
 *  Two things reach it.  A BUILD hands out its passes one at a time,
 *  each as a `world` handle.  The script composes each and asks for the
 *  next.  A MOVE hands out the world that moves, as a `moving` handle,
 *  with the beats its clock owes. */
#include <string.h>

#include "log.h"
#include "mesh/internal.h"
#include "pipeline.h"
#include "net/net.h"
#include "script.h"
#include "build.h"

static struct
{
    int what; /* DRIVE_BUILD or DRIVE_MOVE */
    int rc;
} s_drive;

int net_drive_what(void)
{
    return s_drive.what;
}

/*  One turn of the world, handed to the script.  Answers 0, or -1 with
 *  the reason already reported. */
static int drive(int what)
{
    s_drive.what = what;
    s_drive.rc   = 0;
    if (!script_rule_object("frame", "frame", &s_drive))
    {
        R_ERR("mesh", "arc.rules.frame did nothing: no such rule, or it answered false");
        return -1;
    }
    return s_drive.rc;
}

int net_drive_build(void)
{
    int rc = drive(DRIVE_BUILD);
    return rc != 0 ? rc : build_rc();
}

int net_drive_move(void)
{
    return drive(DRIVE_MOVE);
}
