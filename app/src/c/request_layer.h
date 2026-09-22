#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB_REQUEST_SLOTS 4

typedef enum { TB_SEND_OK, TB_SEND_FAILED, TB_SEND_UNREACHABLE } TbSendResult;
typedef enum { TB_OUTCOME_RESPONSE, TB_OUTCOME_TIMEOUT, TB_OUTCOME_UNREACHABLE, TB_OUTCOME_CANCELLED } TbRequestOutcome;

typedef void (*TbRequestCallback)(void *owner, uint32_t sequence, TbRequestOutcome outcome, int32_t result);

typedef struct {
  TbSendResult (*send)(void *context, uint8_t kind, uint32_t sequence);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  uint32_t (*now)(void *context);
  void *context;
} TbRequestPorts;

typedef struct {
  bool active;
  uint8_t kind;
  uint8_t attempts;
  uint8_t max_attempts;
  bool unreachable;
  uint32_t sequence;
  uint32_t timeout;
  uint32_t deadline;
  TbRequestCallback callback;
  void *owner;
} TbRequestSlot;

typedef struct {
  TbRequestPorts ports;
  TbRequestSlot slots[TB_REQUEST_SLOTS];
  uint32_t next_sequence;
} TbRequestLayer;

void tb_requests_init(TbRequestLayer *layer, TbRequestPorts ports);
uint32_t tb_requests_submit(TbRequestLayer *layer, uint8_t kind, uint32_t timeout, uint8_t max_attempts,
                            TbRequestCallback callback, void *owner, uint32_t *sequence_out);
bool tb_requests_response(TbRequestLayer *layer, uint32_t sequence, int32_t result);
void tb_requests_send_failed(TbRequestLayer *layer, uint32_t sequence, bool unreachable);
void tb_requests_tick(TbRequestLayer *layer);
void tb_requests_cancel(TbRequestLayer *layer, uint32_t sequence);
void tb_requests_cancel_all(TbRequestLayer *layer);
bool tb_requests_pending(const TbRequestLayer *layer, uint32_t sequence);
