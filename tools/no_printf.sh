#!/bin/sh
#  No printf in the renderer, the road network, the interface or the app.  Messages go through the log
#  (R_ERR / R_WARN / R_NOTE / R_DBG, src/util/log.h) and developer dumps
#  and report lines go through dumpf (src/util/dump.h), which writes to
#  the dump file when one is asked for and to stdout when none is.  A
#  bare printf goes only to stdout, cannot be redirected, and is invisible
#  to every switch the rest of the output obeys.
#
#  Run from anywhere; prints each offending line and fails.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
#  Every spelling of a write straight to the terminal: printf and puts,
#  and fprintf, fputs and fputc aimed at stderr or stdout.  A write to a
#  FILE the code opened itself is not one of these.  src/app/stub.c is
#  the game when the build found no SDL3, and the log is not linked into
#  that binary, so it says so with the only stream it has.
hits=$(grep -rnE '(^|[^A-Za-z0-9_])(printf|puts)\(|fprintf\((stderr|stdout)|fputs\([^;]*,\s*(stderr|stdout)\)|fputc\([^;]*,\s*(stderr|stdout)\)|perror\(' "$root/src/render" "$root/src/network" "$root/src/ui" "$root/src/app" 2>/dev/null | grep -v '\.h:' | grep -v '/src/app/stub\.c:' || true)
if [ -n "$hits" ]; then
    echo "printf is not used in the renderer, the network, the interface or the app; use R_DBG/R_NOTE for messages, dumpf for dumps:"
    echo "$hits"
    exit 1
fi
echo "no printf in src/render, src/network, src/ui or src/app"
