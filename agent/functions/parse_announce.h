#ifndef PARSE_ANNOUNCE_H
#define PARSE_ANNOUNCE_H

int parse_announce(char *msg, char *port, 
            struct resource_node *resources, int *res_count);

#endif /* PARSE_ANNOUNCE_H */