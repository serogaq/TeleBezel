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
} TbPrefsRecord;

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
} TbMessageRecord;

void tb_cursor_init(TbCursor *cursor, const uint8_t *data, uint16_t length);
bool tb_codec_next(TbCursor *cursor, uint8_t *type, TbCursor *body);
bool tb_codec_account(TbCursor *body, TbAccountRecord *out);
bool tb_codec_prefs(TbCursor *body, TbPrefsRecord *out);
bool tb_codec_chat(TbCursor *body, TbChatRecord *out);
bool tb_codec_message(TbCursor *body, TbMessageRecord *out);
bool tb_codec_text(TbCursor *body, TbSpan *out);
