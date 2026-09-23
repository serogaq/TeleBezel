#include <assert.h>
#include <string.h>
#include "codec.h"
#include "generated/codec_vectors.h"
#include "generated/protocol.h"
#include "text.h"

static bool equals(TbSpan span, const char *expected) {
  return span.length == strlen(expected) && memcmp(span.data, expected, span.length) == 0;
}

static TbCursor single(const uint8_t *data, uint16_t length, uint8_t expected_type) {
  TbCursor cursor;
  TbCursor body;
  uint8_t type = 0;
  tb_cursor_init(&cursor, data, length);
  assert(tb_codec_next(&cursor, &type, &body));
  assert(type == expected_type && cursor.offset == cursor.length);
  return body;
}

int main(void) {
  TbCursor body = single(tb_vector_account, sizeof(tb_vector_account), TB_RECORD_ACCOUNT);
  TbAccountRecord account;
  assert(tb_codec_account(&body, &account));
  assert(equals(account.id, "00112233-4455-4677-8899-aabbccddeeff") && equals(account.name, "Работа"));
  assert(account.state == TB_ACCOUNT_STATE_READY && account.flags == TB_ACCOUNT_FLAG_DEFAULT);

  body = single(tb_vector_prefs, sizeof(tb_vector_prefs), TB_RECORD_PREFS);
  TbPrefsRecord prefs;
  assert(tb_codec_prefs(&body, &prefs));
  assert(prefs.chat_list == TB_LIST_ARCHIVE && equals(prefs.host, "tg.example:443"));
  assert(prefs.show_archive == 0 && prefs.unread_mode == TB_UNREAD_MODE_MESSAGES);

  body = single(tb_vector_summary, sizeof(tb_vector_summary), TB_RECORD_SUMMARY);
  TbSummaryRecord summary;
  assert(tb_codec_summary(&body, &summary));
  assert(summary.connection == TB_CONNECTION_UPDATING && summary.proxy == 1 && summary.unread_chats == 70000 &&
         summary.unread_messages == UINT32_MAX);

  body = single(tb_vector_status, sizeof(tb_vector_status), TB_RECORD_STATUS);
  TbStatusRecord status;
  assert(tb_codec_status(&body, &status));
  assert(status.connection == TB_CONNECTION_READY && status.proxy == 0);

  body = single(tb_vector_chat, sizeof(tb_vector_chat), TB_RECORD_CHAT);
  TbChatRecord chat;
  assert(tb_codec_chat(&body, &chat));
  assert(equals(chat.id, "-1009007199254740993") && equals(chat.title, "Группа 👋"));
  assert(chat.type == TB_CHAT_TYPE_BASIC_GROUP && chat.flags == 9 && chat.unread == 1234 && chat.last_date == 1700000000);
  assert(chat.preview_kind == TB_KIND_PHOTO && equals(chat.preview_sender, "Ада") && equals(chat.preview_text, "Подпись к фото"));

  body = single(tb_vector_message, sizeof(tb_vector_message), TB_RECORD_MESSAGE);
  TbMessageRecord message;
  assert(tb_codec_message(&body, &message));
  assert(equals(message.id, "9007199254740993") && message.date == 1700000123 && message.flags == 3);
  assert(message.kind == TB_KIND_VOICE_NOTE && message.duration == 74 && equals(message.sender, "Ada Lovelace"));
  assert(equals(message.text, "Hello, мир 😀"));

  body = single(tb_vector_message_truncated, sizeof(tb_vector_message_truncated), TB_RECORD_MESSAGE);
  assert(tb_codec_message(&body, &message));
  assert((message.flags & TB_MESSAGE_FLAG_TRUNCATED) && equals(message.text, "Длин…"));

  body = single(tb_vector_text, sizeof(tb_vector_text), TB_RECORD_TEXT);
  TbSpan text;
  assert(tb_codec_text(&body, &text) && equals(text, "При"));

  for (uint16_t cut = 0; cut < sizeof(tb_vector_chat); ++cut) {
    TbCursor cursor;
    TbCursor partial;
    uint8_t type = 0;
    tb_cursor_init(&cursor, tb_vector_chat, cut);
    assert(!tb_codec_next(&cursor, &type, &partial) || !tb_codec_chat(&partial, &chat));
  }
  uint8_t padded[sizeof(tb_vector_account) + 1];
  memcpy(padded, tb_vector_account, sizeof(tb_vector_account));
  padded[1] = (uint8_t)(padded[1] + 1);
  padded[sizeof(tb_vector_account)] = 0;
  body = single(padded, sizeof(padded), TB_RECORD_ACCOUNT);
  assert(!tb_codec_account(&body, &account));

  char buffer[8];
  const uint8_t cyrillic[] = {0xD0, 0x9F, 0xD1, 0x80, 0xD0, 0xB8, 0xD0, 0xB2};
  tb_copy_span(buffer, sizeof(buffer), (TbSpan){cyrillic, sizeof(cyrillic)});
  assert(strcmp(buffer, "При") == 0);
  char identifier[21];
  assert(tb_copy_id(identifier, sizeof(identifier), (TbSpan){(const uint8_t *)"-1009007199254740993", 20}, true));
  int64_t parsed = 0;
  assert(tb_parse_id(identifier, &parsed) && parsed == INT64_C(-1009007199254740993));
  assert(!tb_copy_id(identifier, sizeof(identifier), (TbSpan){(const uint8_t *)"-5", 2}, false));
  assert(!tb_copy_id(identifier, sizeof(identifier), (TbSpan){(const uint8_t *)"012", 3}, true));
  assert(!tb_copy_id(identifier, sizeof(identifier), (TbSpan){(const uint8_t *)"12a", 3}, true));
  assert(!tb_copy_id(identifier, sizeof(identifier), (TbSpan){(const uint8_t *)"9223372036854775808", 19}, true));
  assert(tb_copy_id(identifier, sizeof(identifier), (TbSpan){(const uint8_t *)"9223372036854775807", 19}, true));
  assert(tb_parse_id("-9223372036854775808", &parsed) && parsed == INT64_MIN);
  assert(!tb_parse_id("-9223372036854775809", &parsed));
  char uuid[37];
  assert(tb_copy_uuid(uuid, sizeof(uuid), (TbSpan){(const uint8_t *)"00112233-4455-4677-8899-aabbccddeeff", 36}));
  assert(!tb_copy_uuid(uuid, sizeof(uuid), (TbSpan){(const uint8_t *)"00112233-4455-4677-8899_aabbccddeeff", 36}));
  TbBudget budget = {0, 8};
  char *copy = tb_budget_copy(&budget, (TbSpan){(const uint8_t *)"abcdef", 6}, 3, false);
  assert(copy && strcmp(copy, "abc") == 0 && budget.used == 4);
  assert(!tb_budget_copy(&budget, (TbSpan){(const uint8_t *)"abcdef", 6}, 6, false));
  char *forced = tb_budget_copy(&budget, (TbSpan){(const uint8_t *)"abcdef", 6}, 6, true);
  assert(forced && budget.used == 11);
  tb_budget_free(&budget, copy);
  tb_budget_free(&budget, forced);
  assert(budget.used == 0);
  return 0;
}
