#!/usr/bin/env bash
# Build and start the server on every VM listed in machines.txt.
#
#   MP1_REPO=https://github.com/CyT1ng/CS425_MP1.git ./deploy.sh
#
# Run this ONCE, from ONE machine (your laptop, or any VM). It ssh's out to all
# of them itself -- do not run it on each VM.
#
# Environment:
#   MP1_REPO   the git URL the VMs clone from                (required)
#   MP1_USER   ssh username, if it differs from your local one
#   MP1_DIR    directory on each VM, relative to its home    (default: mp1)
#   LINES      lines per log file                            (default: 300000)
#
# The VMs have no persistent storage: after a reboot the clone, the binaries and
# the logs are all gone. Running this again is the entire recovery procedure.
set -euo pipefail

REPO=${MP1_REPO:?set MP1_REPO to your git URL}
USER_AT=${MP1_USER:+$MP1_USER@}
DIR=${MP1_DIR:-mp1}
LINES=${LINES:-300000}
BRANCH=$(git rev-parse --abbrev-ref HEAD)

# The VMs clone from GitHub, so anything not pushed is not deployed. Finding
# that out after an hour of debugging is the most expensive mistake here.
if [ -n "$(git status --porcelain)" ] || \
   [ "$(git rev-parse HEAD)" != "$(git rev-parse "@{upstream}" 2>/dev/null || echo none)" ]; then
    echo "deploy.sh: commit and push first -- the VMs would build your old code."
    echo "           (or re-run with SKIP_GIT_CHECK=1)"
    [ -z "${SKIP_GIT_CHECK:-}" ] && exit 1
fi

OUT=$(mktemp -d)
echo "deploying branch '$BRANCH' to every machine in machines.txt"

# One ssh per machine, all started before any is waited for -- the same idea as
# the querier itself. Note the loop reads from a process substitution, not a
# pipe: a `while` on the right of a pipe runs in a subshell, and the background
# jobs would die with it.
pids=(); names=(); logs=()
while read -r id host port; do
    echo "  starting machine $id ($host:$port)"
    ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new "$USER_AT$host" \
        bash -s -- "$REPO" "$BRANCH" "$DIR" "$id" "$port" "$LINES" \
        > "$OUT/machine-$id.log" 2>&1 <<'REMOTE' &
set -euo pipefail
REPO=$1; BRANCH=$2; DIR=$3; ID=$4; PORT=$5; LINES=$6

# Relative, so it resolves against THIS machine's home -- the laptop that sent
# this script has no idea what the home directory here is called.
case "$DIR" in /*) ;; *) DIR="$HOME/$DIR" ;; esac

pkill -f './server' || true      # our previous run
pkill -f 'log-server' || true    # and the older version, if it still holds the port

if [ -d "$DIR/.git" ]; then
    git -C "$DIR" fetch --quiet origin "$BRANCH"
    git -C "$DIR" reset --hard --quiet FETCH_HEAD
else
    rm -rf "$DIR"
    git clone --quiet --branch "$BRANCH" "$REPO" "$DIR"
fi

cd "$DIR"
make --quiet all
./gen_logs.sh "$ID" "$LINES" "$DIR" > /dev/null
nohup ./server "$ID" "$PORT" "$DIR" > "$DIR/server.out" 2>&1 &

# Wait until it is really accepting connections, rather than sleeping and
# hoping. bash's /dev/tcp needs nothing installed.
for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
        exec 3<&- 3>&-
        echo "machine $ID up on port $PORT ($(wc -l < "$DIR/machine.$ID.log") lines)"
        exit 0
    fi
    sleep 0.2
done
echo "machine $ID FAILED to start" >&2
tail -n 20 "$DIR/server.out" >&2 || true
exit 1
REMOTE
    pids+=("$!"); names+=("machine $id ($host)"); logs+=("$OUT/machine-$id.log")
done < <(grep -vE '^[[:space:]]*(#|$)' machines.txt)

# Each machine's status checked separately: a bare `wait` returns only the last
# job's result and would hide nine failures out of ten.
echo
failed=0
for i in "${!pids[@]}"; do
    if wait "${pids[$i]}"; then
        echo "  ok      ${names[$i]}"
    else
        echo "  FAILED  ${names[$i]}"
        tail -n 5 "${logs[$i]}" | sed 's/^/            /'
        failed=$((failed + 1))
    fi
done

echo
if [ "$failed" -eq 0 ]; then
    echo "all ${#pids[@]} machines up.  try:  ./query machines.txt -c ERROR"
else
    echo "$failed machine(s) failed. full output in $OUT"
    exit 1
fi
