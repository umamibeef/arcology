#!/bin/sh
#  fp.sh -- the eight views, hashed: the first section of the battery.
#  `tools/battery.py` is the check and holds the reference; this is the
#  short name for its views alone.
#
#      sh tools/fp.sh                 # against build/arcology
#      sh tools/fp.sh path/to/binary  # against another build
exec python3 "$(dirname "$0")/battery.py" --only views ${1:+--binary "$1"}
