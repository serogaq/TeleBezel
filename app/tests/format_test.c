#include <assert.h>
#include <string.h>
#include "errors.h"
#include "format.h"
#include "generated/protocol.h"

int main(void) {
  TbStrings strings;
  memset(&strings, 0, sizeof(strings));
  strings.kind_photo = "Фото";
  strings.kind_voice_note = "Голосовое";
  strings.kind_sticker = "Стикер";
  strings.kind_document = "Файл";
  strings.kind_unsupported = "Не поддерживается";
  strings.kind_service = "Служебное";
  strings.action_title_changed = "Название изменено";
  strings.action_pinned = "закрепил(а) сообщение";
  strings.today = "Сегодня";
  strings.yesterday = "Вчера";
  strings.phone_unreachable = "Нет связи с телефоном";
  strings.protocol_error = "Ошибка";
  strings.just_now = "только что";
  strings.minutes_ago = "%d м. назад";
  strings.yesterday_at = "вчера в %s";
  strings.day_at = "%s в %s";
  char out[96];
  tb_format_content(out, sizeof(out), &strings, TB_KIND_TEXT, 0, 0, NULL, "Привет");
  assert(strcmp(out, "Привет") == 0);
  tb_format_content(out, sizeof(out), &strings, TB_KIND_PHOTO, 0, 0, NULL, "Подпись");
  assert(strcmp(out, "[Фото] Подпись") == 0);
  tb_format_content(out, sizeof(out), &strings, TB_KIND_VOICE_NOTE, 0, 74, "", "");
  assert(strcmp(out, "[Голосовое 1:14]") == 0);
  tb_format_content(out, sizeof(out), &strings, TB_KIND_STICKER, 0, 0, "😀", "");
  assert(strcmp(out, "[Стикер 😀]") == 0);
  tb_format_content(out, sizeof(out), &strings, TB_KIND_DOCUMENT, 0, 0, "report.pdf", "caption");
  assert(strcmp(out, "[Файл report.pdf] caption") == 0);
  tb_format_content(out, sizeof(out), &strings, TB_KIND_SERVICE, TB_ACTION_TITLE_CHANGED, 0, "Новое", "");
  assert(strcmp(out, "Название изменено: Новое") == 0);
  tb_format_content(out, sizeof(out), &strings, TB_KIND_SERVICE, TB_ACTION_CUSTOM, 0, "", "Custom text");
  assert(strcmp(out, "Custom text") == 0);
  tb_format_content(out, sizeof(out), &strings, 200, 0, 0, "", "");
  assert(strcmp(out, "[Не поддерживается]") == 0);
  char small[8];
  tb_format_content(small, sizeof(small), &strings, TB_KIND_TEXT, 0, 0, NULL, "abcdefghijk");
  assert(strlen(small) == 7);
  assert(strcmp(tb_error_text(&strings, TB_ERROR_PHONE_UNREACHABLE), "Нет связи с телефоном") == 0);
  assert(strcmp(tb_error_text(&strings, 12345), "Ошибка") == 0);

  const time_t now = 1700000000;
  tb_format_time(out, sizeof(out), now - 60, now, true);
  assert(strcmp(out, "22:12") == 0);
  tb_format_time(out, sizeof(out), now - 60, now, false);
  assert(strcmp(out, "10:12pm") == 0);
  tb_format_time(out, sizeof(out), now - 5 * 86400, now, true);
  assert(strcmp(out, "09.11") == 0);
  tb_format_time(out, sizeof(out), now - 400 * 86400, now, true);
  assert(strcmp(out, "10.10.22") == 0);
  tb_format_day(out, sizeof(out), &strings, now, now);
  assert(strcmp(out, "Сегодня") == 0);
  tb_format_day(out, sizeof(out), &strings, now - 86400, now);
  assert(strcmp(out, "Вчера") == 0);
  tb_format_day(out, sizeof(out), &strings, now - 3 * 86400, now);
  assert(strcmp(out, "11.11.2023") == 0);
  assert(tb_same_day(now, now - 3600) && !tb_same_day(now, now - 86400));
  tb_format_clock(out, sizeof(out), now, true);
  assert(strcmp(out, "22:13") == 0);
  tb_format_ago(out, sizeof(out), &strings, now - 59, now, true);
  assert(strcmp(out, "только что") == 0);
  tb_format_ago(out, sizeof(out), &strings, now + 5, now, true);
  assert(strcmp(out, "только что") == 0);
  tb_format_ago(out, sizeof(out), &strings, now - 60, now, true);
  assert(strcmp(out, "1 м. назад") == 0);
  tb_format_ago(out, sizeof(out), &strings, now - 3599, now, true);
  assert(strcmp(out, "59 м. назад") == 0);
  tb_format_ago(out, sizeof(out), &strings, now - 3600, now, true);
  assert(strcmp(out, "21:13") == 0);
  tb_format_ago(out, sizeof(out), &strings, now - 86400, now, true);
  assert(strcmp(out, "вчера в 22:13") == 0);
  tb_format_ago(out, sizeof(out), &strings, now - 2 * 86400, now, false);
  assert(strcmp(out, "12.11 в 10:13pm") == 0);
  tb_format_count(out, sizeof(out), 0);
  assert(strcmp(out, "0") == 0);
  tb_format_count(out, sizeof(out), 9999);
  assert(strcmp(out, "9999") == 0);
  tb_format_count(out, sizeof(out), 12345);
  assert(strcmp(out, "12k") == 0);
  tb_format_count(out, sizeof(out), UINT32_MAX);
  assert(strcmp(out, "999k") == 0);
  tb_format_badge(out, sizeof(out), 5, TB_CHAT_FLAG_MENTION);
  assert(strcmp(out, "@5") == 0);
  tb_format_badge(out, sizeof(out), 1234, 0);
  assert(strcmp(out, "99+") == 0);
  tb_format_badge(out, sizeof(out), 0, TB_CHAT_FLAG_MARKED_UNREAD);
  assert(strcmp(out, "\xE2\x80\xA2") == 0);
  tb_format_badge(out, sizeof(out), 0, 0);
  assert(out[0] == '\0');
  return 0;
}
