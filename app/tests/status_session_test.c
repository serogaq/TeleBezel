#include <assert.h>
#include "generated/protocol.h"
#include "status_session.h"

typedef struct {
  uint32_t now;
  int sends;
  TbSendResult next;
  bool timer;
  uint8_t kind;
  uint32_t sequence;
  TbStatus status;
} Fake;

static TbSendResult send(void *context, uint8_t kind, uint32_t sequence) {
  Fake *fake = context;
  fake->sends++;
  fake->kind = kind;
  fake->sequence = sequence;
  return fake->next;
}
static bool schedule(void *context, uint32_t delay) { Fake *fake = context; (void)delay; fake->timer = true; return true; }
static void cancel(void *context) { ((Fake *)context)->timer = false; }
static uint32_t now(void *context) { return ((Fake *)context)->now; }
static void render(void *context, TbStatus status) { ((Fake *)context)->status = status; }

static void expire(TbRequestLayer *layer, Fake *fake, uint32_t milliseconds) {
  fake->now += milliseconds;
  tb_requests_tick(layer);
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  TbStatusSession session;
  tb_requests_init(&layer, (TbRequestPorts){send, schedule, cancel, now, &fake});
  tb_status_session_init(&session, &layer, (TbStatusPorts){render, &fake});

  tb_status_session_start(&session, true);
  assert(fake.sends == 1 && fake.kind == TB_REQUEST_HELLO && fake.status == TB_STATUS_CHECKING);
  tb_status_session_receive(&session, true, TB_RESPONSE_READY, (int32_t)fake.sequence, 0);
  assert(fake.kind == TB_REQUEST_STATUS && session.phase == TB_SESSION_STATUS);
  const uint32_t status_sequence = fake.sequence;
  tb_status_session_receive(&session, true, TB_RESPONSE_STATUS, (int32_t)status_sequence + 7, TB_RESULT_OK);
  assert(session.phase == TB_SESSION_STATUS);
  tb_status_session_receive(&session, true, TB_RESPONSE_STATUS, (int32_t)status_sequence, TB_RESULT_OK);
  assert(session.phase == TB_SESSION_IDLE && fake.status == TB_STATUS_CONNECTED && !fake.timer);

  tb_status_session_start(&session, true);
  expire(&layer, &fake, 1500);
  expire(&layer, &fake, 1500);
  expire(&layer, &fake, 1500);
  assert(session.phase == TB_SESSION_IDLE && fake.status == TB_STATUS_BACKEND_UNAVAILABLE);
  tb_status_session_receive(&session, true, TB_RESPONSE_READY, 99, 0);
  assert(session.phase == TB_SESSION_STATUS && fake.kind == TB_REQUEST_STATUS);
  tb_status_session_receive(&session, true, TB_RESPONSE_STATUS, (int32_t)fake.sequence, TB_RESULT_API_UNAUTHORIZED);
  assert(fake.status == TB_STATUS_API_UNAUTHORIZED);

  const int before = fake.sends;
  tb_status_session_retry(&session);
  assert(fake.sends == before + 1 && fake.kind == TB_REQUEST_HELLO && session.phase == TB_SESSION_HELLO);
  tb_status_session_retry(&session);
  assert(fake.sends == before + 1);

  tb_status_session_stop(&session);
  fake.next = TB_SEND_UNREACHABLE;
  tb_status_session_start(&session, true);
  assert(session.phase == TB_SESSION_IDLE && fake.status == TB_STATUS_PHONE_UNREACHABLE);

  fake.next = TB_SEND_OK;
  tb_status_session_start(&session, true);
  tb_requests_send_failed(&layer, fake.sequence, true);
  tb_requests_send_failed(&layer, fake.sequence, true);
  tb_requests_send_failed(&layer, fake.sequence, true);
  assert(fake.status == TB_STATUS_PHONE_UNREACHABLE);

  tb_status_session_start(&session, false);
  assert(fake.status == TB_STATUS_BACKEND_UNAVAILABLE);
  tb_status_session_receive(&session, true, TB_RESPONSE_REFRESH, 0, 0);
  assert(session.phase == TB_SESSION_STATUS);
  tb_status_session_receive(&session, false, 0, 0, 0);
  assert(session.phase == TB_SESSION_IDLE && fake.status == TB_STATUS_PROTOCOL_ERROR);
  return 0;
}
