#pragma once

#include <string>

/**
 * Creates, binds, and starts listening on a Unix domain socket at the given path.
 *
 * @param socket_path Filesystem path where the socket will be created
 * @return The socket's file descriptor, or -1 on failure
 */
int setup_ipc_socket(const char* socket_path);

/**
 * Blocks until File Monitor connects and sends a message.
 *
 * @param ipc_fd File descriptor of a socket already listening (from setup_ipc_socket)
 * @return The message received from File Monitor
 */
std::string wait_for_file_monitor(int ipc_fd);