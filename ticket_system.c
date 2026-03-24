#define _POSIX_C_SOURCE 200809L
#include "ticket_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>

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

/*
 * Local educational signature seed only.
 * This is NOT cryptography and must not be used in real systems.
 */
#define SECRET_KEY "LAB_ONLY_SECRET_DO_NOT_USE_IN_REAL_SYSTEMS"

typedef enum {
    ST_CREATED = 0,
    ST_WAITING,
    ST_CHECKING,
    ST_REQUESTING,
    ST_DECREMENTING,
    ST_TICKETING,
    ST_SIGNING,
    ST_DONE_OK,
    ST_DONE_FAIL
} thread_state_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int total;
    int arrived;
    int generation;
} simple_barrier_t;

typedef struct {
    int id;
    atomic_int state;
    int success;
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

static const char *state_name(thread_state_t st) {
    switch (st) {
        case ST_CREATED: return "CREATED";
        case ST_WAITING: return "WAITING";
        case ST_CHECKING: return "CHECKING";
        case ST_REQUESTING: return "REQUESTING";
        case ST_DECREMENTING: return "DECR";
        case ST_TICKETING: return "TICKET";
        case ST_SIGNING: return "SIGNING";
        case ST_DONE_OK: return "SUCCESS";
        case ST_DONE_FAIL: return "FAILED";
        default: return "UNKNOWN";
    }
}

static const char *state_color(thread_state_t st) {
    switch (st) {
        case ST_CREATED: return CLR_GRAY;
        case ST_WAITING: return CLR_BLUE;
        case ST_CHECKING: return CLR_CYAN;
        case ST_REQUESTING: return CLR_YELLOW;
        case ST_DECREMENTING: return CLR_RED;
        case ST_TICKETING: return CLR_MAGENTA;
        case ST_SIGNING: return CLR_BLUE;
        case ST_DONE_OK: return CLR_GREEN;
        case ST_DONE_FAIL: return CLR_RED;
        default: return CLR_RESET;
    }
}

static void set_state(thread_info_t *info, thread_state_t st) {
    atomic_store(&info->state, st);
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

static const char *format_stock_value(int value, char *buffer, size_t buffer_len) {
    if (value == STOCK_NOT_AVAILABLE) {
        return "n/a";
    }
    snprintf(buffer, buffer_len, "%d", value);
    return buffer;
}

static void *buyer_thread(void *arg) {
    thread_arg_t *targ = (thread_arg_t *)arg;
    simulation_ctx_t *ctx = targ->ctx;
    thread_info_t *info = targ->info;

    snprintf(
        info->worker_thread_name,
        sizeof(info->worker_thread_name),
        "thr-%llu",
        (unsigned long long)(uintptr_t)pthread_self()
    );

    set_state(info, ST_WAITING);
    barrier_wait(&ctx->start_barrier);

    set_state(info, ST_CHECKING);
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

    set_state(info, ST_REQUESTING);
    info->did_request = 1;
    tiny_delay((unsigned)info->id, 2);

    if (snapshot > 0) {
        set_state(info, ST_DECREMENTING);
        tiny_delay((unsigned)info->id, 3);

        info->did_decrement = 1;
        int stock_after = atomic_fetch_sub(&ctx->tickets_remaining, 1) - 1;

        set_state(info, ST_TICKETING);
        tiny_delay((unsigned)info->id, 3);

        long sale_id = atomic_fetch_add(&ctx->sale_counter, 1) + 1;
        info->did_ticket = 1;

        set_state(info, ST_SIGNING);
        tiny_delay((unsigned)info->id, 4);

        info->success = 1;
        issue_receipt(info, sale_id, stock_after);
        info->did_sign = 1;
        atomic_fetch_add(&ctx->success_count, 1);
        set_state(info, ST_DONE_OK);
    } else {
        info->success = 0;
        info->stock_after = STOCK_NOT_AVAILABLE;
        atomic_fetch_add(&ctx->fail_count, 1);
        set_state(info, ST_DONE_FAIL);
    }

    atomic_fetch_add(&ctx->done_count, 1);
    return NULL;
}

static void count_states(simulation_ctx_t *ctx, int *out, int n) {
    for (int i = 0; i < n; i++) out[i] = 0;

    for (int i = 0; i < ctx->cfg.threads; i++) {
        int st = atomic_load(&ctx->infos[i].state);
        if (st >= 0 && st < n) out[st]++;
    }
}

static void print_divider(void) {
    printf("+--------+----------+-------+-----+-----+-----+-----+-----+----------+-----------------------------------+------------------+\n");
}

static void render_dashboard(simulation_ctx_t *ctx, int final_frame) {
    int counts[10];
    count_states(ctx, counts, 10);

    int remaining = atomic_load(&ctx->tickets_remaining);
    int sold = atomic_load(&ctx->success_count);
    int failed = atomic_load(&ctx->fail_count);
    int done = atomic_load(&ctx->done_count);
    int oversold = sold > ctx->cfg.tickets ? sold - ctx->cfg.tickets : 0;

    printf("\x1b[H\x1b[J");
    printf(CLR_BOLD "Ticket Race Simulator (intentionally flawed concurrent mode)" CLR_RESET "\n");

    printf("Tickets iniciais: %d | Threads: %d | Restantes: %d | Vendidos: %d | Falhas: %d | Oversold: %d\n",
           ctx->cfg.tickets, ctx->cfg.threads, remaining, sold, failed, oversold);

        printf("Estados -> waiting:%d checking:%d request:%d decrement:%d ticket:%d signing:%d success:%d failed:%d | done:%d/%d\n",
           counts[ST_WAITING], counts[ST_CHECKING], counts[ST_REQUESTING],
            counts[ST_DECREMENTING], counts[ST_TICKETING], counts[ST_SIGNING],
            counts[ST_DONE_OK], counts[ST_DONE_FAIL],
           done, ctx->cfg.threads);
        printf("Etapas por thread: CHK=verificou estoque | REQ=solicitou compra | DEC=descontou estoque | TKT=gerou ticket | SIG=gerou assinatura\n\n");

    if (!final_frame) {
        print_divider();
        printf("| Thread | State    | Result | CHK | REQ | DEC | TKT | SIG | Sale ID  | Receipt                           | Signature        |\n");
        print_divider();

        int limit = ctx->cfg.rows_to_show < ctx->cfg.threads ? ctx->cfg.rows_to_show : ctx->cfg.threads;

        for (int i = 0; i < limit; i++) {
            thread_info_t *info = &ctx->infos[i];
            thread_state_t st = (thread_state_t)atomic_load(&info->state);
            const char *color = state_color(st);
            const char *result = info->success ? "OK" : (st == ST_DONE_FAIL ? "NO" : "...");

            printf("| %sT%-5d%s | %s%-8s%s | %-6s |  %c  |  %c  |  %c  |  %c  |  %c  | %-8ld | %-33.33s | %-16.16s |\n",
                   CLR_BOLD, info->id, CLR_RESET,
                   color, state_name(st), CLR_RESET,
                   result,
                   info->did_check ? 'Y' : '-',
                   info->did_request ? 'Y' : '-',
                   info->did_decrement ? 'Y' : '-',
                   info->did_ticket ? 'Y' : '-',
                   info->did_sign ? 'Y' : '-',
                   info->sale_id,
                   info->receipt[0] ? info->receipt : "-",
                   info->signature[0] ? info->signature : "-");
        }

        print_divider();
    }

    if (final_frame) {
        printf("\n%s\n", "-----------------------------------------------------------------");
        printf(" RESULT OF THE OPERATION: FLAWED_CONCURRENT\n");
        printf("%s\n", "-----------------------------------------------------------------");
        printf(" Buyers triggered         : %d\n", ctx->cfg.threads);
        printf(" Successful purchases     : %d\n", sold);
        printf(" Failed/Rejected attempts : %d\n", failed);
        if (oversold > 0) {
            printf(" OVERSOLD IN THIS BATCH   : %d tickets!\n", oversold);
        }
        printf(" Current remaining stock  : %d\n", remaining);

        printf("%s\n", "-----------------------------------------------------------------");
        printf(" DETAILED BUYER ACTIVITY\n");
        printf("%s\n", "-----------------------------------------------------------------");
        printf("%-10s %-16s %-10s %-12s %-11s\n",
               "Buyer", "Thread", "Saw stock", "Proof Token", "Stock after");
        printf("%s\n", "-----------------------------------------------------------------");

        int limit = ctx->cfg.rows_to_show < ctx->cfg.threads ? ctx->cfg.rows_to_show : ctx->cfg.threads;
        for (int i = 0; i < limit; i++) {
            thread_info_t *attempt = &ctx->infos[i];
            char buyer_id[16];
            char saw_stock[16];
            char stock_after_text[16];
            char token[13];

            snprintf(buyer_id, sizeof(buyer_id), "B%05d", attempt->id);
            if (attempt->signature[0]) {
                memcpy(token, attempt->signature, 12);
                token[12] = '\0';
            } else {
                strcpy(token, "-");
            }

            printf("%-10s %-16s %-10s %-12s %-11s\n",
                   buyer_id,
                   attempt->worker_thread_name,
                   format_stock_value(attempt->stock_observed_before, saw_stock, sizeof(saw_stock)),
                   token,
                   format_stock_value(attempt->stock_after, stock_after_text, sizeof(stock_after_text)));
        }
        printf("%s\n", "-----------------------------------------------------------------");

        printf("\nResumo final:\n");
        printf(" Threads: %d\n", ctx->cfg.threads);
        printf(" Compras: %d\n", sold);
        printf(" Oversold: %d\n", oversold);
        printf(" Compras falharam: %d\n", failed);
        printf(" Estoque restante: %d\n", remaining);
    }

    fflush(stdout);
}

int run_simulation(const sim_config_t *cfg) {
    if (!cfg || cfg->tickets <= 0 || cfg->threads <= 0 || cfg->rows_to_show <= 0) {
        fprintf(stderr, "Configuração inválida.\n");
        return 1;
    }

    simulation_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = *cfg;

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
        return 1;
    }

    for (int i = 0; i < cfg->threads; i++) {
        ctx.infos[i].id = i + 1;
        atomic_init(&ctx.infos[i].state, ST_CREATED);
        ctx.infos[i].success = 0;
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

    return 0;
}