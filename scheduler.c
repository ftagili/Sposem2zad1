#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

#include <errno.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include "workload.h"

#define VARIANT_ID 18
#define BURST_MIN_UNITS 4
#define BURST_MAX_UNITS 10
#define RR_QUANTUM_UNITS 3
#define MEAN_GAP_T2 21
#define MIN_TASKS 20
#define DEFAULT_TASK_COUNT 24
#define DEFAULT_SEED 0x18u
#define TIME_SCALE 2
#define GAP_MIN_T2 2
#define GAP_MAX_T2 40
#define MAX_TRACE_PRINT 40
#define DEFAULT_LIVE_TICK_US 1000u
#define LIVE_STACK_SIZE (64u * 1024u)
#define SIGNAL_STACK_SIZE (64u * 1024u)
#define DEMO_DEFAULT_TICK_US 100000u
#define DEMO_DEFAULT_SLICES 12u
#define DEMO_MIN_BURST_LINES 2u
#define DEMO_BURST_VARIATION_MASK 0x03u
#define DEMO_CONTINUOUS_PRINT_MASK 0x01ffu

#if defined(__GNUC__) || defined(__clang__)
#define SPO_UNUSED __attribute__((unused))
#else
#define SPO_UNUSED
#endif

typedef enum algorithm {
    ALG_SRT = 0,
    ALG_RR3 = 1
} algorithm_t;

typedef struct cpu_context {
    uint64_t r0;
    uint64_t r1;
    uint64_t r2;
    uint64_t pc;
    uint64_t flags;
} cpu_context_t;

typedef struct task {
    size_t id;
    int arrival_t2;
    int burst_t2;
    int remaining_t2;
    int first_run_t2;
    int finish_t2;
    cpu_context_t ctx;
} task_t;

typedef struct queue {
    int *data;
    size_t cap;
    size_t len;
    size_t head;
} queue_t;

typedef struct trace_segment {
    int start_t2;
    int end_t2;
    int pid;
} trace_segment_t;

typedef struct trace {
    trace_segment_t *items;
    size_t len;
    size_t cap;
} trace_t;

typedef struct metrics {
    double avg_turnaround;
    double avg_waiting;
    double cpu_utilization_pct;
    int total_time_t2;
    int context_switches;
} metrics_t;

typedef struct sim_result {
    task_t *tasks;
    size_t task_count;
    trace_t trace;
    metrics_t metrics;
} sim_result_t;

typedef struct cli_options {
    size_t task_count;
    uint32_t seed;
    unsigned tick_us;
    bool self_test;
    bool no_trace;
    bool demo_threads;
} cli_options_t;

#ifndef _WIN32
typedef struct live_task_state {
    ucontext_t context;
    unsigned char *stack;
} live_task_state_t;

typedef struct demo_thread_state {
    ucontext_t context;
    unsigned char *stack;
    const char *label;
    size_t label_len;
    volatile sig_atomic_t epoch;
    uint64_t spin_state;
} demo_thread_state_t;

typedef struct live_runtime {
    task_t *tasks;
    size_t task_count;
    algorithm_t algorithm;
    unsigned tick_us;
    live_task_state_t *task_states;
    queue_t rr_queue;
    trace_t trace;
    metrics_t metrics;
    ucontext_t scheduler_context;
    sigjmp_buf scheduler_jump;
    sigset_t timer_block_mask;
    sigset_t wait_mask;
    sigset_t saved_mask;
    struct sigaction old_action;
    struct itimerval old_timer;
    stack_t old_sigstack;
    unsigned char *signal_stack;
    bool action_installed;
    bool signal_stack_installed;
    bool timer_installed;
    volatile sig_atomic_t dispatch_started;
    volatile sig_atomic_t scheduler_jump_ready;
    volatile sig_atomic_t tick_pending;
    volatile sig_atomic_t tick_has_source;
    volatile sig_atomic_t tick_source;
    int now_t2;
    int busy_t2;
    size_t completed;
    int current;
    int logical_current;
    int rr_current;
    int rr_slice_left_t2;
} live_runtime_t;

static live_runtime_t *g_live_runtime = NULL;

typedef struct demo_runtime {
    demo_thread_state_t threads[2];
    ucontext_t scheduler_context;
    sigset_t timer_block_mask;
    sigset_t saved_mask;
    struct sigaction old_action;
    struct itimerval old_timer;
    stack_t old_sigstack;
    unsigned char *signal_stack;
    bool action_installed;
    bool signal_stack_installed;
    bool timer_installed;
    volatile sig_atomic_t dispatch_started;
    volatile sig_atomic_t current_valid;
    volatile sig_atomic_t current;
    volatile sig_atomic_t tick_pending;
    unsigned tick_us;
    size_t slice_index;
    size_t start_index;
} demo_runtime_t;

static demo_runtime_t *g_demo_runtime = NULL;
#endif

static double to_units(int t2) {
    return (double)t2 / (double)TIME_SCALE;
}

static double dabs_local(double x) {
    return (x < 0.0) ? -x : x;
}

static SPO_UNUSED double monotonic_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER freq;
    LARGE_INTEGER ctr;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&ctr)) {
        return 0.0;
    }
    return (double)ctr.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#endif
}

static uint32_t lcg_next(uint32_t *state) {
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

static int rand_inclusive(uint32_t *state, int lo, int hi) {
    const uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(lcg_next(state) % span);
}

static void init_task(task_t *task, size_t id, int arrival_t2, int burst_units) {
    task->id = id;
    task->arrival_t2 = arrival_t2;
    task->burst_t2 = burst_units * TIME_SCALE;
    task->remaining_t2 = task->burst_t2;
    task->first_run_t2 = -1;
    task->finish_t2 = -1;
    task->ctx.r0 = (uint64_t)(0x1000u + id);
    task->ctx.r1 = (uint64_t)(0x2000u + id * 3u);
    task->ctx.r2 = (uint64_t)(0x3000u + id * 5u);
    task->ctx.pc = 0u;
    task->ctx.flags = 0u;
}

static bool queue_init(queue_t *q, size_t cap) {
    q->data = (int *)calloc(cap, sizeof(int));
    if (q->data == NULL) {
        return false;
    }
    q->cap = cap;
    q->len = 0;
    q->head = 0;
    return true;
}

static void queue_free(queue_t *q) {
    free(q->data);
    q->data = NULL;
    q->cap = 0;
    q->len = 0;
    q->head = 0;
}

static bool queue_push(queue_t *q, int value) {
    size_t tail;

    if (q->len >= q->cap) {
        return false;
    }
    tail = (q->head + q->len) % q->cap;
    q->data[tail] = value;
    q->len++;
    return true;
}

static bool queue_pop(queue_t *q, int *out_value) {
    if (q->len == 0) {
        return false;
    }
    *out_value = q->data[q->head];
    q->head = (q->head + 1) % q->cap;
    q->len--;
    return true;
}

static bool trace_append(trace_t *trace, int start_t2, int end_t2, int pid) {
    trace_segment_t *new_items;
    size_t new_cap;

    if (trace->len > 0) {
        trace_segment_t *last = &trace->items[trace->len - 1];
        if (last->pid == pid && last->end_t2 == start_t2) {
            last->end_t2 = end_t2;
            return true;
        }
    }

    if (trace->len == trace->cap) {
        new_cap = (trace->cap == 0) ? 32u : trace->cap * 2u;
        new_items =
            (trace_segment_t *)realloc(trace->items, new_cap * sizeof(trace_segment_t));
        if (new_items == NULL) {
            return false;
        }
        trace->items = new_items;
        trace->cap = new_cap;
    }

    trace->items[trace->len].start_t2 = start_t2;
    trace->items[trace->len].end_t2 = end_t2;
    trace->items[trace->len].pid = pid;
    trace->len++;
    return true;
}

static void trace_free(trace_t *trace) {
    free(trace->items);
    trace->items = NULL;
    trace->len = 0;
    trace->cap = 0;
}

static void emulate_cpu_tick(task_t *task, int now_t2) {
    uint64_t v;
    uint64_t mix_arg;

    v = task->ctx.r0 ^ (uint64_t)(task->remaining_t2 + now_t2);
    v = spo_asm_add_u64(v, (uint64_t)(task->id * 17u + 3u));
    v = spo_asm_double_u64(v ^ task->ctx.r1);
    mix_arg = (uint64_t)(task->remaining_t2 * 13 + (int)task->id);
    v = spo_asm_mix_u64(v, mix_arg);

    task->ctx.r2 ^= v;
    task->ctx.r1 += (uint64_t)(task->id + 1u);
    task->ctx.r0 = task->ctx.r2 ^ task->ctx.r1;
    task->ctx.pc += 1u;
}

static void context_switch(cpu_context_t *from, cpu_context_t *to, metrics_t *metrics) {
    if (from == to) {
        return;
    }
    if (from != NULL) {
        from->flags ^= 0x01u;
    }
    if (to != NULL) {
        to->flags ^= 0x02u;
    }
    metrics->context_switches++;
}

static void timer_wait_tick(unsigned tick_us) {
    if (tick_us == 0u) {
        return;
    }
#ifdef _WIN32
    {
        DWORD ms = (DWORD)((tick_us + 999u) / 1000u);
        if (ms == 0u) {
            ms = 1u;
        }
        Sleep(ms);
    }
#else
    {
        struct timespec req;
        req.tv_sec = (time_t)(tick_us / 1000000u);
        req.tv_nsec = (long)(tick_us % 1000000u) * 1000L;
        while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        }
    }
#endif
}

static void cleanup_result(sim_result_t *result) {
    free(result->tasks);
    result->tasks = NULL;
    result->task_count = 0;
    trace_free(&result->trace);
    memset(&result->metrics, 0, sizeof(result->metrics));
}

static void finalize_metrics(const task_t *tasks, size_t count, int total_time_t2,
                             int busy_t2, metrics_t *metrics) {
    size_t i;
    double turnaround_sum_t2 = 0.0;
    double waiting_sum_t2 = 0.0;

    metrics->total_time_t2 = total_time_t2;
    metrics->cpu_utilization_pct =
        (total_time_t2 > 0) ? ((double)busy_t2 * 100.0 / (double)total_time_t2) : 0.0;

    for (i = 0; i < count; ++i) {
        double turnaround = (double)(tasks[i].finish_t2 - tasks[i].arrival_t2);
        double waiting = turnaround - (double)tasks[i].burst_t2;
        turnaround_sum_t2 += turnaround;
        waiting_sum_t2 += waiting;
    }

    metrics->avg_turnaround = (turnaround_sum_t2 / (double)count) / (double)TIME_SCALE;
    metrics->avg_waiting = (waiting_sum_t2 / (double)count) / (double)TIME_SCALE;
}

static int pick_srt(const task_t *tasks, size_t count, int now_t2) {
    size_t i;
    int best = -1;

    for (i = 0; i < count; ++i) {
        if (tasks[i].arrival_t2 > now_t2 || tasks[i].remaining_t2 <= 0) {
            continue;
        }
        if (best < 0 || tasks[i].remaining_t2 < tasks[(size_t)best].remaining_t2 ||
            (tasks[i].remaining_t2 == tasks[(size_t)best].remaining_t2 &&
             tasks[i].arrival_t2 < tasks[(size_t)best].arrival_t2) ||
            (tasks[i].remaining_t2 == tasks[(size_t)best].remaining_t2 &&
             tasks[i].arrival_t2 == tasks[(size_t)best].arrival_t2 &&
             tasks[i].id < tasks[(size_t)best].id)) {
            best = (int)i;
        }
    }
    return best;
}

static bool simulate_model(const task_t *base_tasks, size_t count, algorithm_t algorithm,
                           unsigned tick_us, sim_result_t *result) {
    queue_t rr_queue;
    size_t completed = 0;
    int now_t2 = 0;
    int busy_t2 = 0;
    int current = -1;
    int rr_left_t2 = 0;
    size_t i;

    memset(result, 0, sizeof(*result));

    result->tasks = (task_t *)malloc(count * sizeof(task_t));
    if (result->tasks == NULL) {
        return false;
    }
    memcpy(result->tasks, base_tasks, count * sizeof(task_t));
    result->task_count = count;

    if (!queue_init(&rr_queue, count + 4u)) {
        cleanup_result(result);
        return false;
    }

    while (completed < count) {
        int next = -1;

        timer_wait_tick(tick_us);

        if (algorithm == ALG_RR3) {
            for (i = 0; i < count; ++i) {
                if (result->tasks[i].arrival_t2 == now_t2 &&
                    result->tasks[i].remaining_t2 > 0) {
                    if (!queue_push(&rr_queue, (int)i)) {
                        queue_free(&rr_queue);
                        cleanup_result(result);
                        return false;
                    }
                }
            }
        }

        if (algorithm == ALG_SRT) {
            next = pick_srt(result->tasks, count, now_t2);
        } else {
            if (current < 0) {
                if (queue_pop(&rr_queue, &next)) {
                    rr_left_t2 = RR_QUANTUM_UNITS * TIME_SCALE;
                }
            } else {
                next = current;
            }
        }

        if (next != current) {
            cpu_context_t *from = (current >= 0) ? &result->tasks[(size_t)current].ctx : NULL;
            cpu_context_t *to = (next >= 0) ? &result->tasks[(size_t)next].ctx : NULL;
            context_switch(from, to, &result->metrics);
            current = next;
        }

        if (current < 0) {
            if (!trace_append(&result->trace, now_t2, now_t2 + 1, -1)) {
                queue_free(&rr_queue);
                cleanup_result(result);
                return false;
            }
            now_t2++;
            continue;
        }

        if (result->tasks[(size_t)current].first_run_t2 < 0) {
            result->tasks[(size_t)current].first_run_t2 = now_t2;
        }

        emulate_cpu_tick(&result->tasks[(size_t)current], now_t2);
        result->tasks[(size_t)current].remaining_t2--;
        busy_t2++;

        if (algorithm == ALG_RR3) {
            rr_left_t2--;
        }

        if (!trace_append(&result->trace, now_t2, now_t2 + 1,
                          (int)result->tasks[(size_t)current].id)) {
            queue_free(&rr_queue);
            cleanup_result(result);
            return false;
        }

        if (result->tasks[(size_t)current].remaining_t2 == 0) {
            result->tasks[(size_t)current].finish_t2 = now_t2 + 1;
            completed++;
            current = -1;
            rr_left_t2 = 0;
        } else if (algorithm == ALG_RR3 && rr_left_t2 == 0) {
            if (!queue_push(&rr_queue, current)) {
                queue_free(&rr_queue);
                cleanup_result(result);
                return false;
            }
            current = -1;
        }

        now_t2++;
    }

    finalize_metrics(result->tasks, count, now_t2, busy_t2, &result->metrics);
    queue_free(&rr_queue);
    return true;
}

#ifndef _WIN32
static bool SPO_UNUSED run_posix_timer_scheduler(const task_t *base_tasks, size_t count,
                                                 algorithm_t algorithm, unsigned tick_us,
                                                 sim_result_t *result) {
    queue_t rr_queue;
    struct itimerval timer;
    struct itimerval old_timer;
    sigset_t timer_mask;
    sigset_t saved_mask;
    size_t completed = 0;
    int now_t2 = 0;
    int busy_t2 = 0;
    int current = -1;
    int rr_left_t2 = 0;
    bool timer_installed = false;
    bool mask_changed = false;
    size_t i;

    memset(&rr_queue, 0, sizeof(rr_queue));
    memset(&timer, 0, sizeof(timer));
    memset(&old_timer, 0, sizeof(old_timer));
    memset(result, 0, sizeof(*result));

    result->tasks = (task_t *)malloc(count * sizeof(task_t));
    if (result->tasks == NULL) {
        return false;
    }
    memcpy(result->tasks, base_tasks, count * sizeof(task_t));
    result->task_count = count;

    if (!queue_init(&rr_queue, count + 4u)) {
        cleanup_result(result);
        return false;
    }

    sigemptyset(&timer_mask);
    sigaddset(&timer_mask, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &timer_mask, &saved_mask) != 0) {
        queue_free(&rr_queue);
        cleanup_result(result);
        return false;
    }
    mask_changed = true;

    tick_us = (tick_us == 0u) ? DEFAULT_LIVE_TICK_US : tick_us;
    timer.it_interval.tv_sec = (time_t)(tick_us / 1000000u);
    timer.it_interval.tv_usec = (suseconds_t)(tick_us % 1000000u);
    timer.it_value = timer.it_interval;
    if (timer.it_value.tv_sec == 0 && timer.it_value.tv_usec == 0) {
        timer.it_value.tv_usec = (suseconds_t)DEFAULT_LIVE_TICK_US;
        timer.it_interval.tv_usec = (suseconds_t)DEFAULT_LIVE_TICK_US;
    }

    if (setitimer(ITIMER_REAL, &timer, &old_timer) != 0) {
        sigprocmask(SIG_SETMASK, &saved_mask, NULL);
        queue_free(&rr_queue);
        cleanup_result(result);
        return false;
    }
    timer_installed = true;

    while (completed < count) {
        int next = -1;
        int sig_rc;

        do {
            sig_rc = sigwaitinfo(&timer_mask, NULL);
        } while (sig_rc < 0 && errno == EINTR);

        if (sig_rc != SIGALRM) {
            if (timer_installed) {
                setitimer(ITIMER_REAL, &old_timer, NULL);
            }
            if (mask_changed) {
                sigprocmask(SIG_SETMASK, &saved_mask, NULL);
            }
            queue_free(&rr_queue);
            cleanup_result(result);
            return false;
        }

        if (algorithm == ALG_RR3) {
            for (i = 0; i < count; ++i) {
                if (result->tasks[i].arrival_t2 == now_t2 &&
                    result->tasks[i].remaining_t2 > 0) {
                    if (!queue_push(&rr_queue, (int)i)) {
                        if (timer_installed) {
                            setitimer(ITIMER_REAL, &old_timer, NULL);
                        }
                        if (mask_changed) {
                            sigprocmask(SIG_SETMASK, &saved_mask, NULL);
                        }
                        queue_free(&rr_queue);
                        cleanup_result(result);
                        return false;
                    }
                }
            }
        }

        if (algorithm == ALG_SRT) {
            next = pick_srt(result->tasks, count, now_t2);
        } else if (current < 0) {
            if (queue_pop(&rr_queue, &next)) {
                rr_left_t2 = RR_QUANTUM_UNITS * TIME_SCALE;
            }
        } else {
            next = current;
        }

        if (next != current) {
            cpu_context_t *from = (current >= 0) ? &result->tasks[(size_t)current].ctx : NULL;
            cpu_context_t *to = (next >= 0) ? &result->tasks[(size_t)next].ctx : NULL;
            context_switch(from, to, &result->metrics);
            current = next;
        }

        if (current < 0) {
            if (!trace_append(&result->trace, now_t2, now_t2 + 1, -1)) {
                if (timer_installed) {
                    setitimer(ITIMER_REAL, &old_timer, NULL);
                }
                if (mask_changed) {
                    sigprocmask(SIG_SETMASK, &saved_mask, NULL);
                }
                queue_free(&rr_queue);
                cleanup_result(result);
                return false;
            }
            now_t2++;
            continue;
        }

        if (result->tasks[(size_t)current].first_run_t2 < 0) {
            result->tasks[(size_t)current].first_run_t2 = now_t2;
        }

        emulate_cpu_tick(&result->tasks[(size_t)current], now_t2);
        result->tasks[(size_t)current].remaining_t2--;
        busy_t2++;

        if (algorithm == ALG_RR3) {
            rr_left_t2--;
        }

        if (!trace_append(&result->trace, now_t2, now_t2 + 1,
                          (int)result->tasks[(size_t)current].id)) {
            if (timer_installed) {
                setitimer(ITIMER_REAL, &old_timer, NULL);
            }
            if (mask_changed) {
                sigprocmask(SIG_SETMASK, &saved_mask, NULL);
            }
            queue_free(&rr_queue);
            cleanup_result(result);
            return false;
        }

        if (result->tasks[(size_t)current].remaining_t2 == 0) {
            result->tasks[(size_t)current].finish_t2 = now_t2 + 1;
            completed++;
            current = -1;
            rr_left_t2 = 0;
        } else if (algorithm == ALG_RR3 && rr_left_t2 == 0) {
            if (!queue_push(&rr_queue, current)) {
                if (timer_installed) {
                    setitimer(ITIMER_REAL, &old_timer, NULL);
                }
                if (mask_changed) {
                    sigprocmask(SIG_SETMASK, &saved_mask, NULL);
                }
                queue_free(&rr_queue);
                cleanup_result(result);
                return false;
            }
            current = -1;
        }

        now_t2++;
    }

    finalize_metrics(result->tasks, count, now_t2, busy_t2, &result->metrics);
    if (timer_installed) {
        setitimer(ITIMER_REAL, &old_timer, NULL);
    }
    if (mask_changed) {
        sigprocmask(SIG_SETMASK, &saved_mask, NULL);
    }
    queue_free(&rr_queue);
    return true;
}
#endif

static bool parse_u64(const char *s, uint64_t *out) {
    char *endptr = NULL;
    unsigned long long value;

    errno = 0;
    value = strtoull(s, &endptr, 10);
    if (errno != 0 || endptr == s || *endptr != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [--count N] [--seed S] [--tick-us U] [--no-trace] [--self-test] [--demo-threads]\n",
           prog_name);
    printf("  --count N    Number of processes (N >= %d, default: %d)\n", MIN_TASKS,
           DEFAULT_TASK_COUNT);
    printf("  --seed S     Seed for deterministic workload generation (default: %u)\n",
           DEFAULT_SEED);
    printf("  --tick-us U  Timer tick duration in microseconds (POSIX live default: %u)\n",
           DEFAULT_LIVE_TICK_US);
    printf("  --no-trace   Hide timeline segments\n");
    printf("  --self-test  Run internal scheduler checks\n");
    printf("  --demo-threads  Run a simple two-thread preemption demo (POSIX only)\n");
}

static bool parse_cli(int argc, char **argv, cli_options_t *opts) {
    int i;

    opts->task_count = DEFAULT_TASK_COUNT;
    opts->seed = DEFAULT_SEED;
    opts->tick_us = 0u;
    opts->self_test = false;
    opts->no_trace = false;
    opts->demo_threads = false;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--count") == 0) {
            uint64_t raw;
            if (i + 1 >= argc || !parse_u64(argv[++i], &raw) || raw < MIN_TASKS) {
                return false;
            }
            opts->task_count = (size_t)raw;
        } else if (strcmp(argv[i], "--seed") == 0) {
            uint64_t raw;
            if (i + 1 >= argc || !parse_u64(argv[++i], &raw)) {
                return false;
            }
            opts->seed = (uint32_t)raw;
        } else if (strcmp(argv[i], "--tick-us") == 0) {
            uint64_t raw;
            if (i + 1 >= argc || !parse_u64(argv[++i], &raw)) {
                return false;
            }
            opts->tick_us = (unsigned)raw;
        } else if (strcmp(argv[i], "--self-test") == 0) {
            opts->self_test = true;
        } else if (strcmp(argv[i], "--no-trace") == 0) {
            opts->no_trace = true;
        } else if (strcmp(argv[i], "--demo-threads") == 0) {
            opts->demo_threads = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return false;
        } else {
            return false;
        }
    }
    return true;
}

static bool generate_variant18_workload(task_t *tasks, size_t count, uint32_t seed) {
    int *gaps_t2 = NULL;
    size_t gaps_count;
    int target_sum_t2;
    bool gaps_found = false;
    uint32_t burst_state;
    int arrival_t2 = 0;
    size_t i;

    if (count < MIN_TASKS) {
        return false;
    }

    gaps_count = (count > 0) ? (count - 1u) : 0u;
    target_sum_t2 = (int)(gaps_count * (size_t)MEAN_GAP_T2);

    if (gaps_count > 0) {
        uint32_t attempt;
        gaps_t2 = (int *)calloc(gaps_count, sizeof(int));
        if (gaps_t2 == NULL) {
            return false;
        }

        for (attempt = 0; attempt < 8192u && !gaps_found; ++attempt) {
            uint32_t st = seed ^ (0x9e3779b9u + attempt * 97u);
            int sum = 0;
            size_t j;
            if (gaps_count == 1u) {
                if (target_sum_t2 >= GAP_MIN_T2 && target_sum_t2 <= GAP_MAX_T2) {
                    gaps_t2[0] = target_sum_t2;
                    gaps_found = true;
                }
                continue;
            }
            for (j = 0; j < gaps_count - 1u; ++j) {
                gaps_t2[j] = rand_inclusive(&st, GAP_MIN_T2, GAP_MAX_T2);
                sum += gaps_t2[j];
            }
            gaps_t2[gaps_count - 1u] = target_sum_t2 - sum;
            if (gaps_t2[gaps_count - 1u] >= GAP_MIN_T2 &&
                gaps_t2[gaps_count - 1u] <= GAP_MAX_T2) {
                gaps_found = true;
            }
        }

        if (!gaps_found) {
            for (i = 0; i < gaps_count; ++i) {
                gaps_t2[i] = (i % 2u == 0u) ? 20 : 22;
            }
        }
    }

    burst_state = seed ^ 0xa5a5a5a5u;
    for (i = 0; i < count; ++i) {
        int burst_units = rand_inclusive(&burst_state, BURST_MIN_UNITS, BURST_MAX_UNITS);
        if (i > 0) {
            arrival_t2 += gaps_t2[i - 1u];
        }
        init_task(&tasks[i], i + 1u, arrival_t2, burst_units);
    }

    free(gaps_t2);
    return true;
}

static void print_workload(const task_t *tasks, size_t count) {
    size_t i;
    int min_burst = BURST_MAX_UNITS;
    int max_burst = BURST_MIN_UNITS;
    int gap_sum_t2 = 0;

    printf("\nInput workload (variant %d):\n", VARIANT_ID);
    printf("  required algorithms: SRT and RR(3)\n");
    printf("  required burst range: %d..%d\n", BURST_MIN_UNITS, BURST_MAX_UNITS);
    printf("  required avg gap: 10.5\n");
    printf("  process pairs: %zu\n\n", count);

    printf("%-4s %-10s %-10s\n", "PID", "arrival", "burst");
    printf("%-4s %-10s %-10s\n", "----", "----------", "----------");
    for (i = 0; i < count; ++i) {
        int burst_units = tasks[i].burst_t2 / TIME_SCALE;
        if (burst_units < min_burst) {
            min_burst = burst_units;
        }
        if (burst_units > max_burst) {
            max_burst = burst_units;
        }
        if (i > 0) {
            gap_sum_t2 += tasks[i].arrival_t2 - tasks[i - 1u].arrival_t2;
        }
        printf("%-4zu %-10.1f %-10.1f\n", tasks[i].id, to_units(tasks[i].arrival_t2),
               to_units(tasks[i].burst_t2));
    }

    if (count > 1u) {
        printf("\nRange check: burst_min=%d, burst_max=%d\n", min_burst, max_burst);
        printf("Average gap: %.2f\n",
               ((double)gap_sum_t2 / (double)(count - 1u)) / (double)TIME_SCALE);
    }
}

static void print_trace(const trace_t *trace) {
    size_t i;
    size_t limit = (trace->len < MAX_TRACE_PRINT) ? trace->len : MAX_TRACE_PRINT;

    printf("Timeline (compressed segments, time in model units):\n");
    for (i = 0; i < limit; ++i) {
        if (trace->items[i].pid < 0) {
            printf("  [%.1f, %.1f): idle\n", to_units(trace->items[i].start_t2),
                   to_units(trace->items[i].end_t2));
        } else {
            printf("  [%.1f, %.1f): P%d\n", to_units(trace->items[i].start_t2),
                   to_units(trace->items[i].end_t2), trace->items[i].pid);
        }
    }
    if (trace->len > limit) {
        printf("  ... (%zu more segments)\n", trace->len - limit);
    }
}

static void print_result_table(const char *title, const sim_result_t *result) {
    size_t i;

    printf("\n=== %s ===\n", title);
    printf("Total modeled time: %.1f\n", to_units(result->metrics.total_time_t2));
    printf("CPU utilization: %.2f%%\n", result->metrics.cpu_utilization_pct);
    printf("Context switches: %d\n", result->metrics.context_switches);
    printf("Average turnaround: %.2f\n", result->metrics.avg_turnaround);
    printf("Average waiting: %.2f\n", result->metrics.avg_waiting);

    printf("\n%-4s %-8s %-8s %-8s %-11s %-10s\n", "PID", "arrival", "burst", "finish",
           "turnaround", "waiting");
    printf("%-4s %-8s %-8s %-8s %-11s %-10s\n", "----", "--------", "--------", "--------",
           "-----------", "----------");
    for (i = 0; i < result->task_count; ++i) {
        double turnaround =
            to_units(result->tasks[i].finish_t2 - result->tasks[i].arrival_t2);
        double waiting = turnaround - to_units(result->tasks[i].burst_t2);
        printf("%-4zu %-8.1f %-8.1f %-8.1f %-11.1f %-10.1f\n", result->tasks[i].id,
               to_units(result->tasks[i].arrival_t2), to_units(result->tasks[i].burst_t2),
               to_units(result->tasks[i].finish_t2), turnaround, waiting);
    }
}

static bool approx_eq(double a, double b) {
    return dabs_local(a - b) < 1e-9;
}

static bool run_self_tests(void) {
    bool ok = true;
    task_t tiny_a[2];
    task_t tiny_b[2];
    task_t generated[MIN_TASKS];
    sim_result_t result = {0};

    puts("[self-test] Test 1: SRT preempts a longer running process");
    init_task(&tiny_a[0], 1u, 0, 8);
    init_task(&tiny_a[1], 2u, 2 * TIME_SCALE, 2);
    if (!simulate_model(tiny_a, 2u, ALG_SRT, 0u, &result)) {
        puts("  FAIL: simulation failed");
        return false;
    }
    if (!approx_eq(result.metrics.avg_waiting, 1.0)) {
        printf("  FAIL: expected avg waiting 1.0, got %.2f\n", result.metrics.avg_waiting);
        ok = false;
    } else {
        puts("  PASS");
    }
    cleanup_result(&result);

    puts("[self-test] Test 2: RR(3) rotates correctly");
    init_task(&tiny_b[0], 1u, 0, 5);
    init_task(&tiny_b[1], 2u, 0, 5);
    if (!simulate_model(tiny_b, 2u, ALG_RR3, 0u, &result)) {
        puts("  FAIL: simulation failed");
        return false;
    }
    if (!approx_eq(result.metrics.avg_waiting, 4.0)) {
        printf("  FAIL: expected avg waiting 4.0, got %.2f\n", result.metrics.avg_waiting);
        ok = false;
    } else {
        puts("  PASS");
    }
    cleanup_result(&result);

    puts("[self-test] Test 3: generated workload obeys variant constraints");
    if (!generate_variant18_workload(generated, MIN_TASKS, DEFAULT_SEED)) {
        puts("  FAIL: workload generation failed");
        return false;
    }
    {
        size_t i;
        int min_b = 1000;
        int max_b = -1;
        int gap_sum_t2 = 0;
        for (i = 0; i < MIN_TASKS; ++i) {
            int burst = generated[i].burst_t2 / TIME_SCALE;
            if (burst < min_b) {
                min_b = burst;
            }
            if (burst > max_b) {
                max_b = burst;
            }
            if (i > 0) {
                gap_sum_t2 += generated[i].arrival_t2 - generated[i - 1u].arrival_t2;
            }
        }
        if (min_b < BURST_MIN_UNITS || max_b > BURST_MAX_UNITS) {
            puts("  FAIL: burst range violated");
            ok = false;
        }
        if (!approx_eq(
                ((double)gap_sum_t2 / (double)(MIN_TASKS - 1)) / (double)TIME_SCALE,
                10.5)) {
            printf("  FAIL: mean gap expected 10.5, got %.2f\n",
                   ((double)gap_sum_t2 / (double)(MIN_TASKS - 1)) / (double)TIME_SCALE);
            ok = false;
        }
    }
    if (ok) {
        puts("  PASS");
    }

    return ok;
}

#ifndef _WIN32
static void demo_timer_handler(int signo, siginfo_t *info, void *ucontext_void) {
    demo_runtime_t *rt = g_demo_runtime;

    (void)signo;
    (void)info;
    (void)ucontext_void;
    if (rt == NULL) {
        return;
    }
    if (!rt->dispatch_started || !rt->current_valid) {
        return;
    }
    rt->tick_pending = 1;
}

static void demo_destroy(demo_runtime_t *rt) {
    size_t i;

    if (rt->timer_installed) {
        setitimer(ITIMER_REAL, &rt->old_timer, NULL);
        rt->timer_installed = false;
    }
    if (rt->action_installed) {
        sigaction(SIGALRM, &rt->old_action, NULL);
        rt->action_installed = false;
    }
    if (rt->signal_stack_installed) {
        sigaltstack(&rt->old_sigstack, NULL);
        rt->signal_stack_installed = false;
    }
    if (g_demo_runtime == rt) {
        g_demo_runtime = NULL;
    }
    free(rt->signal_stack);
    rt->signal_stack = NULL;

    for (i = 0; i < 2u; ++i) {
        free(rt->threads[i].stack);
        rt->threads[i].stack = NULL;
    }
}

static void demo_write_label_burst(demo_runtime_t *rt, const demo_thread_state_t *thread,
                                   unsigned count) {
    unsigned i;

    sigprocmask(SIG_BLOCK, &rt->timer_block_mask, NULL);
    for (i = 0u; i < count; ++i) {
        ssize_t written = write(STDOUT_FILENO, thread->label, thread->label_len);
        if (written < 0) {
            break;
        }
    }
    sigprocmask(SIG_UNBLOCK, &rt->timer_block_mask, NULL);
}

static bool demo_yield_if_requested(demo_runtime_t *rt, size_t idx) {
    demo_thread_state_t *thread = &rt->threads[idx];
    bool yielded = false;

    if (!rt->tick_pending) {
        return false;
    }
    if (sigprocmask(SIG_BLOCK, &rt->timer_block_mask, NULL) != 0) {
        return false;
    }

    if (rt->tick_pending && rt->current_valid && rt->current == (sig_atomic_t)idx) {
        rt->tick_pending = 0;
        rt->current_valid = 0;
        yielded = true;
        if (swapcontext(&thread->context, &rt->scheduler_context) != 0) {
            _exit(9);
        }
    }

    sigprocmask(SIG_UNBLOCK, &rt->timer_block_mask, NULL);
    return yielded;
}

static void demo_worker_entry(uintptr_t worker_idx) {
    demo_runtime_t *rt = g_demo_runtime;
    demo_thread_state_t *thread;
    sig_atomic_t seen_epoch = -1;
    size_t idx = (size_t)worker_idx;

    if (rt == NULL || idx >= 2u) {
        return;
    }

    sigprocmask(SIG_UNBLOCK, &rt->timer_block_mask, NULL);
    thread = &rt->threads[idx];
    for (;;) {
        sig_atomic_t epoch = thread->epoch;

        if (demo_yield_if_requested(rt, idx)) {
            continue;
        }
        if (epoch != seen_epoch) {
            unsigned burst =
                DEMO_MIN_BURST_LINES +
                (unsigned)(thread->spin_state & (uint64_t)DEMO_BURST_VARIATION_MASK);
            seen_epoch = epoch;
            demo_write_label_burst(rt, thread, burst);
        } else if ((thread->spin_state & DEMO_CONTINUOUS_PRINT_MASK) == 0u) {
            demo_write_label_burst(rt, thread, 1u);
        }
        if (demo_yield_if_requested(rt, idx)) {
            continue;
        }

        thread->spin_state = spo_asm_mix_u64(thread->spin_state + (uint64_t)idx + 1u,
                                             UINT64_C(0x9e3779b97f4a7c15));
    }
}

static bool demo_init_contexts(demo_runtime_t *rt) {
    static const char demo_label_1[] = "1111\n";
    static const char demo_label_2[] = "2222\n";
    uint64_t seed_base;
    volatile size_t i;

    seed_base = (uint64_t)(monotonic_seconds() * 1000000.0);
    if (seed_base == 0u) {
        seed_base = UINT64_C(0x13579bdf2468ace0);
    }
    rt->start_index = (size_t)(seed_base & 1u);

    rt->threads[0].label = demo_label_1;
    rt->threads[0].label_len = sizeof(demo_label_1) - 1u;
    rt->threads[1].label = demo_label_2;
    rt->threads[1].label_len = sizeof(demo_label_2) - 1u;

    for (i = 0; i < 2u; ++i) {
        rt->threads[i].epoch = 0;
        rt->threads[i].spin_state =
            spo_asm_mix_u64(seed_base + UINT64_C(0x1000) + (uint64_t)(i * 97u),
                            UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)(i + 1u));
        if (getcontext(&rt->threads[i].context) != 0) {
            return false;
        }
        rt->threads[i].stack = (unsigned char *)malloc(LIVE_STACK_SIZE);
        if (rt->threads[i].stack == NULL) {
            return false;
        }
        rt->threads[i].context.uc_link = &rt->scheduler_context;
        rt->threads[i].context.uc_stack.ss_sp = rt->threads[i].stack;
        rt->threads[i].context.uc_stack.ss_size = LIVE_STACK_SIZE;
        rt->threads[i].context.uc_stack.ss_flags = 0;
        rt->threads[i].context.uc_sigmask = rt->timer_block_mask;
        makecontext(&rt->threads[i].context, (void (*)(void))demo_worker_entry, 1,
                    (uintptr_t)i);
    }

    return true;
}

static bool demo_start_timer(demo_runtime_t *rt) {
    struct sigaction action;
    struct itimerval timer;
    stack_t ss;

    rt->signal_stack = (unsigned char *)malloc(SIGNAL_STACK_SIZE);
    if (rt->signal_stack == NULL) {
        return false;
    }

    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = rt->signal_stack;
    ss.ss_size = SIGNAL_STACK_SIZE;
    if (sigaltstack(&ss, &rt->old_sigstack) != 0) {
        free(rt->signal_stack);
        rt->signal_stack = NULL;
        return false;
    }
    rt->signal_stack_installed = true;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = demo_timer_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGALRM);

    if (sigaction(SIGALRM, &action, &rt->old_action) != 0) {
        sigaltstack(&rt->old_sigstack, NULL);
        rt->signal_stack_installed = false;
        free(rt->signal_stack);
        rt->signal_stack = NULL;
        return false;
    }
    rt->action_installed = true;

    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = (time_t)(rt->tick_us / 1000000u);
    timer.it_interval.tv_usec = (suseconds_t)(rt->tick_us % 1000000u);
    timer.it_value = timer.it_interval;
    if (timer.it_value.tv_sec == 0 && timer.it_value.tv_usec == 0) {
        timer.it_value.tv_usec = (suseconds_t)DEMO_DEFAULT_TICK_US;
        timer.it_interval.tv_usec = (suseconds_t)DEMO_DEFAULT_TICK_US;
    }

    g_demo_runtime = rt;
    if (setitimer(ITIMER_REAL, &timer, &rt->old_timer) != 0) {
        g_demo_runtime = NULL;
        sigaction(SIGALRM, &rt->old_action, NULL);
        rt->action_installed = false;
        sigaltstack(&rt->old_sigstack, NULL);
        rt->signal_stack_installed = false;
        free(rt->signal_stack);
        rt->signal_stack = NULL;
        return false;
    }
    rt->timer_installed = true;
    return true;
}

static int run_demo_loop(demo_runtime_t *rt) {
    while (rt->slice_index < DEMO_DEFAULT_SLICES) {
        int next = (int)((rt->start_index + rt->slice_index) % 2u);

        rt->tick_pending = 0;
        rt->threads[next].epoch++;
        rt->current = next;
        rt->current_valid = 1;
        rt->dispatch_started = 1;

        if (swapcontext(&rt->scheduler_context, &rt->threads[next].context) != 0) {
            return 8;
        }

        rt->current_valid = 0;
        rt->slice_index++;
    }

    rt->current_valid = 0;
    rt->tick_pending = 0;
    return 0;
}

static int run_demo_threads(unsigned tick_us) {
    demo_runtime_t rt;
    int rc;

    memset(&rt, 0, sizeof(rt));
    rt.tick_us = (tick_us == 0u) ? DEMO_DEFAULT_TICK_US : tick_us;
    rt.current = 0;

    sigemptyset(&rt.timer_block_mask);
    sigaddset(&rt.timer_block_mask, SIGALRM);

    if (!demo_init_contexts(&rt)) {
        demo_destroy(&rt);
        return 8;
    }
    if (sigprocmask(SIG_BLOCK, &rt.timer_block_mask, &rt.saved_mask) != 0) {
        demo_destroy(&rt);
        return 8;
    }
    if (!demo_start_timer(&rt)) {
        demo_destroy(&rt);
        sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
        return 8;
    }

    puts("Demo: timer switches between two user threads.");
    printf("Tick: %u us\n", rt.tick_us);
    puts("If everything works, each active thread prints several lines until the timer");
    puts("switches execution to the other thread.");
    fflush(stdout);

    rc = run_demo_loop(&rt);
    if (rc != 0) {
        demo_destroy(&rt);
        sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
        return rc;
    }

    demo_destroy(&rt);
    sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
    puts("Demo finished.");
    return 0;
}

static bool task_is_runnable(const task_t *task, int now_t2) {
    return task->arrival_t2 <= now_t2 && task->remaining_t2 > 0;
}

static void live_worker_entry(uintptr_t worker_seed) {
    live_runtime_t *rt = g_live_runtime;
    size_t idx = (worker_seed > 0u) ? (size_t)(worker_seed - 1u) : 0u;
    volatile uint64_t acc =
        (uint64_t)worker_seed * UINT64_C(0x9e3779b97f4a7c15) + UINT64_C(0x1234);

    if (rt != NULL && idx < rt->task_count) {
        sigprocmask(SIG_UNBLOCK, &rt->timer_block_mask, NULL);
    }

    for (;;) {
        acc = spo_asm_add_u64(acc, (uint64_t)worker_seed + 3u);
        acc = spo_asm_double_u64(acc ^ UINT64_C(0x517cc1b727220a95));
        acc = spo_asm_mix_u64(acc, acc >> 11u);

        if (rt != NULL && idx < rt->task_count && rt->tick_pending &&
            rt->current == (int)idx) {
            rt->tick_source = (sig_atomic_t)idx;
            rt->tick_has_source = 1;
            if (swapcontext(&rt->task_states[idx].context, &rt->scheduler_context) != 0) {
                _exit(9);
            }
        }
    }
}

static void live_timer_handler(int signo, siginfo_t *info, void *ucontext_void) {
    live_runtime_t *rt = g_live_runtime;

    (void)signo;
    (void)info;
    (void)ucontext_void;
    if (rt == NULL) {
        return;
    }
    if (!rt->dispatch_started && rt->current < 0) {
        return;
    }

    rt->tick_pending = 1;
}

static void live_destroy(live_runtime_t *rt) {
    size_t i;

    if (rt->timer_installed) {
        setitimer(ITIMER_REAL, &rt->old_timer, NULL);
        rt->timer_installed = false;
    }
    if (rt->action_installed) {
        sigaction(SIGALRM, &rt->old_action, NULL);
        rt->action_installed = false;
    }
    if (rt->signal_stack_installed) {
        sigaltstack(&rt->old_sigstack, NULL);
        rt->signal_stack_installed = false;
    }
    if (g_live_runtime == rt) {
        g_live_runtime = NULL;
    }
    free(rt->signal_stack);
    rt->signal_stack = NULL;

    if (rt->task_states != NULL) {
        for (i = 0; i < rt->task_count; ++i) {
            free(rt->task_states[i].stack);
        }
    }
    free(rt->task_states);
    rt->task_states = NULL;

    queue_free(&rt->rr_queue);
}

static bool live_enqueue_rr_arrivals(live_runtime_t *rt, int now_t2) {
    size_t i;

    for (i = 0; i < rt->task_count; ++i) {
        if (rt->tasks[i].arrival_t2 == now_t2 && rt->tasks[i].remaining_t2 > 0) {
            if (!queue_push(&rt->rr_queue, (int)i)) {
                return false;
            }
        }
    }
    return true;
}

static bool live_init_contexts(live_runtime_t *rt) {
    volatile size_t i;

    rt->task_states = (live_task_state_t *)calloc(rt->task_count, sizeof(live_task_state_t));
    if (rt->task_states == NULL) {
        return false;
    }

    for (i = 0; i < rt->task_count; ++i) {
        if (getcontext(&rt->task_states[i].context) != 0) {
            return false;
        }
        rt->task_states[i].stack = (unsigned char *)malloc(LIVE_STACK_SIZE);
        if (rt->task_states[i].stack == NULL) {
            return false;
        }
        rt->task_states[i].context.uc_link = &rt->scheduler_context;
        rt->task_states[i].context.uc_stack.ss_sp = rt->task_states[i].stack;
        rt->task_states[i].context.uc_stack.ss_size = LIVE_STACK_SIZE;
        rt->task_states[i].context.uc_stack.ss_flags = 0;
        rt->task_states[i].context.uc_sigmask = rt->timer_block_mask;
        makecontext(&rt->task_states[i].context, (void (*)(void))live_worker_entry, 1,
                    (uintptr_t)(i + 1u));
    }

    return true;
}

static bool live_start_timer(live_runtime_t *rt) {
    struct sigaction action;
    struct itimerval timer;
    stack_t ss;

    rt->signal_stack = (unsigned char *)malloc(SIGNAL_STACK_SIZE);
    if (rt->signal_stack == NULL) {
        return false;
    }

    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = rt->signal_stack;
    ss.ss_size = SIGNAL_STACK_SIZE;
    if (sigaltstack(&ss, &rt->old_sigstack) != 0) {
        free(rt->signal_stack);
        rt->signal_stack = NULL;
        return false;
    }
    rt->signal_stack_installed = true;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = live_timer_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGALRM);

    if (sigaction(SIGALRM, &action, &rt->old_action) != 0) {
        sigaltstack(&rt->old_sigstack, NULL);
        rt->signal_stack_installed = false;
        free(rt->signal_stack);
        rt->signal_stack = NULL;
        return false;
    }
    rt->action_installed = true;

    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = (time_t)(rt->tick_us / 1000000u);
    timer.it_interval.tv_usec = (suseconds_t)(rt->tick_us % 1000000u);
    timer.it_value = timer.it_interval;
    if (timer.it_value.tv_sec == 0 && timer.it_value.tv_usec == 0) {
        timer.it_value.tv_usec = (suseconds_t)DEFAULT_LIVE_TICK_US;
        timer.it_interval.tv_usec = (suseconds_t)DEFAULT_LIVE_TICK_US;
    }

    g_live_runtime = rt;
    if (setitimer(ITIMER_REAL, &timer, &rt->old_timer) != 0) {
        g_live_runtime = NULL;
        sigaction(SIGALRM, &rt->old_action, NULL);
        rt->action_installed = false;
        sigaltstack(&rt->old_sigstack, NULL);
        rt->signal_stack_installed = false;
        free(rt->signal_stack);
        rt->signal_stack = NULL;
        return false;
    }
    rt->timer_installed = true;
    return true;
}

static int live_pick_next(live_runtime_t *rt) {
    int next = -1;

    if (rt->algorithm == ALG_SRT) {
        return pick_srt(rt->tasks, rt->task_count, rt->now_t2);
    }

    if (rt->rr_current >= 0 && rt->rr_slice_left_t2 > 0 &&
        task_is_runnable(&rt->tasks[(size_t)rt->rr_current], rt->now_t2)) {
        return rt->rr_current;
    }

    rt->rr_current = -1;
    if (queue_pop(&rt->rr_queue, &next)) {
        rt->rr_current = next;
        rt->rr_slice_left_t2 = RR_QUANTUM_UNITS * TIME_SCALE;
        return next;
    }

    return -1;
}

static bool live_process_tick(live_runtime_t *rt) {
    int ran = rt->tick_has_source ? (int)rt->tick_source : -1;

    rt->tick_pending = 0;
    rt->tick_has_source = 0;
    rt->tick_source = 0;
    rt->current = -1;

    if (ran >= 0) {
        task_t *task = &rt->tasks[(size_t)ran];

        emulate_cpu_tick(task, rt->now_t2);
        task->remaining_t2--;
        rt->busy_t2++;

        if (rt->algorithm == ALG_RR3) {
            rt->rr_slice_left_t2--;
        }

        if (!trace_append(&rt->trace, rt->now_t2, rt->now_t2 + 1, (int)task->id)) {
            return false;
        }

        if (task->remaining_t2 == 0) {
            task->finish_t2 = rt->now_t2 + 1;
            rt->completed++;
            rt->logical_current = -1;
            if (rt->algorithm == ALG_RR3) {
                rt->rr_current = -1;
                rt->rr_slice_left_t2 = 0;
            }
        } else if (rt->algorithm == ALG_RR3 && rt->rr_slice_left_t2 == 0) {
            if (!queue_push(&rt->rr_queue, ran)) {
                return false;
            }
            rt->rr_current = -1;
            rt->logical_current = -1;
        }
    } else {
        if (!trace_append(&rt->trace, rt->now_t2, rt->now_t2 + 1, -1)) {
            return false;
        }
        rt->logical_current = -1;
    }

    rt->now_t2++;
    if (rt->algorithm == ALG_RR3) {
        if (!live_enqueue_rr_arrivals(rt, rt->now_t2)) {
            return false;
        }
    }

    return true;
}

static bool live_wait_for_tick(live_runtime_t *rt) {
    while (!rt->tick_pending) {
        if (sigsuspend(&rt->wait_mask) == -1 && errno != EINTR) {
            return false;
        }
    }
    return true;
}

static bool live_dispatch(live_runtime_t *rt, int next) {
    if (next != rt->logical_current) {
        cpu_context_t *from =
            (rt->logical_current >= 0) ? &rt->tasks[(size_t)rt->logical_current].ctx : NULL;
        cpu_context_t *to = &rt->tasks[(size_t)next].ctx;
        context_switch(from, to, &rt->metrics);
        rt->logical_current = next;
    }

    if (rt->tasks[(size_t)next].first_run_t2 < 0) {
        rt->tasks[(size_t)next].first_run_t2 = rt->now_t2;
    }
    rt->dispatch_started = 1;
    rt->current = next;

    if (swapcontext(&rt->scheduler_context, &rt->task_states[(size_t)next].context) != 0) {
        return false;
    }
    return true;
}

static bool live_run_loop(live_runtime_t *rt) {
    while (rt->completed < rt->task_count) {
        volatile int next;

        if (rt->tick_pending) {
            if (!live_process_tick(rt)) {
                return false;
            }
        }

        if (rt->completed >= rt->task_count) {
            break;
        }

        next = live_pick_next(rt);
        if (next < 0) {
            if (rt->logical_current >= 0) {
                context_switch(&rt->tasks[(size_t)rt->logical_current].ctx, NULL,
                               &rt->metrics);
                rt->logical_current = -1;
            }
            if (!live_wait_for_tick(rt)) {
                return false;
            }
            continue;
        }

        if (!live_dispatch(rt, next)) {
            return false;
        }
    }

    rt->scheduler_jump_ready = 0;
    return true;
}

static bool SPO_UNUSED run_live_scheduler(const task_t *base_tasks, size_t count,
                                          algorithm_t algorithm, unsigned tick_us,
                                          sim_result_t *result) {
    live_runtime_t rt;

    memset(&rt, 0, sizeof(rt));
    memset(result, 0, sizeof(*result));

    rt.task_count = count;
    rt.algorithm = algorithm;
    rt.tick_us = (tick_us == 0u) ? DEFAULT_LIVE_TICK_US : tick_us;
    rt.current = -1;
    rt.logical_current = -1;
    rt.rr_current = -1;
    rt.tick_has_source = 0;
    rt.tick_source = 0;

    rt.tasks = (task_t *)malloc(count * sizeof(task_t));
    if (rt.tasks == NULL) {
        return false;
    }
    memcpy(rt.tasks, base_tasks, count * sizeof(task_t));

    if (!queue_init(&rt.rr_queue, count + 4u)) {
        free(rt.tasks);
        return false;
    }

    sigemptyset(&rt.timer_block_mask);
    sigaddset(&rt.timer_block_mask, SIGALRM);

    if (!live_init_contexts(&rt)) {
        live_destroy(&rt);
        free(rt.tasks);
        return false;
    }

    if (sigprocmask(SIG_BLOCK, &rt.timer_block_mask, &rt.saved_mask) != 0) {
        live_destroy(&rt);
        free(rt.tasks);
        return false;
    }

    rt.wait_mask = rt.saved_mask;
    sigdelset(&rt.wait_mask, SIGALRM);

    if (rt.algorithm == ALG_RR3) {
        if (!live_enqueue_rr_arrivals(&rt, 0)) {
            live_destroy(&rt);
            sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
            free(rt.tasks);
            return false;
        }
    }

    if (!live_start_timer(&rt)) {
        live_destroy(&rt);
        sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
        free(rt.tasks);
        return false;
    }

    if (!live_run_loop(&rt)) {
        live_destroy(&rt);
        sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
        free(rt.tasks);
        trace_free(&rt.trace);
        return false;
    }

    finalize_metrics(rt.tasks, count, rt.now_t2, rt.busy_t2, &rt.metrics);

    result->tasks = rt.tasks;
    result->task_count = count;
    result->trace = rt.trace;
    result->metrics = rt.metrics;

    live_destroy(&rt);
    sigprocmask(SIG_SETMASK, &rt.saved_mask, NULL);
    return true;
}
#endif

static const char *execution_mode_label(void) {
#ifdef _WIN32
    return "simulation fallback (Windows build)";
#else
    return "timer-driven user-space context scheduler (ucontext + SIGALRM)";
#endif
}

static bool run_scheduler_execution(const task_t *tasks, size_t count, algorithm_t algorithm,
                                    unsigned tick_us, sim_result_t *result) {
#ifdef _WIN32
    return simulate_model(tasks, count, algorithm, tick_us, result);
#else
    return run_live_scheduler(tasks, count, algorithm, tick_us, result);
#endif
}

#ifdef _WIN32
static int run_demo_threads(unsigned tick_us) {
    (void)tick_us;
    fputs("--demo-threads requires POSIX/WSL because it uses ucontext and SIGALRM.\n",
          stderr);
    return 8;
}
#endif

int main(int argc, char **argv) {
    cli_options_t opts;
    task_t *workload = NULL;
    sim_result_t srt = {0};
    sim_result_t rr = {0};
    bool best_is_srt;

    if (!parse_cli(argc, argv, &opts)) {
        print_usage(argv[0]);
        return 1;
    }

    if (opts.self_test) {
        if (!run_self_tests()) {
            return 2;
        }
        return 0;
    }
    if (opts.demo_threads) {
        return run_demo_threads(opts.tick_us);
    }

    workload = (task_t *)calloc(opts.task_count, sizeof(task_t));
    if (workload == NULL) {
        fprintf(stderr, "Allocation failure for %zu tasks\n", opts.task_count);
        return 3;
    }

    if (!generate_variant18_workload(workload, opts.task_count, opts.seed)) {
        fprintf(stderr, "Failed to generate workload for variant %d\n", VARIANT_ID);
        free(workload);
        return 4;
    }

    print_workload(workload, opts.task_count);
    printf("\nExecution mode: %s\n", execution_mode_label());

    if (!run_scheduler_execution(workload, opts.task_count, ALG_SRT, opts.tick_us, &srt)) {
        fprintf(stderr, "SRT execution failed\n");
        free(workload);
        return 5;
    }
    if (!run_scheduler_execution(workload, opts.task_count, ALG_RR3, opts.tick_us, &rr)) {
        cleanup_result(&srt);
        fprintf(stderr, "RR(3) execution failed\n");
        free(workload);
        return 6;
    }

    print_result_table("SRT", &srt);
    if (!opts.no_trace) {
        print_trace(&srt.trace);
    }

    print_result_table("RR(3)", &rr);
    if (!opts.no_trace) {
        print_trace(&rr.trace);
    }

    best_is_srt = srt.metrics.avg_waiting <= rr.metrics.avg_waiting;
    printf("\nVariant %d is even, so the criterion is minimal average waiting time.\n",
           VARIANT_ID);
    printf("Best algorithm for this test: %s\n", best_is_srt ? "SRT" : "RR(3)");

    cleanup_result(&srt);
    cleanup_result(&rr);
    free(workload);
    return 0;
}
