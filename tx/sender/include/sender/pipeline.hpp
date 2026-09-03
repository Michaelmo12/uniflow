#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "uniflow.pb.h"

/**
 * Reads a file, computes its hash, splits it into FEC blocks, encodes
 * parity for each block, and builds every DATA and PARITY packet needed
 * to send it -- everything up to (but not including) actually sending.
 *
 * @param file_path Path of the file to process
 * @return Every block's packets, in block order (data packets first, then
 *         parity), one inner vector per block -- or {} on any failure
 *         (already logged to stderr by this function)
 */
std::vector<std::vector<uniflow::UniflowPacket>> build_packets_for_file(const std::string& file_path);

/**
 * Interleaves every block's packets into transmission order and sends
 * them all over UDP.
 *
 * @param blocks_of_packets Every block's packets, in block order (as
 *        produced by build_packets_for_file)
 * @return How many packets were sent successfully, out of the total
 */
uint32_t send_all_packets(const std::vector<std::vector<uniflow::UniflowPacket>>& blocks_of_packets);

/**
 * Fully processes one file notified by File Monitor: builds every packet
 * it needs, then sends them all to Receiver.
 *
 * @param file_path Path of the file to process, as received from File Monitor
 */
void process_file(const std::string& file_path);