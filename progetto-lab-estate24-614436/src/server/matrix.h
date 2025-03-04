#ifndef MATRIX_H
#define MATRIX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>
#include <strings.h>

/*
    generate_matrix:
        Genera una matrice 4x4 di lettere casuali(2 caratteri utili + 1 per il terminatore).
        Si assume che 'matrix' sia un array di 16 stringhe da 3 char ciascuna.
*/
void generate_matrix(char matrix[16][5], unsigned int seed);

/*
    count_letters:
        Conta i caratteri "logici" di una parola, considerando "Qu" come un singolo carattere.
        Si assume che 'word' sia una stringa valida terminata da '\0'.
*/
int count_letters(const char *word);

/*
    is_word_in_matrix:
        Verifica se una data parola può essere formata dalla matrice.
        Si assume che 'matrix' contenga 16 stringhe valide e 'word' sia una stringa valida.
*/
bool is_word_in_matrix(char matrix[16][5], const char *word);

#endif // MATRIX_H
