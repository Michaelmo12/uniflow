#pragma once

/**
 * Creates, binds, and starts listening on a Unix domain socket at the given path.
 *
 * @param socket_path Filesystem path where the socket will be created
 * @return The socket's file descriptor, or -1 on failure
 */
int setup_ipc_socket(const char* socket_path);
