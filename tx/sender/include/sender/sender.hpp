#pragma once

#include <string>

#include "uniflow.pb.h"

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

/**
 * Builds a minimal UniflowPacket wrapping the given message.
 *
 * @param message Text to wrap in the packet's payload
 * @return A populated UniflowPacket, ready to serialize
 */
uniflow::UniflowPacket build_packet(const std::string& message);

/**
 * Serializes the packet and sends it as one UDP datagram to the given address.
 *
 * @param packet The packet to send
 * @param ip Destination IP address as a string, e.g. "127.0.0.1"
 * @param port Destination UDP port
 * @return true on success, false on failure
 */
bool send_packet(const uniflow::UniflowPacket& packet, const char* ip, int port);