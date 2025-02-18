#define _GNU_SOURCE // soluzione per errore implicit declaration of signal.h

/*
    server_paroliere.c

    logica principale del server paroliere

*/

// include
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdarg.h> // per log_event e safe_ptrintf
#include <ctype.h>

// definizioni protocollo
#define MSG_OK 'K'
#define MSG_ERR 'E'
#define MSG_REGISTRA_UTENTE 'R'
#define MSG_MATRICE 'M'
#define MSG_TEMPO_PARTITA 'T'
#define MSG_TEMPO_ATTESA 'A'
#define MSG_PAROLA 'W'
#define MSG_PUNTI_FINALI 'F'
#define MSG_PUNTI_PAROLA 'P'
#define MSG_SERVER_SHUTDOWN 'B'
#define MSG_CANCELLA_UTENTE 'D'
#define MSG_LOGIN_UTENTE 'L'
#define MSG_POST_BACHECA 'H'
#define MSG_SHOW_BACHECA 'S'

// costanti
#define MAX_CLIENTS 32
#define USERNAME_LEN 32
#define BUFFER_SIZE 512
#define MAX_WORDS_USED 256 // massimo numero di parole proposte da un client in una partita
#define MAX_BACHECA_MSG 8
#define MAX_REGISTERED_USERS 1000
#define MAX_SCORE_MSG MAX_CLIENTS

// ======================= dichiarazioni extern =======================
extern int server_init(
    int port,
    int game_duration_sec,
    int break_time_sec,
    const char *dict_file,
    const char *matrix_file,
    int seed,
    int disconnect_timeout_sec);
extern int server_run();
extern void server_shutdown();

typedef struct trie_node trie_node;
// funzioni per trie, definite in dictionary.c
extern void *load_dictionary_trie(const char *filename);
extern bool trie_search(trie_node *root, const char *word);
extern void trie_free(trie_node *node);

// funzioni per matrice, definite in matrix.c
extern void generate_matrix(char matrix[16][5], unsigned int seed);
extern bool is_word_in_matrix(char matrix[16][5], const char *word);
extern int count_letters(const char *word);

// ======================= struttra dati =======================

// struttura per gestione un client
typedef struct
{
    int sockfd;
    bool connected;
    char username[USERNAME_LEN];
    int score;

    // parole gia' proposte da client
    char used_words[MAX_WORDS_USED][32];
    int used_words_count;

    bool score_sent;
    pthread_t thread_id;

} client_info;

// struttura per gestione registrazion utenti
typedef struct
{
    char username[USERNAME_LEN];
    bool deleted;
} RegisteredUser;

// struttura per gestione bacheca (coda circolare)
typedef struct
{
    char username[USERNAME_LEN];
    char message[128];
} BachecaMsg;

typedef struct
{
    BachecaMsg messages[MAX_BACHECA_MSG];
    int front;
    int count;
} Bacheca;

// struttura per messaggi di punteggio
typedef struct
{
    char username[USERNAME_LEN];
    int score;
} ScoreMsg;

typedef struct
{
    ScoreMsg messages[MAX_SCORE_MSG];
    int count;
    int expected;
} scoreQueue;

// server globale
typedef struct
{
    int port;
    int server_sockfd;
    client_info clients[MAX_CLIENTS];
    pthread_mutex_t clients_mutex;

    // matrice di gioco
    char matrix[16][5];
    int seed;

    // parametri/ stato della partita
    bool game_running;      // true se partita in corso
    time_t game_start_time; // orario inizio partita
    int game_duration;      // durata partita in secondi
    int break_time;         // pausa tra partite in secondi
    time_t break_start_time;

    // dizionario caricato in trie
    void *dictionary;

    bool stop; // flag di shutdown (impostato da SIGINT)

    // thread orchestratore per cicolo partita/pausa
    pthread_t orchestrator_thread_id;
    // trhead scorer
    pthread_t scorer_thread_id;

    int disconnect_timeout; // timeout per inattivita' del client (sec)

    // se specificato, file contenente matrice di gioco
    char *matrix_filename;
    FILE *matrix_fp;

    // log file and mutex
    FILE *log_fp;
    pthread_mutex_t log_mutex;

    // server name
    char server_name[128];

    // utenti registrati
    RegisteredUser registered_users[MAX_REGISTERED_USERS];
    int registered_count;
    pthread_mutex_t registered_mutex;
} server_paroliere;

// variabile globale del server
static server_paroliere g_server;

// variabile globale per la bacheca
static Bacheca bacheca = {.front = 0, .count = 0};
static pthread_mutex_t bacheca_mutex = PTHREAD_MUTEX_INITIALIZER;

// coda dei punteggi
scoreQueue g_score_queue = {.count = 0, .expected = 0};
pthread_mutex_t score_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t score_queue_cond = PTHREAD_COND_INITIALIZER;

// per sincronizzazione dell'invio di classifica
bool ranking_sent = false;
pthread_mutex_t ranking_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ranking_cond = PTHREAD_COND_INITIALIZER;

// Definizione e inizializzazione di un mutex globale per l'output della console
pthread_mutex_t console_mutex = PTHREAD_MUTEX_INITIALIZER;

// ======================= Funzioni di comunicazione =======================
void safe_printf(const char *format, ...)
{
    va_list args;
    pthread_mutex_lock(&console_mutex);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    pthread_mutex_unlock(&console_mutex);

}
/*
    send_mesage:
    invia un messaggio al client secondo il protocollo:
        [1 byte type] [4 bytre lunghezza (network order)] [data]
*/

static int send_message(int sockfd, char type, const char *data, unsigned int length)
{
    unsigned int netlen = htonl(length);
    if (write(sockfd, &type, 1) != 1)
    {
        return -1;
    }

    if (write(sockfd, &netlen, 4) != 4)
    {
        return -1;
    }
    if (length > 0)
    {
        if (write(sockfd, data, length) != (ssize_t)length)
        {
            return -1;
        }
    }
    return 0;
}

/*
    receive_message:s
    riceve un messaggio, se il socket ha un timeout impostato(per inattivita' del client), un erroro (errno == EAGAIN/EWOULDBLOCK)
    permettera' di rilevare il client inattivo
*/
static int receive_message(int sockfd, char *type, char *data, unsigned int *length)
{
    if (!type || !data || !length)
    {
        return -1;
    }
    ssize_t n = read(sockfd, type, 1);
    if (n <= 0)
    {
        return -1;
    }

    unsigned int netlen;
    n = read(sockfd, &netlen, 4);
    if (n <= 0)
    {
        return -1;
    }

    unsigned int msglen = ntohl(netlen);
    *length = msglen;

    if (msglen > 0)
    {
        n = read(sockfd, data, msglen);
        if (n <= 0)
        {
            return -1;
        }
        data[n] = '\0'; // Aggiungi il carattere di terminazione null
    }
    else
    {
        data[0] = '\0';
    }

    return 0;
}

// ======================= logging =======================
/*
    log_event:
        registra un evento sul file di log con timestamp
        funzione con mutex per garantire l'accesso esclusivo al file di log
*/
static void log_event(const char *format, ...)
{
    if (g_server.log_fp == NULL)
        return;

    pthread_mutex_lock(&g_server.log_mutex);
    // timestamp
    time_t now = time(NULL);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(g_server.log_fp, "[%s] [%s] ", timestr, g_server.server_name);

    // log
    va_list args;
    va_start(args, format);
    vfprintf(g_server.log_fp, format, args);
    va_end(args);
    fprintf(g_server.log_fp, "\n");

    // flush
    fflush(g_server.log_fp);

    pthread_mutex_unlock(&g_server.log_mutex);
}

// ======================= lettura matrice da file =======================
/*
    read_matrix_from_file:
        se e' specificato un file di matrici, legge una riga e ne estrae 16 token (celle), separati da spazi o tab.ADJ_FREQUENCY
        se si raggiunge la fine del fine, il puntatore viene fatto rewind per ciclare le matrici
*/

static bool read_matrix_from_file(char matrix[16][5])
{
    char line[1024];
    if (fgets(line, sizeof(line), g_server.matrix_fp) == NULL)
    {
        // se raggiunto EOF, fai rewind
        rewind(g_server.matrix_fp);
        if (fgets(line, sizeof(line), g_server.matrix_fp) == NULL)
        {
            log_event("[SYSTEM] Errore lettura file matrici: %s", strerror(errno));
            return false;
        }
    }

    // tokenizza la riga per ottenere 16 celle
    char *token = strtok(line, " \t\r\n");
    for (int i = 0; i < 16; i++)
    {
        if (token == NULL)
        {
            log_event("[SYSTEM] Formato file matrici non valido (token %d mancancte)", i);
            return false;
        }

        // controllo token "qu"
        if (strcasecmp(token, "qu") == 0)
        {
            strncpy(matrix[i], "Qu", 5); // forza 'qu' come un unico token
        }
        else
        {
            char c = toupper(token[0]);
            if (!isalpha(c))
            {
                log_event("[SYSTEM] Carattere non valido '%c' nel file matrici", c);
                return false;
            }
            // prende solo il primo carattere
            matrix[i][0] = c;
            matrix[i][1] = '\0';
        }
        matrix[i][4] = '\0';
        token = strtok(NULL, " \t,\r,\n");
    }
    return true;
}

// ======================= gestione SIGINT =======================
/*
    signal_handler:
        imposta il flag di stop del server e chiude socket in ascolto per uscire dal loop di accept
*/
static void signal_handler(int signo)
{
    (void)signo;
    log_event("[SYSTEM] Ricevuto SIGINT, avvio shutdown");
    server_shutdown();
}

void sigalarm_handler(int signo)
{
    // per evitare waring
    (void)signo;
    // rimane vuoto, unico scopo e' interrompere la read bloccante
}

// ======================= broadcast di shutdown =======================
/*
    broadcast_server_shutdown:
        invia a tutti i client connessi un messaggio di shutdown
*/
static void broadcast_server_shutdown()
{
    pthread_mutex_lock(&g_server.clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_server.clients[i].connected)

        {
            // Forza la chiusura delle operazioni I/O sul socket
            shutdown(g_server.clients[i].sockfd, SHUT_RDWR);
            send_message(g_server.clients[i].sockfd, MSG_SERVER_SHUTDOWN, "Server shutdown", strlen("Server shutdown") + 1);
            close(g_server.clients[i].sockfd);

            // segnalazione disconnessione
            g_server.clients[i].connected = false;
            g_server.clients[i].username[0] = '\0';
            log_event("[ACCEPT] Chiusura connessione per client in slot %d", i);
        }
    }
    pthread_mutex_unlock(&g_server.clients_mutex);
}
// ======================= push_score =======================
/*
    push_score:
    Aggiunge il punteggio di un utente alla coda dei punteggi se l'username non è vuoto.
*/
void push_score(const char *username, int score)
{
    if (username[0] == '\0')
        return;
    pthread_mutex_lock(&score_queue_mutex);
    if (g_score_queue.count < MAX_SCORE_MSG)
    {
        strncpy(g_score_queue.messages[g_score_queue.count].username, username, USERNAME_LEN - 1);
        g_score_queue.messages[g_score_queue.count].username[USERNAME_LEN - 1] = '\0';
        g_score_queue.messages[g_score_queue.count].score = score;
        g_score_queue.count++;
        log_event("[SYSTEM] Punteggio push: %s -> %d (count=%d)", username, score, g_score_queue.count);
    }
    pthread_cond_signal(&score_queue_cond);
    pthread_mutex_unlock(&score_queue_mutex);
}

//======================= THREAD ORCHESTRATOR =======================
// thread per gestione client, orchestratore
/*
    orchestrator_thread:
        gestisce il ciclo partita/pausa
        - all'inizio di ogni partita, imposta game_running, registra l'orario, rigenera la matrice(o legge dal file se specificato),
        e azzera i punteggi e le parole usate dai client
        - dopo la durata della partita, invia i punteggi finali a tutti i client connessi, logga l'evento
        e attende per un periodio di pausa prima di iniziare una nuova partita
*/
static void *orchestrator_thread(void *arg)
{
    (void)arg;
    // Abilita la cancellazione
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (!g_server.stop)
    {
        pthread_testcancel();
        // inizio partita
        pthread_mutex_lock(&g_server.clients_mutex);
        g_server.game_running = true;
        g_server.game_start_time = time(NULL);
        log_event("[ORCHESTRATOR] Inizio partita");
        pthread_mutex_unlock(&g_server.clients_mutex);

        // se specificato, leggi matrice da file
        if (g_server.matrix_fp)
        {
            rewind(g_server.matrix_fp); // assicura la lettura all'inizio
            if (!read_matrix_from_file(g_server.matrix))
            {
                log_event("[ORCHESTRATOR] Impossibile leggere matrice dal file, generazione casuale di matrice");
                unsigned int effective_seed = (g_server.seed >= 0) ? (unsigned int)g_server.seed : (unsigned int)time(NULL);
                generate_matrix(g_server.matrix, effective_seed);
            }
        }
        else
        {
            unsigned int effective_seed = (g_server.seed >= 0) ? (unsigned int)g_server.seed : (unsigned int)time(NULL);
            generate_matrix(g_server.matrix, effective_seed);
        }

        // reset punteggi e parole usate
        pthread_mutex_lock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; ++i)
        {
            if (g_server.clients[i].connected)
            {
                g_server.clients[i].score = 0;
                g_server.clients[i].used_words_count = 0;
                g_server.clients[i].score_sent = false;
            }
        }
        pthread_mutex_unlock(&g_server.clients_mutex);

        log_event("[ORCHESTRATOR] Nuova partiata iniziata, durata %d secondi", g_server.game_duration);
        safe_printf("[ORCHESTRATOR] Nuova partita iniziata, durata %d secondi\n", g_server.game_duration);

        // attesa durante la partita
        time_t game_start = g_server.game_start_time;
        while (!g_server.stop && difftime(time(NULL), game_start) < g_server.game_duration)
        {
            pthread_testcancel();
            usleep(10000);
        }

        // fine partita
        pthread_mutex_lock(&g_server.clients_mutex);
        g_server.game_running = false;
        int count_connected = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (g_server.clients[i].connected && g_server.clients[i].username[0] != '\0')
            {
                count_connected++;
            }
        }
        log_event("[ORCHESTRATOR] Fine partita, %d client loggati", count_connected);
        pthread_mutex_unlock(&g_server.clients_mutex);

        // Quando la partita termina, invia il segnale SIGALRM a tutti i thread client per "svegliarli":
        pthread_mutex_lock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (g_server.clients[i].connected)
            {
                pthread_kill(g_server.clients[i].thread_id, SIGALRM);
            }
        }
        pthread_mutex_unlock(&g_server.clients_mutex);

        // Attende brevemente per consentire ai client di inviare il punteggio
        sleep(1);

        // Forza l'invio del punteggio per i client che non l'hanno ancora inviato
        pthread_mutex_lock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (g_server.clients[i].connected && !g_server.clients[i].score_sent)
            {
                push_score(g_server.clients[i].username, g_server.clients[i].score);
                g_server.clients[i].score_sent = true;
                log_event("[ORCHESTRATOR] Punteggio forzato inviato per %s", g_server.clients[i].username);
            }
        }
        pthread_mutex_unlock(&g_server.clients_mutex);

        // Imposta il numero di punteggi attesi nella coda per questa partita
        pthread_mutex_lock(&score_queue_mutex);
        g_score_queue.expected = count_connected;
        pthread_mutex_unlock(&score_queue_mutex);

        // Aspetta che la classifica sia stata inviata
        pthread_mutex_lock(&ranking_mutex);
        while (!ranking_sent)
        {
            if (g_server.stop)
                break;

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1; // timeout di 1 secondo
            int ret = pthread_cond_timedwait(&ranking_cond, &ranking_mutex, &ts);
            (void)ret;
            pthread_testcancel();
            if (g_server.stop)
                break;
        }
        ranking_sent = false;
        pthread_mutex_unlock(&ranking_mutex);

        // imposta il tempo di inizio della pausa
        g_server.break_start_time = time(NULL);

        // pausa tra partite
        safe_printf("[ORCHESTRATOR] Partita terminata, pausa tra partite di %d secondi\n", g_server.break_time);
        log_event("[ORCHESTRATOR] Inizio pausa di %d secondi", g_server.break_time);

        while (!g_server.stop && difftime(time(NULL), g_server.break_start_time) < g_server.break_time)
        {
            pthread_testcancel();
            usleep(10000);
        }
    }
    return NULL;
}
// ======================= thread scorer =======================
/*
    scorer_thread:
        gestisce la raccolta e l'elaborazione dei punteggi finali della partita
        - attende che tutti i client abbiano inviato i propri punteggi tramite la coda condivisa
        - utilizza una condition variable per sincronizzarsi con i thread client e attendere il completamento
        - quando tutti i punteggi attesi sono stati raccolti:
            + ordina i punteggi in ordine decrescente per generare la classifica finale
            + costruisce un messaggio MSG_PUNTI_FINALI contenente la classifica in formato CSV
            + invia il messaggio MSG_PUNTI_FINALI a tutti i client connessi
        - registra l'evento nel file di log
        - si ripete per ogni partita, azzerando la coda dei punteggi per la partita successiva
        - termina solo quando il server viene arrestato
*/
void *scorer_thread(void *arg)
{
    (void)arg;
    // Abilita la cancellazione
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (!g_server.stop)
    {
        pthread_testcancel();
        pthread_mutex_lock(&score_queue_mutex);

        // Se expected è 0, non ci sono client connessi: rilascio e attendo un po'
        if (g_score_queue.expected == 0)
        {
            pthread_mutex_unlock(&score_queue_mutex);
            sleep(1);
            continue;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // attesa massima di 1 secondo

        // attesa finche count == expected
        while (g_score_queue.count < g_score_queue.expected && !g_server.stop)
        {
            int ret = pthread_cond_timedwait(&score_queue_cond, &score_queue_mutex, &ts);
            (void)ret;
            pthread_testcancel();
            if (g_server.stop)
                break;
        }

        // se il server e' in shutdown esce
        if (g_server.stop)
        {
            pthread_mutex_unlock(&score_queue_mutex);
            break;
        }

        // copia locale di punteggi raccolti
        int n = g_score_queue.count;
        ScoreMsg local_scores[MAX_SCORE_MSG];
        for (int i = 0; i < n; i++)
        {
            local_scores[i] = g_score_queue.messages[i];
        }

        // reset della coda per prossima partita
        g_score_queue.count = 0;
        pthread_mutex_unlock(&score_queue_mutex);

        // ordinamento (bubble sort)
        for (int i = 0; i < n - 1; i++)
        {
            for (int j = i + 1; j < n; j++)
            {
                if (local_scores[i].score < local_scores[j].score)
                {
                    ScoreMsg temp = local_scores[i];
                    local_scores[i] = local_scores[j];
                    local_scores[j] = temp;
                }
            }
        }

        // costruzione della stringa per classifica
        char classifica[BUFFER_SIZE] = "";
        int offset = 0;
        for (int i = 0; i < n; i++)
        {
            if (i < n - 1)
                offset += snprintf(classifica + offset, sizeof(classifica) - offset, "%s, %d, ", local_scores[i].username, local_scores[i].score);
            else
                offset += snprintf(classifica + offset, sizeof(classifica) - offset, "%s, %d", local_scores[i].username, local_scores[i].score);
        }
        // invio classifica
        pthread_mutex_lock(&g_server.clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (g_server.clients[i].connected)
            {
                send_message(g_server.clients[i].sockfd, MSG_PUNTI_FINALI, classifica, strlen(classifica) + 1);
            }
        }
        pthread_mutex_unlock(&g_server.clients_mutex);

        log_event("[SCORER] Parita terminata, classifica finale: \n%s", classifica);
        safe_printf("Partita termintata, classifica:\n%s\n", classifica);

        // Segnala che la classifica è stata inviata
        pthread_mutex_lock(&ranking_mutex);
        ranking_sent = true;
        pthread_cond_signal(&ranking_cond);
        pthread_mutex_unlock(&ranking_mutex);
    }

    return NULL;
}
// ======================= thread client =======================
/*
    client_thread:
        gestisce la comunicazione con client
        - all'inizio, imposta timeout per ricezione (SO_RCVTIMEO) per rilevare client inattivi
        - elabora messaggi ricevuti dal client:
            + MSG_REGISTRA_UTENTE: registra un nuovo utente, se il nome non e' gia' in uso, e logga l'evento
            + MSG_LOGIN_UTENTE: verifica se l'utente e' registrato, e se e' registrato e non cancellato consente di riaccedere e logga l'evento
            + MSG_CANCELLA_UTENTE: cancella (deregistra) il nome utente e logga l'evento.
            + MSG_PAROLA: se la partita è in corso, verifica la parola (dizionario e matrice), calcola il punteggio
                      e logga l'evento; se la parola era già proposta, restituisce 0 punti.
            + MSG_MATRICE: invia la matrice corrente.
    - Se la ricezione fallisce (incluso il timeout per inattività), il client viene disconnesso e loggato.
*/

static void *client_thread(void *arg)
{
    // Abilita la cancellazione
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    int idx = *(int *)arg;
    free(arg);

    int sockfd;
    pthread_mutex_lock(&g_server.clients_mutex);
    sockfd = g_server.clients[idx].sockfd;
    pthread_mutex_unlock(&g_server.clients_mutex);

    // impostazione gestore per SIGALRM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigalarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    // impostazione timeout di ricezione sul socket, gestione client inattivi
    struct timeval timeout;
    timeout.tv_sec = g_server.disconnect_timeout;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    time_t last_activity = time(NULL);

    // buffer per messaggi
    char type;
    char data[BUFFER_SIZE];
    unsigned int length = 0;

    while (!g_server.stop)
    {
        pthread_testcancel();
        pthread_mutex_lock(&g_server.clients_mutex);
        bool game_active = g_server.game_running;

        pthread_mutex_unlock(&g_server.clients_mutex);
        // se il gioco terminato e il punteggio non e' stato inviato, invialo alla coda
        if (!game_active && !g_server.clients[idx].score_sent)
        {
            push_score(g_server.clients[idx].username, g_server.clients[idx].score);
            g_server.clients[idx].score_sent = true;
            log_event("[CLIENT] Client %s: punteggio inviato alla coda", g_server.clients[idx].username);
        }

        // controllo periodico
        if (difftime(time(NULL), last_activity) > g_server.disconnect_timeout)
        {
            log_event("[CLIENT] Disconnessione per inattivita': %s", g_server.clients[idx].username);
            send_message(sockfd, MSG_SERVER_SHUTDOWN, "Disconnessione per inattivita'", 30);
            break;
        }

        if (receive_message(sockfd, &type, data, &length) < 0)
        {
            if (errno == EINTR)
            {
                // La read() è stata interrotta da SIGALRM: semplicemente riprova.
                pthread_testcancel();
                continue;
            }
            // se il client non comunica per un certo periodo, rivcervera' timeout (errno == EAGAIN/EWOULDBLOCK)
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                send_message(sockfd, MSG_SERVER_SHUTDOWN, "Disconnessione per inattivita'", 30);
                log_event("[CLIENT] Timeout di inattivita' per il client %s", g_server.clients[idx].username);
                break;
            }
            else
            {
                log_event("[CLIENT] Errore nella ricezione per il client %s", g_server.clients[idx].username);
                break;
            }
        }
        else
        {
            last_activity = time(NULL);
        }

        if (type == MSG_SERVER_SHUTDOWN)
        {
            safe_printf("\n[SERVER] Shutdown: %s\n", data);
            break;
        }

        // elabora messaggio
        switch (type)
        {
        case MSG_REGISTRA_UTENTE:
        {
            pthread_mutex_lock(&g_server.registered_mutex);
            pthread_mutex_lock(&g_server.clients_mutex);
            safe_printf("[SERVER] Ricevuto messaggio di registrazione per l'utente: %s\n", data);
            log_event("[CLIENT] Ricevuta registrazione: %s", data);

            // verifica nome utente
            if (strlen(data) > 10 || strpbrk(data, "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == NULL)
            {
                send_message(sockfd, MSG_ERR, "Nome utente non valido", strlen("Nome utente non valido") + 1);
                pthread_mutex_unlock(&g_server.clients_mutex);
                pthread_mutex_unlock(&g_server.registered_mutex);
                break;
            }

            int existing_index = -1;
            bool already_connected = false;

            // Cerca utente esistente(anche cancellato)
            for (int i = 0; i < g_server.registered_count; i++)
            {
                if (strcasecmp(g_server.registered_users[i].username, data) == 0)
                {
                    existing_index = i;
                    break;
                }
            }

            // controlla se' e' gia' connesso
            for (int i = 0; i < MAX_CLIENTS; i++)
            {
                if (g_server.clients[i].connected && strcmp(g_server.clients[i].username, data) == 0)
                {
                    already_connected = true;
                    break;
                }
            }

            // Gestione utente esistente
            if (existing_index != -1)
            {
                if (!g_server.registered_users[existing_index].deleted)
                {
                    send_message(sockfd, MSG_ERR, "Nome utente già registrato", 28);
                }
                else
                {
                    // Riattiva l'utente solo se non è connesso
                    if (already_connected)
                    {
                        send_message(sockfd, MSG_ERR, "Nome utente già in uso", 24);
                    }
                    else
                    {
                        g_server.registered_users[existing_index].deleted = false;
                        send_message(sockfd, MSG_OK, "Registrazione riattivata", 25);
                        log_event("[CLIENT] Utente riattivato: %s", data);
                    }
                }
            }
            else
            {
                // Aggiungi nuovo utente
                if (already_connected)
                {
                    send_message(sockfd, MSG_ERR, "Nome utente già in uso", 24);
                }
                else if (g_server.registered_count >= MAX_REGISTERED_USERS)
                {
                    send_message(sockfd, MSG_ERR, "Limite utenti raggiunto", 24);
                }
                else
                {
                    strncpy(g_server.registered_users[g_server.registered_count].username,
                            data, USERNAME_LEN - 1);
                    g_server.registered_users[g_server.registered_count].deleted = false;
                    g_server.registered_count++;
                    send_message(sockfd, MSG_OK, "Registrazione completata", 25);
                    log_event("[CLIENT] Nuovo utente registrato: %s", data);
                }
            }

            pthread_mutex_unlock(&g_server.registered_mutex);
            pthread_mutex_unlock(&g_server.clients_mutex);
            break;
        }

        case MSG_LOGIN_UTENTE:
        {
            safe_printf("[SERVER] Ricevuto messaggio di login per l'utente: %s\n", data);
            log_event("[CLIENT] Ricevuto login: %s", data);
            // Controlla se il client è già autenticato
            if (strlen(g_server.clients[idx].username) > 0)
            {
                send_message(sockfd, MSG_ERR, "Sei già autenticato", 20);
                log_event("[CLIENT] Tentativo di login multiplo da &s", g_server.clients[idx].username);
                break;
            }

            pthread_mutex_lock(&g_server.clients_mutex);
            pthread_mutex_lock(&g_server.registered_mutex);

            bool already_registered = false;
            bool in_use = false;

            for (int i = 0; i < g_server.registered_count; i++)
            {
                if (strcmp(g_server.registered_users[i].username, data) == 0 &&
                    !g_server.registered_users[i].deleted)
                {
                    already_registered = true;
                    break;
                }
            }

            // Controlla se già connesso
            if (already_registered)
            {
                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (g_server.clients[i].connected &&
                        strcmp(g_server.clients[i].username, data) == 0)
                    {
                        in_use = true;
                        break;
                    }
                }
            }

            if (!already_registered)
            {
                send_message(sockfd, MSG_ERR, "Utente non registrato", 22);
                log_event("[CLIENT] Tentativo di login all'utente %s non registrato", data);
            }
            else if (in_use)
            {
                send_message(sockfd, MSG_ERR, "Utente gia' connesso", 20);
                log_event("[CLIENT] Tentativo di login all'utente %s gia' connesso", g_server.clients[idx].username);
            }
            else
            {
                strcpy(g_server.clients[idx].username, data); // Login corretto
                send_message(sockfd, MSG_OK, "Login effettuato", 17);
                log_event("[CLIENT] Login effettuato con succeso, utente %s", data);
            }

            pthread_mutex_unlock(&g_server.clients_mutex);
            pthread_mutex_unlock(&g_server.registered_mutex);
            break;
        }

        case MSG_CANCELLA_UTENTE:
        {
            safe_printf("[SERVER] Ricevuto comando di cancellazione per l'utente: %s\n", data);
            log_event("[CLIENT] Ricevuta cancellazione registrazione: %s", data);
            pthread_mutex_lock(&g_server.registered_mutex);

            bool trovato = false;
            for (int i = 0; i < g_server.registered_count; i++)
            {
                if (strcmp(g_server.registered_users[i].username, data) == 0 &&
                    !g_server.registered_users[i].deleted)
                {
                    g_server.registered_users[i].deleted = true;
                    trovato = true;
                    break;
                }
            }

            pthread_mutex_unlock(&g_server.registered_mutex);

            if (trovato)
            {
                send_message(sockfd, MSG_OK, "Utente cancellato", 17);
                log_event("[CLIENT] Utente cancellato: %s", data);
            }
            else
            {
                send_message(sockfd, MSG_ERR, "Utente non trovato", 18);
                log_event("[CLIENT] Tentativo di cancellazione utente %s non esistente", data);
            }
            break;
        }
        break;

        case MSG_PAROLA:
        {
            safe_printf("[SERVER] Ricevuto comando per parola: %s\n", data);
            log_event("[CLIENT] Ricevuta parola: %s", data);
            if (!g_server.dictionary)
            {
                send_message(sockfd, MSG_ERR, "Dizionario non caricato", strlen("Dizionario non caricato") + 1);
                break;
            }
            // controllo se la partita e' in corso, nel caso positivo non si accettano le parole
            pthread_mutex_lock(&g_server.clients_mutex);
            bool game_active = g_server.game_running;
            bool is_connected = g_server.clients[idx].connected;
            pthread_mutex_unlock(&g_server.clients_mutex);

            if (!is_connected)
                break;
            if (!game_active)
            {
                send_message(sockfd, MSG_TEMPO_ATTESA, "partita non avviata", strlen("partita non avviata") + 1);
                break;
            }

            // verifica la parola:
            // 1) controllo dizionario
            if (!trie_search(g_server.dictionary, data))
            {
                send_message(sockfd, MSG_ERR, "Parola non presente in dizionario", strlen("Parola non presente in dizionario") + 1);
                break;
            }
            // 2) controllo presenza nella matrice
            if (!is_word_in_matrix(g_server.matrix, data))
            {
                send_message(sockfd, MSG_ERR, "Parola non presente in matrice", strlen("Parola non presente in matrice"));
                break;
            }
            // 3) calcolo punteggio (numero di lettere "logiche", con Qu = 1)
            int points = count_letters(data);
            // 4) verifica se e' ripetuta
            pthread_mutex_lock(&g_server.clients_mutex);
            bool repeated = false;
            for (int i = 0; i < g_server.clients[idx].used_words_count; i++)
            {
                if (strcmp(g_server.clients[idx].used_words[i], data) == 0)
                {
                    repeated = true;
                    break;
                }
            }
            if (repeated)
            {
                pthread_mutex_unlock(&g_server.clients_mutex);
                char msg[1024];
                snprintf(msg, sizeof(msg), "Parola '%s' gia' proposta: 0 punti", data);
                send_message(sockfd, MSG_PUNTI_PAROLA, msg, strlen(msg) + 1);
                log_event("[DICTIONARY] Parola ripetuta da '%s' : %s", g_server.clients[idx].username, data);
            }
            else
            {
                // registra la parola e aggiorna il punteggio
                if (g_server.clients[idx].used_words_count < MAX_WORDS_USED)
                {
                    strcpy(g_server.clients[idx].used_words[g_server.clients[idx].used_words_count], data);
                    g_server.clients[idx].used_words_count++;
                }
                g_server.clients[idx].score += points;
                pthread_mutex_unlock(&g_server.clients_mutex);
                char msg[1024];
                snprintf(msg, sizeof(msg), "Parola '%s' accettata: %d punti", data, points);
                send_message(sockfd, MSG_PUNTI_PAROLA, msg, strlen(msg) + 1);
                log_event("[DICTIONARY] Utente '%s' ha inviato parola '%s' assegnado %d punti", g_server.clients[idx].username, data, points);
            }
            break;
        }

        case MSG_MATRICE:
        {
            safe_printf("[SERVER] Ricevuto comando per matrice\n");
            log_event("[CLIENT] Ricevuto comando matrice");

            pthread_mutex_lock(&g_server.clients_mutex);
            bool game_active = g_server.game_running;
            pthread_mutex_unlock(&g_server.clients_mutex);

            if (game_active)
            {
                // invio della matrice corrente come stringa, 16 celle separate da spazio
                char matrix_buf[BUFFER_SIZE] = "";
                for (int i = 0; i < 16; i++)
                {
                    strcat(matrix_buf, g_server.matrix[i]);
                    if (i < 15)
                        strcat(matrix_buf, " ");
                }
                send_message(sockfd, MSG_MATRICE, matrix_buf, (unsigned int)strlen(matrix_buf) + 1);

                // calcolo tempo residuo in secondi
                int remaining = g_server.game_duration - (int)difftime(time(NULL), g_server.game_start_time);
                char time_str[32];
                snprintf(time_str, sizeof(time_str), "%d", remaining);

                // invio
                send_message(sockfd, MSG_TEMPO_PARTITA, time_str, strlen(time_str) + 1);
            }
            else
            {
                // !game_active
                // calcola il tempo rimanente fino all'inizio della prossima partita
                int remaining_break = g_server.break_time - (int)difftime(time(NULL), g_server.break_start_time);
                if (remaining_break < 0)
                {
                    remaining_break = 0;
                }

                // Costruisce una stringa CSV: primo campo il tempo di default, secondo il tempo rimanente
                char csv_str[64];
                snprintf(csv_str, sizeof(csv_str), "%d secondi, e l'inizio della nuova partita tra %d", g_server.break_time, remaining_break);
                send_message(sockfd, MSG_MATRICE, csv_str, strlen(csv_str) + 1);
            }
        }

        break;

        case MSG_POST_BACHECA:
        {
            safe_printf("[SERVER] Ricevuto comando per post bacheca\n");
            log_event("[CLIENT] Ricevuto comando post bacheca");
            // client invia un messaggio da postare sulla bacheca
            pthread_mutex_lock(&bacheca_mutex);
            BachecaMsg nuovo;
            strncpy(nuovo.username, g_server.clients[idx].username, USERNAME_LEN - 1);
            strncpy(nuovo.message, data, 127);

            if (bacheca.count < MAX_BACHECA_MSG)
            {
                bacheca.messages[(bacheca.front + bacheca.count) % MAX_BACHECA_MSG] = nuovo;
                bacheca.count++;
            }
            else
            {
                bacheca.messages[bacheca.front] = nuovo;
                bacheca.front = (bacheca.front + 1) % MAX_BACHECA_MSG;
            }
            pthread_mutex_unlock(&bacheca_mutex);
            send_message(sockfd, MSG_OK, "Messaggio postato", strlen("Messaggio postato") + 1);
            break;
        }

        case MSG_SHOW_BACHECA:
        {
            safe_printf("[SERVER] Ricevuto comando per show bacheca\n");
            log_event("[CLIENT] Ricevuto comando show bacheca");
            // invia al client il contenuto attuale della bachca
            pthread_mutex_lock(&bacheca_mutex);
            char csv_buffer[2048] = {0};
            for (int i = 0; i < bacheca.count; i++)
            {
                int pos = (bacheca.front + i) % MAX_BACHECA_MSG;
                // alterna nome e messaggio
                strcat(csv_buffer, bacheca.messages[pos].username);
                strcat(csv_buffer, ",");
                strcat(csv_buffer, bacheca.messages[pos].message);
                if (i < bacheca.count - 1)
                {
                    strcat(csv_buffer, ",");
                }
            }
            send_message(sockfd, MSG_SHOW_BACHECA, csv_buffer, strlen(csv_buffer) + 1);
            pthread_mutex_unlock(&bacheca_mutex);
            break;
        }

        case MSG_PUNTI_FINALI:
        {
            safe_printf("Classifica ricevuta per il client %s: %s\n", g_server.clients[idx].username, data);
            log_event("[CLIENT] Classifica ricetua per  %s: %s", g_server.clients[idx].username, data);
            break;
        }
        default:
        {
            send_message(sockfd, MSG_ERR, "Tipo messaggio sconosciuto", strlen("Tipo messaggio sconosciuto") + 1);
        }
        break;
        }
    }

    // invio finale del punteggio, se non gia' fatto
    if (!g_server.clients[idx].score_sent)
    {
        push_score(g_server.clients[idx].username, g_server.clients[idx].score);
        g_server.clients[idx].score_sent = true;
        log_event("[CLIENT] Punteggio finale inviato per %s", g_server.clients[idx].username);
    }
    // chiusura socket, aggiornamento dello stato del client
    close(sockfd);
    pthread_mutex_lock(&g_server.clients_mutex);
    g_server.clients[idx].connected = false;
    g_server.clients[idx].username[0] = '\0';
    pthread_mutex_unlock(&g_server.clients_mutex);
    log_event("[CLIENT] Client terminato");
    return NULL;
}

// ======================= FUNZIONI PUBBLICHE DEL SERVER =======================
/*
    server_init:
        inizializzazione struttura globale del server:
        - imposta parametri di gioco e disconnessione
        - inizializza client mutex
        - carica dizionario in un trie
        - se specificato, apre il file delle matrici(altrimenti generazione casuale)
        - imposta seed, per numeri pseudocasuali
        - generazione matrice iniziale
        - apertura file di log
        - crea e configura il socket in ascolto
*/

int server_init(
    int port,
    int game_duration_sec,
    int break_time_sec,
    const char *dict_file,
    const char *matrix_file,
    int seed,
    int disconnect_timeout_sec)
{
    signal(SIGPIPE, SIG_IGN);
    // costruttore
    memset(&g_server, 0, sizeof(g_server));
    g_server.port = port;
    g_server.game_duration = game_duration_sec;
    g_server.break_time = break_time_sec;
    g_server.stop = false;
    g_server.game_running = false;
    g_server.seed = seed;

    pthread_mutex_init(&g_server.clients_mutex, NULL);
    pthread_mutex_init(&g_server.registered_mutex, NULL);
    pthread_mutex_init(&g_server.log_mutex, NULL);

    // impostazione timeout per la disconnessione di client inattivi
    g_server.disconnect_timeout = disconnect_timeout_sec;

    // caricamento il dizionario nel trie
    g_server.dictionary = load_dictionary_trie(dict_file);
    if (!g_server.dictionary)
    {
        fprintf(stderr, "ERROR: impossibile caricare il dizionario da %s. \n", dict_file);
        exit(EXIT_FAILURE);
    }

    log_event("[SYSTEM] Dizionario caricato da %s", dict_file);

    // se e' stato specificato un file di matrici, lo apre
    if (matrix_file != NULL)
    {
        g_server.matrix_filename = strdup(matrix_file);
        g_server.matrix_fp = fopen(matrix_file, "r");
        if (g_server.matrix_fp == NULL)
        {
            perror("fopen matrici");
            // se il file non viene aperto, si passa alla generazione casuale
            free(g_server.matrix_filename);
            fprintf(stderr, "ERRORE: File delle matrici non trovato.\n");
            exit(EXIT_FAILURE); // Termina il server
        }
        log_event("[SYSTEM] File matrici aperto: %s", matrix_file);
    }
    else
    {
        // generazione casuale di una matrice, placeholader
        unsigned int effective_seed = (g_server.seed >= 0) ? (unsigned int)g_server.seed : (unsigned int)time(NULL);
        generate_matrix(g_server.matrix, effective_seed);
        log_event("[SYSTEM] Matrice generata casualmente (seed=%u)", effective_seed);
    }

    if (g_server.matrix_fp)
    {
        rewind(g_server.matrix_fp);
    }

    // apertura file di log in modalita' append
    g_server.log_fp = fopen("paroliere.log", "a");
    if (g_server.log_fp == NULL)
    {
        perror("fopen log");
    }
    else
    {
        log_event("[SYSTEM] File di log aperto");
    }

    // creazione del socket in ascolto
    g_server.server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server.server_sockfd < 0)
    {
        perror("socket");
        return -1;
    }

    int opt_value = 1;
    setsockopt(g_server.server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_value, sizeof(opt_value));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_server.server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(g_server.server_sockfd);
        return -1;
    }
    if (listen(g_server.server_sockfd, 8) < 0)
    {
        perror("listen");
        close(g_server.server_sockfd);
        return -1;
    }

    safe_printf("[SERVER] In ascolto sulla porta %d \n", port);
    log_event("[SYSTEM] Server in ascolto sulla porta %d", port);
    return 0;
}

/*
    server_run:
        avvia il thread orchestrator ed entra nel loop di accept per gestire client,
        per ogni nuova connessione:
            - trova uno slot libero nell'array di client
            - registra il nuovo client e crea un thread dedicato
*/
int server_run()
{
    // impostazione gestor di SIGIN per shutdown ordinato
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    log_event("[SYSTEM] Gestore SIGINT impostato");

    // avvio thread orchestrator
    if (pthread_create(&g_server.orchestrator_thread_id, NULL, orchestrator_thread, NULL) != 0)
    {
        perror("pthread_creat orchestrator");
        return -1;
    }
    log_event("[SYSTEM] Thread orchestrator avviato");

    if (pthread_create(&g_server.scorer_thread_id, NULL, scorer_thread, NULL) != 0)
    {
        perror("pthread_create scorer");
        return -1;
    }

    log_event("[SYSTEM] Thread scorer avviato");

    // loop di accept per le connessioni client
    while (!g_server.stop)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int newsock = accept(g_server.server_sockfd, (struct sockaddr *)&client_addr, &client_len);
        if (newsock < 0)
        {
            if (g_server.stop)
            {
                break;
            }
            perror("accept");
            continue;
        }
        log_event("[ACCEPT] Nuova connessione accettata");
        safe_printf("[SERVER] nuovo client connesso \n");

        char welcome[256];
        snprintf(welcome, sizeof(welcome), "Benvenuto sul server %s", g_server.server_name);

        // cerca uno slot libero
        pthread_mutex_lock(&g_server.clients_mutex);
        int idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!g_server.clients[i].connected)
            {
                idx = i;
                break;
            }
        }
        if (idx < 0)
        {
            pthread_mutex_unlock(&g_server.clients_mutex);
            send_message(newsock, MSG_ERR, "Server pieno", strlen("Server pieno") + 1);
            close(newsock);
            log_event("[ACCEPT] Connessione rifiutata: server pieno");
            continue;
        }
        g_server.clients[idx].sockfd = newsock;
        g_server.clients[idx].connected = true;
        g_server.clients[idx].score = 0;
        g_server.clients[idx].used_words_count = 0;
        g_server.clients[idx].username[0] = '\0';
        pthread_mutex_unlock(&g_server.clients_mutex);

        // creazione thread per gestire il client, passando l'indice come argomento
        int *arg = malloc(sizeof(int));
        *arg = idx;
        if (pthread_create(&g_server.clients[idx].thread_id, NULL, client_thread, arg) != 0)
        {
            perror("pthread_create client");
            free(arg);
            pthread_mutex_lock(&g_server.clients_mutex);
            g_server.clients[idx].connected = false;
            pthread_mutex_unlock(&g_server.clients_mutex);
            close(newsock);
            log_event("[ACCEPT] Errore creazione thread per client in slot %d", idx);
        }
        else
        {
            log_event("[ACCEPT] Thread per client in slot %d avviato", idx);
        }
    }

    safe_printf("[SERVER] Uscita dal loop di accept \n");
    log_event("[SYSTEM] Server: uscita dal loop di accept");
    return 0;
}

/*
    server_shutdown:
        esegue lo shutdown ordinato del server:
            + imposta stop flag
            + invia MSG_SERVER_SHUTDWON a tutti client
            + chiude il socket in ascolto
            + attende la termionazione del thread orch.
            + libera risorse
*/

void server_shutdown()
{
    static bool shutdown_in_progress = false;
    if (shutdown_in_progress)
    {
        return;
    }

    shutdown_in_progress = true;

    g_server.stop = true;
    safe_printf("[SERVER] avvio shutdown... \n");
    log_event("[SYSTEM] Avvio shutdown");

    // Risveglia tutti i thread bloccati sui condition variable:
    pthread_cond_broadcast(&score_queue_cond);
    pthread_cond_broadcast(&ranking_cond);

    // invio shutdown a tutti i client
    broadcast_server_shutdown();

    // interruzione accept() sul socket in ascolto
    close(g_server.server_sockfd);
    log_event("[SYSTEM] Socket in ascolto chiuso");

    // cancella e unisci il thread orchestrator
    pthread_cancel(g_server.orchestrator_thread_id);
    pthread_join(g_server.orchestrator_thread_id, NULL);
    log_event("[SYSTEM] Thread orchestrator terminato");

    if (g_server.dictionary)
    {
        trie_free((trie_node *)g_server.dictionary);
        g_server.dictionary = NULL;
    }

    // cancella e unisce i thread client
    pthread_mutex_lock(&g_server.clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_server.clients[i].connected)
        {
            pthread_cancel(g_server.clients[i].thread_id);
            pthread_join(g_server.clients[i].thread_id, NULL);
            log_event("[SYSTEM] Thread client in slot %d terminato", i);
        }
    }
    pthread_mutex_unlock(&g_server.clients_mutex);

    pthread_cancel(g_server.scorer_thread_id);
    pthread_join(g_server.scorer_thread_id, NULL);
    log_event("[SYSTEM] Thread scorer terminato");

    safe_printf("[SERVER] Shutdown completato.\n");
    log_event("[SYSTEM] Shutdown completato");

    pthread_mutex_destroy(&g_server.clients_mutex);
    pthread_mutex_destroy(&g_server.log_mutex);
    pthread_mutex_destroy(&g_server.registered_mutex);
    pthread_mutex_destroy(&score_queue_mutex);
    pthread_mutex_destroy(&bacheca_mutex);
    pthread_mutex_destroy(&ranking_mutex);
    pthread_cond_destroy(&score_queue_cond);
    pthread_cond_destroy(&ranking_cond);

    if (g_server.log_fp)
    {
        fclose(g_server.log_fp);
        g_server.log_fp = NULL;
    }

    if (g_server.matrix_fp)
        fclose(g_server.matrix_fp);
    if (g_server.matrix_filename)
        free(g_server.matrix_filename);
}

void server_set_name(const char *name)
{
    // copia il nome del server nella struttura globale
    strncpy(g_server.server_name, name, sizeof(g_server.server_name) - 1);
    g_server.server_name[sizeof(g_server.server_name) - 1] = '\0';
    log_event("[SYSTEM] Nome server impostato a: %s", g_server.server_name);
}