#include "diag.h"
#if defined(TB_DIAG)
#include <pebble.h>
#include <string.h>

#define TB_DIAG_PERIOD 5000

static uintptr_t s_base;
static uint32_t s_stack_max;
static uint32_t s_heap_min;
static uint32_t s_inbox;
static uint32_t s_outbox;
static TbDiagProbe s_probe;
static AppTimer *s_timer;
static const char *s_window = "none";
static const char *s_stack[8];
static uint8_t s_depth;

static void track_window(const char *event, const char *window) {
  if (strcmp(event, "window_push") == 0) {
    if (s_depth < 8) { s_stack[s_depth++] = window; }
    s_window = window;
  } else if (strcmp(event, "window_pop") == 0) {
    for (uint8_t index = s_depth; index > 0; --index) {
      if (strcmp(s_stack[index - 1], window) == 0) {
        for (uint8_t move = index; move < s_depth; ++move) { s_stack[move - 1] = s_stack[move]; }
        --s_depth;
        break;
      }
    }
    s_window = s_depth ? s_stack[s_depth - 1] : "none";
  } else {
    s_window = window;
  }
}

__attribute__((noinline)) void tb_diag_stack(void) {
  volatile uint8_t marker = 0;
  const uintptr_t here = (uintptr_t)&marker;
  if (s_base && here < s_base) {
    const uint32_t depth = (uint32_t)(s_base - here);
    if (depth > s_stack_max) { s_stack_max = depth; }
  }
}

void tb_diag_event(const char *event, const char *window) {
  tb_diag_stack();
  if (window) { track_window(event, window); }
  const uint32_t free_bytes = (uint32_t)heap_bytes_free();
  if (s_heap_min == 0 || free_bytes < s_heap_min) { s_heap_min = free_bytes; }
  TbDiagCounters counters = {0};
  if (s_probe) { s_probe(&counters); }
  APP_LOG(APP_LOG_LEVEL_INFO, "TBDIAG event=%s window=%s heap_free=%lu heap_min=%lu", event, s_window, (unsigned long)free_bytes,
          (unsigned long)s_heap_min);
  APP_LOG(APP_LOG_LEVEL_INFO, "TBDIAG stack_depth=%lu heap_used=%lu inbox_bytes=%lu outbox_bytes=%lu", (unsigned long)s_stack_max,
          (unsigned long)heap_bytes_used(), (unsigned long)s_inbox, (unsigned long)s_outbox);
  APP_LOG(APP_LOG_LEVEL_INFO, "TBDIAG queued=%u timers=%u items=%u budget=%lu draft_bytes=%u", counters.queued, counters.timers, counters.items,
          (unsigned long)counters.budget, counters.draft_bytes);
}

void tb_diag_start(void *base, TbDiagProbe probe) {
  s_base = (uintptr_t)base;
  s_probe = probe;
  s_heap_min = 0;
  s_stack_max = 0;
  s_depth = 0;
  tb_diag_event("start", "none");
}

void tb_diag_inbox(uint32_t bytes) {
  s_inbox = bytes;
  tb_diag_event("appmsg_in", NULL);
}

void tb_diag_outbox(uint32_t bytes) {
  s_outbox = bytes;
  tb_diag_event("appmsg_out", NULL);
}

static void fired(void *context) {
  (void)context;
  s_timer = app_timer_register(TB_DIAG_PERIOD, fired, NULL);
  tb_diag_event("periodic", NULL);
}

void tb_diag_periodic(bool enabled) {
  if (enabled && !s_timer) {
    s_timer = app_timer_register(TB_DIAG_PERIOD, fired, NULL);
  } else if (!enabled && s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}
#endif
