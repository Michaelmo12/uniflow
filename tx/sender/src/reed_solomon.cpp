#include "sender/reed_solomon.hpp"
#include "cauchy_256.h"

bool init_reed_solomon() {
    return cauchy_256_init() == 0;
}

std::vector<std::vector<uint8_t>> encode_parity(
    const std::vector<std::vector<uint8_t>>& data_blocks,
    int k
) {
    int n = static_cast<int>(data_blocks.size());
    int block_size = static_cast<int>(data_blocks[0].size());

    std::vector<const unsigned char*> data_ptrs(n);
    for (int i = 0; i < n; ++i) {
        data_ptrs[i] = data_blocks[i].data();
    }

    //flat buffer of k * block_size bytes
    std::vector<uint8_t> recovery_blocks(k * block_size);
    if (cauchy_256_encode(n, k, data_ptrs.data(), recovery_blocks.data(), block_size)) {
        return {};
    }

    //chops it back into k separate, individual packets, matching the shape of the input data_blocks
    std::vector<std::vector<uint8_t>> parity_blocks(k);
    for (int i = 0; i < k; ++i) {
        parity_blocks[i].assign(
            recovery_blocks.begin() + i * block_size,
            recovery_blocks.begin() + (i + 1) * block_size
        );
    }
    return parity_blocks;
}