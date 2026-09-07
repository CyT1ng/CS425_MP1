#!/usr/bin/env bash
# Latency experiment for the report.
#
#   ./scripts/measure.sh [config] [trials]
#
# The spec pins the configuration exactly: "the average query latency (defined
# as time for the last machine to respond with the last byte to the query) when
# 4 machines each store 60 MB log files", plotted for frequent / infrequent /
# rare queries, "at least 5 trials per data point".
#
# So: 4 machines, 60 MB each, 3 query classes, >= 5 trials. Defaults to 7 trials
# so you can drop a warm-up and still clear the bar.
#
# Latency comes from log-query's OWN reported number, not from
# `time ./log-query`. Wrapping the process would fold in exec and dynamic-linker
# startup, which is not what the spec defines the metric as.
#
# WARM VS COLD CACHE: the first read of a 60 MB file comes off disk, later ones
# come out of the page cache, and the difference is larger than the effect you
# are trying to measure. You cannot drop caches without root on the VMs, so
# standardize on WARM: one discarded warm-up per class, then the timed trials.
# Say so explicitly in the report -- it is a real methodology choice, and stating
# it is worth more than pretending the question does not exist.
set -euo pipefail

CONFIG=${1:-config/machines.txt}
TRIALS=${2:-7}
OUT=${OUT:-report/data/latency.csv}

# Match counts, not vibes. Put these numbers in the report -- "frequent" means
# nothing to a grader without them. Assumes ~600k lines per 60 MB log.
#   rare      ~1e-5 of lines -> a handful of matches cluster-wide
#   infrequent~1e-3 of lines -> ~2.4k lines, small transfer
#   frequent  ~1e-1 of lines -> ~240k lines, ~24 MB back to the querier
declare -A QUERIES=(
    [rare]='RARE_TOKEN_7f3a'
    [infrequent]='SOMEWHAT_TOKEN_b21c'
    [frequent]='FREQUENT_TOKEN_e50d'
)

mkdir -p "$(dirname "$OUT")"
echo "query_class,trial,latency_ms,total_lines,machines_ok" > "$OUT"

for class in rare infrequent frequent; do
    pattern="${QUERIES[$class]}"
    echo "=== $class ($pattern) ==="

    # Warm-up, discarded.
    ./bin/log-query --config "$CONFIG" -- -c "$pattern" > /dev/null 2>&1 || true

    for t in $(seq 1 "$TRIALS"); do
        # TODO: have log-query emit a machine-readable summary line, e.g.
        #   SUMMARY latency_ms=412 total_lines=1423 machines_ok=4
        # and parse it here. Printing a parseable line beats scraping the pretty
        # table, and it keeps the human output free to stay readable.
        line=$(./bin/log-query --config "$CONFIG" --counts-only -- "$pattern" \
               | grep '^SUMMARY' || true)
        ms=$(sed -n 's/.*latency_ms=\([0-9]*\).*/\1/p'   <<< "$line")
        n=$( sed -n 's/.*total_lines=\([0-9]*\).*/\1/p'  <<< "$line")
        ok=$(sed -n 's/.*machines_ok=\([0-9]*\).*/\1/p'  <<< "$line")
        echo "$class,$t,${ms:-NA},${n:-NA},${ok:-NA}" >> "$OUT"
        echo "  trial $t: ${ms:-NA} ms, ${n:-NA} lines"
    done
done

echo
echo "wrote $OUT"
echo "now: python3 report/plot_latency.py $OUT"
