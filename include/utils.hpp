#pragma once

#include <cstdint>
#include <string>

uint32_t crc32(const std::string& data);

uint32_t crc32_frame(const std::string& payload, uint32_t block_id,
                      uint32_t packet_index, uint8_t type, uint32_t payload_size,
                      uint64_t file_size);

std::string bytes_to_hex(const std::string& bytes);

std::string make_status_json(const std::string& type,
                              const std::string& file_name,
                              uint32_t block_id,
                              uint32_t total_blocks,
                              const std::string& file_hash_hex,
                              uint64_t file_size);
