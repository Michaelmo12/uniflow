#pragma once

#include <cstdint>

// path where the Unix domain socket lives for the file monitor and sender to talk
constexpr const char* IPC_SOCKET_PATH = "/tmp/uniflow_monitor_to_sender.sock";

// destination for the UDP packets sent to Receiver
constexpr const char* RECEIVER_IP = "10.126.146.57";
constexpr int RECEIVER_PORT = 5005;

// Max size of a single IPC message, in bytes
constexpr int MAX_IPC_MESSAGE_SIZE = 1024;

// FEC parameters and payload size — agreed with RX, must match his Receiver exactly
constexpr uint32_t FEC_N = 100;    // data packets per block
constexpr uint32_t FEC_K = 70;     // parity packets per block
constexpr int PAYLOAD_SIZE = 1024; // bytes per packet (file content chunk size)