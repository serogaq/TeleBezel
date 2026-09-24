#include "fake.h"
#include "send_tracker.h"

typedef struct {
  int changes;
  TbSendStatus last;
} Observer;

static void observed(void *context, const TbSendStatus *status) {
  Observer *observer = context;
  observer->changes++;
  observer->last = *status;
}

static void state_record(Buf *buf, uint8_t type, uint32_t draft, uint8_t state, uint8_t code, uint8_t flags, const char *account,
                         const char *chat, const char *message) {
  begin(buf, type);
  put32(buf, draft); put8(buf, state); put8(buf, code); put16(buf, 0); put8(buf, flags);
  putstr8(buf, account); putstr8(buf, chat); putstr8(buf, message); putstr8(buf, "Family"); putstr8(buf, "On my way");
  end(buf);
}

static const char *ACCOUNT = "00112233-4455-4677-8899-aabbccddeeff";

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  Observer observer = {0};
  TbSendTracker tracker;
  tb_send_init(&tracker, &layer, (TbSendPorts){observed, &observer});
  TbSendTarget target = {0};
  strcpy(target.account, ACCOUNT);
  strcpy(target.chat, "-1009007199254740993");
  strcpy(target.reply, "55");

  assert(tb_send_start(&tracker, &target, 7, false, "Family", "On my way"));
  assert(fake.sends == 1 && fake_last(&fake)->kind == TB_REQUEST_SEND && fake_last(&fake)->draft_id == 7);
  assert(strcmp(fake_last(&fake)->message, "55") == 0 && fake_last(&fake)->attempt == 0);
  assert(tb_send_busy(&tracker) && observer.last.phase == TB_SENDING_SUBMITTING);
  assert(tb_send_start(&tracker, &target, 7, false, "Family", "On my way"));
  assert(fake.sends == 1);
  TbSendTarget elsewhere = target;
  strcpy(elsewhere.chat, "42");
  assert(!tb_send_start(&tracker, &elsewhere, 8, false, "Other", "x"));
  assert(fake.sends == 1);

  Buf other = {0};
  state_record(&other, TB_RECORD_SEND_STATE, 99, TB_SEND_STATE_SENT, 0, 0, ACCOUNT, "42", "5");
  assert(!tb_send_payload(&tracker, other.data, other.length));
  assert(tracker.status.phase == TB_SENDING_SUBMITTING);

  Buf moved = {0};
  state_record(&moved, TB_RECORD_SEND_STATE, 7, TB_SEND_STATE_SENT, 0, 0, ACCOUNT, "42", "5");
  assert(!tb_send_payload(&tracker, moved.data, moved.length));

  Buf pending = {0};
  state_record(&pending, TB_RECORD_SEND_STATE, 7, TB_SEND_STATE_PENDING, 0, 0, ACCOUNT, "-1009007199254740993", "");
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &pending, 0, 1);
  assert(observer.last.phase == TB_SENDING_PENDING && tb_send_busy(&tracker));
  const int seen = observer.changes;
  assert(tb_send_payload(&tracker, pending.data, pending.length));
  assert(observer.changes == seen);

  Buf sent = {0};
  state_record(&sent, TB_RECORD_SEND_STATE, 7, TB_SEND_STATE_SENT, 0, TB_SEND_FLAG_REPLY_DROPPED, ACCOUNT, "-1009007199254740993", "900");
  assert(tb_send_payload(&tracker, sent.data, sent.length));
  assert(observer.last.phase == TB_SENDING_SENT && strcmp(observer.last.message, "900") == 0);
  assert(observer.last.flags & TB_SEND_FLAG_REPLY_DROPPED);
  assert(tb_send_payload(&tracker, pending.data, pending.length));
  assert(tracker.status.phase == TB_SENDING_SENT);
  assert(!tb_send_start(&tracker, &target, 7, true, "Family", "On my way"));

  assert(tb_send_start(&tracker, &elsewhere, 8, false, "Other", "Hi"));
  const uint32_t second = fake_last_sequence(&fake);
  deliver(&layer, second, TB_RESULT_SEND_FORBIDDEN, 0, NULL, 0, 1);
  assert(observer.last.phase == TB_SENDING_FAILED && observer.last.code == TB_RESULT_SEND_FORBIDDEN);
  assert(!tb_send_start(&tracker, &elsewhere, 8, true, "Other", "Hi"));

  Buf limited = {0};
  state_record(&limited, TB_RECORD_SEND_STATE, 9, TB_SEND_STATE_FAILED, TB_RESULT_SEND_RATE_LIMITED, TB_SEND_FLAG_RETRYABLE, ACCOUNT, "42", "");
  assert(tb_send_start(&tracker, &elsewhere, 9, false, "Other", "Again"));
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &limited, 0, 1);
  assert(observer.last.phase == TB_SENDING_FAILED && (observer.last.flags & TB_SEND_FLAG_RETRYABLE));
  assert(tb_send_start(&tracker, &elsewhere, 9, true, "Other", "Again"));
  assert(fake_last(&fake)->attempt == 1 && fake_last(&fake)->draft_id == 9);

  fake.now += TB_SEND_TIMEOUT;
  tb_requests_tick(&layer);
  fake.now += TB_SEND_TIMEOUT;
  tb_requests_tick(&layer);
  fake.now += TB_SEND_TIMEOUT;
  tb_requests_tick(&layer);
  assert(observer.last.phase == TB_SENDING_UNKNOWN);
  assert(fake_last(&fake)->attempt == 1);
  assert(tb_send_check(&tracker));
  assert(fake_last(&fake)->kind == TB_REQUEST_SEND_CHECK && fake_last(&fake)->draft_id == 9);

  Buf restored = {0};
  state_record(&restored, TB_RECORD_PENDING_SEND, 12, TB_SEND_STATE_PENDING, 0, TB_SEND_FLAG_RESTORED, ACCOUNT, "43", "");
  assert(!tb_send_payload(&tracker, restored.data, restored.length) || tracker.status.draft_id == 12);
  tb_send_close(&tracker);
  TbSendTracker fresh;
  Observer again = {0};
  tb_send_init(&fresh, &layer, (TbSendPorts){observed, &again});
  assert(tb_send_payload(&fresh, restored.data, restored.length));
  assert(fresh.status.restored && fresh.status.draft_id == 12 && strcmp(fresh.status.target.chat, "43") == 0);
  assert(again.last.phase == TB_SENDING_PENDING && strcmp(again.last.title, "Family") == 0);
  tb_send_forget(&fresh, 12);
  assert(fresh.status.draft_id == 12);
  return 0;
}
