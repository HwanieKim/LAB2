#include "dictionary.h"
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
    if (word == NULL)
    {
        printf("Errore: la parola non può essere NULL.\n");
        return;
    }
    int word_len = strlen(word);
    if (word_len == 0 || word_len > 255)
    {
        printf("Errore: la lunghezza della parola deve essere compresa tra 1 e 255 caratteri.\n");
        return;
    }
    // TODO: Stessi controlli di prima, world_len, word != NULL, ecc.
    trie_node *curr = root;
    for (int i = 0; word[i]; i++)
    {
        char c = (char)tolower((unsigned char)word[i]);
        // se trovia una q, verifichiamo sia seguito da u
        if (c == 'q')
        {
            if (word[i + 1] == '\0' || tolower(word[i + 1]) != 'u')
            {
                // se c'e' una 'q' non seguita da 'u', ignoriamo
                continue;
            }

            // qu come unico token, indice 26
            int index = 26;
            if (!curr->children[index])
            {
                curr->children[index] = trie_create_node();
            }
            curr = curr->children[index];
            i++; // salta 'u'
            continue;
        }
        // ignora caratteri non alfabetici
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
    if (word == NULL)
    {
        printf("Errore: la parola non può essere NULL.\n");
        return false;
    }

    int word_len = strlen(word);
    if (word_len == 0 || word_len > 255)
    {
        printf("Errore: la lunghezza della parola deve essere compresa tra 1 e 255 caratteri.\n");
        return false;
    }
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
            i++; // salta 'u'
            continue;
        }
        if (c < 'a' || c > 'z')
            continue;
        int index = c - 'a';
        if (!curr->children[index])
            return false;
        curr = curr->children[index];
    }
    // se la posizione e' valida ed end_of_word == true, allora esiste
    return (curr && curr->end_of_word);
}

// trie_free
// dealloca ricorsivamente
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

void *load_dictionary_trie(const char *filename)
{
    if (filename == NULL)
    {
        printf("Errore: il nome del file del dizionario non può essere NULL.\n");
        return NULL;
    }

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
        // rimuove eventuale newline
        char *p = strchr(buffer, '\n');
        if (p)
        {
            *p = '\0';
        }
        // se la riga e' vuota
        if (strlen(buffer) == 0)
        {
            continue;
        }
        trie_insert(root, buffer);
    }
    fclose(fp);
    return (void *)root;
}