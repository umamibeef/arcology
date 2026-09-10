#!/bin/sh
#  fp.sh -- the pixel battery: eight fixed views at a fixed framebuffer,
#  hashed.  This is the ONLY exact check the renderer has.  A geometry
#  change is not output-identical until these eight hashes match.
#
#  The size is pinned because a shot follows the window, and a window
#  that opens at another display density hashes differently with nothing
#  moved -- so the script prints the size it got.  Views five and six
#  carry a railway, its level meets and their gates, which the four
#  line views do not; seven and eight frame a 2x2 band INTERCHANGE --
#  Atlanta's three blocks in a row and Maltron's lone one -- which none
#  of the others reaches, so without them the battery is blind to the slab
#  a driver crosses one on.  Neither frames WATER: the water shader moves
#  every frame, so a view with a river in it hashes differently each run
#  and the battery would cry wolf.
#
#      sh tools/fp.sh                 # against build/arcology
#      sh tools/fp.sh path/to/binary  # against another build
#
#  ALWAYS `rm -f /tmp/v?.png` first: a crashed run leaves the previous
#  PNGs in place and the script then reports stale hashes as a pass.
b=${1:-build/arcology}
rm -f /tmp/v1.png /tmp/v2.png /tmp/v3.png /tmp/v4.png /tmp/v5.png /tmp/v6.png /tmp/v7.png /tmp/v8.png
{ "$b" cities/atlanta.sc2 --mute --win 1280x800 --shot /tmp/v1.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --plan --centre 70,16 --zoomf 40 --shot /tmp/v2.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --plan --centre 68,54 --zoomf 26 --shot /tmp/v3.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --outline --plan --centre 70,16 --zoomf 16 --shot /tmp/v4.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --centre 63,50 --zoomf 26 --traffic-t 26 --shot /tmp/v5.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --centre 32,9 --zoomf 34 --traffic-t 26 --shot /tmp/v6.png
  "$b" cities/atlanta.sc2 --mute --win 1280x800 --centre 104,63 --zoomf 6 --shot /tmp/v7.png
  "$b" cities/maltron.sc2 --mute --win 1280x800 --centre 66,28 --zoomf 5 --shot /tmp/v8.png
} >/dev/null 2>&1
shasum /tmp/v1.png /tmp/v2.png /tmp/v3.png /tmp/v4.png /tmp/v5.png /tmp/v6.png /tmp/v7.png /tmp/v8.png | cut -c1-10 | tr '\n' ' '
python3 -c "import struct; d=open('/tmp/v1.png','rb').read(); print(' at %dx%d' % struct.unpack('>II', d[16:24]))"
