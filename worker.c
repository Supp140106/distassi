#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <string.h>

#include "common.h"

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    char port_str[16];
    sprintf(port_str, "%d", PORT);
    const char *port = port_str;

    if (argc == 2) {
        host = argv[1];
    } else if (argc >= 3) {
        host = argv[1];
        port = argv[2];
    } else if (argc == 1) {
        printf("Usage: %s <server_ip> [port]\n", argv[0]);
        printf("Defaulting to %s:%s\n\n", host, port);
    }

    int sock = -1;
    while (1) {
        if (sock < 0) {
            sock = connect_to_server(host, port);
            if (sock < 0) {
                sleep(1);
                continue;
            }
            send(sock, &(int){REQUEST_TASK}, sizeof(int), 0);
            printf("Connected to server and registered as worker.\n");
        }

        int size;
        if (recv(sock, &size, sizeof(int), 0) <= 0) {
            printf("Server disconnected. Reconnecting...\n");
            close(sock);
            sock = -1;
            sleep(1);
            continue;
        }

        char *buffer = malloc(size);
        if (recv_all(sock, buffer, size) == -1) {
            printf("Network error during task receive. Reconnecting...\n");
            free(buffer);
            close(sock);
            sock = -1;
            continue;
        }
char filename[256];
        sprintf(filename, "received_task_%d.out", getpid());
        FILE *f = fopen(filename, "wb");
        fwrite(buffer, 1, size, f);
        fclose(f);

        chmod(filename, 0755);

        printf("Executing task...\n");
        char cmd[512];
        sprintf(cmd, "./%s", filename);
        FILE *pipe = popen(cmd, "r");
        if (!pipe) {
            perror("popen failed");
            close(sock);
            free(buffer);
            continue;
        }

        char output_buffer[4096] = {0};
        int total_read = 0;
        while (fgets(output_buffer + total_read, sizeof(output_buffer) - total_read, pipe) != NULL) {
            total_read = strlen(output_buffer);
        }
        pclose(pipe);

        int worker_id = getpid();
        if (send(sock, &worker_id, sizeof(int), 0) <= 0 ||
            send(sock, &total_read, sizeof(int), 0) <= 0 ||
            send(sock, output_buffer, total_read, 0) <= 0) {
            printf("Network error during result send. Reconnecting...\n");
            close(sock);
            sock = -1;
            free(buffer);
            remove(filename);
            continue;
        }

        printf("Task completed. Output sent back to server.\n");

        free(buffer);
        remove(filename);
        // Loop back to wait for the next task on the same socket
    }

    return 0;
}
