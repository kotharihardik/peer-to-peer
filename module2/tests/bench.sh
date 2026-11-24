#!/usr/bin/env bash
# Measures how lookup cost grows with the number of peers, which is the
# O(log N) claim, and how much background traffic decentralization costs.
#
#   Usage:  module2/tests/bench.sh [work_dir]
#   Env:    SIZES="4 8 16 32"   ring sizes to measure
#           LOOKUPS=40          lookups per ring size
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PEER="${PEER_BIN:-$ROOT/peer}"
BASE="${BASE_PORT:-9000}"
SIZES="${SIZES:-2 4 8 16 32}"
LOOKUPS="${LOOKUPS:-40}"
W="${1:-$(mktemp -d /tmp/dhtbench.XXXXXX)}"
mkdir -p "$W"

PIDS=""
stop_all() { kill -9 $PIDS 2>/dev/null; PIDS=""; sleep 0.5; }
trap 'stop_all' EXIT

start_peer() {
    local name=$1 port=$2 boot=${3:-}
    rm -f "$W/$name.in"; mkfifo "$W/$name.in"
    if [ -n "$boot" ]; then "$PEER" "127.0.0.1:$port" "127.0.0.1:$boot" < "$W/$name.in" > "$W/$name.log" 2>&1 &
    else "$PEER" "127.0.0.1:$port" < "$W/$name.in" > "$W/$name.log" 2>&1 & fi
    PIDS="$PIDS $!"; disown
    sleep 100000 > "$W/$name.in" & PIDS="$PIDS $!"; disown
}

# ask <peer> <command> -> output in $OUT
ask() {
    local log="$W/$1.log" before i
    before=$(wc -c < "$log")
    echo "$2" > "$W/$1.in"
    for ((i = 0; i < 150; i++)); do
        OUT=$(tail -c +$((before + 1)) "$log")
        case "$OUT" in *">> "*) return 0 ;; esac
        sleep 0.1
    done
}

printf "%6s  %12s  %14s  %22s\n" "peers" "avg hops" "max hops" "msgs/lookup (hops+2)"
printf "%6s  %12s  %14s  %22s\n" "-----" "--------" "--------" "--------------------"
RESULTS=""

for N in $SIZES; do
    stop_all
    # peer 0 creates the ring; the rest join through a random existing peer,
    # so no single peer acts as a bootstrap server.
    start_peer p0 $BASE; sleep 0.8
    for ((i = 1; i < N; i++)); do
        prev=$(( RANDOM % i ))
        start_peer p$i $((BASE + i * 2)) $((BASE + prev * 2))
        sleep 0.35
    done
    sleep $(( 6 + N / 4 ))     # let stabilize() and the finger tables settle

    # Confirm the ring actually closed before trusting any numbers
    ask p0 "ring"
    CLOSED=$(echo "$OUT" | grep -c "ring is closed")
    SEEN=$(echo "$OUT" | sed -n 's/.*the ring is closed (\([0-9]*\) node.*/\1/p')

    TOTAL=0; MAX=0; COUNT=0
    for ((k = 0; k < LOOKUPS; k++)); do
        # ask from a different peer each time, for random keys
        from=p$(( RANDOM % N ))
        ask $from "key bench_file_${k}.bin"
        h=$(echo "$OUT" | sed -n 's/.*(\([0-9]*\) hop(s)).*/\1/p' | tail -1)
        [ -z "$h" ] && continue
        TOTAL=$((TOTAL + h)); COUNT=$((COUNT + 1))
        [ "$h" -gt "$MAX" ] && MAX=$h
    done

    if [ "$COUNT" -gt 0 ]; then
        AVG=$(echo "scale=2; $TOTAL / $COUNT" | bc)
        MSGS=$(echo "scale=2; ($TOTAL / $COUNT) + 2" | bc)
    else
        AVG="n/a"; MSGS="n/a"
    fi
    printf "%6s  %12s  %14s  %22s   (ring closed: %s nodes)\n" "$N" "$AVG" "$MAX" "$MSGS" "${SEEN:-?}"
    RESULTS="$RESULTS$N $AVG $MAX\n"

    # background maintenance traffic per peer: sample stats 10 s apart
    if [ "$N" = "32" ] || [ "$N" = "8" ]; then
        ask p1 "stats"; M1=$(echo "$OUT" | sed -n 's/.*messages sent *: \([0-9]*\).*/\1/p')
        sleep 10
        ask p1 "stats"; M2=$(echo "$OUT" | sed -n 's/.*messages sent *: \([0-9]*\).*/\1/p')
        if [ -n "$M1" ] && [ -n "$M2" ]; then
            echo "        idle maintenance traffic at N=$N: $(echo "scale=1; ($M2 - $M1) / 10" | bc) msgs/sec/peer"
        fi
    fi
done

echo
echo "log2(N) for reference:"
for N in $SIZES; do printf "  N=%-3s log2=%s\n" "$N" "$(echo "scale=2; l($N)/l(2)" | bc -l)"; done
