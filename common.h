// begin Aman
#ifndef COMMON_H
#define COMMON_H

#define PORT 8080
#define UDP_DISCOVERY_PORT 8888
#define SUBMIT 1
#define REQUEST_TASK 2
#define SEND_STATS 3

// Function to reliably receive `size` bytes from `sock` into `buffer`.
// Returns 0 on success, or -1 on network failure/disconnection.
int recv_all(int sock, char *buffer, int size);

// Function to resolve DNS/IP and connect securely to the target
int connect_to_server(const char *hostname, const char *port);

// Set up and run a UDP broadcast loop for the server to announce itself
void *udp_broadcast_thread(void *arg);

// Discover the active server on the LAN. Fills server_ip.
// Returns 1 if found, 0 if timed out.
int discover_server(char *server_ip, int timeout_sec);

#endif // COMMON_H
// end Aman
