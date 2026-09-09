#!/bin/sh
#  strip_check.sh -- the strip composition against the pipeline's own
#  numbers, over a whole city.  See tools/strip_check.lua for what it
#  compares and why the two cannot be identical to the bit.
set -e
bin=${1:-build/arcology}
root=$(cd "$(dirname "$0")/.." && pwd)
out=$("$bin" "$root/cities/atlanta.sc2" --mute --win 640x400 \
      --lua "$root/tools/strip_check.lua" \
      --lua-eval 'arc.rules.strip_check_report()' 2>/dev/null | grep '^strips')
echo "$out"
echo "$out" | grep -q " 0 past " || { echo "the composition and the pipeline disagree"; exit 1; }
echo "$out" | grep -qE " [1-9][0-9]* pairs" || { echo "no strip was composed at all"; exit 1; }
