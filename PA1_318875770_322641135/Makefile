CC= gcc
CFLAGS= -Wall -Wextra -O2 -std=c11 -Wpedantic -march=native
# using gcc version 14.2
all: server client
.PHONY: all
server: server.c
	$(CC) $(CFLAGS) -o server server.c
client: client.c
	$(CC) $(CFLAGS) -o client client.c -lm
clean:
	\rm -f server client
