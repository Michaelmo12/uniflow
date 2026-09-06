# File Monitor

Watches a directory and tells Sender (the C++ process) about any file that
shows up, so Sender can pick it up and transmit it.

## How

Uses Linux inotify (`inotify_simple`), watching for two events:

- **CLOSE_WRITE** — a file opened for writing was closed. Covers normal
  writes and cross-filesystem moves (those are copy+delete under the hood).
- **MOVED_TO** — a file was moved/renamed in from the same filesystem.
  Atomic, so the file is already complete when this fires.

Together these only fire once a file is actually finished, so there's no
need to debounce or poll file size to guess when a write is done.

For each event, `notify_sender()` opens a Unix domain socket connection to
Sender at `/tmp/uniflow_monitor_to_sender.sock`, writes the file's absolute
path as raw UTF-8 bytes (no framing), and closes the connection — equivalent
to:

```
echo -n "/path/to/file" | nc -U -q1 /tmp/uniflow_monitor_to_sender.sock
```

If Sender isn't reachable, it logs to stderr and keeps watching — nothing
crashes, and there's no state to get out of sync since Sender takes a fresh
connection per file.

Linux-only (inotify), which is fine since this project only runs on WSL2/Linux.

## Running it

```
pip install inotify_simple
python3 file_monitor.py /path/to/watch
```

Leave off the path and it watches `./watched_files` (relative to whatever
your shell's `cwd` is when you run it — not the script's own location).
That folder is created automatically if it doesn't exist yet.
