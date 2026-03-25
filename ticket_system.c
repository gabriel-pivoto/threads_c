#define _POSIX_C_SOURCE 200809L
#include "ticket_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <unistd.h>

#define CLR_RESET   "\x1b[0m"
#define CLR_RED     "\x1b[31m"
#define CLR_GREEN   "\x1b[32m"
#define CLR_YELLOW  "\x1b[33m"
#define CLR_BLUE    "\x1b[34m"
#define CLR_MAGENTA "\x1b[35m"
#define CLR_CYAN    "\x1b[36m"
#define CLR_GRAY    "\x1b[90m"
#define CLR_BOLD    "\x1b[1m"

#define STOCK_NOT_AVAILABLE (-999999)
#define MAX_EVENTS 16

/*
 * Local educational signature seed only.
 * This is NOT cryptography and must not be used in real systems.
 */
#define SECRET_KEY "LAB_ONLY_SECRET_DO_NOT_USE_IN_REAL_SYSTEMS"

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int total;
    int arrived;
    int generation;
} simple_barrier_t;

typedef struct {
    long timestamp_ms;
    char text[128];
} event_t;

typedef struct {
    int id;
    atomic_int exec_state;
    final_result_t final_result;
    int did_check;
    int did_request;
    int did_decrement;
    int did_ticket;
    int did_sign;
    int stock_observed_before;
    long sale_id;
    int stock_after;
    char worker_thread_name[32];
    char receipt[160];
    char signature[32];
} thread_info_t;

typedef struct {
    sim_config_t cfg;

    atomic_int tickets_remaining;
    atomic_int success_count;
    atomic_int fail_count;
    atomic_int done_count;
    atomic_long sale_counter;

    simple_barrier_t start_barrier;

    thread_info_t *infos;
    pthread_t *threads;
    
    pthread_mutex_t render_lock;
    event_t events[MAX_EVENTS];
    int event_head;
} simulation_ctx_t;

typedef struct {
    simulation_ctx_t *ctx;
    thread_info_t *info;
} thread_arg_t;

static void barrier_init(simple_barrier_t *b, int total) {
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->total = total;
    b->arrived = 0;
    b->generation = 0;
}

static void barrier_destroy(simple_barrier_t *b) {
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->cond);
}

static void barrier_wait(simple_barrier_t *b) {
    pthread_mutex_lock(&b->mutex);
    int gen = b->generation;
    b->arrived++;

    if (b->arrived == b->total) {
        b->generation++;
        b->arrived = 0;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (gen == b->generation) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    }

    pthread_mutex_unlock(&b->mutex);
}

static uint64_t fnv1a64_bytes(const unsigned char *data, size_t len) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void toy_sign(const char *message, char out_hex[32]) {
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "%s|%s", SECRET_KEY, message);
    uint64_t sig = fnv1a64_bytes((const unsigned char *)buffer, strlen(buffer));
    snprintf(out_hex, 32, "%016llx", (unsigned long long)sig);
}

static void tiny_delay(unsigned seed, int stage) {
    unsigned mix = seed * 1103515245u + 12345u + (unsigned)(stage * 9973u);
    long us = 500 + (long)(mix % 6000);

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = us * 1000L;
    nanosleep(&ts, NULL);
}

static const char *exec_state_name(exec_state_t st) {
    switch (st) {
        case EXEC_WAITING: return "WAITING";
        case EXEC_CHECKING: return "CHECK";
        case EXEC_REQUESTING: return "REQ";
        case EXEC_DECREMENTING: return "DECR";
        case EXEC_TICKETING: return "TICKET";
        case EXEC_SIGNING: return "SIGN";
        case EXEC_TERMINAL_OK: return "SUCCESS";
        case EXEC_TERMINAL_FAIL: return "FAILED";
        default: return "?";
    }
}

static const char *result_name(final_result_t res) {
    switch (res) {
        case RESULT_IN_PROGRESS: return "RUNNING";
        case RESULT_SUCCESS: return "SUCCESS";
        case RESULT_FAILED: return "FAILED";
        default: return "?";
    }
}

static const char *exec_state_color(exec_state_t st) {
    switch (st) {
        case EXEC_WAITING: return CLR_BLUE;
        case EXEC_CHECKING: return CLR_CYAN;
        case EXEC_REQUESTING: return CLR_YELLOW;
        case EXEC_DECREMENTING: return CLR_RED;
        case EXEC_TICKETING: return CLR_MAGENTA;
        case EXEC_SIGNING: return CLR_BLUE;
        case EXEC_TERMINAL_OK: return CLR_GREEN;
        case EXEC_TERMINAL_FAIL: return CLR_RED;
        default: return CLR_RESET;
    }
}

static const char *result_color(final_result_t res) {
    switch (res) {
        case RESULT_SUCCESS: return CLR_GREEN;
        case RESULT_FAILED: return CLR_RED;
        case RESULT_IN_PROGRESS: return CLR_YELLOW;
        default: return CLR_RESET;
    }
}

static void add_event(simulation_ctx_t *ctx, const char *fmt, ...) {
    pthread_mutex_lock(&ctx->render_lock);
    int idx = ctx->event_head % MAX_EVENTS;
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    strncpy(ctx->events[idx].text, buf, sizeof(ctx->events[idx].text) - 1);
    ctx->events[idx].text[sizeof(ctx->events[idx].text) - 1] = '\0';
    ctx->event_head++;
    pthread_mutex_unlock(&ctx->render_lock);
}

static void issue_receipt(thread_info_t *info, long sale_id, int stock_after) {
    info->sale_id = sale_id;
    info->stock_after = stock_after;

    snprintf(
        info->receipt,
        sizeof(info->receipt),
        "buyer=T%05d;sale=%06ld;stock_after=%d",
        info->id,
        sale_id,
        stock_after
    );

    toy_sign(info->receipt, info->signature);
}

static void *buyer_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    simulation_ctx_t *ctx = targ->ctx;
    thread_info_t *info = targ->info;

    snprintf(
        info->worker_thread_name,
        sizeof(info->worker_thread_name),
        "T%05d",
        info->id
    );

    atomic_store(&info->exec_state, (int)EXEC_WAITING);
    info->final_result = RESULT_IN_PROGRESS;
    barrier_wait(&ctx->start_barrier);

    atomic_store(&info->exec_state, (int)EXEC_CHECKING);
    info->did_check = 1;
    tiny_delay((unsigned)info->id, 1);

    /*
     * INTENTIONAL RACE (didactic): stale snapshot / split invariant.
     *
     * Step 1: thread reads shared stock into local snapshot.
     * Step 2: after delay, thread decides based on stale value.
     * Step 3: accepted threads decrement shared stock in a later operation.
     *
     * Because there is no end-to-end synchronization covering check+decrement,
     * many threads can observe snapshot > 0 and still issue concurrent sales,
     * producing oversell, negative stock, and inconsistent final state.
     */
    int snapshot = atomic_load(&ctx->tickets_remaining);
    info->stock_observed_before = snapshot;
    add_event(ctx, "%s saw stock %d", info->worker_thread_name, snapshot);

    atomic_store(&info->exec_state, (int)EXEC_REQUESTING);
    info->did_request = 1;
    tiny_delay((unsigned)info->id, 2);

    if (snapshot > 0) {
        atomic_store(&info->exec_state, (int)EXEC_DECREMENTING);
        tiny_delay((unsigned)info->id, 3);
        int stock_after = STOCK_NOT_AVAILABLE;
        int expected = atomic_load(&ctx->tickets_remaining);
        int reserved = 0;

        while (expected > 0) {
            if (atomic_compare_exchange_weak(&ctx->tickets_remaining, &expected, expected - 1)) {
                reserved = 1;
                stock_after = expected - 1;
                break;
            }
        }

        if (reserved) {
            info->did_decrement = 1;
            add_event(ctx, "%s decremented to %d", info->worker_thread_name, stock_after);

            atomic_store(&info->exec_state, (int)EXEC_TICKETING);
            tiny_delay((unsigned)info->id, 3);

            long sale_id = atomic_fetch_add(&ctx->sale_counter, 1) + 1;
            info->did_ticket = 1;

            atomic_store(&info->exec_state, (int)EXEC_SIGNING);
            tiny_delay((unsigned)info->id, 4);

            info->did_sign = 1;
            issue_receipt(info, sale_id, stock_after);
            add_event(ctx, "%s issued receipt", info->worker_thread_name);

            atomic_store(&info->exec_state, (int)EXEC_TERMINAL_OK);
            info->final_result = RESULT_SUCCESS;
            atomic_fetch_add(&ctx->success_count, 1);
        } else {
            atomic_store(&info->exec_state, (int)EXEC_TERMINAL_FAIL);
            info->final_result = RESULT_FAILED;
            info->stock_after = STOCK_NOT_AVAILABLE;
            add_event(ctx, "%s failed: stock exhausted", info->worker_thread_name);
            atomic_fetch_add(&ctx->fail_count, 1);
        }
    } else {
        atomic_store(&info->exec_state, (int)EXEC_TERMINAL_FAIL);
        info->final_result = RESULT_FAILED;
        info->stock_after = STOCK_NOT_AVAILABLE;
        add_event(ctx, "%s failed: stock exhausted", info->worker_thread_name);
        atomic_fetch_add(&ctx->fail_count, 1);
    }

    atomic_fetch_add(&ctx->done_count, 1);
    return NULL;
}

static void render_progress_bar(int done, int total, int width) {
    int filled = (done * width) / total;
    printf("[");
    for (int i = 0; i < filled; i++) printf("=");
    for (int i = filled; i < width; i++) printf("-");
    printf("] %3d%%", (done * 100) / total);
}

static void render_dashboard(simulation_ctx_t *ctx, int final_frame) {
    pthread_mutex_lock(&ctx->render_lock);

    int remaining = atomic_load(&ctx->tickets_remaining);
    int sold = atomic_load(&ctx->success_count);
    int failed = atomic_load(&ctx->fail_count);
    int done = atomic_load(&ctx->done_count);
    int oversold = sold > ctx->cfg.tickets ? sold - ctx->cfg.tickets : 0;

    printf("\x1b[H\x1b[J");
    printf(CLR_BOLD "=== Ticket Race Simulator (Flawed Concurrent) ===" CLR_RESET "\n\n");

    printf("Progress: ");
    render_progress_bar(done, ctx->cfg.threads, 30);
    printf("\n");
    printf("Tickets: %d initial | Stock: %d | Sold: %d | Failed: %d | Oversold: %d\n\n",
           ctx->cfg.tickets, remaining, sold, failed, oversold);

    if (!final_frame) {
        printf(CLR_BOLD "THREADS (showing %d of %d):" CLR_RESET "\n", 
               ctx->cfg.rows_to_show < ctx->cfg.threads ? ctx->cfg.rows_to_show : ctx->cfg.threads,
               ctx->cfg.threads);
        printf("%-10s %-8s %-9s %-10s %-8s\n", "Thread", "ExecState", "Result", "Sale#", "Stock");
        printf("%s\n", "---------------------------------------------");

        int limit = ctx->cfg.rows_to_show < ctx->cfg.threads ? ctx->cfg.rows_to_show : ctx->cfg.threads;
        for (int i = 0; i < limit; i++) {
            thread_info_t *info = &ctx->infos[i];
            exec_state_t st = (exec_state_t)atomic_load(&info->exec_state);
            const char *st_color = exec_state_color(st);
            const char *res_color = result_color(info->final_result);
            char sale_str[16] = "-";
            char stock_str[16] = "-";
            
            if (info->sale_id > 0) snprintf(sale_str, sizeof(sale_str), "%ld", info->sale_id);
            if (info->stock_observed_before != STOCK_NOT_AVAILABLE) {
                snprintf(stock_str, sizeof(stock_str), "%d", info->stock_after);
            }

            printf("%s%-10s%s %s%-8s%s %s%-9s%s %10s %8s\n",
                   CLR_BOLD, info->worker_thread_name, CLR_RESET,
                   st_color, exec_state_name(st), CLR_RESET,
                   res_color, result_name(info->final_result), CLR_RESET,
                   sale_str, stock_str);
        }

        printf("\n" CLR_BOLD "Recent events:" CLR_RESET "\n");
        int start = (ctx->event_head >= MAX_EVENTS) ? (ctx->event_head - MAX_EVENTS) : 0;
        for (int i = start; i < ctx->event_head; i++) {
            int idx = i % MAX_EVENTS;
            if (ctx->events[idx].text[0]) {
                printf("  %s\n", ctx->events[idx].text);
            }
        }
    }

    if (final_frame) {
        printf("\n" CLR_BOLD "=== FINAL REPORT ===" CLR_RESET "\n");
        printf("Buyers:           %d\n", ctx->cfg.threads);
        printf("Successful:       %d\n", sold);
        printf("Failed/Rejected:  %d\n", failed);
        printf("Oversold:         %d\n", oversold);
        printf("Remaining stock:  %d\n", remaining);

        printf("\n" CLR_BOLD "Invariant Checks:" CLR_RESET "\n");

        int success_actual = 0, fail_actual = 0;
        for (int i = 0; i < ctx->cfg.threads; i++) {
            if (ctx->infos[i].final_result == RESULT_SUCCESS) success_actual++;
            if (ctx->infos[i].final_result == RESULT_FAILED) fail_actual++;
        }

        int inv_success = (success_actual == sold);
        int inv_failed = (fail_actual == failed);
        int inv_done = (done == sold + failed);
        int inv_sigs = 1;
        for (int i = 0; i < ctx->cfg.threads; i++) {
            if (ctx->infos[i].signature[0] && ctx->infos[i].final_result != RESULT_SUCCESS) {
                inv_sigs = 0;
                break;
            }
        }
        int inv_sales = 1;
        for (int i = 0; i < ctx->cfg.threads; i++) {
            if (ctx->infos[i].sale_id > 0 && ctx->infos[i].final_result != RESULT_SUCCESS) {
                inv_sales = 0;
                break;
            }
        }

        printf("  success_count == RESULT_SUCCESS threads: %s\n", inv_success ? CLR_GREEN "PASS" CLR_RESET : CLR_RED "FAIL" CLR_RESET);
        printf("  fail_count == RESULT_FAILED threads:    %s\n", inv_failed ? CLR_GREEN "PASS" CLR_RESET : CLR_RED "FAIL" CLR_RESET);
        printf("  done == sum of SUCCESS+FAILED:          %s\n", inv_done ? CLR_GREEN "PASS" CLR_RESET : CLR_RED "FAIL" CLR_RESET);
        printf("  signatures only for SUCCESS:           %s\n", inv_sigs ? CLR_GREEN "PASS" CLR_RESET : CLR_RED "FAIL" CLR_RESET);
        printf("  sale_ids only for SUCCESS:             %s\n", inv_sales ? CLR_GREEN "PASS" CLR_RESET : CLR_RED "FAIL" CLR_RESET);

        printf("\n" CLR_BOLD "Resumo final:" CLR_RESET "\n");
        printf(" Threads: %d\n", ctx->cfg.threads);
        printf(" Compras: %d\n", sold);
        printf(" Oversold: %d\n", oversold);
        printf(" Compras falharam: %d\n", failed);
        printf(" Estoque restante: %d\n", remaining);
    }

    fflush(stdout);
    pthread_mutex_unlock(&ctx->render_lock);
}

int show_interactive_menu(sim_config_t *cfg) {
    printf("\n" CLR_BOLD "=== Ticket Race Simulator ===" CLR_RESET "\n");
    printf("Configure simulation parameters:\n\n");
    
    printf("Tickets: ");
    fflush(stdout);
    if (scanf("%d", &cfg->tickets) != 1 || cfg->tickets <= 0) {
        cfg->tickets = 10;
    }
    
    printf("Threads: ");
    fflush(stdout);
    if (scanf("%d", &cfg->threads) != 1 || cfg->threads <= 0) {
        cfg->threads = 10;
    }
    
    printf("Display rows (0=all): ");
    fflush(stdout);
    if (scanf("%d", &cfg->rows_to_show) != 1 || cfg->rows_to_show < 0) {
        cfg->rows_to_show = 0;
    }
    if (cfg->rows_to_show == 0) cfg->rows_to_show = cfg->threads;
    
    printf("\nStarting simulation...\n\n");
    sleep(1);
    return 0;
}

int run_simulation(const sim_config_t *cfg) {
    if (!cfg || cfg->tickets <= 0 || cfg->threads <= 0 || cfg->rows_to_show <= 0) {
        fprintf(stderr, "Configuração inválida.\n");
        return 1;
    }

    simulation_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = *cfg;

    pthread_mutex_init(&ctx.render_lock, NULL);

    atomic_init(&ctx.tickets_remaining, cfg->tickets);
    atomic_init(&ctx.success_count, 0);
    atomic_init(&ctx.fail_count, 0);
    atomic_init(&ctx.done_count, 0);
    atomic_init(&ctx.sale_counter, 0);

    barrier_init(&ctx.start_barrier, cfg->threads);

    ctx.infos = calloc((size_t)cfg->threads, sizeof(thread_info_t));
    ctx.threads = calloc((size_t)cfg->threads, sizeof(pthread_t));
    thread_arg_t *args = calloc((size_t)cfg->threads, sizeof(thread_arg_t));

    if (!ctx.infos || !ctx.threads || !args) {
        fprintf(stderr, "Falha ao alocar memória.\n");
        free(ctx.infos);
        free(ctx.threads);
        free(args);
        barrier_destroy(&ctx.start_barrier);
        pthread_mutex_destroy(&ctx.render_lock);
        return 1;
    }

    for (int i = 0; i < cfg->threads; i++) {
        ctx.infos[i].id = i + 1;
        atomic_init(&ctx.infos[i].exec_state, (int)EXEC_WAITING);
        ctx.infos[i].final_result = RESULT_IN_PROGRESS;
        ctx.infos[i].did_check = 0;
        ctx.infos[i].did_request = 0;
        ctx.infos[i].did_decrement = 0;
        ctx.infos[i].did_ticket = 0;
        ctx.infos[i].did_sign = 0;
        ctx.infos[i].stock_observed_before = STOCK_NOT_AVAILABLE;
        ctx.infos[i].sale_id = 0;
        ctx.infos[i].stock_after = STOCK_NOT_AVAILABLE;
        ctx.infos[i].worker_thread_name[0] = '\0';
        ctx.infos[i].receipt[0] = '\0';
        ctx.infos[i].signature[0] = '\0';

        args[i].ctx = &ctx;
        args[i].info = &ctx.infos[i];
    }

    printf("\x1b[?25l");

    for (int i = 0; i < cfg->threads; i++) {
        if (pthread_create(&ctx.threads[i], NULL, buyer_thread, &args[i]) != 0) {
            fprintf(stderr, "Falha ao criar thread %d.\n", i + 1);
            ctx.cfg.threads = i;
            break;
        }
    }

    while (atomic_load(&ctx.done_count) < ctx.cfg.threads) {
        render_dashboard(&ctx, 0);

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 80000000L;
        nanosleep(&ts, NULL);
    }

    for (int i = 0; i < ctx.cfg.threads; i++) {
        pthread_join(ctx.threads[i], NULL);
    }

    render_dashboard(&ctx, 1);
    printf("\x1b[?25h");

    free(ctx.infos);
    free(ctx.threads);
    free(args);
    barrier_destroy(&ctx.start_barrier);
    pthread_mutex_destroy(&ctx.render_lock);

    return 0;
}