/*
dictionary.c
    gestione dizionario tramite struttura trie
    ogni riga del file contiene una parola (terminata da newline)
    ricerca case-insensitive
*/
//======================= include =======================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// ======================= define =======================
#define ALPHABET_SIZE 27 // aggiunta per un indice per 'qu'

// ======================= struttura trie =======================
typedef struct trie_node
{
    bool end_of_word;
    struct trie_node *children[ALPHABET_SIZE];
} trie_node;

// ======================= dichiarazioni extern =======================
extern trie_node *trie_create_node(void);
extern void trie_insert(trie_node *root, const char *word);
extern bool trie_search(trie_node *root, const char *word);
extern void *load_dictionary_trie(const char *filename);

//======================= creazione nuovo nodo trie =======================
trie_node *trie_create_node(void)
{
    trie_node *node = (trie_node *)malloc(sizeof(trie_node));
    if (node)
    {
        node->end_of_word = false;
        for (int i = 0; i < ALPHABET_SIZE; i++)
        {
            node->children[i] = NULL;
        }
    }
    return node;
}

// ======================= inserzione parola nel trie =======================
void trie_insert(trie_node *root, const char *word)
{
    trie_node *curr = root;
    for (int i = 0; word[i]; i++)
    {
        char c = (char)tolower((unsigned char)word[i]);
        // se trovia una q, verifichiamo sia seguito da u
        if (c == 'q')
        {
            if (word[i + 1] == '\0' || tolower(word[i + 1]) != 'u')
            {
                continue;
            }
            // qu come unico token, indice 26

            int index = 26;
            if (!curr->children[index])
            {
                curr->children[index] = trie_create_node();
            }
            curr = curr->children[index];
            i++; // salta u
            continue;
        }
        if (c < 'a' || c > 'z')
            continue;

        int index = c - 'a';

        if (!curr->children[index])
        {
            curr->children[index] = trie_create_node();
        }
        curr = curr->children[index];
    }
    curr->end_of_word = true;
}

//======================= ricerca =======================
bool trie_search(trie_node *root, const char *word)
{
    trie_node *curr = root;
    for (int i = 0; word[i]; i++)
    {
        char c = (char)tolower((unsigned char)word[i]);

        if (c == 'q')
        {
            if (word[i + 1] == '\0' || tolower(word[i + 1]) != 'u')
            {
                return false;
            }
            int index = 26;
            if (!curr->children[index])
                return false;
            curr = curr->children[index];
            i++;
            continue;
        }
        if (c < 'a' || c > 'z')
            continue;
        int index = c - 'a';
        if (!curr->children[index])
            return false;
        curr = curr->children[index];
    }
    return (curr && curr->end_of_word);
}

void trie_free(trie_node *node)
{
    if (!node)
        return;
    for (int i = 0; i < ALPHABET_SIZE; i++)
    {
        trie_free(node->children[i]);
    }
    free(node);
}
// ======================= caricamento dizionario =======================
/*
    load_dictionary_trie:
        carica un file di dizionario e inserisce le parole nel trie
        ritorna il puntatore al nodo radice del trie;
*/

void *load_dictionary_trie(const char *filename)
{

    FILE *fp = fopen(filename, "r");
    if (!fp)
    {
        perror("fopen dizionario");
        return NULL;
    }
    trie_node *root = trie_create_node();
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp))
    {
        char *p = strchr(buffer, '\n');
        if (p)
        {
            *p = '\0';
        }
        if (strlen(buffer) == 0)
        {
            continue;
        }
        trie_insert(root, buffer);
    }
    fclose(fp);
    return (void *)root;
}