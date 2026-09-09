#!/bin/sh
#  lua_lint.sh -- read every script the repository ships and say what is
#  wrong with it.  The binary does the reading (--lua-lint), because the
#  faults worth catching are about the VOCABULARY -- a setting or a rule
#  that is not one -- and only the program knows what it has.  Globbed
#  here rather than at configure time, so a script added later is read
#  without the build being set up again.
set -e
bin=${1:-build/arcology}
root=$(cd "$(dirname "$0")/.." && pwd)
set -- "$root"/scripts/*.lua "$root"/scripts/*/*.lua
if [ ! -e "$1" ]; then
    echo "lua lint  no scripts"
    exit 0
fi
exec "$bin" --lua-lint "$@"
