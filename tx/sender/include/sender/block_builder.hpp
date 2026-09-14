#pragma once

#include <vector>
#include <cstdint>

/**
 * Splits a file's bytes into blocks of n data packets each, payload_size
 * bytes per packet. If the file doesn't divide evenly, the final packet
 * is zero-padded to a full payload_size — this is the padding your
 * partner flagged, which original_file_size exists to undo later.
 *
 * @param file_bytes The full file contents
 * @param n Data packets per block
 * @param payload_size Bytes per packet
 * @return All blocks; each block contains exactly n packets
 */
std::vector<std::vector<std::vector<uint8_t>>> split_into_blocks(
    const std::vector<uint8_t>& file_bytes,
    int n,
    int payload_size
);