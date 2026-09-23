#include "fake.h"

typedef struct {
  int count;
  int outcomes[16];
  int32_t results[16];
  bool finals[16];
} Log;

static void done(void *owner, uint32_t sequence, const TbResponse *response) {
  Log *log = owner;
  (void)sequence;
  log->outcomes[log->count] = (int)response->outcome;
  log->results[log->count] = response->result;
  log->finals[log->count] = response->final;
  log->count++;
}

int main(void) {
  Fake fake = {0};
  Log log = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbRequestArgs args = {.kind = TB_REQUEST_HISTORY, .page_op = TB_PAGE_OP_NEXT};
  strcpy(args.chat, "-1009007199254740993");
  const uint32_t first = tb_requests_submit(&layer, &args, 1000, 2, true, done, &log);
  assert(first != 0 && fake.sends == 1 && strcmp(fake_last(&fake)->chat, "-1009007199254740993") == 0);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_NEXT);
  fake.now = 400;
  const uint32_t second = tb_requests_submit(&layer, &args, 5000, 1, false, done, &log);
  assert(second != 0 && second != first && fake.timer && fake.delay == 600);
  fake.now = 1000;
  tb_requests_tick(&layer);
  assert(fake.sends == 3 && log.count == 0 && fake.delay == 1000);
  assert(tb_requests_response(&layer, second, 42));
  assert(log.count == 1 && log.outcomes[0] == TB_OUTCOME_RESPONSE && log.results[0] == 42 && log.finals[0]);
  assert(!tb_requests_response(&layer, second, 42));
  fake.now = 2000;
  tb_requests_tick(&layer);
  assert(log.count == 2 && log.outcomes[1] == TB_OUTCOME_TIMEOUT && !fake.timer);

  const uint32_t streamed = tb_requests_submit(&layer, &args, 1000, 1, false, done, &log);
  deliver(&layer, streamed, 0, 0, NULL, 0, 3);
  assert(log.count == 3 && !log.finals[2] && tb_requests_pending(&layer, streamed));
  fake.now = 2900;
  tb_requests_tick(&layer);
  assert(tb_requests_pending(&layer, streamed));
  deliver(&layer, streamed, 0, 0, NULL, 1, 3);
  fake.now = 3800;
  tb_requests_tick(&layer);
  assert(tb_requests_pending(&layer, streamed));
  deliver(&layer, streamed, 0, 0, NULL, 2, 3);
  assert(log.count == 5 && log.finals[4] && !tb_requests_pending(&layer, streamed));

  const uint32_t broken = tb_requests_submit(&layer, &args, 1000, 1, false, done, &log);
  deliver(&layer, broken, 0, 0, NULL, 0, 3);
  deliver(&layer, broken, 0, 0, NULL, 2, 3);
  assert(log.count == 7 && log.outcomes[6] == TB_OUTCOME_PROTOCOL && !tb_requests_pending(&layer, broken));

  const uint32_t silent = tb_requests_submit(&layer, &args, 1000, 3, false, done, &log);
  deliver(&layer, silent, 0, 0, NULL, 0, 2);
  fake.now += 1000;
  tb_requests_tick(&layer);
  assert(log.count == 9 && log.outcomes[8] == TB_OUTCOME_TIMEOUT && fake.sends == 6);

  fake.next = TB_SEND_UNREACHABLE;
  assert(tb_requests_submit(&layer, &args, 1000, 3, false, done, &log) == 0);
  assert(log.count == 9 && tb_requests_immediate_outcome(&layer) == TB_OUTCOME_UNREACHABLE);

  fake.next = TB_SEND_BUSY;
  const int before = fake.sends;
  const uint32_t queued = tb_requests_submit(&layer, &args, 1000, 1, false, done, &log);
  assert(tb_requests_pending(&layer, queued) && fake.delay == TB_QUEUE_RETRY_MS && fake.sends == before);
  fake.next = TB_SEND_OK;
  tb_requests_outbox_ready(&layer);
  assert(fake.sends == before + 1 && fake_last_sequence(&fake) == queued);
  tb_requests_send_failed(&layer, queued, false);
  assert(log.count == 10 && log.outcomes[9] == TB_OUTCOME_TIMEOUT);

  fake.next = TB_SEND_BUSY;
  const uint32_t starving = tb_requests_submit(&layer, &args, 300, 1, false, done, &log);
  for (int step = 0; step < 5; ++step) {
    fake.now += TB_QUEUE_RETRY_MS;
    tb_requests_tick(&layer);
  }
  assert(!tb_requests_pending(&layer, starving) && log.outcomes[log.count - 1] == TB_OUTCOME_TIMEOUT);

  fake.next = TB_SEND_OK;
  const uint32_t failed = tb_requests_submit(&layer, &args, 1000, 2, false, done, &log);
  tb_requests_send_failed(&layer, failed, false);
  assert(tb_requests_pending(&layer, failed));
  tb_requests_send_failed(&layer, failed, true);
  assert(log.outcomes[log.count - 1] == TB_OUTCOME_UNREACHABLE);

  const uint32_t cancelled = tb_requests_submit(&layer, &args, 1000, 2, false, done, &log);
  tb_requests_cancel(&layer, cancelled);
  assert(log.outcomes[log.count - 1] == TB_OUTCOME_CANCELLED && !tb_requests_pending(&layer, cancelled));

  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) { assert(tb_requests_submit(&layer, &args, 1000, 1, false, NULL, NULL) != 0); }
  assert(tb_requests_submit(&layer, &args, 1000, 1, false, NULL, NULL) == 0);
  tb_requests_cancel_all(&layer);
  assert(!fake.timer);

  fake.now = UINT32_MAX - 10;
  const uint32_t wrapped = tb_requests_submit(&layer, &args, 100, 1, false, done, &log);
  fake.now = 50;
  tb_requests_tick(&layer);
  assert(tb_requests_pending(&layer, wrapped));
  fake.now = 95;
  tb_requests_tick(&layer);
  assert(!tb_requests_pending(&layer, wrapped));
  return 0;
}
