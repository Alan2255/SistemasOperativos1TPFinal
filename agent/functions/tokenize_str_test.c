#include <stdio.h>
#include "tokenize_str.h"

int main(void)
{
    char msg[] = "uno,dos,tres,cuatro";
    char *tokens[10];

    int count_tokens = tokenize_str(msg, ",", 10, tokens);

    for (int i = 0; i < count_tokens; i++)
        printf("Token %d: %s\n", i, tokens[i]);

    return 0;
}