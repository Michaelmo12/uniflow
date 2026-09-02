#pragma once

#include <vector>
#include <cstdint>

/**
 * Initializes Longhair's Cauchy Reed-Solomon engine. Call once, before any
 * encode_parity call, to confirm the library linked correctly.
 *
 * @return true if initialization succeeded
 */
bool init_reed_solomon();

/**
 * Generates k parity blocks from a set of data blocks. Any n of the
 * resulting (n + k) blocks are enough to reconstruct all n data blocks.
 *
 * @param data_blocks The n data blocks, all the same size
 * @param k Number of parity blocks to generate
 * @return k parity blocks, or empty on failure
 */
std::vector<std::vector<uint8_t>> encode_parity(
    const std::vector<std::vector<uint8_t>>& data_blocks,
    int k
);