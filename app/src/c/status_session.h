#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "status_state.h"

typedef struct {
  bool (*send)(void *context, uint8_t kind, uint32_t sequence);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  void (*render)(void *context, TbStatus status);
  void *context;
} TbStatusPorts;

typedef enum { TB_SESSION_IDLE, TB_SESSION_HELLO, TB_SESSION_STATUS } TbSessionPhase;
typedef struct {
  TbStatusState state;
  TbStatusPorts ports;
  TbSessionPhase phase;
  uint8_t hello_attempts;
  uint8_t status_attempts;
} TbStatusSession;

void tb_status_session_init(TbStatusSession *session, TbStatusPorts ports);
void tb_status_session_start(TbStatusSession *session, bool transport_ready);
void tb_status_session_timeout(TbStatusSession *session);
void tb_status_session_receive(TbStatusSession *session, bool valid, int32_t kind, int32_t sequence, int32_t result);
void tb_status_session_failed(TbStatusSession *session, uint8_t kind, uint32_t sequence);
void tb_status_session_stop(TbStatusSession *session);
