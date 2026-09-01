#include "sender/sender.hpp"
#include "sender/config.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>


int setup_ipc_socket(const char* socket_path) {
    
    unlink(socket_path); // remove any leftover socket file from a previous run

    // Unix-domain, stream-oriented.
    int ipc_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc_fd < 0) 
    {
        return -1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    if(bind(ipc_fd, (struct sockaddr*)&addr, sizeof(addr))<0)
    {
        close(ipc_fd);
        return -1;
    }

    // one client at a time
    if (listen(ipc_fd, 1) < 0) {
        close(ipc_fd);
        return -1;
    }

    return ipc_fd;
}

std::string wait_for_file_monitor(int ipc_fd){
    //optionally hand you back who connected — their address but we dont need that 
    int client_fd = accept(ipc_fd, nullptr, nullptr);
    if (client_fd < 0) {
        return "";
    }

    char buffer[MAX_IPC_MESSAGE_SIZE] = {0};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_fd);
        return "";
    }

    std::string message(buffer, bytes_read);
    close(client_fd);
    return message;
}

uniflow::UniflowPacket build_packet(const std::string& message) {
    // Placeholder values (block_id, file_hash, crc32) until real file
    // reading and FEC splitting are built. This proves the pipe works.
    uniflow::UniflowPacket packet;
    packet.set_file_name(message);
    packet.set_block_id(0);
    packet.set_packet_index(0);
    packet.set_type(uniflow::UniflowPacket::DATA);
    packet.set_payload(message);
    packet.set_payload_size(static_cast<uint32_t>(message.size()));
    packet.set_total_blocks(1);
    packet.set_file_hash("");
    packet.set_crc32(0);
    return packet;
}

bool send_packet(const uniflow::UniflowPacket& packet, const char* ip, int port) {
    std::string wire_bytes;
    if (!packet.SerializeToString(&wire_bytes)) {
        return false;
    }

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) {
        return false;
    }

    sockaddr_in receiver_addr{};
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &receiver_addr.sin_addr);

    ssize_t sent = sendto(udp_fd, wire_bytes.data(), wire_bytes.size(), 0,
                           (sockaddr*)&receiver_addr, sizeof(receiver_addr));

    close(udp_fd);
    return sent >= 0;
}