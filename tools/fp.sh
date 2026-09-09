#!/bin/sh
#  fp.sh -- the pixel battery: six fixed views at a fixed framebuffer,
#  hashed.  This is the ONLY exact check the renderer has.  A geometry
#  change is not output-identical until these six hashes match.
#
#  The size is pinned because a shot follows the window, and a window
#  that opens at another display density hashes differently with nothing
#  moved -- so the script prints the size it got.  The last two views
#  carry a railway, its level crossings and their gates, which the four
#  road views do not.
#
#      sh tools/fp.sh                 # against build/arcology
#      sh tools/fp.sh path/to/binary  # against another build
#
#  ALWAYS `rm -f /tmp/v?.png` first: a crashed run leaves the previous
#  PNGs in place and the script then reports stale hashes as a pass.
b=${1:-build/arcology}
rm -f /tmp/v1.png /tmp/v2.png /tmp/v3.png /tmp/v4.png /tmp/v5.png /tmp/v6.png
{ "$b" cities/atlanta.sc2 --mute --win 1280x800 --shot /tmp/v1.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --plan --centre 70,16 --zoomf 40 --shot /tmp/v2.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --plan --centre 68,54 --zoomf 26 --shot /tmp/v3.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --outline --plan --centre 70,16 --zoomf 16 --shot /tmp/v4.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --centre 63,50 --zoomf 26 --traffic-t 26 --shot /tmp/v5.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --centre 32,9 --zoomf 34 --traffic-t 26 --shot /tmp/v6.png
} >/dev/null 2>&1
shasum /tmp/v1.png /tmp/v2.png /tmp/v3.png /tmp/v4.png /tmp/v5.png /tmp/v6.png | cut -c1-10 | tr '\n' ' '
python3 -c "import struct; d=open('/tmp/v1.png','rb').read(); print(' at %dx%d' % struct.unpack('>II', d[16:24]))"
