/*  net/net.h: THE STORES AND THE PLUMBING, as the rest of the renderer
 *  reads them.
 *
 *  One header a file in this directory, and this one over them all.  A
 *  reader who wants to know what the band offers opens net/band.h, and
 *  a file outside the directory that wants the lot includes this.
 *
 *  Every declaration under it is answered by a file in this directory.
 *  That is the whole rule.  A name the pipeline shares from somewhere
 *  else belongs in pipeline.h.  A name mesh/ or walk/ answers belongs
 *  with them.
 *
 *  Nothing declared here decides anything.  A store holds what a script
 *  handed back, and a fan offers a reading to a rule and takes its
 *  answer.  What is drawn, and by which family, is the scripts'. */
#ifndef ARC_NET_H
#define ARC_NET_H

#include "net/types.h"
#include "net/band.h"
#include "net/box.h"
#include "net/cut.h"
#include "net/drive.h"
#include "net/family.h"
#include "net/lane.h"
#include "net/strip.h"
#include "net/margin.h"
#include "net/meet.h"
#include "net/network.h"
#include "net/node.h"
#include "net/shelf.h"
#include "net/spur.h"
#include "net/station.h"
#include "net/table.h"
#include "net/traffic.h"
#include "net/report.h"

#endif /* ARC_NET_H */
