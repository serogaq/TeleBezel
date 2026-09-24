#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t queued;
  uint16_t timers;
  uint16_t items;
  uint32_t budget;
  uint16_t draft_bytes;
} TbDiagCounters;

typedef void (*TbDiagProbe)(TbDiagCounters *out);

#if defined(TB_DIAG)
void tb_diag_start(void *base, TbDiagProbe probe);
void tb_diag_event(const char *event, const char *window);
void tb_diag_stack(void);
void tb_diag_inbox(uint32_t bytes);
void tb_diag_outbox(uint32_t bytes);
void tb_diag_periodic(bool enabled);
#else
#define tb_diag_start(base, probe) ((void)(base), (void)(probe))
#define tb_diag_event(event, window) ((void)0)
#define tb_diag_stack() ((void)0)
#define tb_diag_inbox(bytes) ((void)(bytes))
#define tb_diag_outbox(bytes) ((void)(bytes))
#define tb_diag_periodic(enabled) ((void)(enabled))
#endif
