#include "fake.h"
#include "errors.h"
#include "session.h"

static void account_record(Buf *buf, const char *id, const char *name, uint8_t state, uint8_t flags) {
  begin(buf, TB_RECORD_ACCOUNT);
  putstr8(buf, id); putstr8(buf, name); put8(buf, state); put8(buf, flags);
  end(buf);
}
static void prefs_record(Buf *buf, const char *account, uint8_t list, uint8_t show_archive, uint8_t unread_mode) {
  begin(buf, TB_RECORD_PREFS);
  putstr8(buf, account); put8(buf, list); putstr8(buf, "tg.example:443"); put8(buf, show_archive); put8(buf, unread_mode);
  end(buf);
}

#define FIRST "00112233-4455-4677-8899-aabbccddeeff"
#define SECOND "10112233-4455-4677-8899-aabbccddeeff"

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbSession session;
  tb_session_init(&session, &layer, (TbSessionPorts){fake_changed, &fake});

  tb_session_start(&session, true);
  assert(session.phase == TB_SESSION_HELLO && fake_last(&fake)->kind == TB_REQUEST_HELLO);
  const uint32_t hello = fake_last_sequence(&fake);
  tb_session_ready(&session, hello);
  assert(session.phase == TB_SESSION_BOOTSTRAP && fake_last(&fake)->kind == TB_REQUEST_BOOTSTRAP);
  const uint32_t boot = fake_last_sequence(&fake);
  Buf first = {0};
  prefs_record(&first, SECOND, TB_LIST_ARCHIVE, 1, TB_UNREAD_MODE_MESSAGES);
  account_record(&first, FIRST, "Личный", TB_ACCOUNT_STATE_READY, 0);
  Buf second = {0};
  account_record(&second, SECOND, "Work", TB_ACCOUNT_STATE_NEEDS_LOGIN, TB_ACCOUNT_FLAG_DEFAULT);
  deliver(&layer, boot, TB_RESULT_OK, 0, &first, 0, 2);
  assert(session.phase == TB_SESSION_BOOTSTRAP && !session.loaded);
  deliver(&layer, boot, TB_RESULT_OK, 0, &second, 1, 2);
  assert(session.phase == TB_SESSION_IDLE && session.loaded && session.error == TB_ERROR_NONE && session.count == 2);
  assert(strcmp(session.accounts[0].name, "Личный") == 0 && session.chat_list == TB_LIST_ARCHIVE);
  assert(strcmp(session.host, "tg.example:443") == 0 && strcmp(session.default_account, SECOND) == 0);
  assert(session.show_archive && session.unread_mode == TB_UNREAD_MODE_MESSAGES);
  assert(tb_session_initial_account(&session) == -1);
  session.accounts[1].state = TB_ACCOUNT_STATE_READY;
  assert(tb_session_initial_account(&session) == 1);
  assert(tb_session_find(&session, FIRST) == 0);

  tb_session_refresh(&session);
  Buf hidden = {0};
  prefs_record(&hidden, "", TB_LIST_ARCHIVE, 0, TB_UNREAD_MODE_CHATS);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &hidden, 0, 1);
  assert(session.loaded && !session.show_archive && session.chat_list == TB_LIST_MAIN && session.unread_mode == TB_UNREAD_MODE_CHATS);

  tb_session_refresh(&session);
  assert(session.phase == TB_SESSION_BOOTSTRAP && session.count == 0 && !session.loaded);
  const uint32_t refreshed = fake_last_sequence(&fake);
  deliver(&layer, refreshed, TB_RESULT_API_UNAUTHORIZED, 0, NULL, 0, 1);
  assert(session.phase == TB_SESSION_IDLE && session.error == TB_RESULT_API_UNAUTHORIZED);

  tb_session_retry(&session);
  tb_session_ready(&session, fake_last_sequence(&fake));
  const uint32_t broken = fake_last_sequence(&fake);
  Buf garbage = {0};
  begin(&garbage, 99);
  put8(&garbage, 1);
  end(&garbage);
  deliver(&layer, broken, TB_RESULT_OK, 0, &garbage, 0, 2);
  assert(session.error == TB_RESULT_PROTOCOL_ERROR && session.count == 0 && !tb_requests_pending(&layer, broken));

  tb_session_retry(&session);
  const uint32_t lost = fake_last_sequence(&fake);
  fake.now += 1500;
  tb_requests_tick(&layer);
  fake.now += 1500;
  tb_requests_tick(&layer);
  fake.now += 1500;
  tb_requests_tick(&layer);
  assert(session.phase == TB_SESSION_IDLE && session.error == TB_ERROR_NO_RESPONSE);
  tb_session_ready(&session, lost);
  assert(session.phase == TB_SESSION_BOOTSTRAP);

  fake.next = TB_SEND_UNREACHABLE;
  tb_session_start(&session, true);
  assert(session.error == TB_ERROR_PHONE_UNREACHABLE);
  fake.next = TB_SEND_OK;
  tb_session_start(&session, false);
  assert(session.error == TB_ERROR_NO_RESPONSE);
  return 0;
}
