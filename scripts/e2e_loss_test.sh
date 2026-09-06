#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RX_DIR="$ROOT_DIR/rx/receiver"
MONITOR_DIR="$ROOT_DIR/rx/monitor"
SENDER_DIR="$ROOT_DIR/tx/sender"
if [[ -n "${WATCH_DIR:-}" ]]; then
    REMOVE_WATCH_DIR=0
else
    WATCH_DIR="$(mktemp -d /tmp/uniflow-watch.XXXXXX)"
    REMOVE_WATCH_DIR=1
fi
DROP_RATE_PERCENT="${1:-10}"
DROP_RATE="$(awk "BEGIN { printf \"%.6f\", $DROP_RATE_PERCENT / 100 }")"
RUN_DIR="$(mktemp -d /tmp/uniflow-e2e.XXXXXX)"
SOURCE_FILE="$RUN_DIR/source.bin"
RECEIVED_FILE="$RX_DIR/received_files/e2e-test.bin"

session_pid=""
receiver_pid=""
router_pid=""
sender_pid=""
monitor_pid=""

cleanup() {
    set +e
    for pid in "$monitor_pid" "$sender_pid" "$router_pid" "$receiver_pid" "$session_pid"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    for pid in "$monitor_pid" "$sender_pid" "$router_pid" "$receiver_pid" "$session_pid"; do
        if [[ -n "$pid" ]]; then
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -f /tmp/uniflow_status.sock /tmp/uniflow_monitor_to_sender.sock
    rm -rf "$RUN_DIR"
    [[ "$REMOVE_WATCH_DIR" == 1 ]] && rm -rf "$WATCH_DIR"
}
trap cleanup EXIT

if ! [[ "$DROP_RATE_PERCENT" =~ ^([0-9]+([.][0-9]*)?|[.][0-9]+)$ ]]; then
    echo "usage: $0 [drop_percent]" >&2
    exit 2
fi
if awk "BEGIN { exit !($DROP_RATE_PERCENT < 0 || $DROP_RATE_PERCENT > 100) }"; then
    echo "drop percentage must be between 0 and 100" >&2
    exit 2
fi

mkdir -p "$WATCH_DIR" "$RX_DIR/received_files"
rm -f "$RECEIVED_FILE" /tmp/uniflow_status.sock /tmp/uniflow_monitor_to_sender.sock

printf 'Building receiver...\n'
(cd "$RX_DIR" && make clean && make)

printf 'Generating sender protobuf and building sender...\n'
mkdir -p "$SENDER_DIR/generated" "$SENDER_DIR/build"
protoc --cpp_out="$SENDER_DIR/generated" -I "$RX_DIR" "$RX_DIR/uniflow.proto"
cmake -S "$SENDER_DIR" -B "$SENDER_DIR/build" >/dev/null
cmake --build "$SENDER_DIR/build" --parallel >/dev/null

printf 'Creating test file...\n'
head -c 262144 /dev/urandom > "$SOURCE_FILE"

printf 'Starting session manager...\n'
(cd "$RX_DIR" && exec python3 "$MONITOR_DIR/session_manager.py") >"$RUN_DIR/session_manager.log" 2>&1 &
session_pid=$!

for _ in $(seq 1 50); do
    [[ -S /tmp/uniflow_status.sock ]] && break
    sleep 0.1
done
[[ -S /tmp/uniflow_status.sock ]] || { echo "session manager socket was not created" >&2; exit 1; }

printf 'Starting receiver...\n'
(cd "$RX_DIR" && exec ./receiver) >"$RUN_DIR/receiver.log" 2>&1 &
receiver_pid=$!

printf 'Starting chaos router at %.2f%% loss...\n' "$DROP_RATE_PERCENT"
python3 "$MONITOR_DIR/chaos_router.py" \
    --listen-port 5004 \
    --dest-ip 127.0.0.1 \
    --dest-port 5005 \
    --drop-rate "$DROP_RATE" \
    --stats-interval 50 >"$RUN_DIR/router.log" 2>&1 &
router_pid=$!

printf 'Starting sender and file monitor...\n'
(cd "$SENDER_DIR" && exec "$SENDER_DIR/build/sender") >"$RUN_DIR/sender.log" 2>&1 &
sender_pid=$!
cp "$SOURCE_FILE" "$WATCH_DIR/e2e-test.bin"

if python3 -c 'import inotify_simple' >/dev/null 2>&1; then
    python3 "$ROOT_DIR/tx/file_monitor/file_monitor.py" "$WATCH_DIR" >"$RUN_DIR/file_monitor.log" 2>&1 &
    monitor_pid=$!
else
    printf 'inotify_simple is unavailable; using direct sender IPC fallback.\n' >"$RUN_DIR/file_monitor.log"
    for _ in $(seq 1 50); do
        [[ -S /tmp/uniflow_monitor_to_sender.sock ]] && break
        sleep 0.1
    done
    [[ -S /tmp/uniflow_monitor_to_sender.sock ]] || {
        echo "sender monitor socket was not created" >&2
        exit 1
    }
    python3 - "$WATCH_DIR/e2e-test.bin" <<'PY'
import socket
import sys

with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
    client.connect("/tmp/uniflow_monitor_to_sender.sock")
    client.sendall(sys.argv[1].encode("utf-8"))
PY
fi

printf 'Waiting for transfer result...\n'
result=""
for _ in $(seq 1 150); do
    if grep -q 'SUCCESS: File received and validated.' "$RUN_DIR/session_manager.log"; then
        result="success"
        break
    fi
    if grep -q 'FAILED:' "$RUN_DIR/session_manager.log"; then
        result="failure"
        break
    fi
    for pid in "$receiver_pid" "$router_pid" "$sender_pid" "$monitor_pid"; do
        [[ -n "$pid" ]] || continue
        kill -0 "$pid" 2>/dev/null || { result="process-died"; break 2; }
    done
    sleep 0.2
done

printf '\n=== Uniflow E2E result ===\n'
printf 'Configured loss: %.2f%%\n' "$DROP_RATE_PERCENT"
printf 'Result: %s\n' "${result:-timeout}"
printf '%s\n' '--- router ---'
cat "$RUN_DIR/router.log"
printf '%s\n' '--- session manager ---'
cat "$RUN_DIR/session_manager.log"
printf '%s\n' '--- sender ---'
cat "$RUN_DIR/sender.log"
printf '%s\n' '--- receiver ---'
cat "$RUN_DIR/receiver.log"
printf '%s\n' '--- file monitor ---'
cat "$RUN_DIR/file_monitor.log"

if [[ "$result" != "success" ]]; then
    exit 1
fi

cmp "$SOURCE_FILE" "$RECEIVED_FILE"
printf 'Payload comparison: OK\n'
printf 'Transfer test passed at %.2f%% configured packet loss.\n' "$DROP_RATE_PERCENT"
