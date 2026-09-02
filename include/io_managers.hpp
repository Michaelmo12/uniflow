#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "uniflow.pb.h"

class FileManager {
public:
    FileManager() = default;
    ~FileManager();

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;

    int get_fd(const std::string& file_name);

private:
    static std::string sanitize(const std::string& name);

    std::mutex mtx_;
    std::unordered_map<std::string, int> fds_;
};

class IpcClient {
public:
    explicit IpcClient(std::string path);
    ~IpcClient();

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    void send_json(const std::string& json);

private:
    bool connect_locked();

    std::string path_;
    int fd_ = -1;
    std::mutex mtx_;
};

class BlockBufferManager {
public:
    bool add_packet(uniflow::UniflowPacket&& pkt, std::vector<uniflow::UniflowPacket>& out);

private:
    std::mutex mtx_;
    std::unordered_map<uint32_t, std::vector<uniflow::UniflowPacket>> buffers_;
    std::unordered_set<uint32_t> completed_;
};
