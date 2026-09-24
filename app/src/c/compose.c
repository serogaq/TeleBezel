#include "compose.h"
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "errors.h"
#include "generated/protocol.h"

static void changed(TbCompose *compose) {
  ++compose->revision;
  compose->ports.changed(compose->ports.context);
}

static void cancel(TbCompose *compose, uint32_t *pending) {
  if (!*pending) { return; }
  const uint32_t sequence = *pending;
  *pending = 0;
  tb_requests_cancel(compose->requests, sequence);
}

static void free_previews(TbCompose *compose) {
  free(compose->previews);
  compose->previews = NULL;
  compose->template_count = 0;
  compose->template_total = 0;
}

static void free_draft(TbCompose *compose) {
  free(compose->text);
  compose->text = NULL;
  compose->text_length = 0;
  free(compose->dictated);
  compose->dictated = NULL;
  compose->dictated_length = 0;
}

static void base_args(const TbCompose *compose, TbRequestArgs *args, uint8_t kind) {
  memset(args, 0, sizeof(*args));
  args->kind = kind;
  strncpy(args->account, compose->target.send.account, sizeof(args->account) - 1);
  strncpy(args->chat, compose->target.send.chat, sizeof(args->chat) - 1);
  strncpy(args->message, compose->target.send.reply, sizeof(args->message) - 1);
}

uint16_t tb_utf8_units(const char *text, size_t length) {
  uint32_t units = 0;
  for (size_t index = 0; index < length; ++index) {
    const uint8_t byte = (uint8_t)text[index];
    if ((byte & 0xC0) == 0x80) { continue; }
    units += byte >= 0xF0 ? 2 : 1;
  }
  return (uint16_t)(units > UINT16_MAX ? UINT16_MAX : units);
}

static void templates_done(void *owner, uint32_t sequence, const TbResponse *response) {
  TbCompose *compose = owner;
  if (sequence != compose->templates_pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  if (response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK) {
    compose->templates_pending = 0;
    if (!response->final) { tb_requests_cancel(compose->requests, sequence); }
    compose->templates_error = tb_error_from_response(response);
    compose->templates = compose->template_count ? TB_TEMPLATES_READY : TB_TEMPLATES_FAILED;
    compose->templates_stale = compose->template_count > 0;
    changed(compose);
    return;
  }
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    if (!tb_codec_next(&cursor, &type, &body)) { break; }
    if (type == TB_RECORD_TEMPLATES) {
      TbTemplatesRecord header;
      if (!tb_codec_templates(&body, &header)) { continue; }
      free_previews(compose);
      compose->templates_rev = header.revision;
      compose->templates_stale = (header.flags & 1) != 0;
      compose->template_total = header.count;
      const uint8_t count = header.count < compose->config.capacity ? header.count : compose->config.capacity;
      if (count) { compose->previews = calloc(count, compose->config.preview_limit); }
    } else if (type == TB_RECORD_TEMPLATE && compose->previews) {
      TbTemplateRecord item;
      if (!tb_codec_template(&body, &item) || item.index >= compose->config.capacity || item.index >= compose->template_total) { continue; }
      tb_copy_span(compose->previews + (size_t)item.index * compose->config.preview_limit, compose->config.preview_limit, item.preview);
      if (item.index + 1 > compose->template_count) { compose->template_count = (uint8_t)(item.index + 1); }
    }
  }
  if (response->final) {
    compose->templates_pending = 0;
    compose->templates_error = TB_ERROR_NONE;
    compose->templates = TB_TEMPLATES_READY;
    changed(compose);
  }
}

static void draft_done(void *owner, uint32_t sequence, const TbResponse *response) {
  TbCompose *compose = owner;
  if (sequence != compose->draft_pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  if (response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK) {
    compose->draft_pending = 0;
    if (!response->final) { tb_requests_cancel(compose->requests, sequence); }
    compose->draft_error = tb_error_from_response(response);
    compose->draft = TB_DRAFT_FAILED;
    free(compose->text);
    compose->text = NULL;
    compose->text_length = 0;
    if (compose->draft_error == TB_RESULT_CURSOR_LOST) { tb_compose_load_templates(compose, true); }
    changed(compose);
    return;
  }
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    if (!tb_codec_next(&cursor, &type, &body)) { break; }
    if (type == TB_RECORD_DRAFT) {
      TbDraftRecord record;
      if (!tb_codec_draft(&body, &record)) { continue; }
      compose->draft_id = record.id;
      compose->draft_bytes = record.bytes;
      compose->draft_units = record.units;
      compose->too_long = (record.flags & TB_DRAFT_FLAG_TOO_LONG) != 0;
      free(compose->text);
      compose->text = calloc((size_t)(record.bytes < compose->config.text_limit ? record.bytes : compose->config.text_limit) + 4, 1);
      compose->text_length = 0;
    } else if (type == TB_RECORD_TEXT && compose->text) {
      TbSpan part;
      if (!tb_codec_text(&body, &part)) { continue; }
      const size_t room = (size_t)(compose->draft_bytes < compose->config.text_limit ? compose->draft_bytes : compose->config.text_limit) + 3 -
                          compose->text_length;
      const size_t fit = tb_utf8_fit(part.data, part.length, room);
      memcpy(compose->text + compose->text_length, part.data, fit);
      compose->text_length = (uint16_t)(compose->text_length + fit);
      compose->text[compose->text_length] = '\0';
    }
  }
  if (response->final) {
    compose->draft_pending = 0;
    free(compose->dictated);
    compose->dictated = NULL;
    compose->dictated_length = 0;
    if (!compose->draft_id || !compose->text) {
      compose->draft = TB_DRAFT_FAILED;
      compose->draft_error = TB_ERROR_OUT_OF_MEMORY;
    } else {
      compose->draft = TB_DRAFT_READY;
      compose->draft_error = TB_ERROR_NONE;
    }
    changed(compose);
  }
}

void tb_compose_init(TbCompose *compose, TbRequestLayer *requests, TbViewPorts ports, TbComposeConfig config) {
  memset(compose, 0, sizeof(*compose));
  compose->requests = requests;
  compose->ports = ports;
  compose->config = config;
}

bool tb_compose_open(TbCompose *compose, const TbComposeTarget *target) {
  if (!target || !target->send.account[0] || !target->send.chat[0]) { return false; }
  tb_compose_close(compose, false);
  compose->target = *target;
  compose->open = true;
  tb_compose_load_templates(compose, false);
  return true;
}

void tb_compose_load_templates(TbCompose *compose, bool refresh) {
  if (!compose->open) { return; }
  cancel(compose, &compose->templates_pending);
  TbRequestArgs args;
  base_args(compose, &args, TB_REQUEST_TEMPLATES);
  args.page_op = refresh ? TB_PAGE_OP_REFRESH : TB_PAGE_OP_FIRST;
  args.text_limit = compose->config.preview_limit - 1;
  compose->templates = TB_TEMPLATES_LOADING;
  compose->templates_pending = tb_requests_submit(compose->requests, &args, compose->config.timeout, 2, false, templates_done, compose);
  if (!compose->templates_pending) {
    compose->templates_error = tb_error_submit_failed(compose->requests);
    compose->templates = compose->template_count ? TB_TEMPLATES_READY : TB_TEMPLATES_FAILED;
  }
  changed(compose);
}

const char *tb_compose_template(const TbCompose *compose, uint8_t index) {
  if (!compose->previews || index >= compose->template_count) { return ""; }
  return compose->previews + (size_t)index * compose->config.preview_limit;
}

static void discard_current(TbCompose *compose) {
  if (!compose->draft_id) { return; }
  TbRequestArgs args;
  base_args(compose, &args, TB_REQUEST_DRAFT_DISCARD);
  args.draft_id = compose->draft_id;
  tb_requests_submit(compose->requests, &args, compose->config.timeout, 1, false, NULL, NULL);
  compose->draft_id = 0;
}

static bool request_draft(TbCompose *compose, TbRequestArgs *args) {
  cancel(compose, &compose->draft_pending);
  discard_current(compose);
  free(compose->text);
  compose->text = NULL;
  compose->text_length = 0;
  compose->too_long = false;
  args->text_limit = compose->config.text_limit;
  compose->draft = TB_DRAFT_LOADING;
  compose->draft_pending = tb_requests_submit(compose->requests, args, compose->config.timeout, 2, true, draft_done, compose);
  if (!compose->draft_pending) {
    compose->draft = TB_DRAFT_FAILED;
    compose->draft_error = tb_error_submit_failed(compose->requests);
  }
  changed(compose);
  return compose->draft_pending != 0;
}

bool tb_compose_pick(TbCompose *compose, uint8_t index) {
  if (!compose->open || index >= compose->template_count) { return false; }
  TbRequestArgs args;
  base_args(compose, &args, TB_REQUEST_DRAFT);
  args.template_index = index;
  args.templates_rev = compose->templates_rev;
  return request_draft(compose, &args);
}

bool tb_compose_dictated(TbCompose *compose, const char *text, size_t length) {
  if (!compose->open || !text || length == 0) { return false; }
  cancel(compose, &compose->draft_pending);
  free(compose->dictated);
  compose->dictated = NULL;
  compose->dictated_length = 0;
  if (length > TB_COMPOSE_DRAFT_LIMIT) {
    discard_current(compose);
    free(compose->text);
    compose->text = NULL;
    compose->text_length = 0;
    compose->draft_bytes = (uint16_t)(length > UINT16_MAX ? UINT16_MAX : length);
    compose->draft_units = tb_utf8_units(text, length);
    compose->too_long = true;
    compose->draft = TB_DRAFT_READY;
    compose->draft_error = TB_ERROR_NONE;
    changed(compose);
    return true;
  }
  compose->dictated = malloc(length);
  if (!compose->dictated) {
    compose->draft = TB_DRAFT_FAILED;
    compose->draft_error = TB_ERROR_OUT_OF_MEMORY;
    changed(compose);
    return false;
  }
  memcpy(compose->dictated, text, length);
  compose->dictated_length = (uint16_t)length;
  TbRequestArgs args;
  base_args(compose, &args, TB_REQUEST_DRAFT);
  args.payload = compose->dictated;
  args.payload_length = compose->dictated_length;
  return request_draft(compose, &args);
}

bool tb_compose_sendable(const TbCompose *compose) {
  return compose->open && compose->draft == TB_DRAFT_READY && compose->draft_id != 0 && !compose->too_long;
}

void tb_compose_drop_draft(TbCompose *compose) {
  cancel(compose, &compose->draft_pending);
  discard_current(compose);
  free_draft(compose);
  compose->too_long = false;
  compose->draft = TB_DRAFT_NONE;
  compose->draft_error = TB_ERROR_NONE;
}

void tb_compose_close(TbCompose *compose, bool keep_draft) {
  cancel(compose, &compose->templates_pending);
  if (keep_draft) {
    cancel(compose, &compose->draft_pending);
    free_draft(compose);
    compose->draft_id = 0;
    compose->draft = TB_DRAFT_NONE;
  } else {
    tb_compose_drop_draft(compose);
  }
  free_previews(compose);
  compose->templates = TB_TEMPLATES_NONE;
  compose->templates_stale = false;
  compose->open = false;
  memset(&compose->target, 0, sizeof(compose->target));
}
