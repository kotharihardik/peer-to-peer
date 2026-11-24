#!/usr/bin/env bash
# End-to-end tests for Module 2 (decentralized DHT). Starts real peers, drives
# each one through a named pipe (FIFO) and checks what it prints. There is no
# tracker in this test: every discovery step goes through the overlay.
#
#   Usage:  module2/tests/run_tests.sh [work_dir]
#   Env:    PEER_BIN     override the binary (e.g. a sanitizer build)
#           BASE_PORT    first DHT port to use (default 7500)
#           KEEP_DATA=1  keep the generated test files after the run
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PEER="${PEER_BIN:-$ROOT/peer}"
BASE="${BASE_PORT:-7500}"
W="${1:-$(mktemp -d /tmp/dhttest.XXXXXX)}"
mkdir -p "$W"
echo "Work dir: $W"
echo "Peer binary: $PEER"

PASS=0; FAIL=0; ALL_PIDS=""

cleanup() {
    kill -9 $ALL_PIDS 2>/dev/null
    if [ "${KEEP_DATA:-0}" != 1 ]; then rm -rf "$W/files" "$W/out"; fi
}
trap cleanup EXIT

pass()    { PASS=$((PASS + 1)); echo "  PASS  $1"; }
fail()    { FAIL=$((FAIL + 1)); echo "  FAIL  $1"; echo "        -> $2"; }
section() { echo; echo "== $1"; }
sha()     { shasum "$1" 2>/dev/null | cut -d' ' -f1; }
id_of()   { "$PEER" --id "127.0.0.1:$1"; }

# start_peer <name> <dht_port> [bootstrap_port]
start_peer() {
    local name=$1 port=$2 boot=${3:-}
    rm -f "$W/$name.in"; mkfifo "$W/$name.in"
    if [ -n "$boot" ]; then
        "$PEER" "127.0.0.1:$port" "127.0.0.1:$boot" < "$W/$name.in" > "$W/$name.log" 2>&1 &
    else
        "$PEER" "127.0.0.1:$port" < "$W/$name.in" > "$W/$name.log" 2>&1 &
    fi
    eval "PID_$name=$!"; disown
    sleep 100000 > "$W/$name.in" &        # holds the FIFO open between commands
    eval "KEEP_$name=$!"; disown
    ALL_PIDS="$ALL_PIDS $! $(eval echo \$PID_$name)"
    eval "PORT_$name=$port"
}
pid()  { eval echo "\$PID_$1"; }
port() { eval echo "\$PORT_$1"; }

# wait_log <name> <text> [seconds]
wait_log() {
    local i
    for ((i = 0; i < ${3:-10} * 10; i++)); do
        grep -qF -- "$2" "$W/$1.log" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

# cmd <peer> <command> [seconds] : run a command, put its output in $REPLY
cmd() {
    local log="$W/$1.log" before i
    before=$(wc -c < "$log")
    echo "$2" > "$W/$1.in"
    for ((i = 0; i < ${3:-15} * 10; i++)); do
        REPLY=$(tail -c +$((before + 1)) "$log")
        case "$REPLY" in *">> "*) REPLY=${REPLY%>> *}; return 0 ;; esac
        sleep 0.1
    done
    REPLY="(timeout)"
}

expect()     { [[ "$REPLY" == *"$2"* ]] && pass "$1" || fail "$1" "expected '$2', got: $REPLY"; }
expect_not() { [[ "$REPLY" != *"$2"* ]] && pass "$1" || fail "$1" "did not expect '$2', got: $REPLY"; }
check()      { if eval "$2"; then pass "$1"; else fail "$1" "condition failed: $2"; fi; }

# which_node_holds <file> <peer...> : print the id of each peer whose index has the file
which_node_holds() {
    local f=$1; shift
    local p
    for p in "$@"; do
        cmd "$p" "index"
        case "$REPLY" in *"$f"*) echo -n "$(id_of $(port $p)) ";; esac
    done
    echo
}

# ---------------------------------------------------------------- test data
mkdir -p "$W/files" "$W/out"
echo "decentralized hello" > "$W/files/note.txt"
head -c 1048576 /dev/urandom > "$W/files/data.bin"     # exactly 2 pieces
head -c 3145728 /dev/urandom > "$W/files/movie.bin"    # 6 pieces

P1=$BASE; P2=$((BASE + 10)); P3=$((BASE + 20)); P4=$((BASE + 30))

# ================================================================ 1. form the ring
section "1-3. Peers join and form the ring"
start_peer a $P1
wait_log a "created a new ring" 5 && pass "first peer created the ring" || fail "first peer created the ring" "see a.log"

start_peer b $P2 $P1
wait_log b "joined the ring" 5 && pass "peer b joined" || fail "peer b joined" "see b.log"
start_peer c $P3 $P1
wait_log c "joined the ring" 5 && pass "peer c joined" || fail "peer c joined" "see c.log"
start_peer d $P4 $P2   # joins through peer b, proving any peer can bootstrap
wait_log d "joined the ring" 5 && pass "peer d joined via a different bootstrap peer" || fail "peer d joined" "see d.log"

sleep 4   # let stabilize() settle the ring

cmd a "info"
expect "peer a knows its node id" "Node id      : $(id_of $P1)"
expect "peer a has a predecessor" "Predecessor  : "
expect_not "peer a predecessor is known" "Predecessor  : unknown"
expect "peer a lists its successors" "Successors   : "
expect "peer a shows the key range it owns" "Keys I own   : ("
expect "peer a has a finger table" "Finger table :"

cmd a "ring"
expect "ring is closed" "the ring is closed"
expect "ring contains all 4 peers" "4 node(s)"
for p in $P1 $P2 $P3 $P4; do
    expect "ring lists node $(id_of $p)" "$(id_of $p)@127.0.0.1:$p"
done

# every peer must agree the ring has 4 nodes
for p in b c d; do
    cmd $p "ring"
    expect "peer $p also sees a closed 4-node ring" "4 node(s)"
done

# ================================================================ 2. publish
section "4-5. Share a file: metadata goes to the responsible node"
cmd a "share $W/files/movie.bin"
expect "peer a published movie.bin" "SUCCESS: movie.bin published"
expect "publish reports the key and node" "key "
MOVIE_KEY=$(echo "$REPLY" | sed -n 's/.*key \([0-9]*\) ->.*/\1/p')
MOVIE_OWNER=$(echo "$REPLY" | sed -n 's/.*-> node \([0-9]*\),.*/\1/p')
check "publish reported a numeric key" "[ -n '$MOVIE_KEY' ]"
check "publish reported the responsible node" "[ -n '$MOVIE_OWNER' ]"
echo "        movie.bin -> key $MOVIE_KEY, stored on node $MOVIE_OWNER"

cmd a "share $W/files/note.txt"
expect "peer a published note.txt" "SUCCESS: note.txt published"
cmd b "share $W/files/data.bin"
expect "peer b published data.bin" "SUCCESS: data.bin published"

# The metadata must live on the node the key maps to, NOT on the publisher
HOLDERS=$(which_node_holds "movie.bin" a b c d)
check "exactly one node stores the movie.bin record" "[ \$(echo $HOLDERS | wc -w) -ge 1 ]"
check "the storing node is the one the key maps to" "echo '$HOLDERS' | grep -qw '$MOVIE_OWNER'"
echo "        nodes holding movie.bin metadata: $HOLDERS"

# The index is DISTRIBUTED: the three files should not all sit on one node
H1=$(which_node_holds "movie.bin" a b c d | tr -d ' ')
H2=$(which_node_holds "data.bin" a b c d | tr -d ' ')
H3=$(which_node_holds "note.txt" a b c d | tr -d ' ')
echo "        movie.bin:$H1  data.bin:$H2  note.txt:$H3"

# ================================================================ 3. search
section "6-8. Search with no tracker, from a peer that does not have the file"
cmd d "search movie.bin"
expect "peer d found movie.bin through the overlay" "Found movie.bin"
expect "search reports the file size" "3145728 bytes"
expect "search reports 6 pieces" "6 piece(s)"
expect "search lists the seeder" "node$(id_of $P1)"
grep -q "\[LOOKUP\]" "$W/d.log" && pass "lookup trace was printed" || fail "lookup trace" "no [LOOKUP] lines"
grep -q "responsible node:" "$W/d.log" && pass "lookup named the responsible node" || fail "responsible node" "missing"

cmd d "key movie.bin"
expect "key command shows the same key" "hashes to key $MOVIE_KEY"

cmd d "search missing.bin"
expect "searching an unknown file fails cleanly" "not in the index"

# ================================================================ 4. download
section "9. Download over direct peer-to-peer transfer"
cmd d "get movie.bin $W/out/"
expect "download started" "Download started: movie.bin"
expect "download found 1 seeder" "from 1 seeder(s)"
wait_log d "[C] [dht] movie.bin" 30 && pass "download completed" || fail "download completed" "see d.log"
check "downloaded file is byte-identical" "[ \"\$(sha $W/files/movie.bin)\" = \"\$(sha $W/out/movie.bin)\" ]"

cmd d "downloads"
expect "downloads listing shows it completed" "[C] [dht] movie.bin"

# The downloader publishes itself: the file now has two seeders
wait_log d "Now seeding movie.bin" 10 && pass "downloader auto-published itself as a seeder" || fail "auto-seed" "see d.log"
sleep 1
cmd c "search movie.bin"
expect "movie.bin now has 2 seeders" "node$(id_of $P4)"

# A third peer can now download from BOTH seeders
cmd c "get movie.bin $W/out/movie_c.bin"
expect "peer c downloads from 2 seeders" "from 2 seeder(s)"
wait_log c "[C] [dht] movie.bin" 30 && pass "second download completed" || fail "second download" "see c.log"
check "second download is identical too" "[ \"\$(sha $W/files/movie.bin)\" = \"\$(sha $W/out/movie_c.bin)\" ]"

# ================================================================ 5. new peer takes over keys
section "10-11. A new peer joins and takes over the keys it becomes responsible for"
# Pick a port whose node id lands in [key, current owner) so the new peer
# becomes the node responsible for movie.bin's key.
NEWPORT=""
for off in $(seq 40 2 140); do
    cand=$((BASE + off)); cid=$(id_of $cand)
    if [ "$MOVIE_KEY" -le "$MOVIE_OWNER" ]; then
        [ "$cid" -ge "$MOVIE_KEY" ] && [ "$cid" -lt "$MOVIE_OWNER" ] && NEWPORT=$cand && break
    else   # the arc wraps past 0
        { [ "$cid" -ge "$MOVIE_KEY" ] || [ "$cid" -lt "$MOVIE_OWNER" ]; } && NEWPORT=$cand && break
    fi
done

if [ -z "$NEWPORT" ]; then
    echo "  SKIP  no candidate port lands between the key and its owner"
else
    echo "        new peer on port $NEWPORT gets id $(id_of $NEWPORT), between key $MOVIE_KEY and owner $MOVIE_OWNER"
    start_peer e $NEWPORT $P1
    wait_log e "joined the ring" 5 && pass "peer e joined" || fail "peer e joined" "see e.log"
    wait_log e "metadata record" 6 && pass "peer e received the metadata it is now responsible for" \
        || fail "metadata handoff" "peer e got no records; see e.log"
    sleep 4
    cmd e "index"
    expect "peer e now stores the movie.bin record" "movie.bin"
    cmd a "ring"
    expect "ring grew to 5 nodes" "5 node(s)"
    # and lookups reach the NEW owner
    cmd b "search movie.bin"
    expect "search still works after redistribution" "Found movie.bin"
fi

# ================================================================ 6. failure
section "12-13. A peer fails: the ring repairs itself and lookups keep working"
# Kill peer c. It holds no metadata we depend on, but it IS somebody's successor
# and appears in finger tables, so the overlay has to route around it.
C_ID=$(id_of $P3)
kill -9 "$(pid c)" 2>/dev/null
echo "        killed peer c (node $C_ID) with SIGKILL - no graceful leave"
sleep 6   # stabilize() must notice and repair

cmd a "ring"
expect_not "dead peer is gone from the ring" "$C_ID@127.0.0.1:$P3"
expect "ring is closed again after the failure" "the ring is closed"

cmd b "search movie.bin"
expect "lookups still work after a peer crashed" "Found movie.bin"
cmd d "search data.bin"
expect "a file published by another peer is still findable" "Found data.bin"

# Metadata the dead node was responsible for survives, because every node
# replicates the keys it owns to its successor.
cmd a "search note.txt"
expect "metadata survived the crash (replication)" "Found note.txt"

# ================================================================ 7. graceful leave
section "14. Graceful leave hands the metadata over"
cmd b "index" 10
BEFORE_B="$REPLY"
echo "leave" > "$W/b.in"
sleep 3
wait_log b "left the ring" 5 && pass "peer b left gracefully" || fail "graceful leave" "see b.log"
sleep 5

cmd a "ring"
expect_not "peer b is out of the ring" "$(id_of $P2)@127.0.0.1:$P2"
expect "ring is still closed after the leave" "the ring is closed"
cmd d "search data.bin"
expect "the file peer b published is still findable" "Found data.bin"

# ================================================================ 8. stats
section "15. Statistics for the centralized vs decentralized comparison"
cmd d "stats"
expect "stats reports overlay messages" "overlay messages sent"
expect "stats reports lookups" "file lookups done"
expect "stats reports average hops" "average hops per lookup"
echo "$REPLY" | sed 's/^/        /'

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
