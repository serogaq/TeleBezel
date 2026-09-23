#include "chats.h"
#include "errors.h"
#include "fake.h"

#define ACCOUNT "00112233-4455-4677-8899-aabbccddeeff"

static TbChatsConfig config(void) {
  return (TbChatsConfig){.capacity = 5, .page_limit = 3, .text_limit = 40, .text_budget = 2048, .timeout = 10000,
                         .refresh_interval = 60000, .stale_after = 30000};
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  fake.now = 1000;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbChats chats;
  assert(tb_chats_init(&chats, &layer, fake_view_ports(&fake), config()));
  tb_chats_open(&chats, ACCOUNT, TB_LIST_MAIN);
  const TbRequestArgs *args = fake_last(&fake);
  assert(args->kind == TB_REQUEST_CHATS && args->page_op == TB_PAGE_OP_FIRST && args->list == TB_LIST_MAIN);
  assert(strcmp(args->account, ACCOUNT) == 0 && args->page_limit == 3 && args->text_limit == 40);
  assert(chats.load == TB_CHATS_FIRST);
  Buf page = {0};
  summary_record(&page, TB_CONNECTION_UPDATING, 1, 4, 17);
  chat_record(&page, "-1009007199254740993", "Группа", "Привет");
  chat_record(&page, "42", "Ada", "hello");
  Buf rest = {0};
  chat_record(&rest, "43", "Bob", "");
  uint32_t sequence = fake_last_sequence(&fake);
  deliver(&layer, sequence, TB_RESULT_OK, TB_FLAG_HAS_MORE | TB_FLAG_CONNECTION_NOT_READY, &page, 0, 2);
  assert(chats.count == 2 && chats.load == TB_CHATS_FIRST);
  deliver(&layer, sequence, TB_RESULT_OK, TB_FLAG_HAS_MORE, &rest, 1, 2);
  assert(chats.count == 3 && chats.load == TB_CHATS_IDLE && chats.tail == TB_TAIL_MORE && chats.loaded);
  assert(chats.connection_not_ready && strcmp(chats.items[0].id, "-1009007199254740993") == 0);
  assert(chats.connection == TB_CONNECTION_UPDATING && chats.proxy && chats.unread_chats == 4 && chats.unread_messages == 17);
  assert(chats.summary_revision == 1);
  assert(strcmp(chats.items[0].title, "Группа") == 0 && strcmp(chats.items[1].preview, "hello") == 0 && chats.items[2].preview == NULL);
  assert(fake.view_timer && fake.view_delay == 60000);

  tb_chats_load_more(&chats);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_NEXT && chats.load == TB_CHATS_MORE);
  sequence = fake_last_sequence(&fake);
  deliver(&layer, sequence, TB_RESULT_BUSY, 0, NULL, 0, 1);
  assert(chats.tail == TB_TAIL_FAILED && chats.error == TB_RESULT_BUSY && chats.count == 3);
  tb_chats_retry(&chats);
  assert(fake_last(&fake)->page_op == TB_PAGE_OP_RETRY);
  Buf more = {0};
  chat_record(&more, "42", "Duplicate", "");
  chat_record(&more, "44", "Carol", "");
  chat_record(&more, "45", "Dave", "");
  chat_record(&more, "46", "Eve", "");
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, TB_FLAG_HAS_MORE, &more, 0, 1);
  assert(chats.count == 5 && chats.tail == TB_TAIL_FULL && strcmp(chats.items[1].title, "Ada") == 0);
  const int sends = fake.sends;
  tb_chats_load_more(&chats);
  assert(fake.sends == sends);

  tb_chats_refresh(&chats);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_CURSOR_LOST, 0, NULL, 0, 1);
  assert(fake.sends == sends + 2 && fake_last(&fake)->page_op == TB_PAGE_OP_FIRST);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_CURSOR_LOST, 0, NULL, 0, 1);
  assert(chats.error == TB_RESULT_CURSOR_LOST && fake.sends == sends + 2);

  tb_chats_refresh(&chats);
  Buf refreshed = {0};
  chat_record(&refreshed, "46", "Eve", "new");
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &refreshed, 0, 1);
  assert(chats.count == 1 && chats.tail == TB_TAIL_END && chats.error == TB_ERROR_NONE && tb_chats_find(&chats, "46") == 0);

  tb_chats_set_active(&chats, false);
  assert(!fake.view_timer);
  fake.now += 10000;
  tb_chats_set_active(&chats, true);
  assert(fake.view_timer && chats.load == TB_CHATS_IDLE);
  fake.now += 30000;
  tb_chats_set_active(&chats, true);
  assert(chats.load == TB_CHATS_REFRESH);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, TB_FLAG_HAS_MORE_UNKNOWN, &refreshed, 0, 1);
  assert(chats.tail == TB_TAIL_UNKNOWN);
  fake.now += 60000;
  tb_chats_timer(&chats);
  assert(chats.load == TB_CHATS_REFRESH);

  tb_chats_set_list(&chats, TB_LIST_ARCHIVE);
  assert(fake_last(&fake)->list == TB_LIST_ARCHIVE && chats.count == 0);
  const uint32_t stale = fake_last_sequence(&fake);
  tb_chats_open(&chats, "10112233-4455-4677-8899-aabbccddeeff", TB_LIST_MAIN);
  assert(!tb_requests_pending(&layer, stale));
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_ACCOUNT_NEEDS_LOGIN, 0, NULL, 0, 1);
  assert(chats.error == TB_RESULT_ACCOUNT_NEEDS_LOGIN && tb_error_is_account_level(chats.error));
  assert(chats.unread_chats == 0 && chats.unread_messages == 0 && !chats.proxy);
  tb_chats_deinit(&chats);
  return 0;
}
