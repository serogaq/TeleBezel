#include "errors.h"
#include "fake.h"
#include "history.h"

#define ACCOUNT "00112233-4455-4677-8899-aabbccddeeff"
#define CHAT "-1009007199254740993"

static TbHistoryConfig config(uint16_t capacity) {
  return (TbHistoryConfig){.capacity = capacity, .page_limit = 3, .text_limit = 64, .text_budget = 4096, .timeout = 10000,
                           .refresh_interval = 60000, .followup_delays = {3000, 8000}};
}

static void page(Buf *buf, int newest, int count) {
  memset(buf, 0, sizeof(*buf));
  for (int id = newest; id > newest - count; --id) {
    char value[16];
    char text[32];
    snprintf(value, sizeof(value), "%d", id);
    snprintf(text, sizeof(text), "Message %d", id);
    message_record(buf, value, text);
  }
}

static void album_record(Buf *buf, const char *id, const char *text, uint32_t album, uint8_t count) {
  begin(buf, TB_RECORD_MESSAGE);
  putstr8(buf, id); put32(buf, 1700000000); put8(buf, 0); put8(buf, TB_KIND_PHOTO); put8(buf, 0); put16(buf, 0);
  putstr8(buf, "News"); putstr8(buf, ""); putstr16(buf, text);
  putstr8(buf, ""); putstr8(buf, ""); putstr8(buf, ""); putstr8(buf, "");
  put8(buf, TB_MEDIA_FLAG_IMAGE | TB_MEDIA_FLAG_ALBUM | TB_MEDIA_FLAG_CHANNEL_POST); put32(buf, album); put8(buf, count); putstr8(buf, "Editor");
  end(buf);
}

static void ids(const TbHistory *history, const char *expected) {
  char joined[256] = "";
  for (uint16_t index = 0; index < history->count; ++index) {
    if (index) { strcat(joined, ","); }
    strcat(joined, history->items[index].id);
  }
  if (strcmp(joined, expected) != 0) {
    fprintf(stderr, "expected %s got %s\n", expected, joined);
    assert(0);
  }
}

static uint32_t reply(Fake *fake, TbRequestLayer *layer, int32_t result, uint32_t flags, const Buf *buf) {
  const uint32_t sequence = fake_last_sequence(fake);
  deliver(layer, sequence, result, flags, buf, 0, 1);
  return sequence;
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  fake.now = 1000;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbHistory history;
  assert(tb_history_init(&history, &layer, fake_view_ports(&fake), config(6)));
  Buf buf;

  tb_history_open(&history, ACCOUNT, CHAT, 1, false);
  assert(fake_last(&fake)->kind == TB_REQUEST_HISTORY && fake_last(&fake)->page_op == TB_PAGE_OP_FIRST);
  assert(strcmp(fake_last(&fake)->chat, CHAT) == 0 && history.top == TB_TOP_LOADING && !history.loaded);
  page(&buf, 30, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE | TB_FLAG_REFRESH_PENDING, &buf);
  ids(&history, "28,29,30");
  assert(history.loaded && history.top == TB_TOP_MORE && history.followup_due == 4000 && fake.view_timer);
  assert(strcmp(history.items[2].text, "Message 30") == 0);

  fake.now = 4000;
  tb_history_timer(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_REFRESH);
  page(&buf, 31, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE | TB_FLAG_REFRESH_PENDING, &buf);
  ids(&history, "28,29,30,31");
  assert(history.followup_due == 12000);
  fake.now = 12000;
  tb_history_timer(&history);
  page(&buf, 31, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE | TB_FLAG_REFRESH_PENDING, &buf);
  assert(history.followup_due == 0 && history.periodic_due == 72000);

  tb_history_load_older(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_NEXT && history.top == TB_TOP_LOADING);
  const int sends = fake.sends;
  tb_history_load_older(&history);
  tb_history_refresh(&history);
  assert(fake.sends == sends);
  memset(&buf, 0, sizeof(buf));
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_PARTIAL | TB_FLAG_HAS_MORE_UNKNOWN, &buf);
  assert(history.top == TB_TOP_FAILED && history.top_op == TB_PAGE_OP_RETRY);
  tb_history_load_older(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_RETRY);
  page(&buf, 27, 2);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_LOCAL_EXHAUSTED | TB_FLAG_REFRESH_PENDING, &buf);
  ids(&history, "26,27,28,29,30,31");
  assert(history.top == TB_TOP_WAITING && history.top_op == TB_PAGE_OP_RECHECK && history.recheck_due == fake.now + 3000);
  fake.now += 3000;
  tb_history_timer(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_RECHECK);
  memset(&buf, 0, sizeof(buf));
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_LOCAL_EXHAUSTED | TB_FLAG_REFRESH_PENDING, &buf);
  assert(history.top == TB_TOP_WAITING && history.recheck_due == fake.now + 8000);
  fake.now += 8000;
  tb_history_timer(&history);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_LOCAL_EXHAUSTED | TB_FLAG_REFRESH_PENDING, &buf);
  assert(history.top == TB_TOP_START && history.recheck_due == 0);
  tb_history_load_older(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_RECHECK);
  page(&buf, 25, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &buf);
  ids(&history, "23,24,25,26,27,28");
  assert(history.truncated && history.periodic_due == 0 && history.top == TB_TOP_MORE);

  tb_history_refresh(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_FIRST);
  page(&buf, 33, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &buf);
  ids(&history, "31,32,33");
  assert(!history.truncated && history.periodic_due != 0);

  fake.now = history.periodic_due;
  tb_history_timer(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_REFRESH);
  page(&buf, 40, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE | TB_FLAG_REFRESH_PENDING, &buf);
  ids(&history, "31,32,33");
  assert(history.truncated && history.followup_due == 0);

  tb_history_refresh(&history);
  page(&buf, 40, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &buf);
  ids(&history, "38,39,40");
  tb_history_refresh(&history);
  memset(&buf, 0, sizeof(buf));
  message_record(&buf, "40", "Edited");
  message_record(&buf, "39", "Kept");
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &buf);
  ids(&history, "38,39,40");
  assert(strcmp(history.items[2].text, "Edited") == 0);

  tb_history_refresh(&history);
  reply(&fake, &layer, TB_RESULT_BUSY, 0, NULL);
  assert(history.refresh_error == TB_RESULT_BUSY && history.error == TB_ERROR_NONE && history.count == 3);
  tb_history_load_older(&history);
  reply(&fake, &layer, TB_RESULT_CURSOR_LOST, 0, NULL);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_FIRST);
  reply(&fake, &layer, TB_RESULT_CURSOR_LOST, 0, NULL);
  assert(history.op == TB_HISTORY_NONE && history.top == TB_TOP_FAILED && history.top_op == TB_PAGE_OP_FIRST);
  assert(history.top_error == TB_RESULT_CURSOR_LOST && history.count == 3);
  tb_history_load_older(&history);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_FIRST && history.op == TB_HISTORY_FIRST);
  page(&buf, 41, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &buf);
  ids(&history, "39,40,41");
  assert(history.top == TB_TOP_MORE);

  const uint32_t stale = fake.sends ? fake_last_sequence(&fake) : 0;
  tb_history_open(&history, ACCOUNT, "42", 0, false);
  assert(!tb_requests_pending(&layer, stale) && history.count == 0 && !history.loaded);
  reply(&fake, &layer, TB_RESULT_ACCOUNT_NEEDS_LOGIN, 0, NULL);
  assert(history.error == TB_RESULT_ACCOUNT_NEEDS_LOGIN);
  tb_history_retry(&history);
  memset(&buf, 0, sizeof(buf));
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_PARTIAL | TB_FLAG_REFRESH_PENDING | TB_FLAG_HAS_MORE_UNKNOWN, &buf);
  assert(history.loaded && history.count == 0 && history.top == TB_TOP_WAITING && history.top_op == TB_PAGE_OP_REFRESH);
  assert(history.followup_due != 0);
  fake.now = history.followup_due;
  tb_history_timer(&history);
  page(&buf, 5, 2);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &buf);
  ids(&history, "4,5");
  assert(history.top == TB_TOP_MORE);

  tb_history_set_active(&history, false);
  assert(history.periodic_due == 0 && !fake.view_timer);
  fake.now += 61000;
  tb_history_set_active(&history, true);
  assert(history.op == TB_HISTORY_REFRESH);
  const uint32_t pending = fake_last_sequence(&fake);
  tb_history_close(&history);
  assert(!tb_requests_pending(&layer, pending));
  tb_history_open(&history, ACCOUNT, "42", 0, false);
  assert(history.count == 2 && history.op == TB_HISTORY_FIRST);

  Buf chunked;
  page(&chunked, 9, 1);
  const uint32_t streamed = fake_last_sequence(&fake);
  deliver(&layer, streamed, TB_RESULT_OK, TB_FLAG_HAS_MORE, &chunked, 0, 2);
  assert(history.count == 2 && history.op == TB_HISTORY_FIRST);
  page(&chunked, 8, 1);
  deliver(&layer, streamed, TB_RESULT_OK, TB_FLAG_HAS_MORE, &chunked, 1, 2);
  ids(&history, "8,9");

  tb_history_open(&history, ACCOUNT, "-100500", TB_CHAT_TYPE_CHANNEL, false);
  Buf albums = {0};
  album_record(&albums, "103", "", 7, 2);
  album_record(&albums, "102", "Подпись", 7, 1);
  message_record(&albums, "101", "Plain");
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &albums);
  ids(&history, "101,102");
  assert(history.items[1].album == 7 && history.items[1].media_count == 2 && strcmp(history.items[1].text, "Подпись") == 0);
  assert(strcmp(history.items[1].signature, "Editor") == 0 && (history.items[1].media & TB_MEDIA_FLAG_CHANNEL_POST));
  tb_history_load_older(&history);
  Buf older = {0};
  album_record(&older, "100", "", 9, 1);
  album_record(&older, "99", "", 9, 1);
  album_record(&older, "98", "", 7, 3);
  reply(&fake, &layer, TB_RESULT_OK, TB_FLAG_HAS_MORE, &older);
  ids(&history, "98,99,101,102");
  assert(history.items[1].album == 9 && history.items[0].media_count == 3);
  tb_history_deinit(&history);
  return 0;
}
