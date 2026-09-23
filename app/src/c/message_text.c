#include "message_text.h"
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "errors.h"
#include "generated/protocol.h"

static void changed(TbMessageText *reader) { reader->ports.changed(reader->ports.context); }

static void completed(void *owner, uint32_t sequence, const TbResponse *response);

static void submit(TbMessageText *reader) {
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_MESSAGE;
  args.text_limit = reader->limit;
  strncpy(args.account, reader->account, sizeof(args.account) - 1);
  strncpy(args.chat, reader->chat, sizeof(args.chat) - 1);
  strncpy(args.message, reader->message, sizeof(args.message) - 1);
  reader->length = 0;
  reader->text[0] = '\0';
  reader->truncated = false;
  reader->error = TB_ERROR_NONE;
  reader->loading = true;
  reader->pending = tb_requests_submit(reader->requests, &args, reader->timeout, 2, false, completed, reader);
  if (!reader->pending) {
    reader->loading = false;
    reader->error = tb_error_submit_failed(reader->requests);
  }
  changed(reader);
}

static bool append(TbMessageText *reader, const TbResponse *response) {
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    TbSpan part;
    if (!tb_codec_next(&cursor, &type, &body) || type != TB_RECORD_TEXT || !tb_codec_text(&body, &part)) { return false; }
    size_t room = (size_t)reader->limit - reader->length;
    size_t size = part.length < room ? part.length : room;
    if (size < part.length) {
      while (size > 0 && (part.data[size] & 0xC0) == 0x80) { --size; }
      reader->truncated = true;
    }
    memcpy(reader->text + reader->length, part.data, size);
    reader->length = (uint16_t)(reader->length + size);
    reader->text[reader->length] = '\0';
  }
  return true;
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbMessageText *reader = owner;
  if (sequence != reader->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  bool failed = response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK;
  int32_t error = failed ? tb_error_from_response(response) : TB_ERROR_NONE;
  if (!failed && !append(reader, response)) {
    failed = true;
    error = TB_RESULT_PROTOCOL_ERROR;
  }
  if (failed) {
    if (!response->final) {
      reader->pending = 0;
      tb_requests_cancel(reader->requests, sequence);
    }
    reader->pending = 0;
    reader->loading = false;
    reader->error = error;
    reader->length = 0;
    reader->text[0] = '\0';
    changed(reader);
    return;
  }
  if (response->index == 0 && (response->flags & TB_FLAG_TRUNCATED)) { reader->truncated = true; }
  if (response->final) {
    reader->pending = 0;
    reader->loading = false;
    changed(reader);
  }
}

void tb_message_text_init(TbMessageText *reader, TbRequestLayer *requests, TbViewPorts ports, uint16_t limit, uint32_t timeout) {
  memset(reader, 0, sizeof(*reader));
  reader->requests = requests;
  reader->ports = ports;
  reader->limit = limit;
  reader->timeout = timeout;
}

bool tb_message_text_open(TbMessageText *reader, const char *account, const char *chat, const char *message) {
  tb_message_text_close(reader);
  reader->text = malloc((size_t)reader->limit + 1);
  if (!reader->text) {
    reader->error = TB_ERROR_OUT_OF_MEMORY;
    return false;
  }
  strncpy(reader->account, account, sizeof(reader->account) - 1);
  reader->account[sizeof(reader->account) - 1] = '\0';
  strncpy(reader->chat, chat, sizeof(reader->chat) - 1);
  reader->chat[sizeof(reader->chat) - 1] = '\0';
  strncpy(reader->message, message, sizeof(reader->message) - 1);
  reader->message[sizeof(reader->message) - 1] = '\0';
  submit(reader);
  return true;
}

void tb_message_text_retry(TbMessageText *reader) {
  if (!reader->text || reader->loading) { return; }
  submit(reader);
}

void tb_message_text_close(TbMessageText *reader) {
  if (reader->pending) {
    const uint32_t pending = reader->pending;
    reader->pending = 0;
    tb_requests_cancel(reader->requests, pending);
  }
  free(reader->text);
  reader->text = NULL;
  reader->length = 0;
  reader->loading = false;
  reader->truncated = false;
}
