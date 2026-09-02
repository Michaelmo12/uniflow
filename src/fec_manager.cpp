#include "fec_manager.hpp"
#include "config.hpp"

#include <algorithm>
#include <iostream>

#include "cauchy_256.h"

using uniflow::UniflowPacket;

bool fec::init() {
    if (cauchy_256_init() != 0) {
        std::cerr << "[fec] cauchy_256_init() failed — longhair library/header version mismatch\n";
        return false;
    }
    return true;
}

bool fec::reconstruct_block(std::vector<UniflowPacket>& block) {
    if (block.size() != cfg::N) {
        std::cerr << "[fec] reconstruct_block called with " << block.size()
                  << " packets, expected exactly " << cfg::N << "\n";
        return false;
    }

    const bool all_data = std::all_of(block.begin(), block.end(),
        [](const UniflowPacket& p) { return p.type() == UniflowPacket::DATA; });

    if (all_data) {
        std::sort(block.begin(), block.end(),
            [](const UniflowPacket& a, const UniflowPacket& b) {
                return a.packet_index() < b.packet_index();
            });
        return true;
    }

    for (const auto& pkt : block) {
        if (pkt.payload().size() != cfg::PAYLOAD_SIZE) {
            std::cerr << "[fec] block " << block.front().block_id()
                      << ": packet_index=" << pkt.packet_index() << " has payload size "
                      << pkt.payload().size() << ", expected exactly " << cfg::PAYLOAD_SIZE
                      << " — refusing to decode\n";
            return false;
        }
    }

    std::stable_partition(block.begin(), block.end(), [](const UniflowPacket& p) {
        return p.type() == UniflowPacket::DATA;
    });

    std::vector<Block> shards(cfg::N);
    for (uint32_t i = 0; i < cfg::N; ++i) {
        shards[i].row = static_cast<unsigned char>(block[i].packet_index());
        shards[i].data = reinterpret_cast<unsigned char*>(block[i].mutable_payload()->data());
    }

    const int rc = cauchy_256_decode(static_cast<int>(cfg::N), static_cast<int>(cfg::K),
                                      shards.data(), static_cast<int>(cfg::PAYLOAD_SIZE));
    if (rc != 0) {
        std::cerr << "[fec] cauchy_256_decode failed (code " << rc << ") for block "
                  << block.front().block_id() << "\n";
        return false;
    }

    for (uint32_t i = 0; i < cfg::N; ++i) {
        UniflowPacket& pkt = block[i];
        if (pkt.type() != UniflowPacket::DATA) {
            pkt.set_packet_index(shards[i].row);
            pkt.set_type(UniflowPacket::DATA);
            pkt.set_payload_size(cfg::PAYLOAD_SIZE);
        }
    }

    std::sort(block.begin(), block.end(),
        [](const UniflowPacket& a, const UniflowPacket& b) {
            return a.packet_index() < b.packet_index();
        });
    return true;
}
