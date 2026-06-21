#include <string.h>
#include "tokenize_str.h"

int tokenize_str(char *str, char *delim, int max_tokens, char **tokens)
{
    int i = 0;

    if (str == NULL || delim == NULL || tokens == NULL || max_tokens <= 0)
        return 0;

    for (char *token = strtok(str, delim);
         token != NULL && i < max_tokens;
         i++)
    {
        tokens[i] = token;
        token = strtok(NULL, delim);
    }

    return i;
}