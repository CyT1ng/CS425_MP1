#!/usr/bin/env bash
# Pull, rebuild, generate logs, and restart log-server on every VM.
#
#   ./scripts/deploy.sh [config/machines.txt]
#
# Environment:
#   MP1_REPO   the git remote the VMs clone from            (required)
#   MP1_USER   ssh username, if your netid differs from your
#              local one -- it almost certainly does           (default: unset)
#   MP1_DIR    where to put it on each VM, relative to that
#              machine's home unless it starts with '/'     (default: mp1)
#   SEED       log generator seed                           (default: 42)
#   LINES      lines per log                                (default: 300000)
#   BYTES      target bytes per log; overrides LINES. Use
#              BYTES=60000000 for the report's measurements (default: unset)
#   SKIP_GIT_CHECK=1  deploy even with unpushed local work
#
# The VMs have NO PERSISTENT STORAGE. After a reboot the clone, the binaries and
# the generated logs are all gone -- so this rebuilds from scratch rather than
# just restarting a daemon. Run it at the start of every session, and rehearse
# it before the demo.
#
# Requires ssh key auth to the VMs and a PAT or deploy key for the clone.
set -euo pipefail

CONFIG=${1:-config/machines.txt}
REPO=${MP1_REPO:?set MP1_REPO to your course git remote}
DIR=${MP1_DIR:-mp1}
SEED=${SEED:-42}
LINES=${LINES:-300000}
BYTES=${BYTES:-0}
BRANCH=${MP1_BRANCH:-$(git rev-parse --abbrev-ref HEAD)}
SSH_USER=${MP1_USER:-}

# --- deploy what you think you are deploying -------------------------------
# Every VM clones from the REMOTE, so anything still sitting on this laptop is
# not going anywhere. Deploying stale code and then debugging the symptoms is
# the single most expensive mistake available here, so it is a hard stop.
if [ -z "${SKIP_GIT_CHECK:-}" ]; then
    problems=""
    [ -n "$(git status --porcelain)" ] && problems+="  - uncommitted changes in the working tree\n"
    if ! git rev-parse --quiet --verify "@{upstream}" >/dev/null 2>&1; then
        problems+="  - this branch has no upstream; push it first\n"
    elif [ "$(git rev-parse HEAD)" != "$(git rev-parse '@{upstream}')" ]; then
        problems+="  - HEAD differs from @{upstream}; commit and push first\n"
    fi
    if [ -n "$problems" ]; then
        printf 'deploy.sh: the VMs would not get your current code:\n%b' "$problems"
        echo   "           fix that, or re-run with SKIP_GIT_CHECK=1 to deploy anyway."
        exit 1
    fi
fi

OUT=$(mktemp -d "${TMPDIR:-/tmp}/mp1-deploy-XXXXXX")
echo "deploying $BRANCH from $REPO"
echo "per-machine output: $OUT"
echo

# --- fan out ---------------------------------------------------------------
# One ssh per machine, all at once -- ten sequential clone-and-builds is a long
# wait for something that parallelises perfectly.
#
# The loop reads from a process substitution, NOT from a pipe. A `while` on the
# right of a pipe runs in a subshell, its background jobs die with it, and the
# `wait` below would return instantly and report success before a single machine
# had finished.
pids=()
labels=()
logs=()
while read -r id host port; do
    echo "  starting machine $id ($host:$port)"
    ssh -o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new \
        "${SSH_USER:+$SSH_USER@}$host" \
        bash -s -- "$REPO" "$BRANCH" "$DIR" "$id" "$port" "$SEED" "$LINES" "$BYTES" \
        > "$OUT/machine-$id.log" 2>&1 <<'REMOTE' &
set -euo pipefail
REPO=$1; BRANCH=$2; DIR=$3; ID=$4; PORT=$5; SEED=$6; LINES=$7; BYTES=$8

# The path arrives relative on purpose: the deploying laptop has no idea what
# this machine calls its home directory, so it is resolved here.
case "$DIR" in /*) ;; *) DIR="$HOME/$DIR" ;; esac

pkill -f "log-server --id" || true

if [ -d "$DIR/.git" ]; then
    git -C "$DIR" fetch --quiet origin "$BRANCH"
    git -C "$DIR" reset --hard --quiet FETCH_HEAD
else
    rm -rf "$DIR"
    git clone --quiet --branch "$BRANCH" "$REPO" "$DIR"
fi

cd "$DIR"
make --quiet all

if [ "$BYTES" -gt 0 ]; then
    ./bin/log-gen --id "$ID" --seed "$SEED" --bytes "$BYTES" --out-dir "$DIR"
else
    ./bin/log-gen --id "$ID" --seed "$SEED" --lines "$LINES" --out-dir "$DIR"
fi

nohup ./bin/log-server --id "$ID" --port "$PORT" --log-dir "$DIR" \
    > "$DIR/log-server.out" 2>&1 &

# Poll until the daemon is actually accepting connections. `sleep 0.5 && pgrep`
# proves only that a process exists, and on a loaded VM it does not even prove
# that. bash's /dev/tcp needs nothing installed on the machine.
for _ in $(seq 1 50); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
        exec 3<&- 3>&-
        echo "machine $ID: listening on $PORT, $(wc -l < "$DIR/machine.$ID.log") lines"
        exit 0
    fi
    sleep 0.2
done

echo "machine $ID: FAILED to start" >&2
tail -n 20 "$DIR/log-server.out" >&2 || true
exit 1
REMOTE
    pids+=("$!")
    labels+=("machine $id ($host)")
    logs+=("$OUT/machine-$id.log")
done < <(grep -vE '^[[:space:]]*(#|$)' "$CONFIG")

# --- collect ---------------------------------------------------------------
# Each machine's exit status is checked individually. `wait` with no arguments
# returns only the last job's status, which would hide nine failures out of ten.
echo
failed=()
for i in "${!pids[@]}"; do
    if wait "${pids[$i]}"; then
        printf '  ok      %s\n' "${labels[$i]}"
    else
        printf '  FAILED  %s\n' "${labels[$i]}"
        failed+=("$i")
    fi
done

echo
if [ ${#failed[@]} -eq 0 ]; then
    echo "=== all ${#pids[@]} machines up ==="
    echo "try:  ./bin/log-query --config $CONFIG -- -c ERROR"
    exit 0
fi

echo "=== ${#failed[@]} of ${#pids[@]} machines failed ==="
for i in "${failed[@]}"; do
    echo
    echo "--- ${labels[$i]} ---"
    tail -n 15 "${logs[$i]}" 2>/dev/null || true
done
echo
echo "full output per machine: $OUT"
exit 1
