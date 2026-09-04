#!/usr/bin/env bash
# Pull, rebuild, and restart mp1d on every VM.
#
#   ./scripts/deploy.sh [config/machines.txt]
#
# The VMs have NO PERSISTENT STORAGE. After a reboot the clone, the binaries and
# the generated logs are all gone -- so this script has to rebuild from scratch,
# not just restart a daemon. Run it once at the start of every session, and
# rehearse it before the demo.
#
# Requires ssh key auth to the VMs and a PAT or deploy key for the clone.
set -euo pipefail

CONFIG=${1:-config/machines.txt}
REPO=${MP1_REPO:?set MP1_REPO to your course git remote}
DIR=${MP1_DIR:-~/mp1}
SEED=${SEED:-42}
LINES=${LINES:-300000}   # the demo's stated log size

# id host port, skipping comments and blanks
grep -vE '^\s*(#|$)' "$CONFIG" | while read -r id host port; do
    echo "=== machine $id ($host:$port) ==="
    ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new "$host" bash -s -- \
        "$REPO" "$DIR" "$id" "$port" "$SEED" "$LINES" <<'REMOTE' &
set -euo pipefail
REPO=$1; DIR=$2; ID=$3; PORT=$4; SEED=$5; LINES=$6

pkill -f "mp1d --id" || true

if [ -d "$DIR/.git" ]; then
    git -C "$DIR" fetch --quiet origin && git -C "$DIR" reset --hard --quiet origin/HEAD
else
    rm -rf "$DIR" && git clone --quiet "$REPO" "$DIR"
fi

cd "$DIR"
make --quiet all
./bin/mp1gen --id "$ID" --seed "$SEED" --lines "$LINES" --out-dir "$DIR"
nohup ./bin/mp1d --id "$ID" --port "$PORT" --log-dir "$DIR" \
    > "$DIR/mp1d.out" 2>&1 &
sleep 0.5
pgrep -f "mp1d --id $ID" > /dev/null && echo "machine $ID up" || { echo "machine $ID FAILED"; exit 1; }
REMOTE
done

wait
echo "=== deploy complete ==="
