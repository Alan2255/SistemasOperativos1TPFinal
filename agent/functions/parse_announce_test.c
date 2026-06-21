#include <stdio.h>
#include <string.h> 
#include "../agent.h"
#include "parse_announce.h"

int main() {
    char buf[] = "ANNOUNCE 8100 cpu:4 mem:8192 gpu:1";
    printf("------ %s -------\n", buf);

    char port[PORTSTRLEN];
    struct resource_node resources[3];
    int res_count = 0;
    int ret = parse_announce(buf, port, resources, &res_count);

    if (ret != 0) {
        printf("Error parse_announce\n");
        return -1;
    }

    if (strcmp(port, "8100") != 0) {
        printf("Error parse port: expected 8100 got %s\n", port);
        return -1;
    }

    if (res_count != 3) {
        printf("Error parse res_count expected 3 got %d\n", res_count);
        return -1;
    }

    printf("resources parsed:\n");
    for (int i = 0; i < res_count; i++)
        printf("  [%d] name=%s amount=%d\n", i, 
                resources[i].name,
                resources[i].amount);

    if (resources[0].amount != 4 || resources[1].amount != 8192 
                                 || resources[2].amount != 1) {
        printf("Error resource values mismatch\n");
        return -1;
    }

    return 0;
}