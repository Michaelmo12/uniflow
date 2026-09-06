#include "sender/crc32.hpp"
#include <zlib.h>

namespace {

void append_le32(std::vector<uint8_t>& frame, uint32_t value) {
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        frame.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

void append_le64(std::vector<uint8_t>& frame, uint64_t value) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        frame.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

}

uint32_t compute_crc32(const std::vector<uint8_t>& data) {
    uLong checksum = crc32(0L, Z_NULL, 0);
    checksum = crc32(checksum, data.data(), static_cast<uInt>(data.size()));
    return static_cast<uint32_t>(checksum);
}

uint32_t compute_crc32_frame(const std::vector<uint8_t>& payload,
                             uint32_t block_id,
                             uint32_t packet_index,
                             uint8_t type,
                             uint32_t payload_size,
                             uint64_t file_size) {
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 21);
    frame.insert(frame.end(), payload.begin(), payload.end());
    append_le32(frame, block_id);
    append_le32(frame, packet_index);
    frame.push_back(type);
    append_le32(frame, payload_size);
    append_le64(frame, file_size);
    return compute_crc32(frame);
}
