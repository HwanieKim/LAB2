#ifndef SERVER_H
#define SERVER_H

#define _GNU_SOURCE // soluzione per errore implicit declaration of signal.h

#include "common/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>

#define MAX_CLIENTS 32
#define USERNAME_LEN 32
#define MAX_WORDS_USED 256
#define MAX_BACHECA_MSG 8
#define MAX_REGISTERED_USERS 1000
#define MAX_SCORE_MSG MAX_CLIENTS

// ======================= API server =======================

int server_init(
    int port,
    int game_duration_sec,
    int break_time_sec,
    const char *dict_filename,
    const char *matrix_filename,
    int seed,
    int disconnect_after_sec);
int server_run();
void server_shutdown();
void server_set_name(const char *name);

typedef struct trie_node trie_node;
void *load_dictionary_trie(const char *filename);
bool trie_search(trie_node *root, const char *word);
void trie_free(trie_node *node);

void generate_matrix(char matrix[16][5], unsigned int seed);
bool is_word_in_matrix(char matrix[16][5], const char *word);
int count_letters(const char *word);

// ======================= strutture dati =======================

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

    bool in_game;
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

// coda dei punteggi: conterra' i punteggi finali inviati dai client
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
    int server_sockfd; // socket di ascolto per nuove connesioni
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
    // trhead scorer per gestione classifica
    pthread_t scorer_thread_id;

    int disconnect_timeout; // timeout per inattivita' del client (sec)

    // se specificato, file contenente matrice di gioco
    char *matrix_filename;
    FILE *matrix_fp;

    // log file e mutex dedicato
    FILE *log_fp;
    pthread_mutex_t log_mutex;

    // server name
    char server_name[128];

    // utenti registrati
    RegisteredUser registered_users[MAX_REGISTERED_USERS];
    int registered_count;
    pthread_mutex_t registered_mutex;
} server_paroliere;

#endif // SERVER_H
