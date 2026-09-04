#!/usr/bin/env bash
# Bring up a local N-process cluster for development.
#
#   ./scripts/start_cluster.sh [N] [base_port] [log_dir]
#
# Lets you develop the whole MP before the VMs are provisioned: real sockets,
# real fan-out, real failure injection (just kill a PID).
set -euo pipefail

N=${1:-6}
BASE_PORT=${2:-9400}
LOG_DIR=${3:-./local_cluster}
SEED=${SEED:-42}
LINES=${LINES:-50000}

mkdir -p "$LOG_DIR"
: > "$LOG_DIR/pids"

for i in $(seq 1 "$N"); do
    ./bin/mp1gen --id "$i" --seed "$SEED" --lines "$LINES" --out-dir "$LOG_DIR"
    ./bin/mp1d --id "$i" --port $((BASE_PORT + i)) --log-dir "$LOG_DIR" \
        > "$LOG_DIR/mp1d.$i.out" 2>&1 &
    echo $! >> "$LOG_DIR/pids"
    echo "machine $i -> port $((BASE_PORT + i)) pid $!"
done

echo
echo "stop with: kill \$(cat $LOG_DIR/pids)"
echo "kill one machine to test fault tolerance: kill -9 <pid>"
