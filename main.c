#include <stdio.h>
#include <stdlib.h>

#include "ticket_system.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s <tickets> <threads> [rows_to_show]\n\n"
        "Exemplos:\n"
        "  %s 10 100\n"
        "  %s 10 1000 50\n",
        prog, prog, prog
    );
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return 1;
    }

    sim_config_t cfg;
    cfg.tickets = atoi(argv[1]);
    cfg.threads = atoi(argv[2]);
    cfg.rows_to_show = (argc == 4) ? atoi(argv[3]) : cfg.threads;

    if (cfg.tickets <= 0 || cfg.threads <= 0 || cfg.rows_to_show <= 0) {
        fprintf(stderr, "Configuração inválida.\n");
        return 1;
    }

    return run_simulation(&cfg);
}