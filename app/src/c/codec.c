#include "codec.h"
#include <stddef.h>

#define F_U8 0x00
#define F_U16 0x40
#define F_U32 0x80
#define F_STR8 0xC0
#define F_STR16 0x20
#define F_REST 0x60
#define F_END 0xFF

#define U8(type, field) F_U8, offsetof(type, field)
#define U16(type, field) F_U16, offsetof(type, field)
#define U32(type, field) F_U32, offsetof(type, field)
#define STR8(type, field) F_STR8, offsetof(type, field)
#define STR16(type, field) F_STR16, offsetof(type, field)
#define REST(type, field) F_REST, offsetof(type, field)

const uint8_t tb_layout_account[] = {STR8(TbAccountRecord, id), STR8(TbAccountRecord, name), U8(TbAccountRecord, state),
                                     U8(TbAccountRecord, flags), F_END};
const uint8_t tb_layout_prefs[] = {STR8(TbPrefsRecord, default_account), U8(TbPrefsRecord, chat_list), STR8(TbPrefsRecord, host),
                                   U8(TbPrefsRecord, show_archive), U8(TbPrefsRecord, unread_mode), U8(TbPrefsRecord, photo_mode), F_END};
const uint8_t tb_layout_summary[] = {U8(TbSummaryRecord, connection), U8(TbSummaryRecord, proxy), U32(TbSummaryRecord, unread_chats),
                                     U32(TbSummaryRecord, unread_messages), F_END};
const uint8_t tb_layout_status[] = {U8(TbStatusRecord, connection), U8(TbStatusRecord, proxy), F_END};
const uint8_t tb_layout_chat[] = {STR8(TbChatRecord, id), STR8(TbChatRecord, title), U8(TbChatRecord, type), U8(TbChatRecord, flags),
                                  U16(TbChatRecord, unread), U32(TbChatRecord, last_date), U8(TbChatRecord, preview_kind),
                                  U8(TbChatRecord, preview_action), U16(TbChatRecord, preview_duration), STR8(TbChatRecord, preview_sender),
                                  STR8(TbChatRecord, preview_extra), STR16(TbChatRecord, preview_text), U8(TbChatRecord, send), F_END};
const uint8_t tb_layout_message[] = {STR8(TbMessageRecord, id), U32(TbMessageRecord, date), U8(TbMessageRecord, flags),
                                     U8(TbMessageRecord, kind), U8(TbMessageRecord, action), U16(TbMessageRecord, duration),
                                     STR8(TbMessageRecord, sender), STR8(TbMessageRecord, extra), STR16(TbMessageRecord, text),
                                     STR8(TbMessageRecord, reply_id), STR8(TbMessageRecord, reply_sender), STR8(TbMessageRecord, reply_text),
                                     STR8(TbMessageRecord, forward), U8(TbMessageRecord, media), U32(TbMessageRecord, album),
                                     U8(TbMessageRecord, media_count), STR8(TbMessageRecord, signature), F_END};
const uint8_t tb_layout_text[] = {F_STR16, 0, F_END};
const uint8_t tb_layout_templates[] = {U32(TbTemplatesRecord, revision), U8(TbTemplatesRecord, count), U8(TbTemplatesRecord, flags), F_END};
const uint8_t tb_layout_template[] = {U8(TbTemplateRecord, index), U16(TbTemplateRecord, length), STR8(TbTemplateRecord, preview), F_END};
const uint8_t tb_layout_draft[] = {U32(TbDraftRecord, id), U16(TbDraftRecord, bytes), U16(TbDraftRecord, units), U8(TbDraftRecord, flags), F_END};
const uint8_t tb_layout_send_state[] = {U32(TbSendStateRecord, draft_id), U8(TbSendStateRecord, state), U8(TbSendStateRecord, code),
                                        U16(TbSendStateRecord, retry_after), U8(TbSendStateRecord, flags), STR8(TbSendStateRecord, account),
                                        STR8(TbSendStateRecord, chat), STR8(TbSendStateRecord, message), STR8(TbSendStateRecord, title),
                                        STR8(TbSendStateRecord, preview), F_END};
const uint8_t tb_layout_media_info[] = {U8(TbMediaInfoRecord, state), U8(TbMediaInfoRecord, index), U8(TbMediaInfoRecord, count),
                                        U8(TbMediaInfoRecord, flags), U32(TbMediaInfoRecord, tag), U32(TbMediaInfoRecord, total),
                                        U16(TbMediaInfoRecord, retry_after), STR8(TbMediaInfoRecord, item), F_END};
const uint8_t tb_layout_media_data[] = {U32(TbMediaDataRecord, offset), REST(TbMediaDataRecord, data), F_END};

void tb_cursor_init(TbCursor *cursor, const uint8_t *data, uint16_t length) {
  cursor->data = data;
  cursor->length = data ? length : 0;
  cursor->offset = 0;
}

static bool take(TbCursor *cursor, uint16_t size, const uint8_t **out) {
  if ((uint32_t)cursor->offset + size > cursor->length) { return false; }
  *out = cursor->data + cursor->offset;
  cursor->offset = (uint16_t)(cursor->offset + size);
  return true;
}

static uint32_t little(const uint8_t *bytes, uint8_t size) {
  uint32_t value = 0;
  while (size--) { value = (value << 8) | bytes[size]; }
  return value;
}

bool tb_codec_decode(TbCursor *body, const uint8_t *layout, void *out) {
  for (; *layout != F_END; layout += 2) {
    uint8_t *field = (uint8_t *)out + layout[1];
    const uint8_t type = layout[0];
    const uint8_t *bytes = NULL;
    if (type == F_U8 || type == F_U16 || type == F_U32) {
      const uint8_t size = type == F_U8 ? 1 : type == F_U16 ? 2 : 4;
      if (!take(body, size, &bytes)) { return false; }
      const uint32_t value = little(bytes, size);
      if (size == 1) { *field = (uint8_t)value; }
      else if (size == 2) { *(uint16_t *)(void *)field = (uint16_t)value; }
      else { *(uint32_t *)(void *)field = value; }
      continue;
    }
    uint16_t length = (uint16_t)(body->length - body->offset);
    if (type != F_REST) {
      const uint8_t prefix = type == F_STR8 ? 1 : 2;
      if (!take(body, prefix, &bytes)) { return false; }
      length = (uint16_t)little(bytes, prefix);
    }
    TbSpan *span = (TbSpan *)(void *)field;
    if (!take(body, length, &span->data)) { return false; }
    span->length = length;
  }
  return body->offset == body->length;
}

bool tb_codec_next(TbCursor *cursor, uint8_t *type, TbCursor *body) {
  const uint8_t *header = NULL;
  const uint8_t *content = NULL;
  if (!take(cursor, 3, &header)) { return false; }
  const uint16_t length = (uint16_t)little(header + 1, 2);
  if (!take(cursor, length, &content)) { return false; }
  *type = header[0];
  tb_cursor_init(body, content, length);
  return true;
}
