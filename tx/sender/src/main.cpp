#include <iostream>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>

#include "sender/sender.hpp"
#include "sender/config.hpp"
#include "sender/reed_solomon.hpp"
#include "sender/pipeline.hpp"

int main() {
    if (!init_reed_solomon()) {
        std::cerr << "Failed to initialize Reed-Solomon engine\n";
        return 1;
    }

    int ipc_fd = setup_ipc_socket(IPC_SOCKET_PATH);
    if (ipc_fd < 0) {
        std::cerr << "Failed to set up IPC socket\n";
        return 1;
    }

    int udp_fd = setup_udp_socket();
    if (udp_fd < 0) {
        std::cerr << "Failed to set up UDP socket\n";
        return 1;
    }

    sockaddr_in receiver_addr{};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(RECEIVER_PORT);
    if (inet_pton(AF_INET, RECEIVER_IP, &receiver_addr.sin_addr) != 1) {
        std::cerr << "Failed to parse receiver address: " << RECEIVER_IP << "\n";
        close(udp_fd);
        return 1;
    }

    std::cout << "Sender ready. Waiting for File Monitor at " << IPC_SOCKET_PATH << "...\n";

    while (true) {
        std::string file_path = wait_for_file_monitor(ipc_fd);

        if (file_path.empty()) {
            std::cerr << "Failed to get file path from File Monitor\n";
            continue;
        }

        std::cout << "Processing file: " << file_path << "\n";
        process_file(udp_fd, receiver_addr, file_path);

        std::cout << "Waiting for next file...\n";
    }
}