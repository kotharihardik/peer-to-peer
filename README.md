# P2P File Sharing: from a central index to a DHT

Two generations of the same peer-to-peer file-sharing system, in one repository, so
the evolution from **centralized indexing** to **fully decentralized indexing** can be
shown side by side.

| | Module 1 - first generation | Module 2 - second generation |
|---|---|---|
| Model | Napster-style, central index | Chord-style structured overlay |
| "Who has this file?" | ask the tracker | hash the name to a key, route through the overlay |
| Index lives | on 2 tracker servers | spread over every peer |
| Single point of failure | the trackers (2 replicas) | none |
| Lookup cost | 1 message, O(1) | O(log N) hops |
| Groups / users / login | yes | no - one flat global namespace |
| File transfer | **identical in both**: direct peer-to-peer, 512 KB pieces, SHA1 per piece |

The important point: **only the discovery layer changed.** Both modules link the same
file-transfer engine from `common/`, behind a three-method interface
(`common/MetadataIndex.h`). Module 1 implements it with `TrackerIndex`, module 2 with
`DhtIndex`, and nothing else in the download path knows the difference.

Only POSIX sockets/threads, the C++ standard library, and OpenSSL (for SHA1) are used.

---

## 1. Repository layout

```
common/                shared by both modules
  Socket, TcpServer      framed TCP, RAII sockets, thread-per-connection server
  Utils, Protocol.h      helpers, constants, piece arithmetic
  Sha1, FileMetadata     streaming SHA1, per-piece hashing
  SharedFiles            what this peer is allowed to serve
  PeerServer             answers GET_PIECE from other peers
  Download(Manager)      parallel multi-peer download engine
  UploadManager          hash a file, publish it, start seeding
  MetadataIndex.h        <-- the ONE interface that separates the two generations

module1/                first generation (centralized)
  tracker/               the tracker server + its state + tracker-to-tracker sync
  client/                the client, TrackerClient, TrackerIndex
  tests/run_tests.sh     103 end-to-end checks
  tracker_info.txt

module2/                second generation (decentralized)
  Hashing                the consistent-hashing id space and ring arithmetic
  NodeInfo               one peer in the overlay (id + address)
  RoutingTable           successor list, predecessor, finger table
  DistributedIndex       this node's slice of the global index
  DhtNode                the Chord node: server + router + maintenance thread
  DhtIndex               MetadataIndex implemented over the overlay
  Peer                   the application (CLI, wiring)
  tests/run_tests.sh     62 end-to-end checks
  tests/demo.sh          narrated walkthrough for showing the project
```

## 2. Build

Requirements: a C++17 compiler (g++ or clang++) and OpenSSL 3.
On macOS: `brew install openssl@3` (the Makefiles find it through `brew --prefix`).

```bash
make                 # builds both modules
make module1         # just module1/tracker/tracker and module1/client/client
make module2         # just module2/peer
make clean
```

---

# MODULE 1 - centralized tracker (first generation)

## 1.1 Run

`tracker_info.txt` (in `module1/`) lists the trackers, one `ip port` per line:

```
127.0.0.1 9000
127.0.0.1 9001
```

Each tracker also opens a **sync port = client port + 1000** (10000 and 10001 here),
so don't use those ports for clients.

```bash
./module1/tracker/tracker module1/tracker_info.txt 1       # terminal 1
./module1/tracker/tracker module1/tracker_info.txt 2       # terminal 2
./module1/client/client 127.0.0.1:6001 module1/tracker_info.txt   # terminal 3 (6001 = this peer's port)
./module1/client/client 127.0.0.1:6002 module1/tracker_info.txt   # terminal 4
```

Type `quit` in a tracker's terminal (or press Ctrl-C) to shut it down cleanly.

## 1.2 Client commands

| Command | Meaning |
|---|---|
| `create_user <user_id> <password>` | register |
| `login <user_id> <password>` | start a session |
| `logout` | end the session (your files stop being offered) |
| `create_group <group_id>` | create a group; you become its owner |
| `join_group <group_id>` | request to join |
| `leave_group <group_id>` | leave (and stop sharing your files in it) |
| `list_groups` | all groups |
| `list_requests <group_id>` | pending join requests (owner only) |
| `accept_request <group_id> <user_id>` | accept a request (owner only) |
| `upload_file <group_id> <file_path>` | share a file with the group |
| `list_files <group_id>` | files in a group, with the number of online seeders |
| `download_file <group_id> <file_name> <destination>` | download in the background (destination = file path or existing directory) |
| `show_downloads` | `[D] [grp] file 42%`, `[C] [grp] file` (complete), `[F] [grp] file (reason)` |
| `stop_share <group_id> <file_name>` | stop sharing a file |
| `quit` | exit the client |

Example session:

```
alice>> create_user alice pw        bob>> create_user bob pw
alice>> login alice pw              bob>> login bob pw
alice>> create_group g1             bob>> join_group g1
alice>> accept_request g1 bob
alice>> upload_file g1 /data/movie.mp4
                                    bob>> list_files g1
                                    bob>> download_file g1 movie.mp4 /tmp/
                                    [C] [g1] movie.mp4  saved to /tmp/movie.mp4  (pieces from: alice=120)
```

## 1.3 Test

```bash
make module1 && module1/tests/run_tests.sh
```

The script starts real trackers and clients, drives them through named pipes and
checks their output (103 checks, under a minute). It covers:

* one tracker + one client: users, groups, upload, listing, stop_share, and error cases
* two trackers + four clients, each client connected to a different tracker
* downloads: 0-byte file, file under one piece, exactly 2 pieces, short last piece, 40 MB
* multi-peer download (it checks that pieces really came from 2 peers)
* two simultaneous downloads by one client, and the same file downloaded by two clients at once
* a corrupted piece at one seeder (discarded and re-fetched elsewhere); a file where every copy is corrupt (fails cleanly, no partial file left)
* a seeder killed (`kill -9`) in the middle of a 300 MB download
* tracker 1 killed: its clients fail over to tracker 2 and log in again automatically; downloads still work
* tracker 1 restarted: it recovers the full state, including changes made while it was down, and new changes flow both ways
* 90 groups created concurrently from 3 clients on 2 trackers (both trackers end up with all 90)
* clean shutdown through `quit`, SIGTERM, and stdin EOF

Options: `TRACKER_BIN`/`CLIENT_BIN` point the script at other builds (e.g. sanitizer builds), and `HUGE_MB` sets
the size of the disconnect-test file.

Also verified during development:

* **ThreadSanitizer**: full suite, 0 data races or deadlocks
* **AddressSanitizer**: full suite, 0 memory errors
* macOS `leaks --atExit`: tracker and two clients through upload, download and quit, **0 leaks**
* malformed input (random bytes, a frame header claiming 4 GB, half-sent frames,
  bogus sync ops, `GET_PIECE ../../etc/passwd`) on all three ports: rejected, and no crash

---

## 1.4 Architecture

```
            ┌──────────── Tracker 1 ────────────┐  sync (ops + snapshots)  ┌─ Tracker 2 ─┐
            │ TcpServer → Tracker → TrackerState│◄───────────────────────►│    same     │
            │              └→ TrackerSynchronizer│                         └─────────────┘
            └───────────────▲───────────────────┘
          commands/replies  │ (metadata only)
            ┌───────────────┴───────────────┐        GET_PIECE        ┌──────────┐
            │ Client A                      │◄──────────────────────►│ Client B │
            │ TrackerClient  UploadManager  │   piece data (direct)   │  same    │
            │ DownloadManager  PeerServer   │                         └──────────┘
            └───────────────────────────────┘
```

Layers (each layer only talks to the one below it):

```
Networking          common/Socket, common/TcpServer
   ↓
Protocol            length-prefixed frames + text commands (common/Protocol.h)
   ↓
Application logic   module1/tracker/TrackerState + Tracker, module1/client/Client
   ↓
Discovery           common/MetadataIndex  (module1/client/TrackerIndex implements it)
   ↓
File & pieces       common/FileMetadata, common/Sha1, common/Download, common/PeerServer
```

### Module 1 directory layout

```
module1/tracker/   the tracker program
module1/client/    the client program (Client, TrackerClient, TrackerIndex)
module1/tests/     end-to-end test script
common/            everything else: sockets, server base class, file transfer engine
```

### Module 1 classes

| Class | File | Responsibility |
|---|---|---|
| `Socket` | common | RAII owner of a socket fd: non-copyable, closes in its destructor. Connect with timeout, `sendAll`/`recvAll` loops, message framing, send/recv timeouts. |
| `TcpServer` | common | **Base class** with a pure virtual `handleConnection()`. Accept loop plus one thread per connection. Every thread is joinable; `stopServer()` wakes blocked connections (`shutdown`) and joins them. |
| `TrackerState` | Tracker | All users, groups and files and the rules for changing them. One mutex. Every successful change is also described as **sync ops**. No networking. |
| `Tracker` | Tracker | Inherits `TcpServer`. Tracker server: per-connection `Session` (who is logged in), parses commands, calls `TrackerState`, hands changes to the synchronizer. |
| `TrackerSynchronizer` | Tracker | Inherits `TcpServer` (sync port). Snapshot pull at startup; one persistent `PeerLink` per other tracker that streams ops and pushes a snapshot on every (re)connect; applies incoming ops. |
| `Client` | client | Command loop and local session. Owns all client components; their declaration order gives a clean shutdown order. |
| `TrackerClient` | client | The one thread-safe tracker connection. On failure it switches to the next tracker, logs in again, and retries the request. |
| `UploadManager` | client | `upload_file` (hash, then register) and `stop_share`; keeps `SharedFiles` in step with the tracker. |
| `SharedFiles` | client | Thread-safe map `(group, file) → local path`: the only files the peer server will serve. |
| `PeerServer` | client | Inherits `TcpServer`. Seeder side: answers `GET_PIECE` by reading that piece from disk. |
| `DownloadManager` | client | Starts downloads (fetches metadata, picks the destination), owns all downloads, `show_downloads`. |
| `Download` | client | One file: a worker pool over several seeders, per-piece SHA1 checks, retries, `pwrite` to the final file, whole-file check, auto-seed. |
| `FileMetadata` | client | Size, whole-file SHA1 and per-piece SHA1s, computed in one streaming pass. |
| `PeerLink` | Tracker | Outgoing link to one other tracker: a queue plus condition variable (producer-consumer) and its own thread. |
| `Sha1` | client | RAII wrapper over OpenSSL EVP for incremental SHA1. |
| `FileHandle` | client | RAII owner of a plain file descriptor. |

### Module 1 communication flow

**Protocol framing (all channels).** Every message is `[4-byte length, network byte
order][payload]`. TCP is a byte stream: one `recv()` can return half a message, or
two messages glued together. The length prefix tells the receiver exactly where a
message ends. `Socket::recvAll` loops until that many bytes have arrived, and frames
over 64 MB are rejected, so a malformed header cannot make us allocate gigabytes.

| Channel | Request | Response |
|---|---|---|
| client → tracker | the command text, e.g. `join_group g1`. The client adds its peer address to `login`, and sends the hashes with `upload_file g f <size> <sha1> <n> <h1..hn>` | `SUCCESS: …` / `ERROR: …` / a listing |
| client → tracker | `download_file <g> <f>` | `OK <size> <n> <sha1>`, then n piece hashes one per line, then one `<uid> <ip> <port>` line per online seeder |
| peer → peer | `GET_PIECE <group> <file> <index>` | frame `OK` + a frame with the raw piece bytes, or a single `ERROR: …` frame |
| tracker → tracker | `SNAPSHOT_REQUEST` | the full state as sync ops |
| tracker → tracker | `OPS\n<op lines>` | (none, fire-and-forget) |

**Upload:** the client hashes the file (one pass) → `upload_file …` → the tracker
validates it (member? piece count matches size? hashes well-formed? same name with
different content?) → stores it and broadcasts `FILE` + `SEED` → the client adds the
file to `SharedFiles`.

**Download:** `download_file` → the tracker returns hashes and online seeders → the
client creates the output file at its full size → 4 workers fetch pieces in parallel
from different seeders → each piece is SHA1-checked and `pwrite`n at its offset → the
whole-file SHA1 is checked → the client auto-seeds (`upload_file` with the known
metadata) and prints `[C] [group] file`.

### Tracker internals

* **Thread per client connection** (`TcpServer`). The session (the logged-in user) is
  a local variable on that thread, so it needs no lock and disappears when the
  connection closes. A dropped connection logs the user out.
* **`TrackerState` holds everything behind one mutex** (coarse-grained locking). Each
  operation is a few map lookups, and no I/O happens while the lock is held, so one
  lock is fast enough. With a single lock, deadlock is impossible.
* Groups use `set`/`map` (sorted), so both trackers iterate in the same order.
  When an owner leaves, the new owner is the smallest member id, which is
  deterministic on both trackers.
* Validation is done on the tracker, and user identity comes from the session: the
  tracker never trusts a user id sent by the client.

### Client internals

* The main thread runs the command loop. Downloads run in the background (1
  coordinator + up to 4 workers per file), and the peer server has its own threads.
* All tracker traffic, including background auto-seeding, goes through the single
  `TrackerClient`, whose mutex makes every request/response pair atomic.
* Console output from every thread goes through `logLine()` (one mutex), so lines never mix.

### Peer-to-peer transfer and piece management (shared with module 2)

* Pieces are exactly 512 KB; the last one may be smaller. Size 0 means 0 pieces.
* **Parallel multi-peer download:** workers pull piece indexes from a shared queue.
  Piece *i* is first requested from seeder `i mod n`. This round-robin spreads the
  load, so parallel workers naturally talk to different peers.
* **Integrity:** every piece is SHA1-checked as soon as it arrives. A corrupt or
  missing piece is tried on the next seeder. A piece that fails on every seeder 3
  times makes the download fail with `[F]`, and the partial file is deleted. After
  the last piece, the whole-file SHA1 is checked as a final end-to-end check.
* **Peer failure:** connects time out after 3 s and reads after 10 s. After 3
  consecutive failures a seeder is skipped, so a dead peer costs almost nothing.
* **Memory:** `pwrite()` puts each verified piece at its final offset. Memory use is
  one piece per worker, and upload hashing streams the file, so a 1 GB file never
  sits in RAM. `pwrite` does not use the shared file offset, which makes concurrent
  writes to one fd safe.
* **Security:** a peer serves only files registered in `SharedFiles`, looked up by
  (group, file name). A remote peer can never name an arbitrary path.

### Tracker synchronization

Each tracker is a full replica. Changes are replicated as **idempotent operations**:
applying the same op twice changes nothing.

```
USER u pw | ONLINE u ip port | OFFLINE u | GROUP g owner | REQUEST g u | MEMBER g u
LEAVE g u | FILE g f size sha1 n h1..hn | SEED g f u | UNSEED g f u
```

1. **Startup recovery:** a starting tracker pulls a full snapshot from a running peer
   *before* accepting clients.
2. **Live replication:** each change is queued and streamed over **one persistent
   connection per peer**, so ops arrive in the order they happened.
3. **Healing:** a link that (re)connects first pushes a full snapshot (a snapshot is
   just the list of ops that rebuilds the state). Anything lost while the peer was
   down is covered by it, and because ops are idempotent, overlap is harmless. The
   link notices a dead peer within about 1 s: it checks for EOF on the socket.
4. **Race-free queue handling:** the queue is cleared *before* the snapshot is taken.
   Every op already queued is therefore in the snapshot, and nothing is lost.
5. **Sessions across trackers:** "online" status is replicated, but "already logged
   in" is only enforced for sessions on the same tracker. A client that fails over
   after its tracker died can therefore log in again. A late `OFFLINE` from the old
   tracker is ignored if the user already has a live session here, and snapshots
   only include users logged in through the sending tracker, so stale online flags
   left by a crashed tracker don't spread.

Clients read `tracker_info.txt` and use the first reachable tracker. If it dies, the
next request moves to another tracker, logs in again with the saved login, and
retries the request. The user doesn't notice.

### Module 1 design decisions

* **RAII everywhere:** `Socket`, `FileHandle`, `Sha1`, `ifstream`, `lock_guard`,
  and `unique_ptr` for owned objects. Nothing needs a manual `close`/`free` on error paths.
* **Inheritance + polymorphism:** `TcpServer` is written once. `Tracker`,
  `TrackerSynchronizer` and `PeerServer` inherit it and only override
  `handleConnection()`. Each child calls `stopServer()` in its own destructor, so
  no thread can call a virtual function of an object that is already destroyed.
* **Composition over globals:** the old code had 26 global variables. Now each
  program has exactly one owning object (`Tracker`, `Client`) that contains its parts.
  The only global left is the `sig_atomic_t` flag a signal handler must use.
* **Thread-per-connection** instead of `epoll`: simple and easy to explain, and more
  than enough for the specified scale (2 trackers, a handful of clients).
* **Joinable threads, not detached:** shutdown really stops everything. `poll()` with
  a timeout lets the accept loop notice the stop flag; `shutdown()` wakes threads
  blocked in `recv`.
* **SIGPIPE ignored:** otherwise writing to a peer that just disconnected kills the process.
* **No `SO_REUSEPORT`:** it silently lets two processes share a port. Only
  `SO_REUSEADDR` is kept, for fast restarts.
* **Seeders always have complete files.** A client seeds after `upload_file` or after
  a verified download. All seeders therefore have every piece, and round-robin is the
  right piece/peer selection: rarest-first gives no benefit when every piece is
  equally available.

### Module 1 interview questions

| Question | Answer |
|---|---|
| Why is this "peer-to-peer" if there is a tracker? | The tracker only stores metadata (who has which file). File data goes directly between clients. This is the Napster model. |
| The seeder deletes or changes the file while I download it? | The seeder opens the file fresh for each piece. If it was deleted it answers `ERROR`, and we try the next seeder. If it was changed, the SHA1 check fails, the piece is discarded and fetched elsewhere. The result is never corrupt. (`PeerServer::readPiece`) |
| A seeder crashes mid-download? | Connect timeout 3 s and read timeout 10 s, then the piece is tried on the next seeder. After 3 failures in a row that seeder is skipped. (`Download::workerLoop`) |
| No seeder has a good copy? | After 3 rounds the download becomes `[F]` and the partial file is deleted. |
| How do you download from several peers at once? | 4 worker threads share a piece queue; piece *i* starts at seeder *i mod n* (round-robin). |
| Why not rarest-first? | Every seeder has the complete file, so all pieces are equally rare. |
| How is a 1 GB file handled without 1 GB of RAM? | Streaming SHA1, and each piece is written with `pwrite` at offset `index*512KB` as soon as it is verified. |
| Is writing to one file from 4 threads safe? | Yes: `pwrite` uses an explicit offset (no shared file position), and the pieces don't overlap. |
| What if `recv()` returns only part of a message? | Length-prefix framing: read 4 bytes of length, then loop until exactly that many bytes have arrived (`Socket::recvAll`). |
| What if someone sends garbage to your port? | Lengths above 64 MB are rejected, numbers are parsed without exceptions, bad commands get `ERROR`, and each connection runs inside `try/catch`. |
| A client disconnects without logging out? | `recv` fails, the tracker logs the user out and broadcasts `OFFLINE`. |
| A tracker crashes? | Clients fail over to the next tracker and log in again automatically; downloads keep going because pieces come from peers. |
| The crashed tracker restarts? | It pulls a snapshot before accepting clients, and the other tracker pushes one when the link reconnects. |
| How do the trackers stay consistent? | Every change is an idempotent op sent in order over one persistent connection; a full snapshot on every reconnect repairs anything missed. |
| Why idempotent ops? | Duplicates (snapshot + live op) are then harmless, so no sequence numbers or acknowledgements are needed. |
| Two users upload different files with the same name? | Rejected: the whole-file SHA1 differs. |
| Why one mutex in `TrackerState`? | Operations take microseconds and no I/O happens under the lock; one lock also makes deadlock impossible. |
| Why ignore SIGPIPE? | Writing to a closed socket would otherwise kill the whole process. |
| How do you stop a thread that is blocked in `recv()`? | `shutdown()` on its socket makes `recv` return 0, then `join()`. |
| What would you improve for scale? | epoll plus a thread pool instead of thread-per-connection; persist tracker state to disk; partial seeding (serve pieces while still downloading); TLS. |

### Module 1 assumptions and limitations

* State is in memory only. If *both* trackers stop, the state is lost.
* During a network partition, both trackers keep serving. When they reconnect, the
  snapshots are merged by union: additions from both sides survive, but a deletion
  made on one side (leave, stop_share) may be restored by the other side's snapshot.
  If both sides create the same group, the existing owner wins.
* The same user logged in on both trackers at once is not prevented (only one
  session per user per tracker).
* IDs, passwords and file names must not contain whitespace or `|`; file paths
  with spaces are not supported (commands are split on whitespace).
* Passwords are stored and sent in plain text (no TLS), as in the original design.
* IPv4 addresses only.

---

# MODULE 2 - decentralized DHT (second generation)

No tracker. No server. Every peer runs the same program and the index is spread
across all of them.

```
              search movie.bin
                     |
          hash("dht/movie.bin") = key 25236
                     |
       route over the overlay (O(log N) hops)
                     |
        node 42614, responsible for key 25236
                     |
        "peers 11232 and 13261 have this file"
                     |
       connect to those peers and pull pieces directly
```

## 2.1 Why decentralize, and structured vs unstructured

The tracker in module 1 is convenient but it is one machine that knows everything:
it is a single point of failure, a bottleneck, and something someone has to run.
Second-generation systems remove it. There are two ways to do that:

| | Unstructured (Gnutella-style) | Structured (this module) |
|---|---|---|
| Where a file's info lives | nowhere in particular | on the node whose id follows the file's key |
| Search | flood the query to neighbours | route directly towards the key |
| Cost of a search | O(N) messages, whole network touched | O(log N) hops, a handful of messages |
| Rare file | often not found even though it exists | always found if it is there |
| Keyword / partial search | easy (ask everyone) | not supported - you need the exact key |

A structured overlay trades flexible searching for a guaranteed, cheap lookup. That
trade is the essence of a DHT, and this module implements it Chord-style.

## 2.2 Consistent hashing: the identifier space

One circular id space, 16 bits wide (`ID_BITS` in `module2/Hashing.h`), so ids run
`0 .. 65535`. **Peers and files are hashed into the same space:**

```
node id  = SHA1(ip:port)      folded to 16 bits      127.0.0.1:7900 -> 11232
file key = SHA1(scope/name)   folded to 16 bits      "dht/movie.bin" -> 25236
```

The ownership rule is one sentence:

> **the node responsible for a key is the first node clockwise from it**

so a node with id `n` owns the keys in `(predecessor, n]`.

```
            0 ─── 11232 ─── 13261 ─── 42614 ─── 57708 ─── 62105 ─── 65535/0
                    │         │          │         │         │
   owns (62105,11232]   (11232,13261]  (13261,42614]  ...   (57708,62105]
                                            ▲
                              key 25236 lands here, so node 42614 stores
                              the metadata for movie.bin
```

**Why not `hash(file) % numberOfPeers`?** Because that divides by the *count* of
peers. Add or remove one peer and almost every key changes owner, so the whole index
has to move. On the ring, a joining node only takes keys from its single successor,
so on average just `1/N` of the keys move. That is the property consistent hashing
exists for, and the `run_tests.sh` step "a new peer joins and takes over the keys it
becomes responsible for" demonstrates it: the new node receives exactly the records
for its new range, and nothing else in the ring is disturbed.

Load spreading comes from SHA1 behaving like a random function: ids land roughly
evenly, so each node ends up with roughly `1/N` of the key space. With only a handful
of peers the split is visibly uneven (that is expected - real systems add *virtual
nodes*, several ids per machine, to smooth it out; deliberately left out here to keep
the code readable).

## 2.3 Run

Each peer needs one DHT port; **pieces are served on (DHT port + 100)**, so keep the
DHT ports at least a little apart.

```bash
./module2/peer 127.0.0.1:7000                    # first peer: creates the ring
./module2/peer 127.0.0.1:7010 127.0.0.1:7000     # joins through any existing peer
./module2/peer 127.0.0.1:7020 127.0.0.1:7010     # any peer can be the entry point
./module2/peer --id 127.0.0.1:7030               # just print the node id for an address
```

The second argument is a **bootstrap peer**: used only to find where the new peer
belongs. It stores no special index and has no special role afterwards - which is
exactly what keeps this decentralized.

Example session:

```
alice>> share /data/movie.mp4
[LOOKUP] publish movie.mp4  ->  key 25236
[LOOKUP]   hop 1: 11232@127.0.0.1:7000 -> 13261@127.0.0.1:7030
[LOOKUP]   responsible node: 57708@127.0.0.1:7010   (1 hop(s))
SUCCESS: movie.mp4 published (key 25236 -> node 57708, 1 hop(s))

dave>>  search movie.mp4            # dave never spoke to alice before
dave>>  get movie.mp4 /tmp/
[C] [dht] movie.mp4  saved to /tmp/movie.mp4  (pieces from: node11232=6)
[INFO] Now seeding movie.mp4       # dave is now a seeder too
```

## 2.4 Peer commands

| Command | Meaning |
|---|---|
| `share <file_path>` | hash the file, publish its metadata into the DHT, start seeding |
| `unshare <file_name>` | remove it from the DHT and stop seeding |
| `search <file_name>` | DHT lookup: prints the hops, the responsible node and the seeders |
| `get <file_name> <dest>` | look up, then download directly from the discovered peers |
| `downloads` | `[D] [dht] file 42%`, `[C] ...` complete, `[F] ... (reason)` |
| `key <file_name>` | which key a name maps to, and which node is responsible |
| `info` | my node id, predecessor, successor list, finger table, the key range I own |
| `ring` | walk the successor pointers all the way around |
| `index` | the metadata slice stored on **this** node |
| `stats` | messages sent/received, lookups, average hops, average latency |
| `leave` / `quit` | hand my metadata to my successor, close the ring, exit |

## 2.5 Architecture

```
        ┌──────────────────── Peer ────────────────────┐
        │  Peer            command loop                │
        │   ├─ DhtNode     overlay: server + router    │   ← replaces the tracker
        │   │   ├─ RoutingTable      successors, predecessor, fingers
        │   │   └─ DistributedIndex  this node's slice of the index
        │   ├─ DhtIndex    MetadataIndex over the overlay
        │   ├─ SharedFiles          ┐
        │   ├─ UploadManager        │  all from common/,
        │   ├─ PeerServer           │  unchanged from module 1
        │   └─ DownloadManager      ┘
        └──────────────────────────────────────────────┘
             ▲  overlay messages (small)        ▲  GET_PIECE (bulk data)
             │  on the DHT port                 │  on the DHT port + 100
        other peers                         other peers
```

| Class | Responsibility |
|---|---|
| `Hashing` | the id space: `hashToId`, `nodeIdOf`, `fileKey`, and the two ring-arc tests |
| `NodeInfo` | one peer in the overlay: id + ip + port, with its wire format |
| `RoutingTable` | successor list, predecessor, finger table, `closestPrecedingNode`, `forgetNode` |
| `DistributedIndex` | the local `file name -> (metadata, seeders)` map, with soft-state expiry |
| `DhtNode` | inherits `TcpServer`; answers overlay messages, runs `lookup()`, owns the maintenance thread |
| `DhtIndex` | implements `MetadataIndex`: publish / unpublish / lookup over the overlay, plus re-announcing |
| `Peer` | the application: CLI, and it owns all of the above |

Overlay wire protocol (framed text, same `Socket` framing as module 1):

| Request | Reply | Used for |
|---|---|---|
| `PING` | `PONG <id>` | liveness of a predecessor, and learning a bootstrap peer's id |
| `FIND_STEP <key>` | `SUCC <node>` / `NEXT <node>` | one routing step |
| `GET_PRED` | `NODE <node>` / `NONE` | stabilize |
| `GET_SUCCLIST` | `NODES <node>...` | refresh backup successors, walk the ring |
| `NOTIFY <node>` | `OK` + PUT lines | "I am your predecessor"; reply carries handed-off keys |
| `PUT <key> <name> <size> <sha1> <n> <hashes...> <peerId> <ip> <port>` | `OK` | publish or refresh one record |
| `GET <name>` | `FOUND ...` / `NOTFOUND` | read the index |
| `DROP <name> <peerId>` | `OK` | stop sharing |
| `BULK` + PUT lines | `OK` | key handoff and replication |
| `LEAVING <node>`/`NONE`, `SET_SUCC <node>` | `OK` | graceful departure |

## 2.6 How a lookup works

**Lookups are iterative**: the peer that wants the answer asks one node, is told
"ask this one next", and repeats. The alternative (recursive - each node forwards the
query itself) would leave a chain of threads blocked on each other and make hop
counting awkward. Iterative keeps all the state in the asking peer, which is also why
the `[LOOKUP]` trace is easy to print.

The finger table is what makes it fast: `finger[i]` is the node responsible for
`me + 2^i`, so each hop at least halves the remaining distance to the key -
**O(log N) hops**, and 16 fingers cover the whole 65536-id space.

One `FIND_STEP` on the receiving node (`DhtNode::findStep`):

```
if I am my own successor          -> SUCC me            (I am alone, I own everything)
if key == my id                   -> SUCC me
if key in (predecessor, me]       -> SUCC me            (the keys I own)
if key in (me, successor]         -> SUCC successor     (my neighbour owns it)
otherwise n = closestPrecedingNode(key)
    if n == me                    -> NEXT successor     (no useful finger: walk the ring)
    else                          -> NEXT n             (take the big shortcut)
```

The critical property: **the finger table is only an optimization.** If every finger
were wrong or empty, the fallback still steps along the successor chain and the lookup
still finds the right node - just in O(N) instead of O(log N). Correctness rests on
the successor pointers alone, and that is why stale routing data after a crash slows
the system instead of breaking it.

## 2.7 Peer join

```
1. node id = hash(my ip:port)
2. ask the bootstrap peer to route a lookup for my own id
     -> the answer is my successor
3. successor = that node, predecessor = unknown
4. send NOTIFY to my successor
     -> it makes me its predecessor
     -> it replies with the metadata for the keys that are now mine  (handoff)
5. fill the whole finger table once, so early lookups are already fast
6. the maintenance thread takes over from here
```

Only the successor is contacted; nobody else has to be told. The node before me
discovers me on its own, on its next `stabilize()` round, because it asks its
successor "who is your predecessor?" and gets me.

## 2.8 Peer leave

`leave` / `quit` is graceful and loses nothing:

```
1. send my owned metadata to my successor   (BULK)  -> it is now responsible for it
2. tell my successor    : LEAVING <my predecessor>  -> its predecessor becomes mine
3. tell my predecessor  : SET_SUCC <my successor>   -> its successor becomes mine
```

The ring closes immediately instead of waiting for a timeout to be noticed.

## 2.9 Failure handling

A peer killed with `SIGKILL` says nothing, so the ring has to notice and repair.
Four mechanisms, all of them simple:

| Situation | How it is handled |
|---|---|
| Successor died | every node keeps a **successor list** of 3; `stabilize()` finds the successor unreachable and promotes the next one, then refreshes its fingers |
| Predecessor died | `checkPredecessor()` pings it; on failure the pointer is cleared, so the node answers for the wider range and lookups still resolve |
| A lookup is routed into a dead node | the dead node is removed from the routing table immediately (`forgetNode`), and the lookup is retried - the retry takes a different path |
| Stale finger | points at a dead or wrong node; harmless, because `findStep` falls back to the successor chain. Fingers are also refreshed one per round |
| Metadata on a dead node | every node **replicates the keys it owns to its successor** every 5 s, so a lookup that lands on the successor finds the records anyway |
| A peer that crashed is still listed as a seeder | the downloader's connect times out after 3 s and it uses another seeder; the index record expires by itself (see soft state) |

`module2/tests/run_tests.sh` kills a peer with `kill -9` and then asserts that the
ring closes again, that lookups keep working, and that metadata the dead node was
responsible for is still findable.

## 2.10 The distributed index and soft state

Only **metadata** is distributed - size, SHA1, the piece hashes, and the list of peers
that hold the file. The file bytes never enter the DHT, so the node responsible for a
key usually does not have the file at all. That separation (metadata ownership vs file
ownership) is what keeps the overlay cheap.

Every peer record carries a timestamp and is dropped if it is not refreshed within
`ENTRY_TTL_SEC` (90 s). Publishers re-announce every `ANNOUNCE_SEC` (20 s) from the
`DhtIndex` thread, reusing the hashes they already computed. This **soft state** gives
three things for free:

* a peer that crashes disappears from the index by itself
* copies left on a node that is no longer responsible expire instead of lingering
* after any join, leave or crash, the next round of announcements rebuilds the index
  on whichever nodes are responsible *now* - so handoff is an optimisation for speed,
  not something correctness depends on

It is the same idea as a BitTorrent tracker's announce interval.

The local map is keyed by **file name**, not by the numeric key: with 16-bit ids two
different names can hash to the same key, and both have to be storable on the node.

## 2.11 Test and demo

```bash
make module2
module2/tests/run_tests.sh       # 62 assertions, about a minute
module2/tests/demo.sh            # narrated walkthrough, nothing asserted
module2/tests/bench.sh           # hop count vs ring size (the O(log N) measurement)
```

`run_tests.sh` starts real peers, drives them through named pipes and checks their
output. There is no tracker anywhere in it. It covers:

1. four peers join (through **different** bootstrap peers) and the ring closes
2. every peer independently agrees the ring has 4 nodes
3. node ids, predecessor, successor list, owned key range, finger table
4. a file is shared and the metadata lands on the node the key maps to - **not** on the publisher
5. three files spread over different nodes (the index really is distributed)
6. a peer that has never seen the file finds it through the overlay, with a hop trace
7. searching a file that does not exist fails cleanly
8. a full download over direct peer-to-peer transfer, byte-identical to the original
9. the downloader auto-publishes itself, so a third peer downloads from **2 seeders**
10. a new peer joins in the right arc and receives exactly the records it now owns
11. a peer is `kill -9`ed: the ring closes again, lookups still work, replicated metadata survives
12. a graceful `leave` hands the metadata over and the file stays findable
13. lookup statistics

Options: `PEER_BIN` (e.g. a sanitizer build), `BASE_PORT`, `KEEP_DATA=1`.

Also verified:

* **ThreadSanitizer**: full module 2 suite, 0 data races or deadlocks
* **AddressSanitizer**: full module 2 suite, 0 memory errors
* module 1's 103 checks still pass unchanged after the refactor

## 2.12 Module 2 interview questions

| Question | Answer |
|---|---|
| Is the bootstrap peer not just a tracker again? | No. It answers one routing query so the newcomer can find its successor, and it stores no index of its own. Any peer already in the ring can do it, and after joining it is an ordinary neighbour. |
| Why consistent hashing instead of modulo? | Modulo divides by the number of peers, so one join or leave remaps almost every key. On the ring a join only moves keys from one successor - about `1/N` of them. |
| What guarantees a lookup terminates? | Every step either answers `SUCC` or moves strictly closer to the key along the ring. Fingers make it O(log N); the successor-chain fallback keeps it correct even with a completely wrong finger table. There is also a hard step limit so a broken ring cannot spin. |
| Why iterative and not recursive routing? | The asking peer keeps all the state, hops are trivial to count and print, and no chain of threads sits blocked waiting on each other. |
| What if the responsible node crashes with the only copy of the metadata? | Each node replicates the keys it owns to its successor every 5 s, and the lookup naturally lands on that successor once the ring repairs. The publisher's re-announce also restores the record within 20 s. |
| Two files whose names hash to the same key? | Both live on the same node. The local store is keyed by file name, so they do not collide. With 16-bit ids this happens easily and is handled. |
| Two peers with the same node id? | Possible in a 16-bit space (about 1 in 65536 per pair). Not handled; production systems use 160-bit ids, which makes it negligible. Widening `ID_BITS` is a one-line change. |
| How does the node before me learn I joined? | It does not need to be told. On its next `stabilize()` it asks its successor "who is your predecessor?", gets me, and points at me. |
| Is `hash(filename)` not a weakness - you must know the exact name? | Yes, and that is the structured-overlay trade-off: exact-key lookup is cheap and guaranteed, keyword search is not supported. Real systems add a separate keyword index on top. |
| How many messages does a lookup cost vs module 1? | Module 1: exactly 1 request/reply to a known server. Module 2: hops+1 request/reply pairs, where hops grows like log2(N) - 0 to 2 in a 5-peer ring, about 10 for 1000 peers. |
| What did you reuse from module 1? | The entire file-transfer engine: piece splitting, SHA1 verification, the 4-worker parallel downloader, the piece server, framed sockets and the thread-per-connection server. Only the discovery layer is new. |
| How would you improve it? | Virtual nodes for even load, 160-bit ids, replication factor > 2 with explicit repair, a keyword index, and persisting each node's slice to disk. |

## 2.13 Module 2 limitations

* Each node's index slice is in memory only; if every node holding a record dies
  before the publisher re-announces, that record is gone until the next announcement.
* Replication factor is 2 (owner + immediate successor). Losing both at once within
  the announce interval loses those records.
* 16-bit ids keep demo output readable but make node-id collisions possible.
* No virtual nodes, so with few peers the key space is split unevenly.
* Exact-name lookup only - no keyword or prefix search.
* No authentication or encryption: any peer can publish a record for any name. The
  SHA1 checks mean a *corrupt* file can never be assembled, but a peer could still
  advertise a name it does not serve, which just wastes a downloader's timeout.
* A peer must be reachable at the address it publishes (no NAT traversal).

---

# Centralized vs decentralized: comparison

## Measured: lookup cost really is O(log N)

`module2/tests/bench.sh` builds rings of increasing size, then runs 40 lookups for
random keys, each from a randomly chosen peer, and averages the hop count that the
peer reports. Chord's theoretical average is `½·log2(N)`:

| Peers (N) | Avg hops measured | Max hops | `½·log2(N)` predicted | Messages per lookup |
|---|---|---|---|---|
| 2 | 0.00 | 0 | 0.5 | 2 |
| 4 | 0.55 | 2 | 1.0 | 2.6 |
| 8 | 0.85 | 3 | 1.5 | 2.9 |
| 16 | 1.90 | 4 | 2.0 | 3.9 |
| 32 | **2.50** | 4 | **2.5** | 4.5 |

At N=32 the measured average matched the theoretical `½·log2(N)` exactly, and the
worst case never exceeded `log2(N)` - which is the Chord bound. The small rings come
in under the prediction simply because with few peers a lookup often starts on the
node that already owns the key (0 hops).

Doubling the network from 16 to 32 peers added **0.6 hops**, not 16. That is the
whole argument for a structured overlay: the index grows linearly, the lookup cost
grows logarithmically.

Background cost of having no server: **~9 overlay messages per second per peer**,
measured at both N=8 (8.5/s) and N=32 (9.1/s) - constant per peer, independent of
network size, because each peer only ever talks to its neighbours.

File transfer (identical engine in both modules), 200 MB over loopback from 2 seeders:
**0.71 s, ~282 MB/s**, with the 400 pieces split exactly 200/200 between the two
seeders - confirming the round-robin multi-peer download works.

## Side by side

| | Module 1 (tracker) | Module 2 (DHT) |
|---|---|---|
| Lookup messages | 1 request/reply | `hops + 2` request/reply pairs (2.0-4.5 measured) |
| Measured hops | n/a - always one trip to a known server | 0 (N=2) to 2.5 (N=32) |
| Expected hops | O(1) | O(log N) - verified above |
| Lookup latency, loopback | under 1 ms | under 1 ms up to 32 peers; grows with hops |
| Join cost | 1 login message | 1 lookup + 1 NOTIFY + 16 finger lookups |
| Leave cost | 1 logout message | 3 messages + one metadata handoff |
| Steady-state traffic per peer | none | ~4 small messages/second of maintenance |
| Index location | 2 tracker servers, full copy each | ~1/N of the keys on each peer |
| Single point of failure | yes - all trackers down = no discovery | no |
| Peer crash | tracker marks it offline | ring repairs itself, records survive on the successor |
| Node failures tolerated | all but one tracker | any peer, including the one holding a given key |
| Consistency | strong within one tracker; eventual between the two | eventual (soft state + replication) |
| Admin effort | someone must run the trackers | none |
| Keyword search | easy (`list_files` on the server) | not supported |

**The honest summary:** the DHT removes the single point of failure and the need for
anyone to run a server, and it spreads both storage and load. It pays for that with
more hops per lookup, constant background maintenance traffic, weaker consistency, and
the loss of easy keyword search. That is the real trade-off between the first and
second generation, and it is why BitTorrent still ships with both a tracker and a DHT.

---

# Code style (both modules)

The code is written to be simple to read: `using namespace std;` in every file, a
plain `enum`, simple classes, no lambdas or `std::function`, and comments on every
function, and no `std::` prefixes anywhere. The only `::` outside `ClassName::method`
definitions is `::bind` in `Socket.cpp`: with `using namespace std`, a plain `bind()`
would silently resolve to `std::bind` and the socket would never be bound (the comment
there explains it).

Comments state the technique being used and why it was chosen, so the reasoning behind
each design decision is in the code rather than only in this README.

---

# Bugs found in the original Module 1 code and how they were fixed

Before the refactor, bugs were found by reading every file and running the original
binaries.

### Broken features (verified by running the original)
| # | Bug | Fix |
|---|---|---|
| 1 | `join_group`, `leave_group`, `list_groups`, `list_requests`, `accept_request` returned "Unknown command": the client sent `join_group`, the tracker expected `join group`. | One command table in `Tracker::execute` with the spec's command names. |
| 2 | `stop_share` never worked: it checked `arg1 == "share"`, but `arg1` was never parsed. | Proper `stop_share` handling in the tracker and client (`UploadManager::stopShare`). |
| 3 | `download_file` always failed: the tracker's reply (no `OK`/`END`, space-separated hashes, `ip:port:path` seeders) did not match what the client parsed. | One documented reply format, parsed by `DownloadManager::parseMetadata`. |
| 4 | The client spun at 100% CPU forever on stdin EOF. | The loop ends when `getline` fails. |
| 5 | Non-members could list and download a group's files. | Membership checked for `list_files`, `download_file`, `upload_file`, `stop_share`. |

### Tracker synchronization
| # | Bug | Fix |
|---|---|---|
| 6 | Sync replayed the raw client command on the other tracker with `socket = -1`, so session commands ran as the wrong user (or failed), and `activeSessions[-1]` was shared by every synced login. | Replicate explicit, idempotent ops that carry the acting user. |
| 7 | On owner leave, the new owner was `*unordered_set.begin()`, which can differ between trackers. | Sorted `std::set`, so the smallest member id is chosen on both trackers. |
| 8 | Updates sent while the peer was down were lost forever. A new connection per message plus a detached thread per message on the receiver meant ops could be applied out of order. | Persistent ordered link per peer; full snapshot pushed on every reconnect. |
| 9 | A client disconnect marked the user offline locally but never told the other tracker. | Disconnect = logout, which is broadcast as `OFFLINE`. |
| 10 | Synced "logged in" flags blocked a client from logging in on the backup tracker after failover ("User already logged in"). | `localSession` flag: only same-tracker sessions block a login. |
| 11 | Peer tracker number computed as `myNo == 1 ? 2 : 1` with no bounds check: an out-of-range access (crash) with a one-line tracker file. | Peers = every other entry in the file; arguments validated. |
| 12 | The client had no failover after startup, and background downloads opened their own un-authenticated tracker connections. | `TrackerClient`: shared, thread-safe, fails over, re-logs in, retries. |

### Concurrency and memory safety
| # | Bug | Fix |
|---|---|---|
| 13 | The same maps were guarded by different mutexes (`data_mutex`, `metadataMutex`, `user_mutex`, `group_mutex`) or by none (`handleDownloadCommand`, and `create_group` read `activeSessions` before locking): data races. | All state inside `TrackerState` behind one mutex. |
| 14 | `handleDownloadCommand` closed the client socket, then the handler kept using it: a double close. The fd could meanwhile be reused by another connection, which then inherited the old session from `activeSessions`. | Sockets owned by RAII `Socket`s; sessions are per connection, not keyed by fd. |
| 15 | Disconnects never removed `activeSessions` entries: a new connection with a reused fd was "logged in" as the previous user. | Same as above. |
| 16 | SIGPIPE not ignored: a peer disconnecting during `send` killed the tracker or client. | `ignoreSigpipe()` in both programs. |
| 17 | The signal handler called `cout`/`exit()` (not async-signal-safe) while detached threads were running. | The handler only sets a `sig_atomic_t` flag; main shuts down normally and joins all threads. |
| 18 | Everything ran on detached threads, so nothing could be stopped or cleaned up. `quit` on the tracker console (required by the spec) was missing. | `TcpServer` with joinable threads; `quit` + SIGINT/SIGTERM handled. |
| 19 | The client's `>> ` prompt raced with background log output (found by ThreadSanitizer after the refactor). | All console output goes through the `logLine()` mutex. |

### Network protocol
| # | Bug | Fix |
|---|---|---|
| 20 | Replies were read with one `recv(4096)`: long listings were truncated and the leftover bytes corrupted the next reply. | Length-prefixed framing with `recvAll` loops. |
| 21 | The tracker read "until a newline", then dropped the bytes after it (pipelined commands were lost or merged) and stripped every `\n` inside. | Same framing. |
| 22 | Several `send()` calls ignored partial sends. The piece size was sent as a raw host-endian `long long`. | `sendAll` everywhere; lengths in network byte order. |
| 23 | Blocking `connect()` with no timeout (hangs for over a minute on a dead host); no send timeout. | Non-blocking connect + `poll`; `SO_RCVTIMEO`/`SO_SNDTIMEO`. |
| 24 | `stoi`/`stoll` on network input could throw and kill the thread and the whole process. Negative `pieceCount` → `reserve(huge)` → crash. | `parseInt` without exceptions, and full validation of every message. |
| 25 | `SO_REUSEPORT` let two processes bind the same port silently: tracker 2's sync port (5002 + 1000 = 6002) clashed with the README's client port 6002. | `SO_REUSEPORT` removed; new default ports documented. |

### Files, pieces and integrity
| # | Bug | Fix |
|---|---|---|
| 26 | **Security:** peers served any path the requester named (`GET_PIECE /etc/passwd 0`). The tracker even distributed the seeders' absolute paths. | Peers serve only files in `SharedFiles`, looked up by (group, name); paths never leave the machine. |
| 27 | The tracker trusted the user id inside `upload_file`, so anyone could share as anyone. | The user comes from the connection's session. |
| 28 | `system("mkdir -p …")` / `system("rm -rf …")` with the file name in the command: banned by the spec, and a shell-injection hole. Pieces went to `/tmp` and then had to be copied. | No temp files: `pwrite` straight into the pre-sized destination. |
| 29 | Upload hashing and final verification loaded the whole file into RAM (1 GB). | Streaming SHA1 (`Sha1` class over EVP), 512 KB buffer. |
| 30 | On a final hash mismatch the file was still marked complete and auto-seeded (spreading corruption). | Marked `[F]`, the file is deleted, and it is not seeded. |
| 31 | Only one piece and one peer at a time, so no parallel multi-peer download. | Worker pool with round-robin seeder selection. |
| 32 | The received piece size was not compared with the expected size. | Exact size check per piece. |
| 33 | Uploading a different file under an existing name made the uploader a seeder of the old file (downloaders would mix pieces). | Rejected: "A different file with this name is already shared". |
| 34 | Auto-seeding used the basename of the destination, so a download saved under a different name was shared under the wrong name. | Auto-seed reuses the known metadata and file name. |
| 35 | Starting the same download twice overwrote the progress entry. | Rejected while one is still running. |
| 36 | `stopDownload` was declared but never defined; `global.cpp`'s `sharedFiles` was unused. | Removed. |

### Housekeeping
* Removed dead or duplicate files: `Tracker/temp.cpp`, `Tracker/client.cpp` (an old
  commented-out client), and `client/upload.cpp` (a duplicate of `methods.cpp`).
* Removed committed build output (`*.o`, binaries, `.DS_Store`) and added a `.gitignore`.
* `header.h` was included twice over, with its contents duplicated.
* No warnings with `-Wall -Wextra`.
