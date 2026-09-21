#include <assert.h>
#include "status_session.h"
#include "generated/protocol.h"
typedef struct { int sends; bool available; bool timer; uint8_t kind; TbStatus status; } Fake;
static bool send(void *context, uint8_t kind, uint32_t sequence) { Fake *f = context; (void)sequence; f->sends++; f->kind = kind; return f->available; }
static bool schedule(void *context, uint32_t delay) { Fake *f = context; assert(delay == 1500 || delay == 14000); f->timer = true; return true; }
static void cancel(void *context) { ((Fake *)context)->timer = false; }
static void render(void *context, TbStatus status) { ((Fake *)context)->status = status; }
int main(void) {
  Fake fake = {0}; fake.available = true;
  TbStatusSession session;
  TbStatusPorts ports = {send, schedule, cancel, render, &fake};
  tb_status_session_init(&session, ports);
  tb_status_session_start(&session, true);
  assert(fake.sends == 1 && fake.kind == TB_REQUEST_HELLO);
  tb_status_session_receive(&session, true, TB_RESPONSE_READY, (int32_t)session.state.sequence, 0);
  assert(fake.kind == TB_REQUEST_STATUS && session.phase == TB_SESSION_STATUS);
  const uint32_t old = session.state.sequence;
  tb_status_session_timeout(&session);
  assert(session.state.sequence != old);
  tb_status_session_receive(&session, true, TB_RESPONSE_STATUS, (int32_t)old, TB_RESULT_OK);
  assert(session.phase == TB_SESSION_STATUS);
  tb_status_session_receive(&session, true, TB_RESPONSE_STATUS, (int32_t)session.state.sequence, TB_RESULT_OK);
  assert(session.phase == TB_SESSION_IDLE && !fake.timer);
  tb_status_session_init(&session, ports); fake.available = false; fake.sends = 0;
  tb_status_session_start(&session, true);
  assert(fake.sends == 3 && session.phase == TB_SESSION_IDLE);
  tb_status_session_stop(&session);
  return 0;
}
