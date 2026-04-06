//begin Aman
#ifndef COMMON_H
#define COMMON_H

#define PORT 8080
#define SUBMIT 1
#define REQUEST_TASK 2

// Function to reliably receive `size` bytes from `sock` into `buffer`.
// Returns 0 on success, or -1 on network failure/disconnection.
int recv_all(int sock, char *buffer, int size);

// Function to resolve DNS/IP and connect securely to the target
int connect_to_server(const char *hostname, const char *port);

#endif // COMMON_H
//end Aman
