#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <netinet/in.h>

#include "uniflow.pb.h"

/**
 * Creates, binds, and starts listening on a Unix domain socket at the given path.
 *
 * @param socket_path Filesystem path where the socket will be created
 * @return The socket's file descriptor, or -1 on failure
 */
int setup_ipc_socket(const char* socket_path);

/**
 * Blocks until File Monitor connects and sends a file path.
 *
 * @param ipc_fd File descriptor of a socket already listening (from setup_ipc_socket)
 * @return The message (file path in this case) received from File Monitor, or "" on failure
 *         (accept or read error)
 */
std::string wait_for_file_monitor(int ipc_fd);

/**
 * Creates a UDP socket meant to be reused for many sends, rather than
 * opened fresh per packet.
 *
 * @return The socket's file descriptor, or -1 on failure
 */
int setup_udp_socket();

/**
 * Builds a fully-populated UniflowPacket, including computing crc32 over
 * the given payload.
 *
 * @param file_name Original file's name
 * @param block_id Which block this packet belongs to
 * @param packet_index Position within the block (0..N+K-1)
 * @param type DATA or PARITY
 * @param payload This packet's bytes (a file chunk or parity chunk)
 * @param total_blocks Total blocks in this file transfer
 * @param file_hash SHA-256 digest of the original (unpadded) file
 * @param original_file_size Real size of the original file in bytes
 * @return A populated UniflowPacket, ready to serialize
 */
uniflow::UniflowPacket build_packet(
    const std::string& file_name,
    uint32_t block_id,
    uint32_t packet_index,
    uniflow::UniflowPacket::PacketType type,
    const std::vector<uint8_t>& payload,
    uint32_t total_blocks,
    const std::vector<uint8_t>& file_hash,
    uint64_t original_file_size
);

/**
 * Sends one packet over an already-open UDP socket to a fixed destination.
 *
 * @param udp_fd An open UDP socket, from setup_udp_socket()
 * @param packet The packet to send
 * @param receiver_addr The destination address, built once by the caller
 * @return true on success, false on failure
 */
bool send_packet(int udp_fd, const uniflow::UniflowPacket& packet,
                 const sockaddr_in& receiver_addr);