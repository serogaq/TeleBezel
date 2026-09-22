#include <assert.h>
#include <stddef.h>
#include "request_layer.h"

typedef struct {
  uint32_t now;
  int sends;
  TbSendResult next;
  bool timer;
  uint32_t delay;
  int outcomes[8];
  int32_t results[8];
  int count;
} Fake;

static TbSendResult send(void *context, uint8_t kind, uint32_t sequence) {
  Fake *fake = context;
  (void)kind; (void)sequence;
  fake->sends++;
  return fake->next;
}
static bool schedule(void *context, uint32_t delay) { Fake *fake = context; fake->timer = true; fake->delay = delay; return true; }
static void cancel(void *context) { ((Fake *)context)->timer = false; }
static uint32_t now(void *context) { return ((Fake *)context)->now; }
static void done(void *owner, uint32_t sequence, TbRequestOutcome outcome, int32_t result) {
  Fake *fake = owner;
  (void)sequence;
  fake->outcomes[fake->count] = (int)outcome;
  fake->results[fake->count] = result;
  fake->count++;
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, (TbRequestPorts){send, schedule, cancel, now, &fake});
  const uint32_t first = tb_requests_submit(&layer, 1, 1000, 2, done, &fake, NULL);
  fake.now = 400;
  const uint32_t second = tb_requests_submit(&layer, 1, 5000, 1, done, &fake, NULL);
  assert(first != 0 && second != 0 && first != second);
  assert(fake.sends == 2 && fake.timer && fake.delay == 600);
  fake.now = 1000;
  tb_requests_tick(&layer);
  assert(fake.sends == 3 && fake.count == 0 && fake.delay == 1000);
  assert(tb_requests_response(&layer, second, 42));
  assert(fake.count == 1 && fake.outcomes[0] == TB_OUTCOME_RESPONSE && fake.results[0] == 42);
  assert(!tb_requests_response(&layer, second, 42));
  fake.now = 2000;
  tb_requests_tick(&layer);
  assert(fake.count == 2 && fake.outcomes[1] == TB_OUTCOME_TIMEOUT && !fake.timer);

  fake.next = TB_SEND_UNREACHABLE;
  tb_requests_submit(&layer, 1, 1000, 3, done, &fake, NULL);
  assert(fake.count == 3 && fake.outcomes[2] == TB_OUTCOME_UNREACHABLE);

  fake.next = TB_SEND_OK;
  const uint32_t failed = tb_requests_submit(&layer, 1, 1000, 2, done, &fake, NULL);
  tb_requests_send_failed(&layer, failed, false);
  assert(tb_requests_pending(&layer, failed));
  tb_requests_send_failed(&layer, failed, true);
  assert(fake.count == 4 && fake.outcomes[3] == TB_OUTCOME_UNREACHABLE);

  const uint32_t cancelled = tb_requests_submit(&layer, 1, 1000, 2, done, &fake, NULL);
  tb_requests_cancel(&layer, cancelled);
  assert(fake.count == 5 && fake.outcomes[4] == TB_OUTCOME_CANCELLED && !tb_requests_pending(&layer, cancelled));

  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) { assert(tb_requests_submit(&layer, 1, 1000, 1, NULL, NULL, NULL) != 0); }
  assert(tb_requests_submit(&layer, 1, 1000, 1, NULL, NULL, NULL) == 0);
  tb_requests_cancel_all(&layer);
  assert(!fake.timer);

  fake.now = UINT32_MAX - 10;
  const uint32_t wrapped = tb_requests_submit(&layer, 1, 100, 1, done, &fake, NULL);
  fake.now = 50;
  tb_requests_tick(&layer);
  assert(tb_requests_pending(&layer, wrapped));
  fake.now = 95;
  tb_requests_tick(&layer);
  assert(!tb_requests_pending(&layer, wrapped));
  return 0;
}
