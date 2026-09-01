import socket
import os
import hashlib
import json

IPC_SOCKET_PATH = "/tmp/uniflow_status.sock"

def verify_final_file(file_path, expected_hash):
    """מבצע בדיקת SHA-256 על הקובץ הסופי שנוצר בדיסק"""
    sha256 = hashlib.sha256()
    try:
        with open(file_path, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                sha256.update(chunk)
        
        calculated_hash = sha256.digest() # 32 raw bytes
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
    server.bind(IPC_SOCKET_PATH)
    server.listen(1)
    
    print("Session Manager ready. Waiting for Receiver events...")
    conn, addr = server.accept()
    
    completed_blocks = set()
    total_blocks_expected = None
    file_path = "output_file.dat" # יש לעדכן לשם הקובץ שהתקבל
    expected_file_hash = None
    
    with conn:
        while True:
            data = conn.recv(1024)
            if not data:
                break
            
            # פענוח אירוע הסטטוס מה-Receiver
            event = json.loads(data.decode('utf-8'))
            
            if event['type'] == "BLOCK_COMPLETE":
                completed_blocks.add(event['block_id'])
                print(f"Block {event['block_id']} completed.")
                
                # אתחול הנתונים אם זו ההודעה הראשונה
                if total_blocks_expected is None:
                    total_blocks_expected = event['total_blocks']
                    expected_file_hash = bytes.fromhex(event['file_hash_hex']) 
                    
                # בדיקה אם סיימנו את כל הבלוקים
                if len(completed_blocks) == total_blocks_expected:
                    print("All blocks completed. Starting final hash verification...")
                    verify_final_file(file_path, expected_file_hash)
                    break
                    
            elif event['type'] == "BLOCK_FAILED":
                print(f"FAILED: Block {event['block_id']} could not be reconstructed.")
                break

if __name__ == "__main__":
    main()