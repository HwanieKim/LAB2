/*
server_main.c

funzione main del sever "Paroliere"

sintassi: Sintassi:
 *   ./paroliere_srv nome_server porta_server [--matrici data_filename]
 *                   [--durata durata_in_minuti] [--seed rnd_seed]
 *                   [--diz dizionario] [--disconnetti-dopo minuti]
 *
 * Le scelte:
 *  - Il parametro "nome_server" viene fornito ma non usato (il server ascolta su tutte le interfacce).
 *  - Se --matrici viene specificato, il server caricherà le matrici da file; altrimenti le genera casualmente.
 *  - --seed permette di impostare il seed per la generazione pseudocasuale.
 *  - --disconnetti-dopo indica il tempo di inattività dopo il quale il client viene espulso.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

// dichiarazioni extern
extern int server_init(
    int port,
    int game_duration_sec,
    int break_time_sec,
    const char *dict_filename,
    const char *matrix_filename,
    int seed,
    int disconnect_after_sec);
extern int server_run();
extern void server_shutdown();
extern void server_set_name(const char *name);

int main(int argc, char *argv[])
{
    // verfica presenza parametri obbligatori : nome_server e porta
    if (argc < 3)
    {
        fprintf(stderr, "Uso corretto: %s <nome_server> <porta> [--matrici file] [--durata minuti] [--seed rnd_seed] [--diz dizionario] [--disconnetti-dopo minuti]\n",
                argv[0]);
        return 1;
    }

    server_set_name(argv[1]);

    char *endptr;
    long port = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || port < 1025 || port > 65535)
    {
        fprintf(stderr, "Porta non valida: %s. Usa un valore tra 1024 e 65535.\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    // valori default
    int durata_min = 3;                           // durata partita di default : 3 minuti
    int break_min = 1;                            // pausa tra partite : 1 minuto fisso
    const char *dict_filename = "dictionary.txt"; // dizionario di default
    const char *matrix_filename = NULL;           // se non viene fornita, matrice generata casualmente
    int seed = -1;                                // se -1, si usa time(NULL) come seed
    int disconnect_min = 3;                       // timeout inattivita' di default : 3 minuti

    // parsint parametri
    int opt;
    int option_index = 0;
    static struct option long_options[] = {
        {"matrici", required_argument, 0, 'm'},
        {"durata", required_argument, 0, 'd'},
        {"seed", required_argument, 0, 's'},
        {"diz", required_argument, 0, 'z'},
        {"disconnetti-dopo", required_argument, 0, 't'},
        {0, 0, 0, 0}};

    while ((opt = getopt_long(argc, argv, "m:d:s:z:x:t:", long_options, &option_index)) != -1)
    {
        switch (opt)
        {
        case 'm':
            matrix_filename = optarg;
            break;
        case 'd':
            durata_min = atoi(optarg);
            if (durata_min <= 0)
            {
                fprintf(stderr, "[ERROR] Valore della durata deve essere maggiore di 0\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 's':
            seed = atoi(optarg);
            break;
        case 'z':
            dict_filename = optarg;
            break;
        case 't':
            disconnect_min = atoi(optarg);
            if (disconnect_min <= 0)
            {
                fprintf(stderr, "[ERROR] Valore di timeout di disconnessione deve essere maggiore di 0\n");
                exit(EXIT_FAILURE);
            }
            break;
        default:
            fprintf(stderr, "Uso corretto: %s <nome_server> <porta> [--matrici file] [--durata minuti] [--seed rnd_seed] [--diz dizionario] [--disconnetti-dopo minuti]\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    int game_duration_sec = durata_min * 60;
    int break_time_sec = break_min * 60;
    int disconnect_after_sec = disconnect_min * 60;

    // inizializzazione server
    if (server_init(port, game_duration_sec, break_time_sec, dict_filename, matrix_filename, seed, disconnect_after_sec) < 0)
    {
        fprintf(stderr, "Errore inizializzazione server\n");
        return 1;
    }

    // avvio ciclo di accettazione
    if (server_run() < 0)
    {
        fprintf(stderr, "Errore avvio server\n");
    }

    // shutdown server
    server_shutdown();

    return 0;
}