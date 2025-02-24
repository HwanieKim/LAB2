/*
client_paroliere.c
    gestisce tutta la logcia del client:
        + invio e ricezione di messaggi
        + implementa le funzionei di protocollo e crea un thread per ricevere
          continuamente i messaggi dal server
*/

// inlcude
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <ctype.h>

// definizioni di costanti
#define BUFFER_SIZE 512

// costanti del protocollo
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

// variabile volatile globale utilizzato per segnalare terminazione
//  non adotta cancellazione/cancellation point come client, ma flag e shutdown su socket
volatile sig_atomic_t shutdown_flag = 0;

// mutex per sincronizzazione
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
     normalize_word:
         Converte i caratteri in minuscolo ed effettua la gestione speciale del digramma 'qu':
            se incontra 'q' seguito da 'u', li aggiunge entrambi come "qu";
            se trova una 'q' non seguita da 'u', la ignora (da definizione nel codice).
        Tale strategia rende coerente l’invio della parola al server.
 */
void normalize_word(char *word)
{
    char temp[BUFFER_SIZE] = {0};
    int j = 0;
    for (int i = 0; word[i] && j < BUFFER_SIZE - 1; i++)
    {
        char c = tolower((unsigned char)word[i]);
        if (c == 'q')
        {
            if (word[i + 1] == 'u' || word[i + 1] == 'U')
            {
                temp[j++] = 'q';
                temp[j++] = 'u';
                i++;
            }
            else
            {
                // se la 'q' non e' seguita da 'u', ignora
                continue;
            }
        }
        else
        {
            temp[j++] = c;
        }
    }
    temp[j] = '\0';
    strcpy(word, temp);
}

/*
    valida_nome_utente
        verifica il formato della stringa che:
            non superi 10 caratteri
            sia composta da caratteri alfanumerici
            non vuota
*/
bool valida_nome_utente(const char *nome)
{
    int len = strlen(nome);
    if (len == 0 || len > 10)
        return false;
    for (int i = 0; i < len; i++)
    {
        if (!isalnum((unsigned char)nome[i]))
            return false;
    }
    return true;
}

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

static int send_message(int sockfd, char type, const char *data, unsigned int length)
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
static int receive_message(int sockfd, char *type, char *data, unsigned int *length)
{
    ssize_t n = robust_read(sockfd, type, 1);
    if (n <= 0)
    {
        perror("robust_read(type)");
        return -1;
    }

    unsigned int netlen;
    n = robust_read(sockfd, &netlen, 4);
    if (n <= 0)
    {
        perror("robust_read(netlen)");
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
            perror("robust_read(data)");
            return -1;
        }
        data[n] = '\0'; // Aggiungi il carattere di terminazione
    }

    return 0;
}

/*
    client_thread:
        thread dedicato alla ricezione continua dei messaggi del server
         - legge i messaggi usando receive_message
         - li interpreta e li stampa
         - in caso di erroe o di messaggio di shutdown, shutdown_flag = 1
    */
// thread di ricezione, riceve continuamente messaggi dal server e li stampa
void *client_receiver(void *arg)
{
    int sockfd = *(int *)arg;
    char type;
    char data[BUFFER_SIZE];
    unsigned int length;

    while (!shutdown_flag)
    {
        // Se la ricezione fallisce, cerchiamo di capire se è timeout o errore di connessione
        if (receive_message(sockfd, &type, data, &length) < 0)
        {
            // errore EAGAIN / EWOULDBLOCK, read su socket scaduta per timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                fprintf(stderr, "\n[SERVER] Timeout di ricezione\n");
            }
            else
            {
                fprintf(stderr, "\n[SERVER] Errore nella ricezione o disonnessione\n");
            }
            shutdown_flag = 1;
            break;
        }

        // chiusura connessione
        if (type == MSG_SERVER_SHUTDOWN)
        {
            printf("\r\33[2K"); // Cancella la linea corrente
            printf("\n[SERVER] Shutdown: %s\n", data);
            shutdown_flag = 1;
            break;
        }
        pthread_mutex_lock(&output_mutex);

        // \r\33[2K: spostare il cursore all'inizio e cancellare la riga corrente in console
        printf("\r\33[2K");
        switch (type)
        {
        case MSG_OK:
            printf("\n[SERVER] OK: %s\n", data);
            break;
        case MSG_ERR:
            printf("\n[SERVER] ERRORE: %s\n", data);
            break;
        case MSG_MATRICE:
        {
            printf("\n[SERVER] MATRICE: %s\n", data);

            // Copia la stringa per contare i token senza modificarla
            char copy[BUFFER_SIZE];
            strncpy(copy, data, BUFFER_SIZE);
            copy[BUFFER_SIZE - 1] = '\0';

            // Conta quanti token ci sono
            int token_count = 0;
            char *tok = strtok(copy, " ");
            while (tok != NULL)
            {
                token_count++;
                tok = strtok(NULL, " ");
            }
            // Se sono esattamente 16, assumiamo che si tratti della matrice e stampiamo la griglia
            if (token_count == 16)
            {
                char matrix_copy[BUFFER_SIZE];
                strncpy(matrix_copy, data, BUFFER_SIZE);
                matrix_copy[BUFFER_SIZE - 1] = '\0';
                char *token = strtok(matrix_copy, " ");
                for (int row = 0; row < 4; row++)
                {
                    for (int col = 0; col < 4; col++)
                    {
                        if (token != NULL)
                        {
                            printf("%s ", token);
                            token = strtok(NULL, " ");
                        }
                    }
                    printf("\n");
                }
            }
            break;
        }
        case MSG_TEMPO_PARTITA:
            printf("\n[SERVER] TEMPO PARTITA: %s secondi \n", data);
            break;
        case MSG_TEMPO_ATTESA:
            printf("\n[SERVER] TEMPO ATTESA: %s\n", data);
            break;
        case MSG_PUNTI_FINALI:
            printf("\n[SERVER] PUNTI FINALI:\n%s\n", data);
            break;
        case MSG_PUNTI_PAROLA:
            printf("\n[SERVER] PUNTI PAROLA: %s\n", data);
            break;
        case MSG_SHOW_BACHECA:
            printf("\n[SERVER] BACHECA:\n%s\n", data);
            break;
        default:
            printf("\n[SERVER] Tipo messaggio sconosciuto (%c): %s\n", type, data);
            break;
        }
        printf("[PROMPT PAROLIERE]--> ");
        fflush(stdout);
        pthread_mutex_unlock(&output_mutex);
    }
    return NULL;
}

/*
    print_help:
        mostra l'elenco dei comandi disponibili sul client
*/
void print_help()
{
    pthread_mutex_lock(&output_mutex);
    printf("Comandi disponibili:\n");
    printf("  aiuto                         - Mostra l'elenco dei comandi\n");
    printf("  registra_utente <nome>        - Registra un nuovo utente\n");
    printf("  login_utente <nome>           - Effettua il login\n");
    printf("  cancella_registrazione        - Cancella la registrazione\n");
    printf("  matrice                       - Richiede la matrice corrente\n");
    printf("  p <parola>                    - Invia una parola per verifica e punteggio\n");
    printf("  msg <testo_messaggio>         - Posta un messaggio sulla bacheca (max 128 caratteri)\n");
    printf("  show-msg                      - Visualizza il contenuto della bacheca\n");
    printf("  fine                          - Termina la sessione\n");
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);
}

/*
funzione principale della logica di client
    client_run:
        Crea il thread di ricezione messaggi dal server (client_receiver).
        Gestisce la lettura dei comandi da stdin e la loro formattazione verso il server.
        In caso di comando "fine", chiude la connessione e aspetta la terminazione del thread.
 */
void client_run(int sockfd)
{
    pthread_t recv_thread;
    // creazione thread receiver
    if (pthread_create(&recv_thread, NULL, client_receiver, &sockfd) != 0)
    {
        perror("pthread_create");
        return;
    }

    // Stampa il prompt iniziale
    pthread_mutex_lock(&output_mutex);
    printf("[PROMPT PAROLIERE]--> ");
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);

    print_help();

    char input[BUFFER_SIZE];

    while (!shutdown_flag)
    {
        // Ripristina il prompt all'inizio di ogni iterazione
        pthread_mutex_lock(&output_mutex);
        printf("[PROMPT PAROLIERE]--> ");
        fflush(stdout);
        pthread_mutex_unlock(&output_mutex);

        // lettura l'input dal console
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        input[strcspn(input, "\n")] = '\0'; // Rimuovi newline

        // Parsing comando e parametri
        char *comando = strtok(input, " ");
        char *parametro = strtok(NULL, "\n");

        if (comando == NULL)
        {
            printf("Comando non valido.\n");
            continue;
        }

        // Gestione di tutti i comandi con strcmp sul comando estratto
        // REGISTRAZIONE
        if (strcmp(comando, "registra_utente") == 0)
        {
            if (parametro == NULL)
            {
                printf("Specifica un nome utente.\n");
                continue;
            }
            // rimuovi spazi iniziali dal parametro
            while (*parametro == ' ')
                parametro++;
            if (!valida_nome_utente(parametro))
            {
                printf("Nome utente non valido.\n");
                continue;
            }
            send_message(sockfd, MSG_REGISTRA_UTENTE, parametro, strlen(parametro));
        }
        // LOGIN
        else if (strcmp(comando, "login_utente") == 0)
        {
            if (parametro == NULL)
            {
                printf("Specifica un nome utente.\n");
                continue;
            }
            while (*parametro == ' ')
                parametro++;
            if (!valida_nome_utente(parametro))
            {
                printf("Nome utente non valido.\n");
                continue;
            }
            send_message(sockfd, MSG_LOGIN_UTENTE, parametro, strlen(parametro));
        }
        // CANCELLAZIONE
        else if (strcmp(comando, "cancella_registrazione") == 0)
        {
            if (parametro == NULL)
            {
                printf("specifica un nome utente\n");
                continue;
            }
            send_message(sockfd, MSG_CANCELLA_UTENTE, parametro, strlen(parametro));
        }
        // MATRICE
        else if (strcmp(comando, "matrice") == 0)
        {
            send_message(sockfd, MSG_MATRICE, "", 0);
        }
        // MESSAGGIO SU BACHECA
        else if (strcmp(comando, "msg") == 0)
        {
            if (parametro == NULL)
            {
                printf("Specifica un messaggio.\n");
                continue;
            }
            while (*parametro == ' ')
                parametro++;
            send_message(sockfd, MSG_POST_BACHECA, parametro, strlen(parametro) + 1);
        }
        // VISUALIZZA BACHECA
        else if (strcmp(comando, "show-msg") == 0)
        {
            send_message(sockfd, MSG_SHOW_BACHECA, "", 0);
        }
        // PAROLA
        else if (strcmp(comando, "p") == 0)
        {
            if (parametro == NULL)
            {
                printf("Specifica una parola.\n");
                continue;
            }
            while (*parametro == ' ')
                parametro++;
            normalize_word(parametro);
            send_message(sockfd, MSG_PAROLA, parametro, strlen(parametro) + 1);
        }
        // AIUTO
        else if (strcmp(comando, "aiuto") == 0)
        {
            print_help();
        }
        // FINE
        else if (strcmp(comando, "fine") == 0)
        {
            shutdown_flag = 1;
            // Termina le comunicazioni: spegni il socket per scrittura e lettura
            shutdown(sockfd, SHUT_RDWR);
            break;
        }

        else
        {
            printf("Comando non riconosciuto. Digita 'aiuto' per la lista.\n");
        }
    }
    printf("\n");
    close(sockfd);

    pthread_join(recv_thread, NULL);
    printf("Client terminato.\n");
}