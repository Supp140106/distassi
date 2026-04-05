CC = gcc
CFLAGS = -Wall -Wextra -g -pthread

all: server worker client

server: server.c common.c common.h
	$(CC) $(CFLAGS) server.c common.c -o server

worker: worker.c common.c common.h
	$(CC) $(CFLAGS) worker.c common.c -o worker

client: client.c common.c common.h
	$(CC) $(CFLAGS) client.c common.c -o client

clean:
	rm -f server worker client task.out received_task_*.out
