/*
dictionary.h
    gestione dizionario tramite struttura trie
    ogni riga del file contiene una parola (terminata da newline)
    ricerca case-insensitive
*/

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define ALPHABET_SIZE 27 // aggiunta per un indice per 'qu'

typedef struct trie_node
{
    bool end_of_word;
    struct trie_node *children[ALPHABET_SIZE];
} trie_node;

/*
    load_dictionary_trie:
        Carica un dizionario da file in una struttura trie.
        Si assume che 'filename' sia una stringa valida e che il file sia formattato correttamente.
*/
void *load_dictionary_trie(const char *filename);

/*
    trie_free:
        Libera ricorsivamente la memoria allocata per il trie.
        Si assume che 'node' sia un puntatore valido o NULL.
*/
void trie_free(trie_node *node);

/*
    trie_search:
        Ricerca una parola nel trie in modo case-insensitive.
        Si assume che 'root' sia un puntatore valido e 'word' sia una stringa valida.
*/
bool trie_search(trie_node *root, const char *word);

#endif // DICTIONARY_H
