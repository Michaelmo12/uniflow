#include "io_managers.hpp"
#include "config.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using uniflow::UniflowPacket;

FileManager::~FileManager() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& [name, fd] : fds_) {
        (void)name;
        if (fd >= 0) ::close(fd);
    }
}

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

int FileManager::get_fd(const std::string& file_name) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = fds_.find(file_name);
    if (it != fds_.end() && it->second >= 0) {
        return it->second;
    }

    const std::string path = std::string(cfg::OUTPUT_DIR) + "/" + sanitize(file_name);
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    fds_[file_name] = fd;
    return fd;
}

IpcClient::IpcClient(std::string path) : path_(std::move(path)) {
    std::lock_guard<std::mutex> lk(mtx_);
    connect_locked();
}

IpcClient::~IpcClient() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (fd_ >= 0) ::close(fd_);
}

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

bool BlockBufferManager::add_packet(UniflowPacket&& pkt, std::vector<UniflowPacket>& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint32_t block_id = pkt.block_id();

    if (completed_.count(block_id) != 0) {
        return false;
    }

    auto& vec = buffers_[block_id];
    vec.push_back(std::move(pkt));

    if (vec.size() == cfg::N) {
        out = std::move(vec);
        buffers_.erase(block_id);
        completed_.insert(block_id);
        return true;
    }
    return false;
}
