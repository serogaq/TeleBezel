#include <assert.h>
#include <string.h>
#include "notify.h"

typedef struct {
  uint32_t now;
  bool timer;
  uint32_t delay;
  int changes;
} Clock;

static void changed(void *context) { ((Clock *)context)->changes++; }
static bool schedule(void *context, uint32_t delay) {
  Clock *clock = context;
  clock->timer = true;
  clock->delay = delay;
  return true;
}
static void cancel(void *context) { ((Clock *)context)->timer = false; }
static uint32_t now(void *context) { return ((Clock *)context)->now; }

int main(void) {
  Clock clock = {1000, false, 0, 0};
  TbNotify notify;
  tb_notify_init(&notify, (TbNotifyPorts){changed, schedule, cancel, now, &clock});
  assert(tb_notify_top(&notify) == NULL);

  assert(tb_notify_post(&notify, TB_NOTIFY_CONNECTION, TB_NOTIFY_INFO, TB_NOTIFY_NO_ACTION, "Telegram", "Connected"));
  assert(clock.timer && clock.delay == TB_NOTIFY_INFO_TTL);
  assert(tb_notify_post(&notify, TB_NOTIFY_SEND, TB_NOTIFY_ERROR, TB_NOTIFY_CHECK, "Family", "Not sent"));
  assert(tb_notify_post(&notify, TB_NOTIFY_CHATS, TB_NOTIFY_WARNING, TB_NOTIFY_REFRESH, "Chats", "Not updated"));
  assert(notify.count == 3);
  assert(tb_notify_top(&notify)->source == TB_NOTIFY_SEND);

  assert(tb_notify_post(&notify, TB_NOTIFY_CONNECTION, TB_NOTIFY_ERROR, TB_NOTIFY_REFRESH, "Telegram", "Cannot connect"));
  assert(notify.count == 3);
  assert(tb_notify_top(&notify)->source == TB_NOTIFY_CONNECTION);
  assert(strcmp(tb_notify_top(&notify)->body, "Cannot connect") == 0);

  const int before = clock.changes;
  assert(tb_notify_post(&notify, TB_NOTIFY_CONNECTION, TB_NOTIFY_ERROR, TB_NOTIFY_REFRESH, "Telegram", "Cannot connect"));
  assert(clock.changes == before);

  tb_notify_focus(&notify, true);
  assert(notify.focused);
  tb_notify_dismiss(&notify);
  assert(!notify.focused && notify.count == 2);
  assert(tb_notify_top(&notify)->source == TB_NOTIFY_SEND);

  tb_notify_clear(&notify, TB_NOTIFY_SEND);
  assert(notify.count == 1 && tb_notify_top(&notify)->level == TB_NOTIFY_WARNING);

  assert(tb_notify_post(&notify, TB_NOTIFY_SEND, TB_NOTIFY_INFO, TB_NOTIFY_OPEN_CHAT, "Family", "Sent"));
  assert(tb_notify_top(&notify)->source == TB_NOTIFY_CHATS);
  clock.now += TB_NOTIFY_INFO_TTL - 1;
  tb_notify_timer(&notify);
  assert(notify.count == 2);
  clock.now += 1;
  tb_notify_timer(&notify);
  assert(notify.count == 1 && tb_notify_top(&notify)->source == TB_NOTIFY_CHATS);
  assert(!clock.timer);

  assert(tb_notify_post(&notify, TB_NOTIFY_CONNECTION, TB_NOTIFY_ERROR, TB_NOTIFY_REFRESH, "A", "a"));
  assert(tb_notify_post(&notify, TB_NOTIFY_SEND, TB_NOTIFY_ERROR, TB_NOTIFY_CHECK, "B", "b"));
  assert(notify.count == 3);
  char long_body[200];
  memset(long_body, 'x', sizeof(long_body) - 1);
  long_body[sizeof(long_body) - 1] = '\0';
  tb_notify_clear(&notify, TB_NOTIFY_CHATS);
  assert(tb_notify_post(&notify, TB_NOTIFY_CHATS, TB_NOTIFY_INFO, TB_NOTIFY_NO_ACTION, long_body, long_body));
  assert(strlen(tb_notify_top(&notify)->body) == 1);
  const TbNotification *info = NULL;
  for (int index = 0; index < notify.count; ++index) {
    if (notify.items[index].source == TB_NOTIFY_CHATS) { info = &notify.items[index]; }
  }
  assert(info && strlen(info->body) == TB_NOTIFY_BODY_SIZE - 1 && strlen(info->title) == TB_NOTIFY_TITLE_SIZE - 1);
  tb_notify_clear(&notify, TB_NOTIFY_CHATS);
  assert(tb_notify_post(&notify, TB_NOTIFY_CHATS, TB_NOTIFY_ERROR, TB_NOTIFY_REFRESH, "C", "c"));
  assert(!tb_notify_post(&notify, TB_NOTIFY_CHATS + 0, TB_NOTIFY_INFO, TB_NOTIFY_NO_ACTION, "C", "c") || notify.count == 3);

  tb_notify_reset(&notify);
  assert(notify.count == 0 && tb_notify_top(&notify) == NULL && !notify.focused);
  return 0;
}
