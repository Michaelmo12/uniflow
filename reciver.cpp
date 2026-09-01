#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "uniflow.pb.h"
using namespace uniflow;

namespace cfg {
constexpr uint32_t N = 100;                      // data packets per block
constexpr uint32_t K = 70;                      // pairity packets per block
constexpr uint32_t SHARDS_PER_BLOCK = N + K;
constexpr uint32_t PAYLOAD_SIZE = 1024;        //bytes
constexpr uint16_t LISTEN_PORT = 5005;
constexpr const char* STATUS_SOCK_PATH = "/tmp/uniflow_status.sock";
constexpr const char* OUTPUT_DIR = "received_files";
constexpr size_t MAX_DATAGRAM_SIZE = 2048;
} 

uint32_t crc32(const std::string& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string bytes_to_hex(const std::string& bytes) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out += hex[c >> 4];
        out += hex[c & 0x0F];
    }
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string make_status_json(const std::string& type,
                              const std::string& file_name,
                              uint32_t block_id,
                              uint32_t total_blocks,
                              const std::string& file_hash_hex) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"" << type << "\","
        << "\"file_name\":\"" << json_escape(file_name) << "\","
        << "\"block_id\":" << block_id << ","
        << "\"total_blocks\":" << total_blocks << ","
        << "\"file_hash_hex\":\"" << file_hash_hex << "\""
        << "}";
    return oss.str();
}

struct GF256 {
    std::array<uint8_t, 512> exp_tab;
    std::array<uint8_t, 256> log_tab;

    GF256() {
        uint16_t x = 1;
        for (int i = 0; i < 255; ++i) {
            exp_tab[i] = x;
            exp_tab[i + 255] = x;
            log_tab[x] = i;
            x <<= 1;
            if (x & 0x100) x ^= 0x11D; 
        }
        exp_tab[510] = 1;
        exp_tab[511] = 1;
        log_tab[0] = 0;
    }

    uint8_t add(uint8_t a, uint8_t b) const { return a ^ b; }
    
    uint8_t mul(uint8_t a, uint8_t b) const { 
        return (a == 0 || b == 0) ? 0 : exp_tab[log_tab[a] + log_tab[b]]; 
    }
    
    uint8_t div(uint8_t a, uint8_t b) const {
        assert(b != 0 && "GF256 division by zero");
        return (a == 0) ? 0 : exp_tab[log_tab[a] + 255 - log_tab[b]];
    }
};

static const GF256 gf;
#include <vector>

std::vector<std::vector<uint8_t>> generate_encoding_matrix(uint32_t n, uint32_t k) {
    std::vector<std::vector<uint8_t>> matrix(n + k, std::vector<uint8_t>(n, 0));

    for (uint32_t i = 0; i < n; ++i) {
        matrix[i][i] = 1;
    }

    for (uint32_t i = 0; i < k; ++i) {
        uint8_t row_val = n + i;
        for (uint32_t j = 0; j < n; ++j) {
            uint8_t col_val = j;
            
            uint8_t xor_sum = row_val ^ col_val;
            
            matrix[n + i][j] = gf.div(1, xor_sum);
        }
    }

    return matrix;
}
bool invert_matrix(std::vector<std::vector<uint8_t>>& matrix, uint32_t n) {
    std::vector<std::vector<uint8_t>> aug(n, std::vector<uint8_t>(2 * n, 0));
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) aug[i][j] = matrix[i][j];
        aug[i][n + i] = 1;
    }

    // דירוג מטריצה
    for (uint32_t i = 0; i < n; ++i) {
        // חיפוש Pivot שאינו אפס
        if (aug[i][i] == 0) {
            bool swapped = false;
            for (uint32_t k = i + 1; k < n; ++k) {
                if (aug[k][i] != 0) {
                    std::swap(aug[i], aug[k]);
                    swapped = true;
                    break;
                }
            }
            if (!swapped) return false;
        }

        // נרמול השורה כדי שה-Pivot יהיה 1
        uint8_t inv_pivot = gf.div(1, aug[i][i]);
        for (uint32_t j = i; j < 2 * n; ++j) {
            aug[i][j] = gf.mul(aug[i][j], inv_pivot);
        }

        // איפוס שאר השורות בעמודה i
        for (uint32_t k = 0; k < n; ++k) {
            if (k != i) {
                uint8_t factor = aug[k][i];
                if (factor != 0) {
                    for (uint32_t j = i; j < 2 * n; ++j) {
                        aug[k][j] = gf.add(aug[k][j], gf.mul(factor, aug[i][j]));
                    }
                }
            }
        }
    }

    // חילוץ המטריצה ההפוכה מהחצי הימני
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            matrix[i][j] = aug[i][n + j];
        }
    }
    return true;
}

bool reconstruct_block(std::vector<UniflowPacket>& block) {
    if (block.size() != cfg::N) return false;

    bool all_data = true;
    for (const auto& pkt : block) {
        if (pkt.type() != UniflowPacket::DATA) {
            all_data = false;
            break;
        }
    }

    if (all_data) {
        std::sort(block.begin(), block.end(),
            [](const UniflowPacket& a, const UniflowPacket& b) {
                return a.packet_index() < b.packet_index();
            });
        return true;
    }

    auto full_matrix = generate_encoding_matrix(cfg::N, cfg::K);

    std::vector<std::vector<uint8_t>> submatrix(cfg::N, std::vector<uint8_t>(cfg::N, 0));
    for (uint32_t i = 0; i < cfg::N; ++i) {
        uint32_t pkt_idx = block[i].packet_index();
        for (uint32_t j = 0; j < cfg::N; ++j) {
            submatrix[i][j] = full_matrix[pkt_idx][j];
        }
    }

    if (!invert_matrix(submatrix, cfg::N)) {
        std::cerr << "[reconstruct] Matrix inversion failed! Cauchy constraint broken.\n";
        return false;
    }

    std::vector<std::vector<uint8_t>> recovered(cfg::N, std::vector<uint8_t>(cfg::PAYLOAD_SIZE, 0));
    
    for (uint32_t i = 0; i < cfg::N; ++i) {           // i = החבילה המקורית שאנחנו משחזרים עכשיו
        for (uint32_t j = 0; j < cfg::N; ++j) {       // j = רץ על החבילות שהגיעו
            uint8_t factor = submatrix[i][j];
            if (factor == 0) continue;
            
            const std::string& payload = block[j].payload();
            size_t len = std::min((size_t)cfg::PAYLOAD_SIZE, payload.size());
            
            for (size_t b = 0; b < len; ++b) {
                recovered[i][b] = gf.add(recovered[i][b], 
                                         gf.mul(factor, static_cast<uint8_t>(payload[b])));
            }
        }
    }

    std::vector<UniflowPacket> new_block;
    new_block.reserve(cfg::N);
    
    uint32_t block_id = block[0].block_id();
    const std::string file_name = block[0].file_name();
    uint32_t total_blocks = block[0].total_blocks();
    const std::string file_hash = block[0].file_hash();

    for (uint32_t i = 0; i < cfg::N; ++i) {
        UniflowPacket pkt;
        pkt.set_file_name(file_name);
        pkt.set_block_id(block_id);
        pkt.set_packet_index(i);
        pkt.set_type(UniflowPacket::DATA);
        pkt.set_payload(recovered[i].data(), cfg::PAYLOAD_SIZE);
        pkt.set_payload_size(cfg::PAYLOAD_SIZE);
        pkt.set_total_blocks(total_blocks);
        pkt.set_file_hash(file_hash);
        
        new_block.push_back(std::move(pkt));
    }

    block = std::move(new_block);
    return true;
}

class ThreadPool {
public:
    explicit ThreadPool(size_t worker_count) {
        worker_count = std::max<size_t>(1, worker_count);
        workers_.reserve(worker_count);
        for (size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_ = false;
};

class BlockBufferManager {
public:
    bool add_packet(UniflowPacket&& pkt, std::vector<UniflowPacket>& out) {
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

private:
    std::mutex mtx_;
    std::unordered_map<uint32_t, std::vector<UniflowPacket>> buffers_;
    std::unordered_set<uint32_t> completed_;
};

class FileManager {
public:
    ~FileManager() {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& [name, fd] : fds_) {
            (void)name;
            if (fd >= 0) ::close(fd);
        }
    }

    FileManager(const FileManager&) = delete;
    FileManager& operator=(const FileManager&) = delete;
    FileManager() = default;

    int get_fd(const std::string& file_name) {
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

private:
    static std::string sanitize(const std::string& name) {
        std::string out;
        out.reserve(name.size());
        for (char c : name) {
            if (c == '/' || c == '\\') continue;
            out += c;
        }
        if (out.empty() || out == "." || out == "..") out = "unnamed_file";
        return out;
    }

    std::mutex mtx_;
    std::unordered_map<std::string, int> fds_;
};

class IpcClient {
public:
    explicit IpcClient(std::string path) : path_(std::move(path)) {
        std::lock_guard<std::mutex> lk(mtx_);
        connect_locked();
    }

    ~IpcClient() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (fd_ >= 0) ::close(fd_);
    }

    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;

    void send_json(const std::string& json) {
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

private:
    bool connect_locked() {
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

    std::string path_;
    int fd_ = -1;
    std::mutex mtx_;
};

void process_completed_block(std::vector<UniflowPacket> block, FileManager& file_mgr, IpcClient& ipc) {
    if (block.empty()) return;

    const uint32_t block_id = block.front().block_id();
    const std::string file_name = block.front().file_name();
    const uint32_t total_blocks = block.front().total_blocks();
    const std::string file_hash_hex = bytes_to_hex(block.front().file_hash());

    if (!reconstruct_block(block)) {
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

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    if (::mkdir(cfg::OUTPUT_DIR, 0755) < 0 && errno != EEXIST) {
        std::cerr << "warning: could not create output directory '" << cfg::OUTPUT_DIR
                  << "': " << std::strerror(errno) << "\n";
    }

    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "fatal: socket() failed: " << std::strerror(errno) << "\n";
        return 1;
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
        return 1;
    }

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