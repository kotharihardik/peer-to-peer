#!/usr/bin/env bash
# End-to-end tests for Module 1. Starts real trackers and clients, drives each
# one through a named pipe (FIFO) and checks what it prints.
#
#   Usage:  tests/run_tests.sh [work_dir]
#   Env:    TRACKER_BIN / CLIENT_BIN  override the binaries (e.g. sanitizer builds)
#           HUGE_MB                   size of the file used for the disconnect test
#           KEEP_DATA=1               keep the generated test files after the run
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRACKER="${TRACKER_BIN:-$ROOT/tracker/tracker}"
CLIENT="${CLIENT_BIN:-$ROOT/client/client}"
HUGE_MB="${HUGE_MB:-300}"
W="${1:-$(mktemp -d /tmp/p2ptest.XXXXXX)}"
mkdir -p "$W"
echo "Work dir: $W"

PASS=0; FAIL=0; ALL_PIDS=""
T1=9500; T2=9501                      # tracker client ports (sync ports = +1000)
printf "127.0.0.1 $T1\n127.0.0.1 $T2\n" > "$W/ti.txt"      # clients try tracker 1 first
printf "127.0.0.1 $T2\n127.0.0.1 $T1\n" > "$W/ti_rev.txt"  # clients try tracker 2 first

# On exit: stop every process and delete the large test files (logs are kept).
# Set KEEP_DATA=1 to keep the files for debugging.
cleanup() {
    kill -9 $ALL_PIDS 2>/dev/null
    if [ "${KEEP_DATA:-0}" != 1 ]; then
        rm -rf "$W/files" "$W/bobfiles" "$W/bob" "$W/carol" "$W/dave"
    fi
}
trap cleanup EXIT

pass() { PASS=$((PASS + 1)); echo "  PASS  $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL  $1"; echo "        -> $2"; }
section() { echo; echo "== $1"; }
sha() { shasum "$1" 2>/dev/null | cut -d' ' -f1; }

# start <name> <command...> : stdin from <name>.in (FIFO), output to <name>.log
start() {
    local name=$1; shift
    rm -f "$W/$name.in"; mkfifo "$W/$name.in"
    "$@" < "$W/$name.in" > "$W/$name.log" 2>&1 &
    eval "PID_$name=$!"; disown
    sleep 100000 > "$W/$name.in" &       # holds the FIFO open between commands
    eval "KEEP_$name=$!"; disown
    ALL_PIDS="$ALL_PIDS $! $(eval echo \$PID_$name)"
}
pid() { eval echo "\$PID_$1"; }

# wait_log <name> <text> [seconds] : wait until <name>.log contains <text>
wait_log() {
    local i
    for ((i = 0; i < ${3:-10} * 10; i++)); do
        grep -qF -- "$2" "$W/$1.log" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

# cmd <client> <command> [seconds] : run a command, put its output in $REPLY
cmd() {
    local log="$W/$1.log" before i
    before=$(wc -c < "$log")
    echo "$2" > "$W/$1.in"
    for ((i = 0; i < ${3:-10} * 10; i++)); do
        REPLY=$(tail -c +$((before + 1)) "$log")
        case "$REPLY" in *">> "*) REPLY=${REPLY%>> *}; return 0 ;; esac   # next prompt = done
        sleep 0.1
    done
    REPLY="(timeout)"
}

expect()     { [[ "$REPLY" == *"$2"* ]] && pass "$1" || fail "$1" "expected '$2', got: $REPLY"; }
expect_not() { [[ "$REPLY" != *"$2"* ]] && pass "$1" || fail "$1" "did not expect '$2', got: $REPLY"; }
check()      { if eval "$2"; then pass "$1"; else fail "$1" "condition failed: $2"; fi; }
same_file()  { [ -n "$(sha "$2")" ] && [ "$(sha "$1")" = "$(sha "$2")" ]; }

# login_client <name> <port> <tracker_file> : start a client, create + login its user
login_client() {
    start "$1" "$CLIENT" "127.0.0.1:$2" "$W/$3"
    wait_log "$1" "Connected to tracker" 5
    cmd "$1" "create_user $1 pw"
    cmd "$1" "login $1 pw"
    expect "$1 logged in" "Logged in"
}

# ---------------------------------------------------------------- test data
mkdir -p "$W/files" "$W/bob" "$W/carol" "$W/dave" "$W/bobfiles"
echo "hello peer to peer" > "$W/files/small.txt"
head -c 1048576 /dev/urandom > "$W/files/exact.bin"          # exactly 2 pieces
: > "$W/files/empty.bin"                                      # 0 bytes, 0 pieces
head -c 5243003 /dev/urandom > "$W/files/mid.bin"            # 11 pieces, short last piece
head -c 41943040 /dev/urandom > "$W/files/big.bin"           # 80 pieces
head -c 3145728 /dev/urandom > "$W/files/corrupt.bin"
head -c 2097152 /dev/urandom > "$W/files/bad.bin"
head -c $((HUGE_MB * 1048576)) /dev/urandom > "$W/files/huge.bin"
cp "$W/files/corrupt.bin" "$W/files/huge.bin" "$W/bobfiles/"

# ================================================================ 1 tracker + 1 client
section "One tracker + one client: users, groups, upload, list"
start t1 "$TRACKER" "$W/ti.txt" 1
check "tracker 1 started alone" "wait_log t1 'Tracker running' 5"
start alice "$CLIENT" 127.0.0.1:9601 "$W/ti.txt"
wait_log alice "Connected to tracker" 5

cmd alice "create_group g1";            expect "commands need login" "Please login first"
cmd alice "create_user alice pw";       expect "create_user" "User created"
cmd alice "create_user alice pw";       expect "duplicate user rejected" "already exists"
cmd alice "login alice wrong";          expect "wrong password rejected" "Wrong password"
cmd alice "login ghost pw";             expect "unknown user rejected" "does not exist"
cmd alice "login alice pw";             expect "login" "Logged in"
cmd alice "login alice pw";             expect "double login rejected" "Already logged in"
cmd alice "create_group g1";            expect "create_group" "Group created"
cmd alice "create_group g1";            expect "duplicate group rejected" "already exists"
cmd alice "list_groups";                expect "list_groups" "g1"
cmd alice "join_group";                 expect "missing argument -> usage" "Usage"
cmd alice "fly_to_moon";                expect "unknown command" "Unknown command"
cmd alice "upload_file g1 /no/such/file"; expect "upload missing file" "File not found"
cmd alice "upload_file nogroup $W/files/small.txt"; expect "upload to missing group" "Group does not exist"
for f in small.txt exact.bin empty.bin mid.bin big.bin corrupt.bin bad.bin; do
    cmd alice "upload_file g1 $W/files/$f" 20; expect "upload $f" "File shared"
done
cmd alice "upload_file g1 $W/files/huge.bin" 60; expect "upload huge.bin (${HUGE_MB} MB)" "File shared"
cmd alice "list_files g1";              expect "list_files" "small.txt (19 bytes, 1 seeder(s) online)"
cmd alice "stop_share g1 small.txt";    expect "stop_share" "Stopped sharing"
cmd alice "list_files g1";              expect_not "stopped file no longer listed" "small.txt"
cmd alice "stop_share g1 small.txt";    expect "stop_share unknown file" "File not found"
cmd alice "upload_file g1 $W/files/small.txt"; expect "share again" "File shared"
cmd alice "download_file g1 small.txt $W/x"; expect "cannot download from yourself only" "No other seeder"
cmd alice "logout";                     expect "logout" "Logged out"
cmd alice "list_files g1";              expect "logged out -> login required" "Please login first"
cmd alice "login alice pw";             expect "login again" "Logged in"

# ================================================================ 2 trackers + clients
section "Two trackers: recovery on start + live synchronization"
start t2 "$TRACKER" "$W/ti.txt" 2
check "tracker 2 pulled state from tracker 1" "wait_log t2 'Recovered state from tracker' 5"
check "tracker 1 linked to tracker 2" "wait_log t1 'Connected to tracker 127.0.0.1:$T2' 5"

login_client bob 9602 ti_rev.txt                      # bob talks to tracker 2
check "bob is connected to tracker 2" "grep -q 'Connected to tracker 127.0.0.1:$T2' $W/bob.log"
cmd bob "list_groups";                  expect "tracker 2 knows g1 (from snapshot)" "g1"
cmd bob "list_files g1";                expect "non-member cannot list files" "not a member"
cmd bob "download_file g1 mid.bin $W/bob/"; expect "non-member cannot download" "not a member"
cmd bob "join_group g1";                expect "join_group via tracker 2" "Join request sent"
cmd bob "join_group g1";                expect "duplicate join rejected" "already sent"
sleep 0.5
cmd alice "list_requests g1";           expect "request synced to tracker 1" "bob"
cmd bob "accept_request g1 bob";        expect "only owner can accept" "Only the group owner"
cmd alice "accept_request g1 bob";      expect "accept_request" "Request accepted"
sleep 0.5
cmd bob "list_files g1";                expect "member sees files via tracker 2" "mid.bin (5243003 bytes"

section "Downloads: sizes, integrity, show_downloads"
cmd bob "download_file g1 mid.bin $W/bob/"; expect "download starts (directory destination)" "Download started"
check "mid.bin completed" "wait_log bob '[C] [g1] mid.bin' 30"
check "mid.bin identical to original" "same_file $W/files/mid.bin $W/bob/mid.bin"
cmd bob "download_file g1 exact.bin $W/bob/renamed.bin"
check "exact.bin completed (custom name)" "wait_log bob '[C] [g1] exact.bin' 30"
check "exact.bin identical" "same_file $W/files/exact.bin $W/bob/renamed.bin"
cmd bob "download_file g1 empty.bin $W/bob/"
check "empty file completed" "wait_log bob '[C] [g1] empty.bin' 30"
check "empty file has size 0" "[ -f $W/bob/empty.bin ] && [ ! -s $W/bob/empty.bin ]"
cmd bob "download_file g1 small.txt $W/bob/"
check "small file completed" "wait_log bob '[C] [g1] small.txt' 30"
check "small file identical" "same_file $W/files/small.txt $W/bob/small.txt"
cmd bob "download_file g1 nothing.bin $W/bob/"; expect "missing file" "File not found"
cmd bob "download_file g1 mid.bin /no/such/dir/x"; expect "bad destination" "Cannot create destination"
cmd bob "show_downloads";               expect "show_downloads [C] format" "[C] [g1] mid.bin"
check "bob auto-seeds the downloaded file" "wait_log bob 'Now seeding mid.bin: SUCCESS' 10"

section "Multi-peer and simultaneous downloads"
login_client carol 9603 ti.txt                        # carol talks to tracker 1
cmd carol "join_group g1"; sleep 0.3
cmd alice "accept_request g1 carol";    expect "carol accepted" "Request accepted"
sleep 0.3
cmd carol "download_file g1 mid.bin $W/carol/"; expect "download from 2 seeders" "from 2 seeder(s)"
check "multi-peer download completed" "wait_log carol '[C] [g1] mid.bin' 30"
check "pieces came from alice AND bob" "grep '\[C\] \[g1\] mid.bin' $W/carol.log | grep -q 'alice=[1-9].*bob=[1-9]'"
check "multi-peer file identical" "same_file $W/files/mid.bin $W/carol/mid.bin"
cmd carol "download_file g1 big.bin $W/carol/"
cmd carol "download_file g1 exact.bin $W/carol/"
cmd carol "show_downloads";             expect "two downloads listed" "exact.bin"
check "simultaneous download 1 done" "wait_log carol '[C] [g1] big.bin' 60"
check "simultaneous download 2 done" "wait_log carol '[C] [g1] exact.bin' 60"
check "big.bin identical" "same_file $W/files/big.bin $W/carol/big.bin"
check "exact.bin identical" "same_file $W/files/exact.bin $W/carol/exact.bin"

section "Corrupted pieces"
cmd bob "upload_file g1 $W/bobfiles/corrupt.bin" 20; expect "second seeder for corrupt.bin" "File shared"
printf 'GARBAGE' | dd of="$W/bobfiles/corrupt.bin" bs=1 seek=1048600 conv=notrunc 2>/dev/null  # break piece 2 at bob
printf 'GARBAGE' | dd of="$W/bobfiles/corrupt.bin" bs=1 seek=2621440 conv=notrunc 2>/dev/null  # and piece 5
cmd carol "download_file g1 corrupt.bin $W/carol/"
check "download recovers from corrupt pieces" "wait_log carol '[C] [g1] corrupt.bin' 30"
check "corrupt piece detected and discarded" "grep -q 'from bob failed SHA1 check' $W/carol.log"
check "result identical to the original" "same_file $W/files/corrupt.bin $W/carol/corrupt.bin"
printf 'GARBAGE' | dd of="$W/files/bad.bin" bs=1 seek=10 conv=notrunc 2>/dev/null   # only seeder is corrupt
cmd carol "download_file g1 bad.bin $W/carol/"
check "download with only corrupt copies fails" "wait_log carol '[F] [g1] bad.bin  (piece 0 unavailable from all seeders)' 30"
cmd carol "show_downloads";             expect "failed download shown as [F]" "[F] [g1] bad.bin"
check "failed download leaves no partial file" "[ ! -e $W/carol/bad.bin ]"

section "Peer disconnects during download"
cmd bob "upload_file g1 $W/bobfiles/huge.bin" 60; expect "bob also seeds huge.bin" "File shared"
cmd carol "download_file g1 huge.bin $W/carol/"; expect "huge download from 2 seeders" "from 2 seeder(s)"
cmd carol "download_file g1 huge.bin $W/carol/"; expect "duplicate download rejected" "already being downloaded"
sleep 0.2
kill -9 "$(pid bob)"                                   # bob vanishes mid-transfer
check "huge.bin still completes (from alice)" "wait_log carol '[C] [g1] huge.bin' 120"
check "huge.bin identical" "same_file $W/files/huge.bin $W/carol/huge.bin"
if grep -q 'no answer from bob' "$W/carol.log"; then pass "downloader noticed bob disappearing"
else echo "  NOTE  download finished before bob was killed (increase HUGE_MB to exercise this)"; fi
check "tracker 2 noticed bob's disconnect" "wait_log t2 'bob disconnected' 5"

section "Tracker failure and client failover"
kill -9 "$(pid t1)"                                    # alice and carol were on tracker 1
sleep 0.3
cmd alice "list_files g1" 20;           expect "alice keeps working via tracker 2" "Files in group g1"
check "alice re-logged in automatically" "grep -q 'Re-login after failover: SUCCESS' $W/alice.log"
cmd carol "create_group g2" 20;         expect "state change while tracker 1 is down" "Group created"
cmd carol "download_file g1 exact.bin $W/carol/exact_again.bin" 20
check "download works after failover" "wait_log carol 'exact_again.bin' 30 && wait_log carol '[C] [g1] exact.bin  saved to $W/carol/exact_again.bin' 30"

section "Tracker recovery"
start t1 "$TRACKER" "$W/ti.txt" 1
check "restarted tracker 1 recovered state from tracker 2" "wait_log t1 'Recovered state from tracker' 5"
check "tracker 2 re-linked to tracker 1" "wait_log t2 'Connected to tracker 127.0.0.1:$T1' 5"
login_client dave 9604 ti.txt                          # dave talks to restarted tracker 1
cmd dave "list_groups";                 expect "change made during outage is present" "g2"
cmd dave "create_group g3";             expect "new change on recovered tracker" "Group created"
sleep 0.5
cmd alice "list_groups";                expect "change propagated tracker 1 -> tracker 2" "g3"
start mallory "$CLIENT" 127.0.0.1:9605 "$W/ti_rev.txt"; wait_log mallory "Connected to tracker" 5
cmd mallory "login alice pw";           expect "same user cannot log in twice on one tracker" "User already logged in"

section "Concurrent operations"
for c in alice carol dave; do
    ( for i in $(seq 1 30); do echo "create_group c_${c}_$i" > "$W/$c.in"; done ) &
done
sleep 3
cmd dave "list_groups";  n1=$(echo "$REPLY" | grep -c '^c_')
cmd alice "list_groups"; n2=$(echo "$REPLY" | grep -c '^c_')
check "all 90 concurrently created groups on tracker 1 ($n1)" "[ $n1 -eq 90 ]"
check "all 90 concurrently created groups on tracker 2 ($n2)" "[ $n2 -eq 90 ]"
cmd dave "join_group g1"; sleep 0.3; cmd alice "accept_request g1 dave"; sleep 0.3
cmd carol "download_file g1 big.bin $W/carol/big2.bin"
cmd dave "download_file g1 big.bin $W/dave/"
check "concurrent download by carol" "wait_log carol 'big2.bin' 60 && wait_log carol '[C] [g1] big.bin  saved to $W/carol/big2.bin' 60"
check "concurrent download by dave" "wait_log dave '[C] [g1] big.bin' 60"
check "both copies identical" "same_file $W/files/big.bin $W/carol/big2.bin && same_file $W/files/big.bin $W/dave/big.bin"

section "Clean shutdown"
echo "quit" > "$W/dave.in"
check "client exits on quit" "for i in \$(seq 50); do kill -0 $(pid dave) 2>/dev/null || break; sleep 0.1; done; ! kill -0 $(pid dave) 2>/dev/null"
echo "quit" > "$W/t1.in"
check "tracker exits on quit" "for i in \$(seq 50); do kill -0 $(pid t1) 2>/dev/null || break; sleep 0.1; done; ! kill -0 $(pid t1) 2>/dev/null"
kill -TERM "$(pid t2)"
check "tracker exits on SIGTERM" "for i in \$(seq 50); do kill -0 $(pid t2) 2>/dev/null || break; sleep 0.1; done; ! kill -0 $(pid t2) 2>/dev/null"
kill "$KEEP_alice"                                    # EOF on stdin
check "client exits on EOF (no busy loop)" "for i in \$(seq 50); do kill -0 $(pid alice) 2>/dev/null || break; sleep 0.1; done; ! kill -0 $(pid alice) 2>/dev/null"

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
