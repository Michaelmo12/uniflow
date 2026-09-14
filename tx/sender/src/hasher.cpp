#include "sender/hasher.hpp"

#include <openssl/sha.h>

std::vector<uint8_t> compute_sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());
    return hash;
}