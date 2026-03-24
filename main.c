#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "ticket_system.h"

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s <tickets> <threads> [rows_to_show]\n\n"
        "Exemplos:\n"
        "  %s 10 1000 20\n"
        "  %s 10 10000 20\n",
        prog, prog, prog
    );
}

static int read_positive_int_prompt(const char *label, int *out) {
    char buffer[128];
    char *end = NULL;
    long value;
    int attempts = 0;

    for (;;) {
        if (attempts > 10) {
            fprintf(stderr, "Muitas tentativas falhadas.\n");
            return 0;
        }

        printf("%s", label);
        fflush(stdout);

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            fprintf(stderr, "Erro ao ler entrada (EOF ou erro).\n");
            return 0;
        }

        errno = 0;
        value = strtol(buffer, &end, 10);
        if (errno != 0) {
            fprintf(stderr, "Erro de conversão: %s\n", strerror(errno));
            attempts++;
            continue;
        }

        if (end == buffer) {
            printf("Nenhum dígito encontrado. Digite um número e pressione Enter.\n");
            attempts++;
            continue;
        }

        while (*end == ' ' || *end == '\t') end++;
        if (*end != '\n' && *end != '\0') {
            printf("Caracteres inválidos após número. Digite apenas um inteiro.\n");
            attempts++;
            continue;
        }

        if (value <= 0 || value > 1000000000L) {
            printf("Digite um inteiro positivo (1 até 1000000000).\n");
            attempts++;
            continue;
        }

        *out = (int)value;
        printf("  -> Lido: %d\n", (int)value);
        return 1;
    }
}

int main(int argc, char **argv) {
    sim_config_t cfg;

    if (argc == 1) {
        printf("\nTicket Race Simulator (modo interativo)\n");
        printf("----------------------------------------\n");

        if (!read_positive_int_prompt("Quantidade de tickets: ", &cfg.tickets)) {
            fprintf(stderr, "Falha ao ler tickets.\n");
            return 1;
        }

        if (!read_positive_int_prompt("Quantidade de threads/compradores: ", &cfg.threads)) {
            fprintf(stderr, "Falha ao ler threads.\n");
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
        fprintf(stderr, "Configuração inválida.\n");
        return 1;
    }

    return run_simulation(&cfg);
}