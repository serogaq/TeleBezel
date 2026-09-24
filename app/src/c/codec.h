#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  const uint8_t *data;
  uint16_t length;
} TbSpan;

typedef struct {
  const uint8_t *data;
  uint16_t length;
  uint16_t offset;
} TbCursor;

typedef struct {
  TbSpan id;
  TbSpan name;
  uint8_t state;
  uint8_t flags;
} TbAccountRecord;

typedef struct {
  TbSpan default_account;
  uint8_t chat_list;
  TbSpan host;
  uint8_t show_archive;
  uint8_t unread_mode;
} TbPrefsRecord;

typedef struct {
  uint8_t connection;
  uint8_t proxy;
  uint32_t unread_chats;
  uint32_t unread_messages;
} TbSummaryRecord;

typedef struct {
  uint8_t connection;
  uint8_t proxy;
} TbStatusRecord;

typedef struct {
  TbSpan id;
  TbSpan title;
  uint8_t type;
  uint8_t flags;
  uint16_t unread;
  uint32_t last_date;
  uint8_t preview_kind;
  uint8_t preview_action;
  uint16_t preview_duration;
  TbSpan preview_sender;
  TbSpan preview_extra;
  TbSpan preview_text;
  uint8_t send;
} TbChatRecord;

typedef struct {
  TbSpan id;
  uint32_t date;
  uint8_t flags;
  uint8_t kind;
  uint8_t action;
  uint16_t duration;
  TbSpan sender;
  TbSpan extra;
  TbSpan text;
  TbSpan reply_id;
  TbSpan reply_sender;
  TbSpan reply_text;
  TbSpan forward;
} TbMessageRecord;

typedef struct {
  uint32_t revision;
  uint8_t count;
  uint8_t flags;
} TbTemplatesRecord;

typedef struct {
  uint8_t index;
  uint16_t length;
  TbSpan preview;
} TbTemplateRecord;

typedef struct {
  uint32_t id;
  uint16_t bytes;
  uint16_t units;
  uint8_t flags;
} TbDraftRecord;

typedef struct {
  uint32_t draft_id;
  uint8_t state;
  uint8_t code;
  uint16_t retry_after;
  uint8_t flags;
  TbSpan account;
  TbSpan chat;
  TbSpan message;
  TbSpan title;
  TbSpan preview;
} TbSendStateRecord;

void tb_cursor_init(TbCursor *cursor, const uint8_t *data, uint16_t length);
bool tb_codec_next(TbCursor *cursor, uint8_t *type, TbCursor *body);
bool tb_codec_account(TbCursor *body, TbAccountRecord *out);
bool tb_codec_prefs(TbCursor *body, TbPrefsRecord *out);
bool tb_codec_summary(TbCursor *body, TbSummaryRecord *out);
bool tb_codec_status(TbCursor *body, TbStatusRecord *out);
bool tb_codec_chat(TbCursor *body, TbChatRecord *out);
bool tb_codec_message(TbCursor *body, TbMessageRecord *out);
bool tb_codec_text(TbCursor *body, TbSpan *out);
bool tb_codec_templates(TbCursor *body, TbTemplatesRecord *out);
bool tb_codec_template(TbCursor *body, TbTemplateRecord *out);
bool tb_codec_draft(TbCursor *body, TbDraftRecord *out);
bool tb_codec_send_state(TbCursor *body, TbSendStateRecord *out);
