#include "fec_manager.hpp"
#include "config.hpp"

#include <algorithm>
#include <iostream>

#include "cauchy_256.h"

using uniflow::UniflowPacket;

// creates the cauchy_256 tables
// it makes sure that the c library is initialized and that the header and library versions match
bool fec::init() {
    if (cauchy_256_init() != 0) {
        std::cerr << "[fec] cauchy_256_init() failed — longhair library/header version mismatch\n";
        return false;
    }
    return true;
}

// reconstructs a block of  N (100) packets - some of which may be PARITY
bool fec::reconstruct_block(std::vector<UniflowPacket>& block) {
    // checks that the block has exactly N packets, and that all packets have the expected payload size
    if (block.size() != cfg::N) {
        std::cerr << "[fec] reconstruct_block called with " << block.size()
                  << " packets, expected exactly " << cfg::N << "\n";
        return false;
    }
    // checks that all packets are DATA packets, and if so, sorts them by packet_index and returns true
    const bool all_data = std::all_of(block.begin(), block.end(),
        [](const UniflowPacket& p) { return p.type() == UniflowPacket::DATA; });

    if (all_data) {
        std::sort(block.begin(), block.end(),
            [](const UniflowPacket& a, const UniflowPacket& b) {
                return a.packet_index() < b.packet_index();
            });
        return true;
    }

    // checks that all packets have the expected payload size, protects against memory corruption
    // returns false if any packet has a different size
    for (const auto& pkt : block) {
        if (pkt.payload().size() != cfg::PAYLOAD_SIZE) {
            std::cerr << "[fec] block " << block.front().block_id()
                      << ": packet_index=" << pkt.packet_index() << " has payload size "
                      << pkt.payload().size() << ", expected exactly " << cfg::PAYLOAD_SIZE
                      << " — refusing to decode\n";
            return false;
        }
    }
    
    // sorts the block so that all DATA packets are at the front, and all PARITY packets are at the back
    std::stable_partition(block.begin(), block.end(), [](const UniflowPacket& p) {
        return p.type() == UniflowPacket::DATA;
    });

    // creates a vector of Block structs, which are used by the cauchy_256_decode function
    std::vector<Block> shards(cfg::N);
    for (uint32_t i = 0; i < cfg::N; ++i) {
        shards[i].row = static_cast<unsigned char>(block[i].packet_index());
        shards[i].data = reinterpret_cast<unsigned char*>(block[i].mutable_payload()->data());
    }

    // calls the cauchy_256_decode function, 
    // which reconstructs the missing DATA packets from the PARITY packets
    const int rc = cauchy_256_decode(static_cast<int>(cfg::N), static_cast<int>(cfg::K),
                                      shards.data(), static_cast<int>(cfg::PAYLOAD_SIZE));
    if (rc != 0) {
        std::cerr << "[fec] cauchy_256_decode failed (code " << rc << ") for block "
                  << block.front().block_id() << "\n";
        return false;
    }

    // updates the block with the reconstructed DATA packets
    for (uint32_t i = 0; i < cfg::N; ++i) {
        UniflowPacket& pkt = block[i];
        if (pkt.type() != UniflowPacket::DATA) {
            pkt.set_packet_index(shards[i].row);
            pkt.set_type(UniflowPacket::DATA);
            pkt.set_payload_size(cfg::PAYLOAD_SIZE);
        }
    }

    // sorts the block so that all DATA packets are at the front, and all PARITY packets are at the back
    std::sort(block.begin(), block.end(),
        [](const UniflowPacket& a, const UniflowPacket& b) {
            return a.packet_index() < b.packet_index();
        });
    return true;
}
