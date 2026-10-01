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
  uint8_t photo_mode;
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
  uint8_t media;
  uint32_t album;
  uint8_t media_count;
  TbSpan signature;
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

typedef struct {
  uint8_t state;
  uint8_t index;
  uint8_t count;
  uint8_t flags;
  uint32_t tag;
  uint32_t total;
  uint16_t retry_after;
  TbSpan item;
} TbMediaInfoRecord;

typedef struct {
  uint32_t offset;
  TbSpan data;
} TbMediaDataRecord;

extern const uint8_t tb_layout_account[];
extern const uint8_t tb_layout_prefs[];
extern const uint8_t tb_layout_summary[];
extern const uint8_t tb_layout_status[];
extern const uint8_t tb_layout_chat[];
extern const uint8_t tb_layout_message[];
extern const uint8_t tb_layout_text[];
extern const uint8_t tb_layout_templates[];
extern const uint8_t tb_layout_template[];
extern const uint8_t tb_layout_draft[];
extern const uint8_t tb_layout_send_state[];
extern const uint8_t tb_layout_media_info[];
extern const uint8_t tb_layout_media_data[];

void tb_cursor_init(TbCursor *cursor, const uint8_t *data, uint16_t length);
bool tb_codec_next(TbCursor *cursor, uint8_t *type, TbCursor *body);
bool tb_codec_decode(TbCursor *body, const uint8_t *layout, void *out);

#define tb_codec_account(body, out) tb_codec_decode((body), tb_layout_account, (TbAccountRecord *)(out))
#define tb_codec_prefs(body, out) tb_codec_decode((body), tb_layout_prefs, (TbPrefsRecord *)(out))
#define tb_codec_summary(body, out) tb_codec_decode((body), tb_layout_summary, (TbSummaryRecord *)(out))
#define tb_codec_status(body, out) tb_codec_decode((body), tb_layout_status, (TbStatusRecord *)(out))
#define tb_codec_chat(body, out) tb_codec_decode((body), tb_layout_chat, (TbChatRecord *)(out))
#define tb_codec_message(body, out) tb_codec_decode((body), tb_layout_message, (TbMessageRecord *)(out))
#define tb_codec_text(body, out) tb_codec_decode((body), tb_layout_text, (TbSpan *)(out))
#define tb_codec_templates(body, out) tb_codec_decode((body), tb_layout_templates, (TbTemplatesRecord *)(out))
#define tb_codec_template(body, out) tb_codec_decode((body), tb_layout_template, (TbTemplateRecord *)(out))
#define tb_codec_draft(body, out) tb_codec_decode((body), tb_layout_draft, (TbDraftRecord *)(out))
#define tb_codec_send_state(body, out) tb_codec_decode((body), tb_layout_send_state, (TbSendStateRecord *)(out))
#define tb_codec_media_info(body, out) tb_codec_decode((body), tb_layout_media_info, (TbMediaInfoRecord *)(out))
#define tb_codec_media_data(body, out) tb_codec_decode((body), tb_layout_media_data, (TbMediaDataRecord *)(out))
