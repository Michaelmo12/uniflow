#pragma once

#include <cstddef>
#include <cstdint>

namespace cfg {

constexpr uint32_t N = 100;
constexpr uint32_t K = 70;
constexpr uint32_t SHARDS_PER_BLOCK = N + K;
constexpr uint32_t PAYLOAD_SIZE = 1024;

constexpr uint16_t LISTEN_PORT = 5005;
constexpr const char* STATUS_SOCK_PATH = "/tmp/uniflow_status.sock";
constexpr const char* OUTPUT_DIR = "received_files";

constexpr size_t MAX_DATAGRAM_SIZE = 2048;

}
