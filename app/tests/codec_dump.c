#include <stdio.h>
#include <string.h>
#include "codec.h"
#include "generated/protocol.h"
#include "text.h"

static int hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

static void print_span(const char *name, TbSpan span) {
  printf("\"%s\":\"", name);
  for (uint16_t index = 0; index < span.length; ++index) { printf("%02x", span.data[index]); }
  printf("\"");
}

int main(void) {
  static char line[20000];
  static uint8_t bytes[10000];
  while (fgets(line, sizeof(line), stdin)) {
    size_t length = 0;
    for (size_t index = 0; line[index] && line[index + 1] && hex(line[index]) >= 0; index += 2) {
      bytes[length++] = (uint8_t)(hex(line[index]) * 16 + hex(line[index + 1]));
    }
    TbCursor cursor;
    tb_cursor_init(&cursor, bytes, (uint16_t)length);
    while (cursor.offset < cursor.length) {
      uint8_t type = 0;
      TbCursor body;
      if (!tb_codec_next(&cursor, &type, &body)) { printf("{\"error\":\"frame\"}\n"); return 1; }
      if (type == TB_RECORD_ACCOUNT) {
        TbAccountRecord record;
        if (!tb_codec_account(&body, &record)) { printf("{\"error\":\"account\"}\n"); return 1; }
        printf("{\"type\":\"account\",");
        print_span("id", record.id);
        printf(",");
        print_span("name", record.name);
        printf(",\"state\":%d,\"flags\":%d}\n", record.state, record.flags);
      } else if (type == TB_RECORD_PREFS) {
        TbPrefsRecord record;
        if (!tb_codec_prefs(&body, &record)) { printf("{\"error\":\"prefs\"}\n"); return 1; }
        printf("{\"type\":\"prefs\",");
        print_span("defaultAccount", record.default_account);
        printf(",\"chatList\":%d,", record.chat_list);
        print_span("host", record.host);
        printf("}\n");
      } else if (type == TB_RECORD_CHAT) {
        TbChatRecord record;
        if (!tb_codec_chat(&body, &record)) { printf("{\"error\":\"chat\"}\n"); return 1; }
        char id[21];
        const int valid = tb_copy_id(id, sizeof(id), record.id, true);
        printf("{\"type\":\"chat\",\"validId\":%s,", valid ? "true" : "false");
        print_span("id", record.id);
        printf(",");
        print_span("title", record.title);
        printf(",\"unread\":%d,\"previewKind\":%d,", record.unread, record.preview_kind);
        print_span("previewText", record.preview_text);
        printf("}\n");
      } else if (type == TB_RECORD_MESSAGE) {
        TbMessageRecord record;
        if (!tb_codec_message(&body, &record)) { printf("{\"error\":\"message\"}\n"); return 1; }
        char id[21];
        const int valid = tb_copy_id(id, sizeof(id), record.id, false);
        printf("{\"type\":\"message\",\"validId\":%s,", valid ? "true" : "false");
        print_span("id", record.id);
        printf(",\"kind\":%d,\"flags\":%d,", record.kind, record.flags);
        print_span("text", record.text);
        printf("}\n");
      } else if (type == TB_RECORD_TEXT) {
        TbSpan text;
        if (!tb_codec_text(&body, &text)) { printf("{\"error\":\"text\"}\n"); return 1; }
        printf("{\"type\":\"text\",");
        print_span("bytes", text);
        printf("}\n");
      } else {
        printf("{\"error\":\"type\"}\n");
        return 1;
      }
    }
  }
  return 0;
}
