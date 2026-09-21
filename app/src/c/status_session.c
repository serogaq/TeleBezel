#include "status_session.h"
#include "generated/protocol.h"

static void finish(TbStatusSession *session, int32_t code) {
  session->ports.cancel(session->ports.context);
  session->phase = TB_SESSION_IDLE;
  tb_status_state_apply(&session->state, session->state.sequence, code);
  session->ports.render(session->ports.context, session->state.status);
}

static void send_current(TbStatusSession *session) {
  session->ports.cancel(session->ports.context);
  const bool hello = session->phase == TB_SESSION_HELLO;
  uint8_t *attempts = hello ? &session->hello_attempts : &session->status_attempts;
  const uint8_t maximum = hello ? 3 : 2;
  while (*attempts < maximum) {
    ++*attempts;
    if (!hello) {
      tb_status_state_begin(&session->state);
      session->ports.render(session->ports.context, session->state.status);
    }
    if (session->ports.send(session->ports.context, hello ? TB_REQUEST_HELLO : TB_REQUEST_STATUS, session->state.sequence)) {
      if (!session->ports.schedule(session->ports.context, hello ? 1500 : 14000)) {
        finish(session, TB_RESULT_BACKEND_UNAVAILABLE);
      }
      return;
    }
  }
  finish(session, TB_RESULT_BACKEND_UNAVAILABLE);
}

void tb_status_session_init(TbStatusSession *session, TbStatusPorts ports) {
  tb_status_state_init(&session->state);
  session->ports = ports;
  session->phase = TB_SESSION_IDLE;
  session->hello_attempts = 0;
  session->status_attempts = 0;
}

void tb_status_session_start(TbStatusSession *session, bool transport_ready) {
  tb_status_state_begin(&session->state);
  session->hello_attempts = 0;
  session->status_attempts = 0;
  session->phase = TB_SESSION_HELLO;
  if (transport_ready) { send_current(session); }
  else { finish(session, TB_RESULT_BACKEND_UNAVAILABLE); }
}

void tb_status_session_timeout(TbStatusSession *session) {
  if (session->phase != TB_SESSION_IDLE) { send_current(session); }
}

void tb_status_session_receive(TbStatusSession *session, bool valid, int32_t kind, int32_t sequence, int32_t result) {
  if (!valid) {
    if (session->phase != TB_SESSION_IDLE) { finish(session, TB_RESULT_PROTOCOL_ERROR); }
    return;
  }
  if ((kind == TB_RESPONSE_READY && session->phase == TB_SESSION_HELLO && sequence > 0 && (uint32_t)sequence == session->state.sequence) ||
      (kind == TB_RESPONSE_REFRESH && sequence == 0 && session->phase != TB_SESSION_HELLO)) {
    session->phase = TB_SESSION_STATUS;
    session->status_attempts = 0;
    send_current(session);
  } else if (kind == TB_RESPONSE_STATUS && session->phase == TB_SESSION_STATUS && sequence > 0 && (uint32_t)sequence == session->state.sequence) {
    finish(session, result);
  }
}

void tb_status_session_failed(TbStatusSession *session, uint8_t kind, uint32_t sequence) {
  if (sequence != session->state.sequence) { return; }
  if ((kind == TB_REQUEST_HELLO && session->phase == TB_SESSION_HELLO) ||
      (kind == TB_REQUEST_STATUS && session->phase == TB_SESSION_STATUS)) { send_current(session); }
}

void tb_status_session_stop(TbStatusSession *session) {
  session->ports.cancel(session->ports.context);
  session->phase = TB_SESSION_IDLE;
}
