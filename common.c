//begin ashok
#include "common.h"
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>

int recv_all(int sock, char *buffer, int size) {
    int received = 0;
    while (received < size) {
        int r = recv(sock, buffer + received, size - received, 0);
        if (r <= 0) return -1;
        received += r;
    }
    return 0;
}

int connect_to_server(const char *hostname, const char *port) {
    struct addrinfo hints, *res;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, port, &hints, &res) != 0) {
        return -1;
    }

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        close(sockfd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return sockfd;
}
//end ashok
