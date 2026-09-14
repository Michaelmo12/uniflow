#pragma once

#include <vector>
#include "uniflow.pb.h"

/**
 * Reorders packets from block order into interleaved transmission order:
 * packet index 0 from every block, then index 1 from every block, and so
 * on. Spreads a burst of consecutive network losses across many blocks
 * instead of concentrating it in one.
 *
 * @param blocks_of_packets All blocks, each containing exactly N+K packets
 *        in block order (data packets first, then parity)
 * @return A single flat list of all packets, in interleaved send order
 */
std::vector<uniflow::UniflowPacket> interleave(
    const std::vector<std::vector<uniflow::UniflowPacket>>& blocks_of_packets
);  

/** 
 * blocks_of_packets = {
    { B0P0, B0P1, B0P2 },   // block 0
    { B1P0, B1P1, B1P2 },   // block 1
    { B2P0, B2P1, B2P2 }    // block 2
    }

    send_order = { B0P0, B1P0, B2P0, B0P1, B1P1, B2P1, B0P2, B1P2, B2P2 }
    */

