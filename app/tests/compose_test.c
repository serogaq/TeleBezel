#include <stdlib.h>
#include "compose.h"
#include "fake.h"

static void template_list(Buf *buf, uint32_t revision, uint8_t count) {
  begin(buf, TB_RECORD_TEMPLATES);
  put32(buf, revision); put8(buf, count); put8(buf, 0);
  end(buf);
  for (uint8_t index = 0; index < count; ++index) {
    begin(buf, TB_RECORD_TEMPLATE);
    put8(buf, index); put16(buf, 20);
    putstr8(buf, index == 0 ? "Уже еду, буду через десять минут" : "Перезвоню");
    end(buf);
  }
}

static void draft_record(Buf *buf, uint32_t id, const char *text, uint8_t flags) {
  begin(buf, TB_RECORD_DRAFT);
  put32(buf, id); put16(buf, (uint16_t)strlen(text)); put16(buf, (uint16_t)strlen(text)); put8(buf, flags);
  end(buf);
  begin(buf, TB_RECORD_TEXT);
  putstr16(buf, text);
  end(buf);
}

static TbComposeTarget target_for(const char *chat, const char *reply) {
  TbComposeTarget target;
  memset(&target, 0, sizeof(target));
  strcpy(target.send.account, "00112233-4455-4677-8899-aabbccddeeff");
  strcpy(target.send.chat, chat);
  strcpy(target.send.reply, reply);
  strcpy(target.account_name, "Personal");
  strcpy(target.chat_title, "Family");
  return target;
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbCompose compose;
  tb_compose_init(&compose, &layer, fake_view_ports(&fake), (TbComposeConfig){2, 12, 64, 20000});
  TbComposeTarget target = target_for("-1009007199254740993", "55");
  assert(tb_compose_open(&compose, &target));
  assert(fake_last(&fake)->kind == TB_REQUEST_TEMPLATES && compose.templates == TB_TEMPLATES_LOADING);
  strcpy(target.send.chat, "42");
  assert(strcmp(compose.target.send.chat, "-1009007199254740993") == 0);

  Buf list = {0};
  template_list(&list, 70000, 3);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &list, 0, 1);
  assert(compose.templates == TB_TEMPLATES_READY && compose.template_count == 2 && compose.template_total == 3);
  assert(strlen(tb_compose_template(&compose, 0)) <= 11);
  assert(strcmp(tb_compose_template(&compose, 1), "Перезвоню") != 0 || strlen("Перезвоню") <= 11);
  assert(!tb_compose_pick(&compose, 2));

  assert(tb_compose_pick(&compose, 1));
  const TbRequestArgs *picked = fake_last(&fake);
  assert(picked->kind == TB_REQUEST_DRAFT && picked->template_index == 1 && picked->templates_rev == 70000);
  assert(strcmp(picked->chat, "-1009007199254740993") == 0 && strcmp(picked->message, "55") == 0);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_CURSOR_LOST, 0, NULL, 0, 1);
  assert(compose.draft == TB_DRAFT_FAILED && fake_last(&fake)->kind == TB_REQUEST_TEMPLATES);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &list, 0, 1);

  assert(tb_compose_pick(&compose, 0));
  Buf draft = {0};
  draft_record(&draft, 41, "Уже еду", 0);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &draft, 0, 1);
  assert(compose.draft == TB_DRAFT_READY && compose.draft_id == 41 && strcmp(compose.text, "Уже еду") == 0);
  assert(tb_compose_sendable(&compose));

  static char spoken[TB_COMPOSE_DRAFT_LIMIT + 10];
  memset(spoken, 'a', sizeof(spoken));
  const int before = fake.sends;
  assert(tb_compose_dictated(&compose, spoken, sizeof(spoken)));
  assert(fake.sends == before + 1 && fake_last(&fake)->kind == TB_REQUEST_DRAFT_DISCARD);
  assert(compose.too_long && compose.draft_units == sizeof(spoken) && !tb_compose_sendable(&compose));
  assert(compose.text == NULL);

  const char *words = "Привет\nмир";
  assert(tb_compose_dictated(&compose, words, strlen(words)));
  const TbRequestArgs *dictation = fake_last(&fake);
  assert(dictation->kind == TB_REQUEST_DRAFT && dictation->payload_length == strlen(words));
  assert(memcmp(dictation->payload, words, strlen(words)) == 0);
  Buf spoken_draft = {0};
  draft_record(&spoken_draft, 42, words, 0);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_OK, 0, &spoken_draft, 0, 1);
  assert(compose.dictated == NULL && compose.draft_id == 42 && tb_utf8_units(words, strlen(words)) == 10);

  tb_compose_close(&compose, false);
  assert(fake_last(&fake)->kind == TB_REQUEST_DRAFT_DISCARD && fake_last(&fake)->draft_id == 42);
  assert(!compose.open && compose.previews == NULL && compose.text == NULL);
  assert(!tb_compose_pick(&compose, 0));

  TbComposeTarget empty;
  memset(&empty, 0, sizeof(empty));
  assert(!tb_compose_open(&compose, &empty));
  assert(tb_compose_open(&compose, &target));
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_BUSY, 0, NULL, 0, 1);
  assert(compose.templates == TB_TEMPLATES_FAILED && compose.templates_error == TB_RESULT_BUSY);
  tb_compose_close(&compose, true);
  return 0;
}
