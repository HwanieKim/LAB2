CC = gcc
CFLAGS = -Wall -pedantic -Wextra -std=c99 -g  -I./src
LDFLAGS = -lpthread 

SRV_SRCS = src/server/server_main.c src/server/server_paroliere.c src/server/dictionary.c src/server/matrix.c src/common/common.c
CLI_SRCS = src/client/client_main.c src/client/client_paroliere.c src/common/common.c

all: paroliere_srv paroliere_cl

.PHONY: clean

paroliere_srv: $(SRV_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

paroliere_cl: $(CLI_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f paroliere_srv paroliere_cl
