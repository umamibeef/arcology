#!/bin/zsh
#  prove.sh -- an incremental rebuild draws what a full build draws.
#
#  Five edits on Atlanta, each built twice: once incrementally and once
#  with --no-incr.  Every line must say SAME.  The hash is of the sorted
#  tile dump, so it does not care what order the triangles came out in.
#
#      tools/prove.sh
#
#  TMP holds the dumps; set it to keep them.
set -e
ROOT=${0:a:h:h}
cd "$ROOT"
B=${BIN:-build/arcology}
T=${TMP:-$(mktemp -d)}
L='^mesh check\|^road clip\|^lanes\|^sidewalks\|^on-ramps\|^tangent\|^chunks\|^road pieces'
$B cities/Atlanta.sc2 --mute --mesh-check --tile-dump all --dump-to $T/full.txt >/dev/null 2>&1
echo "full $(grep '^tri ' $T/full.txt | sort | md5 | cut -c1-12)  $(grep -a '^mesh check' $T/full.txt)"
for e in 66,113 72,88 90,81 105,84 20,20; do
  $B cities/Atlanta.sc2 --mute --mesh-check --times --tile-dump all --edit $e --dump-to $T/e1.txt >/dev/null 2>&1
  $B cities/Atlanta.sc2 --mute --mesh-check --tile-dump all --edit $e --no-incr --dump-to $T/e2.txt >/dev/null 2>&1
  h1=$(grep '^tri ' $T/e1.txt | sort | md5 | cut -c1-12); h2=$(grep '^tri ' $T/e2.txt | sort | md5 | cut -c1-12)
  c1=$(grep -a "$L" $T/e1.txt | md5 | cut -c1-8);          c2=$(grep -a "$L" $T/e2.txt | md5 | cut -c1-8)
  box=$(grep -a 'junctions:' $T/e1.txt | tail -1 | sed 's/.*| junctions: //')
  p2=$(grep -a 'pass 2  whole pass' $T/e1.txt | tail -1 | awk '{print $(NF-1)}')
  drawn=$(grep -a 'pass 2  junctions and segments drawn' $T/e1.txt | tail -1 | awk '{print $(NF-1)}')
  echo "edit $e $([ $h1 = $h2 ] && [ $c1 = $c2 ] && echo SAME || echo DIFF) $h1 pass2 $p2 ms, drawn $drawn, junctions: $box"
done
cd build && ctest 2>&1 | grep -a "tests passed\|Failed" | head -2
