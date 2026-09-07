#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.hpp"
#include "utils.hpp"
#include "fec_manager.hpp"
#include "thread_pool.hpp"
#include "io_managers.hpp"
#include "uniflow.pb.h"

using namespace uniflow;
// this file is the main entry point for the receiver program,
//  which listens for incoming UDP packets, buffers them, reconstructs blocks using FEC,
//  and writes them to disk. It also sends status updates to a Unix domain socket.

namespace {

// processes a completed block of packets, reconstructs missing packets using FEC if necessary,
void process_completed_block(std::vector<UniflowPacket> block, FileManager& file_mgr, IpcClient& ipc) {
    if (block.empty()) return;

    const uint32_t block_id = block.front().block_id();
    const std::string file_name = block.front().file_name();
    const uint32_t total_blocks = block.front().total_blocks();
    const std::string file_hash_hex = bytes_to_hex(block.front().file_hash());
    const std::string file_hash = block.front().file_hash();
    const uint64_t file_size = block.front().file_size();

    if (!fec::reconstruct_block(block)) {
        std::cerr << "[receiver] block " << block_id << " of '" << file_name
                  << "' failed reconstruction; nothing written to disk\n";
        ipc.send_json(make_status_json("BLOCK_FAILED", file_name, block_id, total_blocks, file_hash_hex, file_size));
        return;
    }

    const int fd = file_mgr.get_fd(file_name, file_hash, file_size);
    if (fd < 0) {
        std::cerr << "[receiver] could not open output file for '" << file_name
                  << "': " << std::strerror(errno) << "\n";
        ipc.send_json(make_status_json("BLOCK_FAILED", file_name, block_id, total_blocks, file_hash_hex, file_size));
        return;
    }

    bool any_write_failed = false;
    for (const auto& pkt : block) {
        if (pkt.type() != UniflowPacket::DATA) continue;

        const size_t declared = pkt.payload_size();
        const size_t available = pkt.payload().size();
        const size_t write_len = (declared > 0 && declared <= available) ? declared : available;

        const uint64_t offset =
            static_cast<uint64_t>(block_id) * cfg::N * cfg::PAYLOAD_SIZE +
            static_cast<uint64_t>(pkt.packet_index()) * cfg::PAYLOAD_SIZE;

        const ssize_t written = ::pwrite(fd, pkt.payload().data(), write_len, static_cast<off_t>(offset));
        if (written < 0 || static_cast<size_t>(written) != write_len) {
            std::cerr << "[receiver] pwrite failed for '" << file_name << "' block " << block_id
                      << " index " << pkt.packet_index() << ": " << std::strerror(errno) << "\n";
            any_write_failed = true;
        }
    }

    if (any_write_failed) {
        ipc.send_json(make_status_json("BLOCK_FAILED", file_name, block_id, total_blocks, file_hash_hex, file_size));
        return;
    }

    ipc.send_json(make_status_json("BLOCK_COMPLETE", file_name, block_id, total_blocks, file_hash_hex, file_size));
}

std::atomic<bool> g_running{true};
void handle_signal(int) { g_running.store(false); }

// creates a UDP socket, binds it to the configured port, and returns the socket file descriptor
int make_listen_socket() {
    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "fatal: socket() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    int reuse = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int rcvbuf = 4 * 1024 * 1024;
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        std::cerr << "warning: could not raise SO_RCVBUF; high packet rates may see "
                     "kernel-level drops before packets ever reach the CRC check\n";
    }

    timeval rcv_timeout{1, 0};
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout, sizeof(rcv_timeout));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(cfg::LISTEN_PORT);

    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "fatal: bind() to UDP port " << cfg::LISTEN_PORT << " failed: "
                  << std::strerror(errno) << "\n";
        ::close(sock);
        return -1;
    }
    return sock;
}

}

int main() {
    if (!fec::init()) {
        std::cerr << "fatal: fec::init() failed (longhair library/header version mismatch)\n";
        return 1;
    }
    // sets up signal handlers for graceful shutdown and ignores SIGPIPE and SIGXFSZ
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGXFSZ, SIG_IGN);

    if (::mkdir(cfg::OUTPUT_DIR, 0755) < 0 && errno != EEXIST) {
        std::cerr << "warning: could not create output directory '" << cfg::OUTPUT_DIR
                  << "': " << std::strerror(errno) << "\n";
    }

    const int sock = make_listen_socket();
    if (sock < 0) return 1;

    std::cout << "[receiver] listening on UDP :" << cfg::LISTEN_PORT
              << " (N=" << cfg::N << " K=" << cfg::K << " PAYLOAD_SIZE=" << cfg::PAYLOAD_SIZE << ")\n";

    FileManager file_mgr;
    IpcClient ipc(cfg::STATUS_SOCK_PATH);
    BlockBufferManager block_mgr;
    ThreadPool pool(std::max(2u, std::thread::hardware_concurrency()));

    std::vector<char> buffer(cfg::MAX_DATAGRAM_SIZE);
    auto last_sweep = std::chrono::steady_clock::now();
    uint64_t datagrams_received = 0;
    uint64_t parse_failures = 0;
    uint64_t crc_failures = 0;
    uint64_t packets_accepted = 0;
    uint64_t blocks_completed = 0;

    // main loop: receives UDP packets, buffers them, reconstructs blocks using FEC, and writes them to disk
    while (g_running.load()) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_sweep >= cfg::STALE_SWEEP_INTERVAL) {
            last_sweep = now;
            for (auto& tb : block_mgr.sweep_stale(cfg::STALE_BLOCK_TIMEOUT)) {
                std::cerr << "[receiver] block " << tb.block_id << " of '" << tb.file_name
                          << "' timed out with " << tb.packets_received << "/" << cfg::N
                          << " packets — giving up, freeing buffer\n";
                ipc.send_json(make_status_json("BLOCK_FAILED", tb.file_name, tb.block_id,
                                                tb.total_blocks, tb.file_hash_hex, tb.file_size));
            }
        }

        sockaddr_in src{};
        socklen_t src_len = sizeof(src);

        const ssize_t n = ::recvfrom(sock, buffer.data(), buffer.size(), 0,
                                      reinterpret_cast<sockaddr*>(&src), &src_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            std::cerr << "[receiver] recvfrom() error: " << std::strerror(errno) << "\n";
            continue;
        }
        if (n == 0) continue;
        ++datagrams_received;

        UniflowPacket pkt;
        if (!pkt.ParseFromArray(buffer.data(), static_cast<int>(n))) {
            ++parse_failures;
            std::cerr << "[receiver] dropped unparsable datagram (" << n << " bytes)\n";
            continue;
        }

        const uint32_t expected_crc = crc32(pkt.payload());
        if (expected_crc != pkt.crc32()) {
            ++crc_failures;
            if (crc_failures == 1 || crc_failures % 100 == 0) {
                std::cerr << "[receiver] CRC mismatch: received=0x"
                          << std::hex << pkt.crc32() << " expected=0x" << expected_crc
                          << std::dec << " block=" << pkt.block_id()
                          << " index=" << pkt.packet_index()
                          << " payload=" << pkt.payload().size()
                          << " file_size=" << pkt.file_size() << "\n";
            }
            continue;
        }

        ++packets_accepted;
        std::vector<UniflowPacket> completed_block;
        if (block_mgr.add_packet(std::move(pkt), completed_block)) {
            ++blocks_completed;
            std::cerr << "[receiver] block ready: blocks=" << blocks_completed
                      << " datagrams=" << datagrams_received
                      << " accepted=" << packets_accepted
                      << " crc_failures=" << crc_failures << "\n";
            pool.enqueue([blk = std::move(completed_block), &file_mgr, &ipc]() mutable {
                process_completed_block(std::move(blk), file_mgr, ipc);
            });
        }
    }

    std::cout << "[receiver] shutting down...\n";
    ::close(sock);
    return 0;
}
