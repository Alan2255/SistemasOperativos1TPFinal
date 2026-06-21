#ifndef PARSE_ANNOUNCE_H
#define PARSE_ANNOUNCE_H

#include "../structures/table_agent.h"

int parse_announce(char *msg, char *port, 
            Resource *resources, int *res_count);

#endif /* PARSE_ANNOUNCE_H */