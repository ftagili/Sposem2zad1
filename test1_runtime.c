#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>

typedef long long hlvm_i64;
typedef hlvm_i64 (*proc_fn_t)(hlvm_i64);

typedef struct Pipe Pipe;
typedef struct Stream Stream;

struct Pipe {
  hlvm_i64 *buffer;
  size_t capacity;
  size_t head;
  size_t count;
  int open_writers;
  int is_stdout_device;
};

struct Stream {
  Pipe *pipe;
  int is_output;
};

typedef struct {
  hlvm_i64 tag;
  hlvm_i64 output;
  hlvm_i64 repeat_count;
} PrintParams;

typedef struct {
  hlvm_i64 tag;
  hlvm_i64 left;
  hlvm_i64 right;
  hlvm_i64 output;
} JoinParams;

typedef enum {
  THREAD_READY = 0,
  THREAD_RUNNING = 1,
  THREAD_BLOCKED = 2,
  THREAD_FINISHED = 3
} thread_state_t;

typedef struct {
  ucontext_t context;
  unsigned char *stack;
  proc_fn_t proc;
  hlvm_i64 params;
  thread_state_t state;
  Pipe *wait_pipe;
} UserThread;

typedef struct {
  UserThread threads[32];
  size_t thread_count;
  size_t rr_cursor;
  int current;
  int quantum_left;
  ucontext_t scheduler_context;
  struct sigaction old_action;
  struct itimerval old_timer;
  int action_installed;
  int timer_installed;
  volatile sig_atomic_t tick_pending;
  unsigned tick_us;
  int failed;
  char error_message[128];
} Runtime;

enum {
  MAX_THREADS = 32,
  STACK_SIZE = 64 * 1024,
  DEFAULT_TICK_US = 1000,
  DEFAULT_QUANTUM_OPS = 5,
  PARAM_TAG_PRINT = 0x50524e54,
  PARAM_TAG_JOIN = 0x4a4f494e
};

static Runtime g_runtime;
static Runtime *g_runtime_active = NULL;
static int g_runtime_ready = 0;

static hlvm_i64 handle_from_ptr(void *ptr) {
  return (hlvm_i64)(intptr_t)ptr;
}

static void *ptr_from_handle(hlvm_i64 handle) {
  return (void *)(intptr_t)handle;
}

static void copy_text(char *dst, size_t dst_size, const char *src) {
  if (dst_size == 0) {
    return;
  }
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_size, "%s", src);
}

static unsigned runtime_tick_us(void) {
  const char *env = getenv("SPO_TASK1_TICK_US");
  if (env && env[0] != '\0') {
    unsigned parsed = (unsigned)strtoul(env, NULL, 10);
    if (parsed > 0) {
      return parsed;
    }
  }
  return DEFAULT_TICK_US;
}

static void runtime_set_error(Runtime *rt, const char *message) {
  if (!rt || rt->failed) {
    return;
  }
  rt->failed = 1;
  copy_text(rt->error_message, sizeof(rt->error_message), message);
}

static Pipe *pipe_create_common(int is_stdout_device) {
  Pipe *pipe = (Pipe *)calloc(1, sizeof(*pipe));
  if (!pipe) {
    return NULL;
  }

  pipe->capacity = 128;
  pipe->buffer = is_stdout_device ? NULL : (hlvm_i64 *)calloc(pipe->capacity, sizeof(*pipe->buffer));
  pipe->is_stdout_device = is_stdout_device;

  if (!is_stdout_device && !pipe->buffer) {
    free(pipe);
    return NULL;
  }

  return pipe;
}

static void pipe_grow(Pipe *pipe) {
  size_t new_capacity;
  hlvm_i64 *new_buffer;
  size_t i;

  if (!pipe || pipe->is_stdout_device) {
    return;
  }

  new_capacity = pipe->capacity ? pipe->capacity * 2 : 128;
  new_buffer = (hlvm_i64 *)calloc(new_capacity, sizeof(*new_buffer));
  if (!new_buffer) {
    return;
  }

  for (i = 0; i < pipe->count; ++i) {
    size_t old_index = (pipe->head + i) % pipe->capacity;
    new_buffer[i] = pipe->buffer[old_index];
  }

  free(pipe->buffer);
  pipe->buffer = new_buffer;
  pipe->capacity = new_capacity;
  pipe->head = 0;
}

static void pipe_push(Pipe *pipe, hlvm_i64 value) {
  size_t index;

  if (!pipe || pipe->is_stdout_device) {
    return;
  }

  if (pipe->count == pipe->capacity) {
    pipe_grow(pipe);
  }

  if (pipe->count == pipe->capacity) {
    return;
  }

  index = (pipe->head + pipe->count) % pipe->capacity;
  pipe->buffer[index] = value;
  pipe->count += 1;
}

static int pipe_ready_for_reader(const Pipe *pipe) {
  return pipe && (pipe->count > 0 || pipe->open_writers <= 0);
}

static void runtime_timer_handler(int signo) {
  (void)signo;
  if (g_runtime_active) {
    g_runtime_active->tick_pending = 1;
  }
}

static void runtime_wake_waiters(Runtime *rt) {
  size_t i;

  if (!rt) {
    return;
  }

  for (i = 0; i < rt->thread_count; ++i) {
    UserThread *thread = &rt->threads[i];
    if (thread->state == THREAD_BLOCKED && pipe_ready_for_reader(thread->wait_pipe)) {
      thread->state = THREAD_READY;
      thread->wait_pipe = NULL;
    }
  }
}

static void thread_yield_current(void) {
  Runtime *rt = g_runtime_active;
  int current;

  if (!rt || rt->current < 0) {
    return;
  }

  current = rt->current;
  rt->threads[(size_t)current].state = THREAD_READY;
  rt->current = -1;
  swapcontext(&rt->threads[(size_t)current].context, &rt->scheduler_context);
}

static void thread_block_current(Pipe *pipe) {
  Runtime *rt = g_runtime_active;
  UserThread *thread;

  if (!rt || rt->current < 0) {
    return;
  }

  thread = &rt->threads[(size_t)rt->current];
  thread->state = THREAD_BLOCKED;
  thread->wait_pipe = pipe;
  rt->current = -1;
  swapcontext(&thread->context, &rt->scheduler_context);
}

static void thread_exit_current(void) {
  Runtime *rt = g_runtime_active;
  int current;

  if (!rt || rt->current < 0) {
    return;
  }

  current = rt->current;
  rt->threads[(size_t)current].state = THREAD_FINISHED;
  rt->threads[(size_t)current].wait_pipe = NULL;
  rt->current = -1;
  swapcontext(&rt->threads[(size_t)current].context, &rt->scheduler_context);
}

static void runtime_reschedule_point(int force_yield) {
  Runtime *rt = g_runtime_active;

  if (!rt || rt->current < 0) {
    return;
  }

  if (rt->tick_pending) {
    rt->tick_pending = 0;
    thread_yield_current();
    return;
  }

  if (force_yield && rt->thread_count > 1) {
    rt->quantum_left -= 1;
    if (rt->quantum_left <= 0) {
      rt->quantum_left = 0;
      thread_yield_current();
    }
  }
}

static void thread_trampoline(uintptr_t index) {
  Runtime *rt = g_runtime_active;
  UserThread *thread;

  if (!rt || index >= rt->thread_count) {
    return;
  }

  thread = &rt->threads[(size_t)index];
  if (thread->proc) {
    (void)thread->proc(thread->params);
  }

  thread_exit_current();
}

static int runtime_init(Runtime *rt) {
  if (!rt) {
    return 0;
  }

  memset(rt, 0, sizeof(*rt));
  rt->current = -1;
  rt->rr_cursor = (size_t)-1;
  rt->tick_us = runtime_tick_us();
  return 1;
}

static int runtime_create_thread(Runtime *rt, proc_fn_t proc, hlvm_i64 params) {
  UserThread *thread;
  size_t index;

  if (!rt || !proc) {
    return 0;
  }

  index = rt->thread_count;
  if (index >= MAX_THREADS) {
    runtime_set_error(rt, "too many logical threads");
    return 0;
  }

  thread = &rt->threads[index];
  memset(thread, 0, sizeof(*thread));
  thread->proc = proc;
  thread->params = params;
  thread->state = THREAD_READY;
  thread->stack = (unsigned char *)malloc(STACK_SIZE);
  if (!thread->stack) {
    runtime_set_error(rt, "thread stack allocation failed");
    return 0;
  }

  if (getcontext(&thread->context) != 0) {
    free(thread->stack);
    thread->stack = NULL;
    runtime_set_error(rt, "getcontext failed");
    return 0;
  }

  thread->context.uc_link = &rt->scheduler_context;
  thread->context.uc_stack.ss_sp = thread->stack;
  thread->context.uc_stack.ss_size = STACK_SIZE;
  thread->context.uc_stack.ss_flags = 0;
  makecontext(&thread->context, (void (*)(void))thread_trampoline, 1, (uintptr_t)index);

  rt->thread_count += 1;
  return 1;
}

static int runtime_start_timer(Runtime *rt) {
  struct sigaction action;
  struct itimerval timer;

  if (!rt) {
    return 0;
  }

  memset(&action, 0, sizeof(action));
  action.sa_handler = runtime_timer_handler;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGALRM, &action, &rt->old_action) != 0) {
    return 0;
  }
  rt->action_installed = 1;

  memset(&timer, 0, sizeof(timer));
  timer.it_interval.tv_sec = (time_t)(rt->tick_us / 1000000u);
  timer.it_interval.tv_usec = (suseconds_t)(rt->tick_us % 1000000u);
  timer.it_value = timer.it_interval;
  if (timer.it_value.tv_sec == 0 && timer.it_value.tv_usec == 0) {
    timer.it_interval.tv_usec = (suseconds_t)DEFAULT_TICK_US;
    timer.it_value.tv_usec = (suseconds_t)DEFAULT_TICK_US;
  }

  if (setitimer(ITIMER_REAL, &timer, &rt->old_timer) != 0) {
    sigaction(SIGALRM, &rt->old_action, NULL);
    rt->action_installed = 0;
    return 0;
  }

  rt->timer_installed = 1;
  return 1;
}

static void runtime_destroy(Runtime *rt) {
  size_t i;

  if (!rt) {
    return;
  }

  if (rt->timer_installed) {
    setitimer(ITIMER_REAL, &rt->old_timer, NULL);
    rt->timer_installed = 0;
  }

  if (rt->action_installed) {
    sigaction(SIGALRM, &rt->old_action, NULL);
    rt->action_installed = 0;
  }

  for (i = 0; i < rt->thread_count; ++i) {
    free(rt->threads[i].stack);
    rt->threads[i].stack = NULL;
  }

  if (g_runtime_active == rt) {
    g_runtime_active = NULL;
  }
}

static size_t runtime_live_thread_count(const Runtime *rt) {
  size_t count = 0;
  size_t i;

  if (!rt) {
    return 0;
  }

  for (i = 0; i < rt->thread_count; ++i) {
    if (rt->threads[i].state != THREAD_FINISHED) {
      count += 1;
    }
  }

  return count;
}

static int runtime_pick_next(Runtime *rt) {
  size_t offset;

  if (!rt || rt->thread_count == 0) {
    return -1;
  }

  for (offset = 0; offset < rt->thread_count; ++offset) {
    size_t idx = (rt->rr_cursor + 1 + offset) % rt->thread_count;
    if (rt->threads[idx].state == THREAD_READY) {
      rt->rr_cursor = idx;
      return (int)idx;
    }
  }

  return -1;
}

static int runtime_run(Runtime *rt) {
  if (!rt) {
    return 0;
  }

  g_runtime_active = rt;
  if (!runtime_start_timer(rt)) {
    runtime_set_error(rt, "failed to start SIGALRM timer");
    return 0;
  }

  while (!rt->failed && runtime_live_thread_count(rt) > 0) {
    int next;

    runtime_wake_waiters(rt);
    next = runtime_pick_next(rt);
    if (next < 0) {
      runtime_wake_waiters(rt);
      next = runtime_pick_next(rt);
      if (next < 0) {
        runtime_set_error(rt, "deadlock: no runnable logical threads");
        break;
      }
    }

    rt->current = next;
    rt->quantum_left = DEFAULT_QUANTUM_OPS;
    rt->threads[(size_t)next].state = THREAD_RUNNING;
    if (swapcontext(&rt->scheduler_context, &rt->threads[(size_t)next].context) != 0) {
      runtime_set_error(rt, "swapcontext failed");
      break;
    }
    rt->current = -1;
  }

  return rt->failed ? 0 : 1;
}

hlvm_i64 openPipeDevice(hlvm_i64 address) {
  Pipe *pipe = pipe_create_common(address == 0x030000);
  return handle_from_ptr(pipe);
}

hlvm_i64 createPipe(void) {
  Pipe *pipe = pipe_create_common(0);
  return handle_from_ptr(pipe);
}

hlvm_i64 getInputStream(hlvm_i64 pipe_handle) {
  Pipe *pipe = (Pipe *)ptr_from_handle(pipe_handle);
  Stream *stream = (Stream *)calloc(1, sizeof(*stream));

  if (!pipe || !stream) {
    free(stream);
    return 0;
  }

  stream->pipe = pipe;
  stream->is_output = 0;
  return handle_from_ptr(stream);
}

hlvm_i64 getOutputStream(hlvm_i64 pipe_handle) {
  Pipe *pipe = (Pipe *)ptr_from_handle(pipe_handle);
  Stream *stream = (Stream *)calloc(1, sizeof(*stream));

  if (!pipe || !stream) {
    free(stream);
    return 0;
  }

  stream->pipe = pipe;
  stream->is_output = 1;
  pipe->open_writers += 1;
  runtime_wake_waiters(g_runtime_active);
  return handle_from_ptr(stream);
}

/* Registers a logical worker in the user-space scheduler. */
hlvm_i64 createThread(hlvm_i64 proc_id, hlvm_i64 params) {
  proc_fn_t proc = (proc_fn_t)(intptr_t)proc_id;
  size_t slot;

  if (!proc) {
    return -1;
  }

  if (!g_runtime_ready) {
    if (!runtime_init(&g_runtime)) {
      return -1;
    }
    g_runtime_ready = 1;
  }

  slot = g_runtime.thread_count;
  if (!runtime_create_thread(&g_runtime, proc, params)) {
    return -1;
  }

  return (hlvm_i64)slot;
}

/* Runs the user-space scheduler until every logical worker finishes. */
hlvm_i64 waitAllThreads(void) {
  int ok = 1;

  if (g_runtime_ready) {
    ok = runtime_run(&g_runtime);
    runtime_destroy(&g_runtime);
    g_runtime_ready = 0;
    memset(&g_runtime, 0, sizeof(g_runtime));
  }

  return ok ? 0 : 1;
}

hlvm_i64 makePrintParams(hlvm_i64 output, hlvm_i64 repeat_count) {
  PrintParams *params = (PrintParams *)calloc(1, sizeof(*params));
  if (!params) {
    return 0;
  }

  params->tag = PARAM_TAG_PRINT;
  params->output = output;
  params->repeat_count = repeat_count;
  return handle_from_ptr(params);
}

hlvm_i64 makeJoinParams(hlvm_i64 left, hlvm_i64 right, hlvm_i64 output) {
  JoinParams *params = (JoinParams *)calloc(1, sizeof(*params));
  if (!params) {
    return 0;
  }

  params->tag = PARAM_TAG_JOIN;
  params->left = left;
  params->right = right;
  params->output = output;
  return handle_from_ptr(params);
}

hlvm_i64 getFirstInput(hlvm_i64 params_handle) {
  JoinParams *params = (JoinParams *)ptr_from_handle(params_handle);
  return (params && params->tag == PARAM_TAG_JOIN) ? params->left : 0;
}

hlvm_i64 getSecondInput(hlvm_i64 params_handle) {
  JoinParams *params = (JoinParams *)ptr_from_handle(params_handle);
  return (params && params->tag == PARAM_TAG_JOIN) ? params->right : 0;
}

hlvm_i64 getOutput(hlvm_i64 params_handle) {
  PrintParams *print_params = (PrintParams *)ptr_from_handle(params_handle);
  JoinParams *join_params = (JoinParams *)ptr_from_handle(params_handle);

  if (print_params && print_params->tag == PARAM_TAG_PRINT) {
    return print_params->output;
  }

  if (join_params && join_params->tag == PARAM_TAG_JOIN) {
    return join_params->output;
  }

  return 0;
}

hlvm_i64 getRepeatCount(hlvm_i64 params_handle) {
  PrintParams *params = (PrintParams *)ptr_from_handle(params_handle);
  return (params && params->tag == PARAM_TAG_PRINT) ? params->repeat_count : 0;
}

hlvm_i64 canRead(hlvm_i64 input_handle) {
  Stream *stream = (Stream *)ptr_from_handle(input_handle);
  Pipe *pipe;

  if (!stream || !stream->pipe) {
    return 0;
  }

  pipe = stream->pipe;
  for (;;) {
    runtime_reschedule_point(0);
    if (pipe->count > 0) {
      return 1;
    }
    if (pipe->open_writers <= 0) {
      return 0;
    }
    if (!g_runtime_active || g_runtime_active->current < 0) {
      return 0;
    }
    thread_block_current(pipe);
  }
}

hlvm_i64 readInt(hlvm_i64 input_handle) {
  Stream *stream = (Stream *)ptr_from_handle(input_handle);
  Pipe *pipe;
  hlvm_i64 value = 0;

  if (!stream || !stream->pipe) {
    return 0;
  }

  pipe = stream->pipe;
  for (;;) {
    runtime_reschedule_point(0);
    if (pipe->count > 0) {
      value = pipe->buffer[pipe->head];
      pipe->head = (pipe->head + 1) % pipe->capacity;
      pipe->count -= 1;
      runtime_reschedule_point(1);
      return value;
    }
    if (pipe->open_writers <= 0) {
      return 0;
    }
    if (!g_runtime_active || g_runtime_active->current < 0) {
      return 0;
    }
    thread_block_current(pipe);
  }
}

hlvm_i64 writeInt(hlvm_i64 output_handle, hlvm_i64 value) {
  Stream *stream = (Stream *)ptr_from_handle(output_handle);
  Pipe *pipe;

  if (!stream || !stream->pipe || !stream->is_output) {
    return -1;
  }

  pipe = stream->pipe;
  runtime_reschedule_point(0);

  if (pipe->is_stdout_device) {
    printf("%lld\n", value);
    fflush(stdout);
  } else {
    pipe_push(pipe, value);
    runtime_wake_waiters(g_runtime_active);
  }

  runtime_reschedule_point(1);
  return 0;
}

hlvm_i64 closeWriter(hlvm_i64 output_handle) {
  Stream *stream = (Stream *)ptr_from_handle(output_handle);
  Pipe *pipe;

  if (!stream || !stream->pipe || !stream->is_output) {
    return -1;
  }

  pipe = stream->pipe;
  if (pipe->open_writers > 0) {
    pipe->open_writers -= 1;
  }
  runtime_wake_waiters(g_runtime_active);
  runtime_reschedule_point(1);
  return 0;
}
