// begin ashok
#include "common.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <stdio.h>

#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int recv_all(int sock, char *buffer, int size) {
  int received = 0;
  while (received < size) {
    int r = recv(sock, buffer + received, size - received, 0);
    if (r <= 0)
      return -1;
    received += r;
  }
  return 0;
}

int send_all(int sock, const char *buffer, int size) {
  int sent = 0;
  while (sent < size) {
    int s = send(sock, buffer + sent, size - sent, 0);
    if (s <= 0)
      return -1;
    sent += s;
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

void *udp_broadcast_thread(void *arg) {
  (void)arg; // Unused
  int sockfd;
  int broadcast_enable = 1;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("UDP Socket creation failed");
    return NULL;
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast_enable,
                 sizeof(broadcast_enable)) < 0) {
    perror("Error in setting Broadcast option");
    close(sockfd);
    return NULL;
  }

  char *message = "SERVER_ONLINE";

  while (1) {
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) == 0) {
      for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
          continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
          if ((ifa->ifa_flags & IFF_UP) && (ifa->ifa_flags & IFF_BROADCAST) &&
              !(ifa->ifa_flags & IFF_LOOPBACK)) {
            struct sockaddr_in *broadaddr =
                (struct sockaddr_in *)ifa->ifa_broadaddr;
            if (broadaddr) {
              struct sockaddr_in dest_addr;
              memset(&dest_addr, 0, sizeof(dest_addr));
              dest_addr.sin_family = AF_INET;
              dest_addr.sin_port = htons(UDP_DISCOVERY_PORT);
              dest_addr.sin_addr = broadaddr->sin_addr;
              sendto(sockfd, message, strlen(message), 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            }
          }
        }
      }
      freeifaddrs(ifap);
    }

    // Also send to 255.255.255.255 as a fallback
    struct sockaddr_in b_addr;
    memset(&b_addr, 0, sizeof(b_addr));
    b_addr.sin_family = AF_INET;
    b_addr.sin_port = htons(UDP_DISCOVERY_PORT);
    b_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
    sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)&b_addr,
           sizeof(b_addr));

    sleep(1);
  }
  close(sockfd);
  return NULL;
}

int discover_server(char *server_ip, int timeout_sec) {
  int sockfd;
  struct sockaddr_in local_addr, server_addr;
  socklen_t addr_len = sizeof(server_addr);
  char buffer[1024];

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    return 0;
  }

  int opt = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

  memset(&local_addr, 0, sizeof(local_addr));
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(UDP_DISCOVERY_PORT);
  local_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
    close(sockfd);
    return 0;
  }

  struct timeval tv;
  tv.tv_sec = timeout_sec;
  tv.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    close(sockfd);
    return 0;
  }

  while (1) {
    int n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0,
                     (struct sockaddr *)&server_addr, &addr_len);
    if (n < 0) {
      close(sockfd);
      return 0; // Timeout
    }
    buffer[n] = '\0';
    if (strcmp(buffer, "SERVER_ONLINE") == 0) {
      inet_ntop(AF_INET, &server_addr.sin_addr, server_ip, INET_ADDRSTRLEN);
      if (strcmp(server_ip, "127.0.0.1") == 0)
        continue; // Ignore localhost loopback if we want public IPs, or we can
                  // just use it. Let's keep it.
      close(sockfd);
      return 1;
    }
  }
}
// end ashok