#pragma once

#include <chrono>
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

    int get_fd(const std::string& file_name, const std::string& file_hash,
               uint64_t file_size);

private:
    static std::string sanitize(const std::string& name);

    std::mutex mtx_;
    std::unordered_map<std::string, int> fds_;
    struct FileIdentity {
        std::string file_hash;
        uint64_t file_size;
    };
    std::unordered_map<std::string, FileIdentity> identities_;
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

struct BlockKey {
    std::string file_name;
    uint32_t block_id;

    bool operator==(const BlockKey& other) const {
        return block_id == other.block_id && file_name == other.file_name;
    }
};

struct BlockKeyHash {
    size_t operator()(const BlockKey& k) const noexcept {
        return std::hash<std::string>()(k.file_name) ^ (std::hash<uint32_t>()(k.block_id) << 1);
    }
};

class BlockBufferManager {
public:
    bool add_packet(uniflow::UniflowPacket&& pkt, std::vector<uniflow::UniflowPacket>& out);

    struct TimedOutBlock {
        std::string file_name;
        uint32_t block_id;
        uint32_t total_blocks;
        std::string file_hash_hex;
        uint64_t file_size;
        size_t packets_received;
    };
    std::vector<TimedOutBlock> sweep_stale(std::chrono::steady_clock::duration timeout);

private:
    struct PartialBlock {
        std::vector<uniflow::UniflowPacket> packets;
        std::chrono::steady_clock::time_point last_update;
    };

    std::mutex mtx_;
    std::unordered_map<BlockKey, PartialBlock, BlockKeyHash> buffers_;
    std::unordered_set<BlockKey, BlockKeyHash> resolved_;
};

