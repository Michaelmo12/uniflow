#pragma once

#include <vector>
#include <cstdint>

/**
 * Computes the SHA-256 hash of the given bytes.
 *
 * @param data The bytes to hash
 * @return The 32-byte SHA-256 digest
 */
std::vector<uint8_t> compute_sha256(const std::vector<uint8_t>& data);