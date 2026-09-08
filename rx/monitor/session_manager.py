import socket
import os
import hashlib
import json

IPC_SOCKET_PATH = "/tmp/uniflow_status.sock"
DEFAULT_OUTPUT_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../receiver/received_files")
)
DEFAULT_BUILD_OUTPUT_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../receiver/build/received_files")
)
CONFIGURED_OUTPUT_DIR = os.environ.get("UNIFLOW_OUTPUT_DIR")
OUTPUT_DIRS = (
    [os.path.abspath(CONFIGURED_OUTPUT_DIR)]
    if CONFIGURED_OUTPUT_DIR
    else [DEFAULT_OUTPUT_DIR, DEFAULT_BUILD_OUTPUT_DIR]
)

def sanitize_filename(file_name):
    sanitized = file_name.replace("/", "").replace("\\", "")
    return sanitized if sanitized not in ("", ".", "..") else "unnamed_file"

def verify_final_file(file_path, expected_hash, expected_size):
    sha256 = hashlib.sha256()
    try:
        with open(file_path, "r+b") as f:
            f.truncate(expected_size)
            f.seek(0)
            for chunk in iter(lambda: f.read(4096), b""):
                sha256.update(chunk)

        calculated_hash = sha256.digest()
        if calculated_hash == expected_hash:
            print("SUCCESS: File received and validated.", flush=True)
        else:
            print("FAILED: Hash mismatch.", flush=True)
    except Exception as e:
        print(f"FAILED: Could not read file for hashing. Error: {e}", flush=True)

def find_output_file(file_name):
    for output_dir in OUTPUT_DIRS:
        candidate = os.path.join(output_dir, sanitize_filename(file_name))
        if os.path.isfile(candidate):
            return candidate
    return os.path.join(OUTPUT_DIRS[0], sanitize_filename(file_name))

def main():
    if os.path.exists(IPC_SOCKET_PATH):
        os.remove(IPC_SOCKET_PATH)
        
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        server.bind(IPC_SOCKET_PATH)
        server.listen(1)

        print("Session Manager ready. Watching output directories:", flush=True)
        for output_dir in OUTPUT_DIRS:
            print(f"  {output_dir}", flush=True)
        while True:
            print("Waiting for Receiver events...", flush=True)
            conn, _addr = server.accept()

            completed_blocks = set()
            total_blocks_expected = None
            file_path = None
            expected_file_hash = None
            expected_file_size = None

            with conn:
                with conn.makefile("r", encoding="utf-8", newline="\n") as stream:
                    for line in stream:
                        event = json.loads(line)

                        if event["type"] == "BLOCK_COMPLETE":
                            completed_blocks.add(event["block_id"])
                            print(f"Block {event['block_id']} completed.", flush=True)

                            if total_blocks_expected is None:
                                total_blocks_expected = event["total_blocks"]
                                file_path = find_output_file(event["file_name"])
                                expected_file_hash = bytes.fromhex(event["file_hash_hex"])
                                expected_file_size = event["file_size"]

                            if len(completed_blocks) == total_blocks_expected:
                                print("All blocks completed. Starting final hash verification...", flush=True)
                                verify_final_file(file_path, expected_file_hash, expected_file_size)
                                completed_blocks = set()
                                total_blocks_expected = None
                                file_path = None
                                expected_file_hash = None
                                expected_file_size = None

                        elif event["type"] == "BLOCK_FAILED":
                            print(f"FAILED: Block {event['block_id']} could not be reconstructed.", flush=True)
                            completed_blocks = set()
                            total_blocks_expected = None
                            file_path = None
                            expected_file_hash = None
                            expected_file_size = None
    finally:
        server.close()
        if os.path.exists(IPC_SOCKET_PATH):
            os.remove(IPC_SOCKET_PATH)

if __name__ == "__main__":
    main()