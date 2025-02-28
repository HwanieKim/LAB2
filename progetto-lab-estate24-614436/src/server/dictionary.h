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

void *load_dictionary_trie(const char *filename);
void trie_free(trie_node *node);
bool trie_search(trie_node *root, const char *word);

#endif // DICTIONARY_H
