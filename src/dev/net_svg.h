/*  dev/net_svg.h: what net_svg.c answers for.
 *
 *  The road network as a diagram, written from its records alone. */
#ifndef ARC_NET_SVG_H
#define ARC_NET_SVG_H

/*  Write the network last built to `path` as an SVG.  `box` is NULL for
 *  the whole network, or "C0,R0,C1,R1" for those tiles alone, drawn
 *  larger.  0 on success. */
int network_svg(const char *path, const char *box);

#endif /* ARC_NET_SVG_H */
