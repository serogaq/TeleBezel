#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "request_layer.h"
#include "status_state.h"

typedef struct {
  void (*render)(void *context, TbStatus status);
  void *context;
} TbStatusPorts;

typedef enum { TB_SESSION_IDLE, TB_SESSION_HELLO, TB_SESSION_STATUS } TbSessionPhase;
typedef struct {
  TbStatusState state;
  TbStatusPorts ports;
  TbRequestLayer *requests;
  TbSessionPhase phase;
  uint32_t pending;
} TbStatusSession;

void tb_status_session_init(TbStatusSession *session, TbRequestLayer *requests, TbStatusPorts ports);
void tb_status_session_start(TbStatusSession *session, bool transport_ready);
void tb_status_session_retry(TbStatusSession *session);
void tb_status_session_receive(TbStatusSession *session, bool valid, int32_t kind, int32_t sequence, int32_t result);
void tb_status_session_stop(TbStatusSession *session);
