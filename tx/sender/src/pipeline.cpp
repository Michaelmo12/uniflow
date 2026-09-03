#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>

#include "uniflow.pb.h"
#include "sender/sender.hpp"
#include "sender/config.hpp"
#include "sender/file_reader.hpp"
#include "sender/hasher.hpp"
#include "sender/block_builder.hpp"
#include "sender/reed_solomon.hpp"
#include "sender/interleaver.hpp"

std::vector<std::vector<uniflow::UniflowPacket>> build_packets_for_file(const std::string& file_path) 
{

    std::vector<uint8_t> file_bytes = read_file(file_path);
    if (file_bytes.empty()) 
    {
        std::cerr << "Failed to read file: " << file_path << "\n";
        return {};
    }

    uint64_t original_file_size = file_bytes.size();
    std::vector<uint8_t> file_hash = compute_sha256(file_bytes);
    std::string file_name = std::filesystem::path(file_path).filename().string();

    std::vector<std::vector<std::vector<uint8_t>>> data_blocks =
        split_into_blocks(file_bytes, FEC_N, PAYLOAD_SIZE);
    uint32_t total_blocks = static_cast<uint32_t>(data_blocks.size());

    std::cout << "File split into " << total_blocks << " block(s)\n";

    std::vector<std::vector<uniflow::UniflowPacket>> blocks_of_packets;
    blocks_of_packets.reserve(total_blocks);

    for (uint32_t block_id = 0; block_id < total_blocks; ++block_id) 
    {
        const std::vector<std::vector<uint8_t>>& block_data_packets = data_blocks[block_id];

        std::vector<std::vector<uint8_t>> block_parity_packets =
            encode_parity(block_data_packets, FEC_K);

        if (block_parity_packets.empty()) 
        {
            std::cerr << "Failed to encode parity for block " << block_id << "\n";
            return {};
        }

        std::vector<uniflow::UniflowPacket> current_block_packets;
        current_block_packets.reserve(FEC_N + FEC_K);

        // turning each of those N(100) raw chunks into an actual, real UniflowPacket
        for (uint32_t packet_index = 0; packet_index < FEC_N; ++packet_index) 
        {
            uniflow::UniflowPacket data_packet = build_packet(
                file_name, block_id, packet_index, uniflow::UniflowPacket::DATA,
                block_data_packets[packet_index], total_blocks, file_hash, original_file_size
            );
            current_block_packets.push_back(std::move(data_packet));
        }

        // turning each of those K(70) parity chunks into an actual, real UniflowPacket
        for (uint32_t parity_index = 0; parity_index < FEC_K; ++parity_index) 
        {
            uint32_t packet_index = FEC_N + parity_index;
            uniflow::UniflowPacket parity_packet = build_packet(
                file_name, block_id, packet_index, uniflow::UniflowPacket::PARITY,
                block_parity_packets[parity_index], total_blocks, file_hash, original_file_size
            );
            current_block_packets.push_back(std::move(parity_packet));
        }

        blocks_of_packets.push_back(std::move(current_block_packets));
    }

    return blocks_of_packets;
}

uint32_t send_all_packets(const std::vector<std::vector<uniflow::UniflowPacket>>& blocks_of_packets) {
    std::vector<uniflow::UniflowPacket> send_order = interleave(blocks_of_packets);

    std::cout << "Sending " << send_order.size() << " packets to "
              << RECEIVER_IP << ":" << RECEIVER_PORT << "...\n";

    uint32_t packets_sent = 0;
    for (const uniflow::UniflowPacket& packet : send_order) {
        if (send_packet(packet, RECEIVER_IP, RECEIVER_PORT)) {
            packets_sent++;
        }
    }

    std::cout << "Sent " << packets_sent << " / " << send_order.size() << " packets\n";
    return packets_sent;
}

void process_file(const std::string& file_path) {
    std::cout << "Received file path: " << file_path << "\n";

    std::vector<std::vector<uniflow::UniflowPacket>> blocks_of_packets = build_packets_for_file(file_path);
    if (blocks_of_packets.empty()) {
        return;
    }

    send_all_packets(blocks_of_packets);
}