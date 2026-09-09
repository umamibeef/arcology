#!/bin/sh
#  No printf in the renderer or the app.  Messages go through the log
#  (R_ERR / R_WARN / R_NOTE / R_DBG, src/util/log.h) and developer dumps
#  and report lines go through dumpf (src/util/dump.h), which writes to
#  the dump file when one is asked for and to stdout when none is.  A
#  bare printf goes only to stdout, cannot be redirected, and is invisible
#  to every switch the rest of the output obeys.
#
#  Run from anywhere; prints each offending line and fails.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
hits=$(grep -rnE '(^|[^A-Za-z0-9_])printf\(' "$root/src/render" "$root/src/app" 2>/dev/null | grep -v '\.h:' || true)
if [ -n "$hits" ]; then
    echo "printf is not used in the renderer or the app; use R_DBG/R_NOTE for messages, dumpf for dumps:"
    echo "$hits"
    exit 1
fi
echo "no printf in src/render or src/app"
