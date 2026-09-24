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
        printf(",\"showArchive\":%d,\"unreadMode\":%d}\n", record.show_archive, record.unread_mode);
      } else if (type == TB_RECORD_SUMMARY) {
        TbSummaryRecord record;
        if (!tb_codec_summary(&body, &record)) { printf("{\"error\":\"summary\"}\n"); return 1; }
        printf("{\"type\":\"summary\",\"connection\":%d,\"proxy\":%d,\"unreadChats\":%lu,\"unreadMessages\":%lu}\n", record.connection,
               record.proxy, (unsigned long)record.unread_chats, (unsigned long)record.unread_messages);
      } else if (type == TB_RECORD_STATUS) {
        TbStatusRecord record;
        if (!tb_codec_status(&body, &record)) { printf("{\"error\":\"status\"}\n"); return 1; }
        printf("{\"type\":\"status\",\"connection\":%d,\"proxy\":%d}\n", record.connection, record.proxy);
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
        printf(",\"send\":%d}\n", record.send);
      } else if (type == TB_RECORD_MESSAGE) {
        TbMessageRecord record;
        if (!tb_codec_message(&body, &record)) { printf("{\"error\":\"message\"}\n"); return 1; }
        char id[21];
        const int valid = tb_copy_id(id, sizeof(id), record.id, false);
        printf("{\"type\":\"message\",\"validId\":%s,", valid ? "true" : "false");
        print_span("id", record.id);
        printf(",\"kind\":%d,\"flags\":%d,", record.kind, record.flags);
        print_span("text", record.text);
        printf(",");
        print_span("replyId", record.reply_id);
        printf("}\n");
      } else if (type == TB_RECORD_TEMPLATES) {
        TbTemplatesRecord record;
        if (!tb_codec_templates(&body, &record)) { printf("{\"error\":\"templates\"}\n"); return 1; }
        printf("{\"type\":\"templates\",\"revision\":%lu,\"count\":%d,\"flags\":%d}\n", (unsigned long)record.revision, record.count,
               record.flags);
      } else if (type == TB_RECORD_TEMPLATE) {
        TbTemplateRecord record;
        if (!tb_codec_template(&body, &record)) { printf("{\"error\":\"template\"}\n"); return 1; }
        printf("{\"type\":\"template\",\"index\":%d,\"length\":%d,", record.index, record.length);
        print_span("preview", record.preview);
        printf("}\n");
      } else if (type == TB_RECORD_DRAFT) {
        TbDraftRecord record;
        if (!tb_codec_draft(&body, &record)) { printf("{\"error\":\"draft\"}\n"); return 1; }
        printf("{\"type\":\"draft\",\"id\":%lu,\"bytes\":%d,\"units\":%d,\"flags\":%d}\n", (unsigned long)record.id, record.bytes,
               record.units, record.flags);
      } else if (type == TB_RECORD_SEND_STATE || type == TB_RECORD_PENDING_SEND) {
        TbSendStateRecord record;
        if (!tb_codec_send_state(&body, &record)) { printf("{\"error\":\"send_state\"}\n"); return 1; }
        printf("{\"type\":\"%s\",\"draftId\":%lu,\"state\":%d,\"code\":%d,\"retryAfter\":%d,\"flags\":%d,",
               type == TB_RECORD_SEND_STATE ? "send_state" : "pending_send", (unsigned long)record.draft_id, record.state, record.code,
               record.retry_after, record.flags);
        print_span("chat", record.chat);
        printf(",");
        print_span("message", record.message);
        printf(",");
        print_span("title", record.title);
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
