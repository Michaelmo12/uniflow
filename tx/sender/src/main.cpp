#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "uniflow.pb.h"
#include "sender/sender.hpp"
#include "sender/config.hpp"

int main() {
    int ipc_fd = setup_ipc_socket(IPC_SOCKET_PATH);
    if (ipc_fd < 0) {
        std::cerr << "Failed to set up IPC socket\n";
        return 1;
    }

    std::cout << "Sender ready. Waiting for File Monitor at " << IPC_SOCKET_PATH << "...\n";

    // ---- wait for File Monitor to connect and send a message ----
    std::string message = wait_for_file_monitor(ipc_fd);
    if (message.empty()) {
        std::cerr << "Failed to get message from File Monitor\n";
        return 1;
    }

    std::cout << "Received from File Monitor: " << message << "\n";

    close(ipc_fd);
    unlink(IPC_SOCKET_PATH);

    // ---- wrap it in a UniflowPacket ----
    uniflow::UniflowPacket packet = build_packet(message);

    std::string wire_bytes;
    if (!packet.SerializeToString(&wire_bytes)) {
        std::cerr << "Failed to serialize packet\n";
        return 1;
    }

    // ---- send it as one UDP packet to Receiver ----
    if (!send_packet(packet, RECEIVER_IP, RECEIVER_PORT)) {
        std::cerr << "Failed to send packet\n";
        return 1;
    }

    // ---- send it as one UDP packet to Receiver ----
    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        std::cerr << "Failed to create UDP socket\n";
        return 1;
    }

    sockaddr_in receiver_addr{};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(RECEIVER_PORT);
    inet_pton(AF_INET, RECEIVER_IP, &receiver_addr.sin_addr);

    ssize_t sent = sendto(udp_fd, wire_bytes.data(), wire_bytes.size(), 0,
                           (sockaddr*)&receiver_addr, sizeof(receiver_addr));

    if (sent < 0) {
        std::cerr << "Failed to send UDP packet\n";
        return 1;
    }

    std::cout << "Sent " << sent << " bytes to Receiver at "
              << RECEIVER_IP << ":" << RECEIVER_PORT << "\n";

    close(udp_fd);
    return 0;
}