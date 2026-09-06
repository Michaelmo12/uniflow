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

uint32_t compute_crc32_frame(const std::vector<uint8_t>& payload,
							 uint32_t block_id,
							 uint32_t packet_index,
							 uint8_t type,
							 uint32_t payload_size,
							 uint64_t file_size);
