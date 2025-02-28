#include "matrix.h"

static const char *LETTERS[] = {
    "A", "B", "C", "D", "E", "F", "G", "H",
    "I", "L", "M", "N", "O", "P", "Qu", "R",
    "S", "T", "U", "V", "Z"};

static int LETTERS_COUNT = 21;

void generate_matrix(char matrix[16][5], unsigned int seed)
{
    srand(seed); // Usa il seed passato dal server
    for (int i = 0; i < 16; i++)
    {
        int idx = rand() % LETTERS_COUNT;
        strncpy(matrix[i], LETTERS[idx], 4);
        matrix[i][4] = '\0'; // terminazione di sicurezza
    }
}

int count_letters(const char *word)
{
    // TODO: Stessi controlli di prima, world_len, word != NULL, ecc.
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

int tokenize_word(const char *word, char tokens[][3])
{
    // TODO: Stessi controlli di prima, world_len, word != NULL, ecc.
    int tcount = 0;
    int i = 0;
    while (word[i])
    {
        char c = (char)toupper((unsigned char)word[i]);
        // Se c='Q' e c2='U', gestiamo "QU" come un unico token
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
        // altrimenti (c != 'q') prendiamo singolo carattere
        tokens[tcount][0] = c;
        tokens[tcount][1] = '\0';
        tcount++;
        i++;
    }
    return tcount;
}

/*
    dfs_find:
      Cerca ricorsivamente nella matrice, a partire dalla cella 'index',
      se è possibile formare la sequenza tokens[pos..].
        - Se tokens[pos] non coincide con matrix[index], restituisce false.
        - Altrimenti, se pos è l’ultimo token, restituisce true.
        - Altrimenti prova ad andare in tutte le 8 direzioni (verticali, orizzontali, diagonali).
        - Usa l’array visted[] per non riusare la stessa cella più di una volta nella parola.
 */
bool dfs_find(char matrix[16][5], char tokens[][3], int pos, int total, int index, bool visted[16])
{
    // se abbiamo gia' matchato tutti i token (pos == total)
    if (pos == total)
        return true; // parola trovata

    // matrix[index] deve matchare con tokens[pos]. confronto case-insensitive
    if (strcasecmp(matrix[index], tokens[pos]) != 0)
    {
        return false;
    }

    // marca cella visistata
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
        // se la cella adiacente non e' ancora usata
        if (!visted[new_index])
        {
            // ricorsiva sul token successivo
            if (dfs_find(matrix, tokens, pos + 1, total, new_index, visted))
            {
                found = true;
            }
        }
    }

    // rimozione segno sulla cella
    visted[index] = false;
    return found;
}

/*
    is_word_in_matrix:
        Verifica se una data parola è "componibile" dalla matrice di lettere.
        - Tokenizza la parola in modo che 'qu' diventi un singolo token.
        - Richiede che la parola abbia almeno 4 caratteri logici (tcount >= 4).
        - Usa la dfs_find() a partire da ognuna delle 16 celle, finché non trova un match o esaurisce tutte le possibilità.
 */
bool is_word_in_matrix(char matrix[16][5], const char *word)
{
    // TODO: Stessi controlli di prima, world_len, word != NULL, ecc.
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
    // parola non presente in matrice
    return false;
}