#!/usr/bin/env bash
# Bring up a pretend cluster of N machines on this one computer, so you can see
# the whole thing work without any VMs. Each "machine" is a separate process
# with its own log file and its own port -- as far as the code is concerned,
# that is a real distributed system.
#
#   ./run_local.sh [N] [lines-per-machine]
#
# Writes local.txt -- the machine list for the pretend cluster. machines.txt is
# the real VMs and is left alone.
set -euo pipefail

N=${1:-3}
LINES=${2:-10000}
DIR=./cluster

make -s all
rm -rf "$DIR"; mkdir -p "$DIR"
: > "$DIR/pids"
: > local.txt

for i in $(seq 1 "$N"); do
    ./gen_logs.sh "$i" "$LINES" "$DIR"
    ./server "$i" $((9000 + i)) "$DIR" > "$DIR/server.$i.out" 2>&1 &
    echo $! >> "$DIR/pids"
    echo "$i 127.0.0.1 $((9000 + i))" >> local.txt
done

sleep 0.5
echo
echo "cluster of $N machines is up. try:"
echo "    ./query local.txt COMMON_TOKEN | tail -5"
echo "    ./query local.txt -c ERROR"
echo "    kill -9 \$(sed -n 2p $DIR/pids)     # kill machine 2, then query again"
echo
echo "stop everything with:  kill \$(cat $DIR/pids)"
