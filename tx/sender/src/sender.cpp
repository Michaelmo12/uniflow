#include "sender/sender.hpp"
#include "sender/config.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>

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