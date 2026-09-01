#pragma once

// path where the Unix domain socket lives for the file monitor and sender to talk
constexpr const char* IPC_SOCKET_PATH = "/tmp/uniflow_monitor_to_sender.sock";

// destination for the UDP packets sent to Receiver
constexpr const char* RECEIVER_IP = "127.0.0.1";
constexpr int RECEIVER_PORT = 5004;

// Max size of a single IPC message, in bytes
constexpr int MAX_IPC_MESSAGE_SIZE = 1024;