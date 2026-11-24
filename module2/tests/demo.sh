#!/usr/bin/env bash
# Narrated demo of the decentralized (second generation) system.
# Unlike run_tests.sh this checks nothing - it just walks through the story in
# order and prints what each peer says, which is what you want when showing the
# project to somebody.
#
#   Usage:  module2/tests/demo.sh [work_dir]
#   Env:    BASE_PORT  first DHT port to use (default 7900)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PEER="${PEER_BIN:-$ROOT/peer}"
BASE="${BASE_PORT:-7900}"
W="${1:-$(mktemp -d /tmp/dhtdemo.XXXXXX)}"
mkdir -p "$W/files" "$W/out"

ALL_PIDS=""
cleanup() { kill -9 $ALL_PIDS 2>/dev/null; rm -rf "$W/files" "$W/out"; }
trap cleanup EXIT

say()  { echo; echo "######## $*"; }
note() { echo "         $*"; }

start_peer() {
    local name=$1 port=$2 boot=${3:-}
    rm -f "$W/$name.in"; mkfifo "$W/$name.in"
    if [ -n "$boot" ]; then "$PEER" "127.0.0.1:$port" "127.0.0.1:$boot" < "$W/$name.in" > "$W/$name.log" 2>&1 &
    else "$PEER" "127.0.0.1:$port" < "$W/$name.in" > "$W/$name.log" 2>&1 & fi
    eval "PID_$name=$!"; disown
    sleep 100000 > "$W/$name.in" & eval "KEEP_$name=$!"; disown
    ALL_PIDS="$ALL_PIDS $! $(eval echo \$PID_$name)"
}
pid() { eval echo "\$PID_$1"; }

# run <peer> <command> : send it and show whatever the peer printed
run() {
    local log="$W/$1.log" before i out
    before=$(wc -c < "$log")
    echo "         --- peer $1: $2"
    echo "$2" > "$W/$1.in"
    for ((i = 0; i < 200; i++)); do
        out=$(tail -c +$((before + 1)) "$log")
        case "$out" in *">> "*) break ;; esac
        sleep 0.1
    done
    tail -c +$((before + 1)) "$log" | sed 's/>> //' | sed '/^$/d' | sed 's/^/         /'
}

P1=$BASE; P2=$((BASE + 10)); P3=$((BASE + 20)); P4=$((BASE + 30)); P5=$((BASE + 40))

echo "hello from a decentralized network" > "$W/files/readme.txt"
head -c 3145728 /dev/urandom > "$W/files/movie.bin"

say "STEP 1  Four peers start and build a Chord ring (there is no tracker)"
start_peer alice $P1;            sleep 1
start_peer bob   $P2 $P1;        sleep 1
start_peer carol $P3 $P1;        sleep 1
start_peer dave  $P4 $P2;        sleep 4
note "each peer hashed its own address into a 16-bit node id:"
for p in $P1 $P2 $P3 $P4; do note "  127.0.0.1:$p -> node $("$PEER" --id 127.0.0.1:$p)"; done

say "STEP 2  Ring relationships: successor, predecessor, finger table"
run alice "info"

say "STEP 3  Walk the successor pointers all the way around"
run alice "ring"

say "STEP 4  Alice shares a file. Only METADATA is published, not the file."
run alice "share $W/files/movie.bin"
note "the key is hash(file name); the metadata went to whichever node owns that key"

say "STEP 5  The index is DISTRIBUTED - look at what each node stores"
run alice "index"
run bob   "index"
run carol "index"
run dave  "index"

say "STEP 6  Dave searches WITHOUT a tracker: the query is routed over the overlay"
run dave "search movie.bin"

say "STEP 7  Which node is responsible for the key, and how many hops away"
run dave "key movie.bin"

say "STEP 8  Dave downloads straight from Alice (peer to peer, 512 KB pieces)"
run dave "get movie.bin $W/out/"
sleep 3
run dave "downloads"
if cmp -s "$W/files/movie.bin" "$W/out/movie.bin"; then note "downloaded file is byte-for-byte identical"; fi

say "STEP 9  Dave now seeds it too, so the file has two sources"
run carol "search movie.bin"

say "STEP 10  A fifth peer joins and TAKES OVER the keys it becomes responsible for"
note "new peer 127.0.0.1:$P5 will get node id $("$PEER" --id 127.0.0.1:$P5)"
start_peer erin $P5 $P1
sleep 5
run erin "index"
run erin "info"
run alice "ring"

say "STEP 11  Carol is KILLED (SIGKILL, no goodbye). The ring must repair itself."
note "killing node $("$PEER" --id 127.0.0.1:$P3) ..."
kill -9 "$(pid carol)" 2>/dev/null
sleep 7
run alice "ring"
note "lookups still work, because every node replicates the keys it owns to its successor"
run dave "search movie.bin"

say "STEP 12  Bob leaves gracefully and hands its metadata to its successor"
run bob "index"
echo "leave" > "$W/bob.in"
sleep 4
tail -2 "$W/bob.log" | sed 's/^/         /'
sleep 4
run alice "ring"
run dave "search movie.bin"

say "STEP 13  Lookup statistics (compare with module 1's single tracker request)"
run dave "stats"

say "DONE   logs are in $W"
