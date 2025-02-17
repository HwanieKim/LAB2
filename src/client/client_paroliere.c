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

volatile sig_atomic_t shutdown_flag = 0;
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;

// normalizzazione prima di inviare la parola
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

// controllo sul formato del nome utente <= 10 caratteri
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

// funizioni per invio e ricezione messaggi
int send_message(int sockfd, char type, const char *data, unsigned int length)
{
    unsigned int netlen = htonl(length);
    if (write(sockfd, &type, 1) != 1)
        return -1;
    if (write(sockfd, &netlen, 4) != 4)
        return -1;
    if (length > 0)
    {
        if (write(sockfd, data, length) != (ssize_t)length)
            return -1;
    }
    return 0;
}

int receive_message(int sockfd, char *type, char *data, unsigned int *length)
{
    ssize_t n = read(sockfd, type, 1);
    if (n <= 0)
    {
        perror("read(type)");
        return -1;
    }

    unsigned int netlen;
    n = read(sockfd, &netlen, 4);
    if (n <= 0)
    {
        perror("read(length)");
        return -1;
    }

    *length = ntohl(netlen); // Converti in formato host
    if (*length > BUFFER_SIZE - 1)
    {
        *length = BUFFER_SIZE - 1; // troncamento per prevenire overflow
    }
    if (*length > 0)
    {
        n = read(sockfd, data, *length);
        if (n <= 0)
        {
            perror("read(data)");
            return -1;
        }
        data[n] = '\0'; // Aggiungi terminazione null
    }
    return 0;
}

// thread di ricezione, riceve continuamente messaggi dal server e li stampa
void *client_receiver(void *arg)
{
    int sockfd = *(int *)arg;
    char type;
    char data[BUFFER_SIZE];
    unsigned int length;

    while (!shutdown_flag)
    {
        if (receive_message(sockfd, &type, data, &length) < 0)
        {
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
        if (type == MSG_SERVER_SHUTDOWN)
        {
            printf("\r\33[2K"); // Cancella la linea corrente
            printf("\n[SERVER] Shutdown: %s\n", data);
            shutdown_flag = 1;
            break;
        }
        pthread_mutex_lock(&output_mutex);
        // Cancella la riga corrente (cancella il prompt ed eventuale input parziale)
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
            // per gestire caso in cui non e' in partita e deve stampare i secondi
            //  Copia la stringa per contare i token senza modificarla
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

// print help
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

// funzione principale della logica di client
void client_run(int sockfd)
{
    pthread_t recv_thread;
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
        if (strcmp(comando, "registra_utente") == 0)
        {
            if (parametro == NULL)
            {
                printf("Specifica un nome utente.\n");
                continue;
            }
            // Pulizia spazi iniziali dal parametro
            while (*parametro == ' ')
                parametro++;
            if (!valida_nome_utente(parametro))
            {
                printf("Nome utente non valido.\n");
                continue;
            }
            send_message(sockfd, MSG_REGISTRA_UTENTE, parametro, strlen(parametro));
        }

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

        else if (strcmp(comando, "cancella_registrazione") == 0)
        {
            if (parametro == NULL)
            {
                printf("specifica un nome utente\n");
                continue;
            }
            send_message(sockfd, MSG_CANCELLA_UTENTE, parametro, strlen(parametro));
        }

        else if (strcmp(comando, "matrice") == 0)
        {
            send_message(sockfd, MSG_MATRICE, "", 0);
        }

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

        else if (strcmp(comando, "show-msg") == 0)
        {
            send_message(sockfd, MSG_SHOW_BACHECA, "", 0);
        }

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

        else if (strcmp(comando, "aiuto") == 0)
        {
            print_help();
        }

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