#ifndef TICKET_SYSTEM_H
#define TICKET_SYSTEM_H

typedef struct {
    int tickets;
    int threads;
    int rows_to_show;
} sim_config_t;

int run_simulation(const sim_config_t *cfg);

#endif