#include "sender/interleaver.hpp"

std::vector<uniflow::UniflowPacket> interleave(
    const std::vector<std::vector<uniflow::UniflowPacket>>& blocks_of_packets
) 
{
    std::vector<uniflow::UniflowPacket> send_order;

    if (blocks_of_packets.empty()) 
    {
        return send_order;
    }

    size_t packets_per_block = blocks_of_packets[0].size();

    // reserve space for all packets in the send order 
    send_order.reserve(blocks_of_packets.size() * packets_per_block);

    // (1) for each packet index, add the corresponding packet (2) from each block to the send order
    for (size_t packet_index = 0; packet_index < packets_per_block; ++packet_index) 
    {
        for (const std::vector<uniflow::UniflowPacket>& current_block_packets : blocks_of_packets) {
            send_order.push_back(current_block_packets[packet_index]);
        }
    }

    return send_order;
}
