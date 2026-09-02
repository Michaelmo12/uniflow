#pragma once

#include <vector>
#include <cstdint>

/**
 * Computes the CRC32 checksum of the given bytes.
 *
 * @param data The bytes to checksum
 * @return The CRC32 checksum
 */
uint32_t compute_crc32(const std::vector<uint8_t>& data);
