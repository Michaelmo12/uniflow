#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
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

namespace {

void process_completed_block(std::vector<UniflowPacket> block, FileManager& file_mgr, IpcClient& ipc) {
    if (block.empty()) return;

    const uint32_t block_id = block.front().block_id();
    const std::string file_name = block.front().file_name();
    const uint32_t total_blocks = block.front().total_blocks();
    const std::string file_hash_hex = bytes_to_hex(block.front().file_hash());

    if (!fec::reconstruct_block(block)) {
        std::cerr << "[receiver] block " << block_id << " of '" << file_name
                  << "' failed reconstruction; nothing written to disk\n";
        ipc.send_json(make_status_json("BLOCK_FAILED", file_name, block_id, total_blocks, file_hash_hex));
        return;
    }

    const int fd = file_mgr.get_fd(file_name);
    if (fd < 0) {
        std::cerr << "[receiver] could not open output file for '" << file_name
                  << "': " << std::strerror(errno) << "\n";
        ipc.send_json(make_status_json("BLOCK_FAILED", file_name, block_id, total_blocks, file_hash_hex));
        return;
    }

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
        }
    }

    ipc.send_json(make_status_json("BLOCK_COMPLETE", file_name, block_id, total_blocks, file_hash_hex));
}

std::atomic<bool> g_running{true};
void handle_signal(int) { g_running.store(false); }

int make_listen_socket() {
    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "fatal: socket() failed: " << std::strerror(errno) << "\n";
        return -1;
    }

    int reuse = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

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

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

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

    while (g_running.load()) {
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

        UniflowPacket pkt;
        if (!pkt.ParseFromArray(buffer.data(), static_cast<int>(n))) {
            std::cerr << "[receiver] dropped unparsable datagram (" << n << " bytes)\n";
            continue;
        }

        if (crc32(pkt.payload()) != pkt.crc32()) {
            continue;
        }

        std::vector<UniflowPacket> completed_block;
        if (block_mgr.add_packet(std::move(pkt), completed_block)) {
            pool.enqueue([blk = std::move(completed_block), &file_mgr, &ipc]() mutable {
                process_completed_block(std::move(blk), file_mgr, ipc);
            });
        }
    }

    std::cout << "[receiver] shutting down...\n";
    ::close(sock);
    return 0;
}
