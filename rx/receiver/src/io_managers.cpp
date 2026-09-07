#include "io_managers.hpp"
#include "config.hpp"
#include "utils.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using uniflow::UniflowPacket;
// this file is in charge of managing file descriptors for writing received packets to disk, 
// and sending status updates to the status socket

FileManager::~FileManager() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [name, fd] : fds_) {
        (void)name;
        if (fd >= 0) ::close(fd);
    }
}

// sanitizes a file name by removing any path components ( '/' or '\')
//  and replacing empty or invalid names with "unnamed_file"
// protects against directory traversal attacks and ensures that the file is created in the output directory
std::string FileManager::sanitize(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == '/' || c == '\\') continue;
        out += c;
    }
    if (out.empty() || out == "." || out == "..") out = "unnamed_file";
    return out;
}

// gets a file descriptor for writing to a file with the given name, hash, and size
int FileManager::get_fd(const std::string& file_name, const std::string& file_hash,
                        uint64_t file_size) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = fds_.find(file_name);
    if (it != fds_.end() && it->second >= 0) {
        const auto identity = identities_.find(file_name);
        if (identity != identities_.end() && identity->second.file_hash == file_hash &&
            identity->second.file_size == file_size) {
            return it->second;
        }
        ::close(it->second);
    }

    const std::string path = std::string(cfg::OUTPUT_DIR) + "/" + sanitize(file_name);
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    fds_[file_name] = fd;
    identities_[file_name] = FileIdentity{file_hash, file_size};
    return fd;
}

// manages a Unix domain socket connection to send status updates to session_manager.py
IpcClient::IpcClient(std::string path) : path_(std::move(path)) {
    std::lock_guard<std::mutex> lk(mtx_);
    // attempts to connect to the socket immediately, but will retry on the first send if it fails
    connect_locked();
}

// closes the socket file descriptor if it is open
IpcClient::~IpcClient() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (fd_ >= 0) ::close(fd_);
}

// attempts to connect to the Unix domain socket, returns true if successful, false otherwise
bool IpcClient::connect_locked() {
    if (fd_ >= 0) return true;

    const int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(s);
        return false;
    }
    fd_ = s;
    return true;
}

// sends a JSON string to the connected Unix domain socket, reconnecting if necessary
void IpcClient::send_json(const std::string& json) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (fd_ < 0 && !connect_locked()) {
        std::cerr << "[ipc] status socket unavailable, dropping event: " << json << "\n";
        return;
    }

    const std::string msg = json + "\n";
    const ssize_t n = ::send(fd_, msg.data(), msg.size(), MSG_NOSIGNAL);
    if (n < 0 || static_cast<size_t>(n) != msg.size()) {
        std::cerr << "[ipc] send failed (" << std::strerror(errno) << "); will reconnect on next event\n";
        ::close(fd_);
        fd_ = -1;
    }
}

// manages a buffer of received packets for each block, 
// and reconstructs missing packets using FEC when enough packets are received
bool BlockBufferManager::add_packet(UniflowPacket&& pkt, std::vector<UniflowPacket>& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    BlockKey key{pkt.file_name(), pkt.block_id()};

    if (resolved_.count(key) != 0) {
        return false;
    }

    auto& entry = buffers_[key];
    entry.last_update = std::chrono::steady_clock::now();
    entry.packets.push_back(std::move(pkt));

    if (entry.packets.size() == cfg::N) {
        out = std::move(entry.packets);
        buffers_.erase(key);
        resolved_.insert(key);
        return true;
    }
    return false;
}

// removes any blocks that have not been updated within the given timeout duration,
std::vector<BlockBufferManager::TimedOutBlock>
BlockBufferManager::sweep_stale(std::chrono::steady_clock::duration timeout) {
    std::vector<TimedOutBlock> out;
    const auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lk(mtx_);
    for (auto it = buffers_.begin(); it != buffers_.end();) {
        if (now - it->second.last_update >= timeout) {
            const UniflowPacket& first_pkt = it->second.packets.front();
            out.push_back(TimedOutBlock{
                it->first.file_name,
                it->first.block_id,
                first_pkt.total_blocks(),
                bytes_to_hex(first_pkt.file_hash()),
                first_pkt.original_file_size(),
                it->second.packets.size(),
            });
            resolved_.insert(it->first);
            it = buffers_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

