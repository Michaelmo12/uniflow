#include "utils.hpp"

#include <array>
#include <cstdio>
#include <sstream>

// this file contains utility functions for computing CRC32 checksums,
//  converting bytes to hex strings, and creating JSON status messages

// computes the CRC32 checksum of a string using a precomputed table
uint32_t crc32(const std::string& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                // uses the polynomial 0xEDB88320 for CRC32
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    // computes the CRC32 checksum of the input data using the precomputed table
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string bytes_to_hex(const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out += hex[c >> 4];
        out += hex[c & 0x0F];
    }
    return out;
}

namespace {
// appends a 32-bit unsigned integer to a string in little-endian order
void append_le32(std::string& out, uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
    out += static_cast<char>((v >> 16) & 0xFF);
    out += static_cast<char>((v >> 24) & 0xFF);
}

// appends a 64-bit unsigned integer to a string in little-endian order
void append_le64(std::string& out, uint64_t v) {
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        out += static_cast<char>((v >> shift) & 0xFF);
    }
}
}

uint32_t crc32_frame(const std::string& payload, uint32_t block_id,
                      uint32_t packet_index, uint8_t type, uint32_t payload_size,
                      uint64_t file_size) {
    std::string frame;
    frame.reserve(payload.size() + 21);
    frame += payload;
    append_le32(frame, block_id);
    append_le32(frame, packet_index);
    frame += static_cast<char>(type);
    append_le32(frame, payload_size);
    append_le64(frame, file_size);
    return crc32(frame);
}

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}

std::string make_status_json(const std::string& type,
                              const std::string& file_name,
                              uint32_t block_id,
                              uint32_t total_blocks,
                              const std::string& file_hash_hex,
                              uint64_t file_size) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"" << type << "\","
        << "\"file_name\":\"" << json_escape(file_name) << "\","
        << "\"block_id\":" << block_id << ","
        << "\"total_blocks\":" << total_blocks << ","
        << "\"file_size\":" << file_size << ","
        << "\"file_hash_hex\":\"" << file_hash_hex << "\""
        << "}";
    return oss.str();
}
