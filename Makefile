CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g  -I./src
LDFLAGS = -lpthread 

SRV_SRCS = src/server/server_main.c src/server/server_paroliere.c src/server/dictionary.c src/server/matrix.c
CLI_SRCS = src/client/client_main.c src/client/client_paroliere.c

all: paroliere_srv paroliere_cl

paroliere_srv: $(SRV_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

paroliere_cl: $(CLI_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f paroliere_srv paroliere_cl

val:
	valgrind --leak-check=full --show-leak-kinds=all ./paroliere_srv localhost 9000 --diz resources/dictionary.txt --matrici resources/matrix.txt
