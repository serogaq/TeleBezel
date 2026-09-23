#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "generated/protocol.h"
#include "request_layer.h"
#include "view_ports.h"

#define FAKE_SENT 128

typedef struct {
  uint32_t now;
  TbSendResult next;
  int sends;
  TbRequestArgs sent[FAKE_SENT];
  uint32_t sequences[FAKE_SENT];
  bool timer;
  uint32_t delay;
  bool view_timer;
  uint32_t view_delay;
  int changed;
} Fake;

static inline TbSendResult fake_send(void *context, uint32_t sequence, const TbRequestArgs *args) {
  Fake *fake = context;
  if (fake->next == TB_SEND_OK && fake->sends < FAKE_SENT) {
    fake->sent[fake->sends] = *args;
    fake->sequences[fake->sends] = sequence;
  }
  if (fake->next == TB_SEND_OK) { fake->sends++; }
  return fake->next;
}
static inline bool fake_schedule(void *context, uint32_t delay) { Fake *fake = context; fake->timer = true; fake->delay = delay; return true; }
static inline void fake_cancel(void *context) { ((Fake *)context)->timer = false; }
static inline uint32_t fake_now(void *context) { return ((Fake *)context)->now; }
static inline void fake_changed(void *context) { ((Fake *)context)->changed++; }
static inline bool fake_view_schedule(void *context, uint32_t delay) { Fake *fake = context; fake->view_timer = true; fake->view_delay = delay; return true; }
static inline void fake_view_cancel(void *context) { ((Fake *)context)->view_timer = false; }

static inline TbRequestPorts fake_request_ports(Fake *fake) { return (TbRequestPorts){fake_send, fake_schedule, fake_cancel, fake_now, fake}; }
static inline TbViewPorts fake_view_ports(Fake *fake) { return (TbViewPorts){fake_changed, fake_view_schedule, fake_view_cancel, fake_now, fake}; }
static const TbRequestArgs *fake_last(const Fake *fake) { return &fake->sent[fake->sends - 1]; }
static inline uint32_t fake_last_sequence(const Fake *fake) { return fake->sequences[fake->sends - 1]; }

typedef struct {
  uint8_t data[8192];
  uint16_t length;
  uint16_t record;
} Buf;

static inline void put8(Buf *buf, uint8_t value) { buf->data[buf->length++] = value; }
static inline void put16(Buf *buf, uint16_t value) { put8(buf, (uint8_t)(value & 0xFF)); put8(buf, (uint8_t)(value >> 8)); }
static inline void put32(Buf *buf, uint32_t value) { put16(buf, (uint16_t)(value & 0xFFFF)); put16(buf, (uint16_t)(value >> 16)); }
static inline void putstr8(Buf *buf, const char *value) {
  const size_t length = strlen(value);
  put8(buf, (uint8_t)length);
  memcpy(buf->data + buf->length, value, length);
  buf->length = (uint16_t)(buf->length + length);
}
static inline void putstr16(Buf *buf, const char *value) {
  const size_t length = strlen(value);
  put16(buf, (uint16_t)length);
  memcpy(buf->data + buf->length, value, length);
  buf->length = (uint16_t)(buf->length + length);
}
static inline void begin(Buf *buf, uint8_t type) { put8(buf, type); buf->record = buf->length; put16(buf, 0); }
static inline void end(Buf *buf) {
  const uint16_t size = (uint16_t)(buf->length - buf->record - 2);
  buf->data[buf->record] = (uint8_t)(size & 0xFF);
  buf->data[buf->record + 1] = (uint8_t)(size >> 8);
}
static inline void chat_record(Buf *buf, const char *id, const char *title, const char *preview) {
  begin(buf, TB_RECORD_CHAT);
  putstr8(buf, id); putstr8(buf, title); put8(buf, TB_CHAT_TYPE_BASIC_GROUP); put8(buf, 0); put16(buf, 3); put32(buf, 1700000000);
  put8(buf, TB_KIND_TEXT); put8(buf, 0); put16(buf, 0); putstr8(buf, "Ada"); putstr8(buf, ""); putstr16(buf, preview);
  end(buf);
}
static inline void summary_record(Buf *buf, uint8_t connection, uint8_t proxy, uint32_t chats, uint32_t messages) {
  begin(buf, TB_RECORD_SUMMARY);
  put8(buf, connection); put8(buf, proxy); put32(buf, chats); put32(buf, messages);
  end(buf);
}
static inline void message_record(Buf *buf, const char *id, const char *text) {
  begin(buf, TB_RECORD_MESSAGE);
  putstr8(buf, id); put32(buf, 1700000000); put8(buf, 0); put8(buf, TB_KIND_TEXT); put8(buf, 0); put16(buf, 0);
  putstr8(buf, "Ada"); putstr8(buf, ""); putstr16(buf, text);
  end(buf);
}
static inline void deliver(TbRequestLayer *layer, uint32_t sequence, int32_t result, uint32_t flags, const Buf *buf, uint16_t index, uint16_t total) {
  TbResponse response;
  memset(&response, 0, sizeof(response));
  response.result = result;
  response.flags = flags;
  response.payload = buf ? buf->data : NULL;
  response.length = buf ? buf->length : 0;
  response.index = index;
  response.total = total;
  assert(tb_requests_chunk(layer, sequence, &response));
}
