"""File Monitor: watches a directory and notifies Sender when a file lands.

More information in the README.md in this directory.
"""

import logging
import os
import socket
import sys

from inotify_simple import INotify, flags

SENDER_SOCKET_PATH: str = "/tmp/uniflow_monitor_to_sender.sock"

logger = logging.getLogger(__name__)


def notify_sender(file_path: str, socket_path: str = SENDER_SOCKET_PATH) -> bool:
    """
    Sends file_path to Sender over a Unix domain socket, raw UTF-8, no
    framing. Same thing `echo -n "path" | nc -U ...` does manually.

    Args:
        file_path: Absolute path of the file to report to Sender.
        socket_path: Path of the Sender's listening Unix domain socket.

    Returns:
        True on success. False if Sender wasn't reachable (not running,
        etc.) — caller just skips this file and keeps watching.
    """
    try:
        path_bytes = file_path.encode("utf-8")
    except UnicodeEncodeError as encode_error:
        logger.error("Failed to encode file path '%s' as UTF-8: %s", file_path, encode_error)
        return False

    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sender_socket:
            sender_socket.connect(socket_path)
            sender_socket.sendall(path_bytes)
        return True
    except OSError as connect_error:
        logger.error("Failed to notify Sender about '%s': %s", file_path, connect_error)
        return False


def watch_directory(watch_path: str, socket_path: str = SENDER_SOCKET_PATH) -> None:
    """
    Watches watch_path forever, notifying Sender each time a file finishes
    writing. Doesn't return on its own — kill the process to stop it.

    Args:
        watch_path: Directory to watch. Gets created (parents included) if
            it doesn't exist yet, and resolved to an absolute path so the
            paths we hand to Sender are always absolute.
        socket_path: Path of the Sender's listening Unix domain socket.
    """
    # full path from root to CWD(current working dictionary)
    watch_path = os.path.abspath(watch_path)

    try:
        os.makedirs(watch_path, exist_ok=True)
    except OSError as create_error:
        logger.error("Failed to create watch directory '%s': %s", watch_path, create_error)
        return

    inotify = INotify()
    watch_flags = flags.CLOSE_WRITE | flags.MOVED_TO
    inotify.add_watch(watch_path, watch_flags)

    logger.info("File Monitor watching '%s', notifying Sender at '%s'...", watch_path, socket_path)

    while True:
        for event in inotify.read():
            # the full path of the new file event.name returns the filename (str) thats changed
            file_path = os.path.join(watch_path, event.name)
            notify_sender(file_path, socket_path)


def main() -> None:
    """
    Entry point: watches the directory given as the first CLI argument, or
    a "watched_files" subfolder of the current working directory if none
    is given. The subfolder is created automatically if it doesn't exist.
    """
    logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
    watch_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.getcwd(), "watched_files")
    watch_directory(watch_path)

if __name__ == "__main__":
    main()
