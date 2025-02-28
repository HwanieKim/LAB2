#include "common.h"

// ======================= Funzioni di comunicazione =======================
/*
    robust_write:
        scrive 'count' byte sul descrittore fd, ripetendo in caso di interruzioni
*/
ssize_t robust_write(int fd, const void *buf, size_t count)
{
    size_t total_written = 0;
    const char *buffer = (const char *)buf;
    while (total_written < count)
    {
        ssize_t written = write(fd, buffer + total_written, count - total_written);
        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        total_written += written;
    }
    return total_written;
}

/*
    robust_read:
        legge 'count' byte dal descrittore fd, ripetendo caso di interrupt
*/
ssize_t robust_read(int fd, void *buf, size_t count)
{
    size_t total_read = 0;
    char *buffer = (char *)buf;
    while (total_read < count)
    {
        ssize_t r = read(fd, buffer + total_read, count - total_read);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break; // fine flusso
        total_read += r;
    }
    return total_read;
}

/*
    send_mesage:
    invia un messaggio al client secondo il protocollo:
        [1 byte type] [4 bytre lunghezza (network order)] [data]
    restituisce 0 in caso di successo, -1 per erroree
*/
int send_message(int sockfd, char type, const char *data, unsigned int length)
{
    // conversione in network length
    unsigned int netlen = htonl(length);
    if (robust_write(sockfd, &type, 1) != 1)
    {
        perror("robust_write(type)");
        return -1;
    }

    if (robust_write(sockfd, &netlen, 4) != 4)
    {
        perror("robust_write(netlen)");
        return -1;
    }
    if (length > 0)
    {
        if (robust_write(sockfd, data, length) != (ssize_t)length)
        {
            perror("robust_write(data)");
            return -1;
        }
    }
    return 0;
}

/*
    receive_message:
    riceve un messaggio dal client:
        [1 byte type] [4 byte lunghezza (network order)] [data]
    popola i parametri in output cone le informazioni ricevute.
    ritorna 0 in caso di successo e -1 in caso di fine connessione/errore
*/
int receive_message(int sockfd, char *type, char *data, unsigned int *length)
{
    ssize_t n = robust_read(sockfd, type, 1);
    if (n <= 0)
    {
        if (n < 0 && (errno != EAGAIN && errno != EINTR))
        {
            perror("robust_read(type)");
        }
        return -1;
    }

    unsigned int netlen;
    n = robust_read(sockfd, &netlen, 4);
    if (n <= 0)
    {
        if (n < 0 && (errno != EAGAIN && errno != EINTR))
        {
            perror("robust_read(netlen)");
        }
        return -1;
    }

    // conversione da network length
    *length = ntohl(netlen);
    if (*length > BUFFER_SIZE - 1)
        *length = BUFFER_SIZE - 1; // troncamento per prevenire overflow

    if (*length > 0)
    {
        n = robust_read(sockfd, data, *length);
        if (n <= 0)
        {
            if (n < 0 && (errno != EAGAIN && errno != EINTR))
            {
                perror("robust_read(data)");
            }
            return -1;
            ;
        }
        data[n] = '\0'; // Aggiungi il carattere di terminazione
    }

    return 0;
}