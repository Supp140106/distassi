#ifndef COMMON_H
#define COMMON_H

#define PORT 8080
#define SUBMIT 1
#define REQUEST_TASK 2

int recv_all(int sock, char *buffer, int size);

int connect_to_server(const char *hostname, const char *port);

#endif 


