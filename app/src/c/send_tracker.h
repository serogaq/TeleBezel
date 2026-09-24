#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "codec.h"
#include "request_layer.h"

#define TB_SEND_TITLE_SIZE 33
#define TB_SEND_PREVIEW_SIZE 41
#define TB_SEND_TIMEOUT 25000
#define TB_SEND_ATTEMPTS 3

typedef enum { TB_SENDING_IDLE, TB_SENDING_SUBMITTING, TB_SENDING_PENDING, TB_SENDING_SENT, TB_SENDING_FAILED, TB_SENDING_UNKNOWN } TbSendPhase;

typedef struct {
  char account[TB_ACCOUNT_ID_SIZE];
  char chat[TB_TELEGRAM_ID_SIZE];
  char reply[TB_TELEGRAM_ID_SIZE];
} TbSendTarget;

typedef struct {
  uint32_t draft_id;
  TbSendPhase phase;
  int32_t code;
  uint16_t retry_after;
  uint8_t flags;
  bool restored;
  TbSendTarget target;
  char message[TB_TELEGRAM_ID_SIZE];
  char title[TB_SEND_TITLE_SIZE];
  char preview[TB_SEND_PREVIEW_SIZE];
} TbSendStatus;

typedef struct {
  void (*changed)(void *context, const TbSendStatus *status);
  void *context;
} TbSendPorts;

typedef struct {
  TbRequestLayer *requests;
  TbSendPorts ports;
  TbSendStatus status;
  uint32_t pending;
  uint8_t attempt;
} TbSendTracker;

void tb_send_init(TbSendTracker *tracker, TbRequestLayer *requests, TbSendPorts ports);
bool tb_send_busy(const TbSendTracker *tracker);
bool tb_send_start(TbSendTracker *tracker, const TbSendTarget *target, uint32_t draft_id, bool again, const char *title,
                   const char *preview);
bool tb_send_check(TbSendTracker *tracker);
bool tb_send_record(TbSendTracker *tracker, const TbSendStateRecord *record, bool restored);
bool tb_send_payload(TbSendTracker *tracker, const uint8_t *payload, uint16_t length);
void tb_send_forget(TbSendTracker *tracker, uint32_t draft_id);
void tb_send_close(TbSendTracker *tracker);
