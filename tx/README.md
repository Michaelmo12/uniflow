# Uniflow — TX (Sender)

Watches a directory for new files and transmits them over UDP using
Reed-Solomon forward error correction, so the transfer survives packet
loss on an adversarial network with no retransmission.

Two processes: **File Monitor** (Python) watches a folder and notifies
**Sender** (C++) over a Unix domain socket whenever a file is ready.
Sender reads it, hashes it, splits it into FEC blocks, encodes parity,
and sends everything to Receiver over UDP.

This covers TX only. Receiver/Session Manager (RX) is a separate
component, run on a separate machine — see
[rx/receiver/README.md](../rx/receiver/README.md). For the full pipeline
picture, see the [root README](../README.md).

## Prerequisites

```bash
sudo apt update
sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev \
                     libssl-dev zlib1g-dev python3-pip
pip install inotify_simple --break-system-packages
```

## Build

Protobuf sources are generated from `proto/uniflow.proto` automatically as
part of the build — no manual `protoc` step needed.

```bash
cmake -S tx/sender -B tx/sender/build
cmake --build tx/sender/build --parallel
```

## Configuration

Edit `tx/sender/include/sender/config.hpp` before running on real hardware:
- `RECEIVER_IP` — currently `127.0.0.1` for local testing; set to the real
  RX machine's IP address for an actual two-machine transfer.
- `RECEIVER_PORT` — `5005` for direct transfer to Receiver. Use `5004` only
  when a Chaos Router is running on the sender host.
- `FEC_N` / `FEC_K` / `PAYLOAD_SIZE` — must match Receiver's values exactly
  (currently 100 / 70 / 1024) or reconstruction will fail.

## Receiver host network setup

The Receiver requests a 4 MB UDP receive buffer. Linux may cap that request
with `net.core.rmem_max`, so run this once on the Receiver computer before
large or high-rate transfers:

```bash
sudo sysctl -w net.core.rmem_max=8388608
sudo sysctl -w net.core.rmem_default=8388608
```

Verify the values with:

```bash
sysctl net.core.rmem_max net.core.rmem_default
```

These settings are runtime settings and reset after reboot. To make them
persistent, add the following lines to `/etc/sysctl.d/99-uniflow.conf` and
run `sudo sysctl --system`:

```text
net.core.rmem_max=8388608
net.core.rmem_default=8388608
```

## Run

Two terminals, in this order — Sender needs to be listening before File
Monitor can notify it:

**Terminal 1 — Sender:**
```bash
cd tx/sender/build
./sender
```

**Terminal 2 — File Monitor**, pointed at whatever folder you want watched:
```bash
cd tx/file_monitor
python3 file_monitor.py /path/to/watched/folder
```
(Creates the folder automatically if it doesn't exist yet.)

Both processes run indefinitely — Sender loops and accepts a new file
after finishing the previous one, so this only needs to be started once.

## Verify it's working

Drop a file into the watched folder:
```bash
echo "hello" > /path/to/watched/folder/test.txt
```

Need bigger files to test with? `../scripts/make_test_files.sh` generates
random test files at 1M/10M/100M/200M/500M/900M (or custom sizes).

File Monitor's terminal should log the notification, and Sender's
terminal should show it processing the file end to end:
```
Received file path: /path/to/watched/folder/test.txt
File split into 1 block(s)
Sending 170 packets to <RECEIVER_IP>:5004...
Sent 170 / 170 packets
Waiting for next file...
```