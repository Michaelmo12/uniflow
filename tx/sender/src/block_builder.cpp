#include "sender/block_builder.hpp"
#include <algorithm>

std::vector<std::vector<std::vector<uint8_t>>> split_into_blocks(
    const std::vector<uint8_t>& file_bytes,
    int n,
    int payload_size
) 
{
    size_t total_bytes = file_bytes.size();
    // packets * packet_size
    size_t bytes_per_block = static_cast<size_t>(n) * payload_size;
    //
    size_t total_blocks = (total_bytes + bytes_per_block - 1) / bytes_per_block;

    if (total_blocks == 0) {
        total_blocks = 1;
    }

    std::vector<std::vector<std::vector<uint8_t>>> blocks;
    blocks.reserve(total_blocks);

    for (size_t block_index = 0; block_index < total_blocks; ++block_index) 
    {
        std::vector<std::vector<uint8_t>> block;
        block.reserve(n);

        // for each block build a packet
        for (int packet_index = 0; packet_index < n; ++packet_index) 
        {
            std::vector<uint8_t> packet(payload_size, 0);
            
            size_t global_packet_index = block_index * n + packet_index;

            // physical offset in the file for this packet
            size_t start_offset = global_packet_index * payload_size;

            if (start_offset < total_bytes) 
            {
                //"how big is a packet supposed to be" (payload_size) against "how many bytes are actually still left in the file from this point on (total_bytes - start_offset)
                size_t bytes_available = std::min(
                    static_cast<size_t>(payload_size),
                    total_bytes - start_offset
                );
                std::copy(
                    file_bytes.begin() + start_offset,
                    file_bytes.begin() + start_offset + bytes_available,
                    packet.begin()
                );
            }
            // add the packet to the block, even if it's zero-padded
            block.push_back(std::move(packet));
        }
        // add the block to the list of blocks
        blocks.push_back(std::move(block));
    }

    return blocks;
}