import socket
import os
import hashlib
import json

IPC_SOCKET_PATH = "/tmp/uniflow_status.sock"
OUTPUT_DIR = "received_files"

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
            print("SUCCESS: File received and validated.")
        else:
            print("FAILED: Hash mismatch.")
    except Exception as e:
        print(f"FAILED: Could not read file for hashing. Error: {e}")

def main():
    if os.path.exists(IPC_SOCKET_PATH):
        os.remove(IPC_SOCKET_PATH)
        
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        server.bind(IPC_SOCKET_PATH)
        server.listen(1)

        print("Session Manager ready. Waiting for Receiver events...")
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
                        print(f"Block {event['block_id']} completed.")

                        if total_blocks_expected is None:
                            total_blocks_expected = event["total_blocks"]
                            file_path = os.path.join(
                                OUTPUT_DIR, sanitize_filename(event["file_name"])
                            )
                            expected_file_hash = bytes.fromhex(event["file_hash_hex"])
                            expected_file_size = event["file_size"]

                        if len(completed_blocks) == total_blocks_expected:
                            print("All blocks completed. Starting final hash verification...")
                            verify_final_file(file_path, expected_file_hash, expected_file_size)
                            break

                    elif event["type"] == "BLOCK_FAILED":
                        print(f"FAILED: Block {event['block_id']} could not be reconstructed.")
                        break
    finally:
        server.close()
        if os.path.exists(IPC_SOCKET_PATH):
            os.remove(IPC_SOCKET_PATH)

if __name__ == "__main__":
    main()