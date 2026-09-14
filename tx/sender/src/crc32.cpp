#include "sender/crc32.hpp"
#include <zlib.h>

uint32_t compute_crc32(const std::vector<uint8_t>& data) {
    uLong checksum = crc32(0L, Z_NULL, 0);
    checksum = crc32(checksum, data.data(), static_cast<uInt>(data.size()));
    return static_cast<uint32_t>(checksum);
}

