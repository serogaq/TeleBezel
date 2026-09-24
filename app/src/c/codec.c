#include "codec.h"
#include <stddef.h>

void tb_cursor_init(TbCursor *cursor, const uint8_t *data, uint16_t length) {
  cursor->data = data;
  cursor->length = data ? length : 0;
  cursor->offset = 0;
}

static bool remaining(const TbCursor *cursor, uint16_t size) { return (uint32_t)cursor->offset + size <= cursor->length; }

static bool u8(TbCursor *cursor, uint8_t *out) {
  if (!remaining(cursor, 1)) { return false; }
  *out = cursor->data[cursor->offset++];
  return true;
}

static bool u16(TbCursor *cursor, uint16_t *out) {
  if (!remaining(cursor, 2)) { return false; }
  *out = (uint16_t)(cursor->data[cursor->offset] | (cursor->data[cursor->offset + 1] << 8));
  cursor->offset += 2;
  return true;
}

static bool u32(TbCursor *cursor, uint32_t *out) {
  if (!remaining(cursor, 4)) { return false; }
  const uint8_t *bytes = cursor->data + cursor->offset;
  *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
  cursor->offset += 4;
  return true;
}

static bool span(TbCursor *cursor, uint16_t length, TbSpan *out) {
  if (!remaining(cursor, length)) { return false; }
  out->data = cursor->data + cursor->offset;
  out->length = length;
  cursor->offset += length;
  return true;
}

static bool str8(TbCursor *cursor, TbSpan *out) {
  uint8_t length = 0;
  return u8(cursor, &length) && span(cursor, length, out);
}

static bool str16(TbCursor *cursor, TbSpan *out) {
  uint16_t length = 0;
  return u16(cursor, &length) && span(cursor, length, out);
}

static bool done(const TbCursor *cursor) { return cursor->offset == cursor->length; }

bool tb_codec_next(TbCursor *cursor, uint8_t *type, TbCursor *body) {
  uint16_t length = 0;
  TbSpan content;
  if (!u8(cursor, type) || !u16(cursor, &length) || !span(cursor, length, &content)) { return false; }
  tb_cursor_init(body, content.data, content.length);
  return true;
}

bool tb_codec_account(TbCursor *body, TbAccountRecord *out) {
  return str8(body, &out->id) && str8(body, &out->name) && u8(body, &out->state) && u8(body, &out->flags) && done(body);
}

bool tb_codec_prefs(TbCursor *body, TbPrefsRecord *out) {
  return str8(body, &out->default_account) && u8(body, &out->chat_list) && str8(body, &out->host) && u8(body, &out->show_archive) &&
         u8(body, &out->unread_mode) && done(body);
}

bool tb_codec_summary(TbCursor *body, TbSummaryRecord *out) {
  return u8(body, &out->connection) && u8(body, &out->proxy) && u32(body, &out->unread_chats) && u32(body, &out->unread_messages) &&
         done(body);
}

bool tb_codec_status(TbCursor *body, TbStatusRecord *out) {
  return u8(body, &out->connection) && u8(body, &out->proxy) && done(body);
}

bool tb_codec_chat(TbCursor *body, TbChatRecord *out) {
  return str8(body, &out->id) && str8(body, &out->title) && u8(body, &out->type) && u8(body, &out->flags) &&
         u16(body, &out->unread) && u32(body, &out->last_date) && u8(body, &out->preview_kind) &&
         u8(body, &out->preview_action) && u16(body, &out->preview_duration) && str8(body, &out->preview_sender) &&
         str8(body, &out->preview_extra) && str16(body, &out->preview_text) && u8(body, &out->send) && done(body);
}

bool tb_codec_message(TbCursor *body, TbMessageRecord *out) {
  return str8(body, &out->id) && u32(body, &out->date) && u8(body, &out->flags) && u8(body, &out->kind) &&
         u8(body, &out->action) && u16(body, &out->duration) && str8(body, &out->sender) && str8(body, &out->extra) &&
         str16(body, &out->text) && str8(body, &out->reply_id) && str8(body, &out->reply_sender) &&
         str8(body, &out->reply_text) && str8(body, &out->forward) && done(body);
}

bool tb_codec_text(TbCursor *body, TbSpan *out) { return str16(body, out) && done(body); }

bool tb_codec_templates(TbCursor *body, TbTemplatesRecord *out) {
  return u32(body, &out->revision) && u8(body, &out->count) && u8(body, &out->flags) && done(body);
}

bool tb_codec_template(TbCursor *body, TbTemplateRecord *out) {
  return u8(body, &out->index) && u16(body, &out->length) && str8(body, &out->preview) && done(body);
}

bool tb_codec_draft(TbCursor *body, TbDraftRecord *out) {
  return u32(body, &out->id) && u16(body, &out->bytes) && u16(body, &out->units) && u8(body, &out->flags) && done(body);
}

bool tb_codec_send_state(TbCursor *body, TbSendStateRecord *out) {
  return u32(body, &out->draft_id) && u8(body, &out->state) && u8(body, &out->code) && u16(body, &out->retry_after) &&
         u8(body, &out->flags) && str8(body, &out->account) && str8(body, &out->chat) && str8(body, &out->message) &&
         str8(body, &out->title) && str8(body, &out->preview) && done(body);
}
