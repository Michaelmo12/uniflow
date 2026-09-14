# uniflow

A raw-UDP file transfer pipeline with Reed-Solomon forward error correction —
files survive packet loss with zero retransmission. Built to move large files
fast across a lossy link (think adversarial network / WSL2 to WSL2) without
paying TCP's retransmit tax.

## How it works

```
watched folder --> File Monitor --> Sender --> [UDP, lossy] --> Chaos Router --> Receiver --> received_files/
   (tx)             (tx)             (tx)                         (rx, optional)   (rx)
                                                                    Session Manager (rx) validates the result
```

1. **File Monitor** (Python, `tx/file_monitor`) watches a directory with
   inotify and tells **Sender** over a Unix socket the moment a file is
   fully written.
2. **Sender** (C++, `tx/sender`) reads the file, hashes it (SHA-256 + CRC32),
   splits it into fixed-size blocks, encodes Reed-Solomon parity shards per
   block (`FEC_N`/`FEC_K`, default 100/70 — any 100 of the 170 total shards
   per block, data + parity in any combination, reconstruct it, so up to 70
   losses per block are tolerated), wraps everything in `uniflow.proto`
   packets, and fires them over UDP.
3. **Chaos Router** (Python, `rx/monitor`, optional) sits between sender and
   receiver and randomly drops packets to simulate loss, so you can prove
   the FEC actually recovers the file.
4. **Receiver** (C++, `rx/receiver`) listens on UDP, reassembles blocks from
   whatever shards arrive, decodes missing ones via Reed-Solomon, and writes
   the file to `received_files/`.
5. **Session Manager** (Python, `rx/monitor`) watches for the receiver to
   finish a file, then re-hashes it and prints `SUCCESS`/`FAILED` — the
   source of truth for whether a transfer actually made it intact.

## Layout

- `proto/uniflow.proto` — the one packet format shared by sender and receiver.
- `tx/` — sender side. See [tx/README.md](tx/README.md).
- `rx/` — receiver side. See [rx/receiver/README.md](rx/receiver/README.md).
- `scripts/e2e_loss_test.sh` — spins up the whole pipeline locally at a given
  packet-loss percentage and asserts the received file matches the source.
- `scripts/make_test_files.sh` — generates random test files of various sizes.

## Install dependencies

Everything runs on Linux (WSL2 is fine). One-time setup:

```bash
sudo apt update
sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev \
                     libssl-dev zlib1g-dev python3-pip
pip install inotify_simple --break-system-packages
```

## Build

Sender and receiver each generate their own protobuf sources from
`proto/uniflow.proto` as part of the build — no manual `protoc` step needed.

```bash
cmake -S rx/receiver -B rx/receiver/build && cmake --build rx/receiver/build --parallel
cmake -S tx/sender   -B tx/sender/build   && cmake --build tx/sender/build   --parallel
```

## Run it end to end (quickest way)

```bash
./scripts/e2e_loss_test.sh 10   # 10% simulated packet loss
```

Builds both sides, starts Session Manager, Receiver, Chaos Router and Sender,
drops a random test file into the watched folder, and reports
success/failure once the transfer completes.

## Run it manually

Four processes, each in its own terminal, started in this order:

```bash
# 1. Session Manager (rx) — validates completed transfers
cd rx/monitor && python3 session_manager.py

# 2. Receiver (rx)
cd rx/receiver/build && ./receiver

# 3. Chaos Router (rx, optional — skip to send with zero simulated loss)
cd rx/monitor && python3 chaos_router.py --listen-port 5004 --dest-ip 127.0.0.1 --dest-port 5005 --drop-rate 0.1

# 4. Sender + File Monitor (tx)
cd tx/sender/build && ./sender
cd tx/file_monitor && python3 file_monitor.py /path/to/watched/folder
```

Drop a file into the watched folder — Sender picks it up automatically, and
Session Manager prints `SUCCESS`/`FAILED` once Receiver finishes it. Details
and config (FEC parameters, ports, real two-machine setup) are in
[tx/README.md](tx/README.md) and [rx/receiver/README.md](rx/receiver/README.md).

## Generate test files

```bash
./scripts/make_test_files.sh                 # 1M/10M/100M/200M/500M/900M in ./test_files
./scripts/make_test_files.sh /some/dir 5M 2G  # custom dir + sizes
```
