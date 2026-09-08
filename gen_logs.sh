#!/usr/bin/env bash
# gen_logs.sh <machine-id> <lines> <dir>
#
# Writes <dir>/machine.<id>.log with contents you can predict in your head --
# which is the whole point, because a test can only check an answer it knows.
#
#   every 4th line   says ERROR          -> lines/4   matches
#   every 10th line  has COMMON_TOKEN    -> lines/10  matches
#   every 1000th     has RARE_TOKEN      -> lines/1000 matches
#   on machine 3 only, every 100th line has ONLY_ON_THREE
#
# No randomness at all, so two machines running this always agree.
set -euo pipefail
id=$1; lines=$2; dir=$3

awk -v id="$id" -v n="$lines" 'BEGIN {
    for (i = 1; i <= n; i++) {
        level = (i % 4 == 0) ? "ERROR" : "INFO"
        extra = ""
        if (i % 10   == 0) extra = extra " COMMON_TOKEN"
        if (i % 1000 == 0) extra = extra " RARE_TOKEN"
        if (id == 3 && i % 100 == 0) extra = extra " ONLY_ON_THREE"
        printf "2026-09-13T00:%02d:%02d %s [machine%d] request %d handled%s\n",
               int(i/60) % 60, i % 60, level, id, i, extra
    }
}' > "$dir/machine.$id.log"

echo "wrote $dir/machine.$id.log  ($lines lines)"
