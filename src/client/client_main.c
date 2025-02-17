/*
client_main.c
    main del client del gioco 'paroliere'

    sintassi:
    > ./paroliere_cl nome_server porta_server
        dove:
            • paroliere_cl `e il nome dell’eseguibile;
            • nome_server `e il nome del server al quale collegarsi;
            • porta_server `e il numero della porta alla quale collegarsi;
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

#define BUFFER_SIZE 512

// dichiarazioni extern
extern void client_run(int sockfd);

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s <nome_server> <porta_server>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_name = argv[1];
    int port = atoi(argv[2]);

    // Aggiungi validazione della porta:
    if (port < 1024 || port > 65535)
    {
        fprintf(stderr, "Porta non valida. Usa un valore tra 1024 e 65535.\n");
        exit(EXIT_FAILURE);
    }
    // risoluzione dell'hostname
    struct hostent *he = gethostbyname(server_name);
    if (!he)
    {
        perror("gethostbyname");
        exit(EXIT_FAILURE);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[CLIENT MAIN] Connesso a %s sulla porta %d\n", server_name, port);

    // avvio client logic
    client_run(sockfd);

    close(sockfd);
    return 0;
}