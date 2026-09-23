#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB_REQUEST_SLOTS 4
#define TB_ACCOUNT_ID_SIZE 37
#define TB_TELEGRAM_ID_SIZE 21
#define TB_QUEUE_RETRY_MS 100

typedef enum { TB_SEND_OK, TB_SEND_FAILED, TB_SEND_UNREACHABLE, TB_SEND_BUSY } TbSendResult;
typedef enum { TB_OUTCOME_RESPONSE, TB_OUTCOME_TIMEOUT, TB_OUTCOME_UNREACHABLE, TB_OUTCOME_CANCELLED, TB_OUTCOME_PROTOCOL } TbRequestOutcome;

typedef struct {
  uint8_t kind;
  uint8_t page_op;
  uint8_t list;
  uint8_t page_limit;
  uint16_t text_limit;
  char account[TB_ACCOUNT_ID_SIZE];
  char chat[TB_TELEGRAM_ID_SIZE];
  char message[TB_TELEGRAM_ID_SIZE];
} TbRequestArgs;

typedef struct {
  TbRequestOutcome outcome;
  int32_t result;
  uint32_t flags;
  uint32_t retry_after;
  const uint8_t *payload;
  uint16_t length;
  uint16_t index;
  uint16_t total;
  bool final;
} TbResponse;

typedef void (*TbRequestCallback)(void *owner, uint32_t sequence, const TbResponse *response);

typedef struct {
  TbSendResult (*send)(void *context, uint32_t sequence, const TbRequestArgs *args);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  uint32_t (*now)(void *context);
  void *context;
} TbRequestPorts;

typedef struct {
  bool active;
  bool queued;
  bool resend_on_timeout;
  bool unreachable;
  uint8_t attempts;
  uint8_t max_attempts;
  uint16_t next_chunk;
  uint32_t sequence;
  uint32_t timeout;
  uint32_t deadline;
  uint32_t queued_since;
  TbRequestCallback callback;
  void *owner;
  TbRequestArgs args;
} TbRequestSlot;

typedef struct {
  TbRequestPorts ports;
  TbRequestSlot slots[TB_REQUEST_SLOTS];
  uint32_t next_sequence;
  bool submitting;
  TbRequestOutcome immediate;
} TbRequestLayer;

void tb_requests_init(TbRequestLayer *layer, TbRequestPorts ports);
uint32_t tb_requests_submit(TbRequestLayer *layer, const TbRequestArgs *args, uint32_t timeout, uint8_t max_attempts,
                            bool resend_on_timeout, TbRequestCallback callback, void *owner);
bool tb_requests_chunk(TbRequestLayer *layer, uint32_t sequence, const TbResponse *chunk);
bool tb_requests_response(TbRequestLayer *layer, uint32_t sequence, int32_t result);
void tb_requests_send_failed(TbRequestLayer *layer, uint32_t sequence, bool unreachable);
void tb_requests_outbox_ready(TbRequestLayer *layer);
void tb_requests_tick(TbRequestLayer *layer);
void tb_requests_cancel(TbRequestLayer *layer, uint32_t sequence);
void tb_requests_cancel_all(TbRequestLayer *layer);
bool tb_requests_pending(const TbRequestLayer *layer, uint32_t sequence);
TbRequestOutcome tb_requests_immediate_outcome(const TbRequestLayer *layer);
