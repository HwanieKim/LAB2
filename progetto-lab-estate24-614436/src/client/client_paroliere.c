/*
client_paroliere.c
    gestisce tutta la logcia del client:
        + invio e ricezione di messaggi
        + implementa le funzionei di protocollo e crea un thread per ricevere
          continuamente i messaggi dal server
*/

#include "client.h"

// variabile volatile globale utilizzato per segnalare terminazione
//  non adotta cancellazione/cancellation point come client, ma flag e shutdown su socket
volatile sig_atomic_t shutdown_flag = 0;

// mutex per sincronizzazione
pthread_mutex_t client_console_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
     normalize_word:
         Converte i caratteri in minuscolo ed effettua la gestione speciale del digramma 'qu':
            se incontra 'q' seguito da 'u', li aggiunge entrambi come "qu";
            se trova una 'q' non seguita da 'u', la ignora (da definizione nel codice).
        Tale strategia rende coerente l’invio della parola al server.
 */
void normalize_word(char *word, int word_len)
{
    if (word_len < 0)
        word_len = strlen(word);

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
    strncpy(word, temp, word_len);
}

/*
    valida_nome_utente
        verifica il formato della stringa che:
            non superi 10 caratteri
            sia composta da caratteri alfanumerici
            non vuota
*/
bool valida_nome_utente(const char *nome, int nome_len)
{
    if (nome_len < 0)
        nome_len = strlen(nome);
    if (nome_len > 10)
        return false;
    for (int i = 0; i < nome_len; i++)
    {
        if (!isalnum((unsigned char)nome[i]))
            return false;
    }
    return true;
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
        pthread_mutex_lock(&client_console_mutex);

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
        pthread_mutex_unlock(&client_console_mutex);
    }
    return NULL;
}

/*
    print_help:
        mostra l'elenco dei comandi disponibili sul client
*/
void print_help()
{
    pthread_mutex_lock(&client_console_mutex);
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
    pthread_mutex_unlock(&client_console_mutex);
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
    pthread_mutex_lock(&client_console_mutex);
    printf("[PROMPT PAROLIERE]--> ");
    fflush(stdout);
    pthread_mutex_unlock(&client_console_mutex);

    print_help();

    char input[BUFFER_SIZE];

    while (!shutdown_flag)
    {
        // Ripristina il prompt all'inizio di ogni iterazione
        pthread_mutex_lock(&client_console_mutex);
        printf("[PROMPT PAROLIERE]--> ");
        fflush(stdout);
        pthread_mutex_unlock(&client_console_mutex);

        // lettura l'input dal console
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            if (feof(stdin))
            {
                printf("End of input detected.\n");
            }
            else
            {
                perror("Error reading input");
            }
            break;
        }

        input[strcspn(input, "\n")] = '\0'; // Rimuovi newline

        // Parsing comando e parametri
        char *comando = strtok(input, " ");
        char *parametro = strtok(NULL, "\n");
        int param_len = parametro ? strlen(parametro) : 0;

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
            if (!valida_nome_utente(parametro, param_len))
            {
                printf("Nome utente non valido.\n");
                continue;
            }
            send_message(sockfd, MSG_REGISTRA_UTENTE, parametro, param_len + 1);
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
            if (!valida_nome_utente(parametro, param_len))
            {
                printf("Nome utente non valido.\n");
                continue;
            }
            send_message(sockfd, MSG_LOGIN_UTENTE, parametro, param_len + 1);
        }
        // CANCELLAZIONE
        else if (strcmp(comando, "cancella_registrazione") == 0)
        {
            if (parametro == NULL)
            {
                printf("specifica un nome utente\n");
                continue;
            }
            send_message(sockfd, MSG_CANCELLA_UTENTE, parametro, param_len + 1);
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
            send_message(sockfd, MSG_POST_BACHECA, parametro, param_len + 1);
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
            normalize_word(parametro, param_len);
            send_message(sockfd, MSG_PAROLA, parametro, param_len + 1);
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