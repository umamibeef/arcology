#!/bin/zsh
#  sweep3.sh <reference binary> -- every city through this build and a
#  reference build, comparing the check lines.  Nothing is written.  A
#  refactor that changes no output prints SAME for all of them.
#
#      tools/sweep3.sh /path/to/reference/arcology
#
#  The shipped cities in cities/ are always swept; set SC2K_CITIES to a
#  game folder to sweep its saves too.  The 400-column cut matters: at
#  160 the tail of the lanes line is lost and a real change hides.
ROOT=${0:a:h:h}
cd "$ROOT"
REF=$1
if [ -z "$REF" ]; then echo "usage: tools/sweep3.sh <reference binary>" >&2; exit 2; fi
find cities ${SC2K_CITIES:+"$SC2K_CITIES"} -maxdepth 3 -iname "*.sc2" | sort | tr "\n" "\0" |
xargs -0 -P $(sysctl -n hw.logicalcpu) -I{} zsh -c '
f="$1"; REF="$2"; ROOT="$3"; cd "$ROOT"
L="^mesh build failed\|^mesh check\|^road clip\|^on-ramps\|^tangent fit  highway\|^lanes\|^sidewalks"
n=$(build/arcology "$f" --mute --mesh-check --dump-to - 2>&1 | grep -a "$L" | cut -c1-400)
r=$($REF "$f" --mute --mesh-check --dump-to - 2>&1 | grep -a "$L" | cut -c1-400)
if [ "$n" = "$r" ]; then printf "SAME %s\n" "$(basename "$f")"
else printf "DIFF %s\n%s\n%s\n" "$(basename "$f")" "$(echo "$r" | sed "s/^/  old: /")" "$(echo "$n" | sed "s/^/  new: /")"; fi' -- {} "$REF" "$ROOT"
