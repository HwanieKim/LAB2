#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE // soluzione per errore implicit declaration of signal.h

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>

#define BUFFER_SIZE 512

#define MSG_OK 'K'
#define MSG_ERR 'E'
#define MSG_REGISTRA_UTENTE 'R'
#define MSG_LOGIN_UTENTE 'L'
#define MSG_CANCELLA_UTENTE 'D'
#define MSG_MATRICE 'M'
#define MSG_PAROLA 'W'
#define MSG_PUNTI_PAROLA 'P'
#define MSG_PUNTI_FINALI 'F'
#define MSG_SERVER_SHUTDOWN 'B'
#define MSG_TEMPO_PARTITA 'T'
#define MSG_TEMPO_ATTESA 'A'
#define MSG_POST_BACHECA 'H'
#define MSG_SHOW_BACHECA 'S'

// ======================= Funzioni di comunicazione =======================
/*
    robust_write:
        scrive 'count' byte sul descrittore fd, ripetendo in caso di interruzioni
*/
ssize_t robust_write(int fd, const void *buf, size_t count);

/*
    robust_read:
        legge 'count' byte dal descrittore fd, ripetendo caso di interrupt
*/
ssize_t robust_read(int fd, void *buf, size_t count);

/*
    send_mesage:
    invia un messaggio al client secondo il protocollo:
        [1 byte type] [4 bytre lunghezza (network order)] [data]
    restituisce 0 in caso di successo, -1 per erroree
*/
int send_message(int sockfd, char type, const char *data, unsigned int length);

/*
    receive_message:
    riceve un messaggio dal client:
        [1 byte type] [4 byte lunghezza (network order)] [data]
    popola i parametri in output cone le informazioni ricevute.
    ritorna 0 in caso di successo e -1 in caso di fine connessione/errore
*/
int receive_message(int sockfd, char *type, char *data, unsigned int *length);

#endif // COMMON_H
