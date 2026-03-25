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

static int read_int(const char *prompt, int *out) {
    char buffer[128];
    
    printf("%s", prompt);
    fflush(stdout);
    
    if (!fgets(buffer, sizeof(buffer), stdin)) {
        return 0;
    }
    
    int value = atoi(buffer);
    if (value <= 0) {
        printf("Digite um numero positivo.\n");
        return 0;
    }
    
    *out = value;
    return 1;
}

int main(int argc, char **argv) {
    sim_config_t cfg;
    
    if (argc == 1) {
        printf("\n=== Ticket Race Simulator ===\n");
        printf("Digite os parametros:\n\n");
        
        if (!read_int("Tickets: ", &cfg.tickets)) {
            fprintf(stderr, "Erro ao ler tickets.\n");
            return 1;
        }
        
        if (!read_int("Threads: ", &cfg.threads)) {
            fprintf(stderr, "Erro ao ler threads.\n");
            return 1;
        }
        
        cfg.rows_to_show = cfg.threads;
    } else if (argc >= 3 && argc <= 4) {
        cfg.tickets = atoi(argv[1]);
        cfg.threads = atoi(argv[2]);
        cfg.rows_to_show = (argc == 4) ? atoi(argv[3]) : cfg.threads;
    } else {
        usage(argv[0]);
        return 1;
    }

    if (cfg.tickets <= 0 || cfg.threads <= 0 || cfg.rows_to_show <= 0) {
        fprintf(stderr, "Configuracao invalida.\n");
        return 1;
    }

    return run_simulation(&cfg);
}