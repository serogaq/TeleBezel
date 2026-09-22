#include "status_session.h"
#include "generated/protocol.h"

#define TB_HELLO_TIMEOUT 1500
#define TB_HELLO_ATTEMPTS 3
#define TB_STATUS_TIMEOUT 14000
#define TB_STATUS_ATTEMPTS 2

static void render(TbStatusSession *session) { session->ports.render(session->ports.context, session->state.status); }

static void settle(TbStatusSession *session, TbStatus status) {
  session->phase = TB_SESSION_IDLE;
  session->pending = 0;
  tb_status_state_set(&session->state, status);
  render(session);
}

static void completed(void *owner, uint32_t sequence, TbRequestOutcome outcome, int32_t result);

static void request_status(TbStatusSession *session) {
  if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
  session->phase = TB_SESSION_STATUS;
  tb_status_state_begin(&session->state);
  render(session);
  if (!tb_requests_submit(session->requests, TB_REQUEST_STATUS, TB_STATUS_TIMEOUT, TB_STATUS_ATTEMPTS, completed, session, &session->pending)) {
    settle(session, TB_STATUS_BACKEND_UNAVAILABLE);
  }
}

static void completed(void *owner, uint32_t sequence, TbRequestOutcome outcome, int32_t result) {
  TbStatusSession *session = owner;
  if (sequence != session->pending || outcome == TB_OUTCOME_CANCELLED) { return; }
  session->pending = 0;
  if (outcome == TB_OUTCOME_UNREACHABLE) {
    settle(session, TB_STATUS_PHONE_UNREACHABLE);
  } else if (outcome == TB_OUTCOME_TIMEOUT) {
    settle(session, TB_STATUS_BACKEND_UNAVAILABLE);
  } else if (session->phase == TB_SESSION_HELLO) {
    request_status(session);
  } else {
    session->phase = TB_SESSION_IDLE;
    tb_status_state_apply(&session->state, session->state.sequence, result);
    render(session);
  }
}

void tb_status_session_init(TbStatusSession *session, TbRequestLayer *requests, TbStatusPorts ports) {
  tb_status_state_init(&session->state);
  session->ports = ports;
  session->requests = requests;
  session->phase = TB_SESSION_IDLE;
  session->pending = 0;
}

void tb_status_session_start(TbStatusSession *session, bool transport_ready) {
  if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
  session->pending = 0;
  tb_status_state_begin(&session->state);
  render(session);
  if (!transport_ready) {
    settle(session, TB_STATUS_BACKEND_UNAVAILABLE);
    return;
  }
  session->phase = TB_SESSION_HELLO;
  if (!tb_requests_submit(session->requests, TB_REQUEST_HELLO, TB_HELLO_TIMEOUT, TB_HELLO_ATTEMPTS, completed, session, &session->pending)) {
    settle(session, TB_STATUS_BACKEND_UNAVAILABLE);
  }
}

void tb_status_session_retry(TbStatusSession *session) {
  if (session->phase == TB_SESSION_IDLE) { tb_status_session_start(session, true); }
}

void tb_status_session_receive(TbStatusSession *session, bool valid, int32_t kind, int32_t sequence, int32_t result) {
  if (!valid) {
    if (session->phase != TB_SESSION_IDLE) {
      if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
      settle(session, TB_STATUS_PROTOCOL_ERROR);
    }
    return;
  }
  if (kind == TB_RESPONSE_READY && sequence > 0) {
    if (session->phase == TB_SESSION_HELLO) { tb_requests_response(session->requests, (uint32_t)sequence, result); }
    else if (session->phase == TB_SESSION_IDLE) { request_status(session); }
  } else if (kind == TB_RESPONSE_REFRESH && sequence == 0 && session->phase != TB_SESSION_HELLO) {
    request_status(session);
  } else if (kind == TB_RESPONSE_STATUS && session->phase == TB_SESSION_STATUS && sequence > 0) {
    tb_requests_response(session->requests, (uint32_t)sequence, result);
  }
}

void tb_status_session_stop(TbStatusSession *session) {
  if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
  session->pending = 0;
  session->phase = TB_SESSION_IDLE;
}
