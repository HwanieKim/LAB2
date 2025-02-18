#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>
#include <strings.h>

// dichiarazioni extern
extern void generate_matrix(char matrix[16][5], unsigned int seed);
extern bool is_word_in_matrix(char matrix[16][5], const char *word);
extern int count_letters(const char *word);

static const char *LETTERS[] = {
    "A", "B", "C", "D", "E", "F", "G", "H",
    "I", "L", "M", "N", "O", "P", "Qu", "R",
    "S", "T", "U", "V", "Z"};

static int LETTERS_COUNT = 21;

// genera una matrice 4x4 di lettere casuali, riempie un array lineare di 16 celle, ognuna fino a 4 char
// generazione randomica di lettere conla funzione rand() e modulo 21

void generate_matrix(char matrix[16][5], unsigned int seed)
{
    srand(seed); // Usa il seed passato dal server
    for (int i = 0; i < 16; i++)
    {
        int idx = rand() % LETTERS_COUNT;
        strncpy(matrix[i], LETTERS[idx], 4);
        matrix[i][4] = '\0';
    }
}

// conta caratteri logici di una parola, qu considerato come 1
// EX) "ciao" -> 4, "ciaoqu" -> 5

int count_letters(const char *word)
{
    int count = 0;
    for (int i = 0; word[i]; i++)
    {
        char c = (char)toupper((unsigned char)word[i]);
        if (c == 'Q')
        {
            char c2 = (char)toupper((unsigned char)word[i + 1]);
            if (c2 == 'U')
            {
                count++;
                i++; // skip u
                continue;
            }
        }
        count++;
    }
    return count;
}

// convertire la parola in un array di token, facilita la ricerca nel dizionario
static int tokenize_word(const char *word, char tokens[][3])
{
    int tcount = 0;
    int i = 0;
    while (word[i])
    {
        char c = (char)toupper((unsigned char)word[i]);
        if (c == 'Q')
        {
            char c2 = (char)toupper((unsigned char)word[i + 1]);
            if (c2 == 'U')
            {
                strcpy(tokens[tcount], "QU");
                tcount++;
                i += 2; // salta u
                continue;
            }
        }
        tokens[tcount][0] = c;
        tokens[tcount][1] = '\0';
        tcount++;
        i++;
    }
    return tcount;
}

// dfs per cercare tokens[pos] in matrice 4x4, lineare row = r, col =c => r*4+c
// matrix[i]  == una stringa come "a" o "qu" (forzato a upper case)
// visted[], segna se l'indice i e' gia' usato

static bool dfs_find(char matrix[16][5], char tokens[][3], int pos, int total, int index, bool visted[16])
{
    if (pos == total)
        return true; // parola terminata con successo

    // matrix[index] deve matchare con tokens[pos]
    if (strcasecmp(matrix[index], tokens[pos]) != 0)
    {
        return false;
    }

    // visitato
    visted[index] = true;

    // controllo ultimo token
    if (pos == total - 1)
    {
        visted[index] = false;
        return true;
    }

    // coordinate
    int row = index / 4;
    int col = index % 4;

    // offset 8 direzioni
    static int dr[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    static int dc[8] = {-1, 0, 1, -1, 1, -1, 0, 1};

    bool found = false;
    for (int i = 0; i < 8 && !found; i++)
    {
        int rr = row + dr[i];
        int cc = col + dc[i];
        if (rr < 0 || rr >= 4 || cc < 0 || cc >= 4) // esclude fuori dalla matrice
            continue;
        int new_index = rr * 4 + cc;
        if (!visted[new_index])
        {
            if (dfs_find(matrix, tokens, pos + 1, total, new_index, visted))
            {
                found = true;
            }
        }
    }

    // smarcare
    visted[index] = false;
    return found;
}

// is_word_in_matrix
//  tokenizza la parola,
//  verifica almneno 4 token di lunghezza per validita' minima
//  inizia la ricerca con dfs_find per controllare se la parola si puo' comporre (anche in diagonale), senza riuso di celle

bool is_word_in_matrix(char matrix[16][5], const char *word)
{
    char tokens[32][3];
    memset(tokens, 0, sizeof(tokens));
    int tcount = tokenize_word(word, tokens);

    // controlla almeno 4 lettere
    if (tcount < 4)
    {
        return false;
    }
    bool visted[16] = {false};

    // dfs partendo da ognuno dei 16 slot
    for (int i = 0; i < 16; i++)
    {
        // reset visted
        for (int j = 0; j < 16; j++)
        {
            visted[j] = false;
        }
        if (dfs_find(matrix, tokens, 0, tcount, i, visted))
        {
            return true; // parola trovata
        }
    }
    return false;
}