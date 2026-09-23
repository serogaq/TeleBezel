#include "errors.h"
#include "fake.h"
#include "message_text.h"

static void text_record(Buf *buf, const char *value) {
  begin(buf, TB_RECORD_TEXT);
  putstr16(buf, value);
  end(buf);
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbMessageText reader;
  tb_message_text_init(&reader, &layer, fake_view_ports(&fake), 12, 10000);
  assert(tb_message_text_open(&reader, "00112233-4455-4677-8899-aabbccddeeff", "-42", "7"));
  assert(fake_last(&fake)->kind == TB_REQUEST_MESSAGE && strcmp(fake_last(&fake)->message, "7") == 0 && fake_last(&fake)->text_limit == 12);
  assert(reader.loading);
  Buf first = {0};
  text_record(&first, "Привет");
  Buf second = {0};
  text_record(&second, " мир!");
  const uint32_t sequence = fake_last_sequence(&fake);
  deliver(&layer, sequence, TB_RESULT_OK, 0, &first, 0, 2);
  assert(reader.loading && strcmp(reader.text, "Привет") == 0);
  deliver(&layer, sequence, TB_RESULT_OK, 0, &second, 1, 2);
  assert(!reader.loading && strcmp(reader.text, "Привет") == 0 && reader.truncated);

  tb_message_text_retry(&reader);
  Buf flagged = {0};
  text_record(&flagged, "abc");
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, TB_FLAG_TRUNCATED, &flagged, 0, 1);
  assert(strcmp(reader.text, "abc") == 0 && reader.truncated);

  tb_message_text_retry(&reader);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_MESSAGE_UNAVAILABLE, 0, NULL, 0, 1);
  assert(!reader.loading && reader.error == TB_RESULT_MESSAGE_UNAVAILABLE && reader.text[0] == '\0');
  tb_message_text_retry(&reader);
  const uint32_t abandoned = fake_last_sequence(&fake);
  tb_message_text_close(&reader);
  assert(!tb_requests_pending(&layer, abandoned) && reader.text == NULL);
  fake.next = TB_SEND_UNREACHABLE;
  assert(tb_message_text_open(&reader, "00112233-4455-4677-8899-aabbccddeeff", "-42", "7"));
  assert(reader.error == TB_ERROR_PHONE_UNREACHABLE && !reader.loading);
  tb_message_text_close(&reader);
  return 0;
}
