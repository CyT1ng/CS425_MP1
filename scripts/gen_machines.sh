#!/usr/bin/env bash
# Generate config/machines.txt for your group.
#
#   ./scripts/gen_machines.sh <gid> [n_vms] [port] > config/machines.txt
#
# The course VM table lists hostnames as fa26-cs425-<gid>NN.cs.illinois.edu,
# where NN is the VM index 01..10 -- not a literal string. Group 03's VMs are
# fa26-cs425-0301 ... fa26-cs425-0310.
set -euo pipefail

GID=${1:?usage: gen_machines.sh <gid> [n_vms] [port]   e.g. gen_machines.sh 03}
N=${2:-10}
PORT=${3:-4425}

echo "# CS425 MP1 cluster -- group $GID"
echo "# <id> <host> <port>"
for i in $(seq 1 "$N"); do
    printf '%-4s %s %s\n' "$i" \
        "fa26-cs425-${GID}$(printf '%02d' "$i").cs.illinois.edu" "$PORT"
done
