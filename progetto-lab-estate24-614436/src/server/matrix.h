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
        genera una matrice 4x4 di lettere casuali, riempie un array lineare di 16 celle, ognuna fino a 4 char
        generazione randomica di lettere conla funzione rand() e modulo LETTERS_COUNT

*/
void generate_matrix(char matrix[16][5], unsigned int seed);

/*
    count_letters:
        conta caratteri logici di una parola, qu considerato come 1
        EX) "ciao" -> 4, "ciaoqu" -> 5
*/
bool is_word_in_matrix(char matrix[16][5], const char *word);

/*
    tokenize_word:
        convertire la parola in un array di token, dove 'qu' viene trattato come unico token
        tokens[pos] e' al massimo 2 caratteri piu' il terminatore
        restituisce il numero di token estratti

*/
int count_letters(const char *word);

#endif // MATRIX_H
