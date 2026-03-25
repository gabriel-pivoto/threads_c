#ifndef TICKET_SYSTEM_H
#define TICKET_SYSTEM_H

typedef enum {
    EXEC_WAITING = 0,
    EXEC_CHECKING,
    EXEC_REQUESTING,
    EXEC_DECREMENTING,
    EXEC_TICKETING,
    EXEC_SIGNING,
    EXEC_TERMINAL_OK,
    EXEC_TERMINAL_FAIL
} exec_state_t;

typedef enum {
    RESULT_IN_PROGRESS = 0,
    RESULT_SUCCESS,
    RESULT_FAILED
} final_result_t;

typedef struct {
    int tickets;
    int threads;
    int rows_to_show;
} sim_config_t;

int show_interactive_menu(sim_config_t *cfg);
int run_simulation(const sim_config_t *cfg);

#endif