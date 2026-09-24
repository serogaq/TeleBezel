#include "send_tracker.h"
#include <stdio.h>
#include <string.h>
#include "errors.h"
#include "generated/protocol.h"
#include "text.h"

static void notify(TbSendTracker *tracker) { tracker->ports.changed(tracker->ports.context, &tracker->status); }

static TbSendPhase phase_of(uint8_t state) {
  switch (state) {
    case TB_SEND_STATE_SENT: return TB_SENDING_SENT;
    case TB_SEND_STATE_FAILED: return TB_SENDING_FAILED;
    case TB_SEND_STATE_UNKNOWN: return TB_SENDING_UNKNOWN;
    default: return TB_SENDING_PENDING;
  }
}

static bool settled(TbSendPhase phase) { return phase == TB_SENDING_SENT || phase == TB_SENDING_FAILED; }

static bool same_target(const TbSendTarget *target, const TbSendStateRecord *record) {
  char account[TB_ACCOUNT_ID_SIZE];
  char chat[TB_TELEGRAM_ID_SIZE];
  tb_copy_span(account, sizeof(account), record->account);
  tb_copy_span(chat, sizeof(chat), record->chat);
  return strcmp(account, target->account) == 0 && strcmp(chat, target->chat) == 0;
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response);

static bool submit(TbSendTracker *tracker, uint8_t kind) {
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = kind;
  args.draft_id = tracker->status.draft_id;
  args.attempt = tracker->attempt;
  strncpy(args.account, tracker->status.target.account, sizeof(args.account) - 1);
  strncpy(args.chat, tracker->status.target.chat, sizeof(args.chat) - 1);
  strncpy(args.message, tracker->status.target.reply, sizeof(args.message) - 1);
  tracker->pending = tb_requests_submit(tracker->requests, &args, TB_SEND_TIMEOUT, TB_SEND_ATTEMPTS, kind == TB_REQUEST_SEND, completed, tracker);
  return tracker->pending != 0;
}

static void apply(TbSendTracker *tracker, const TbSendStateRecord *record) {
  TbSendStatus *status = &tracker->status;
  const TbSendPhase phase = phase_of(record->state);
  const int32_t code = record->code;
  if (settled(status->phase) && !settled(phase)) { return; }
  const bool same = status->phase == phase && status->code == code && status->flags == record->flags;
  status->phase = phase;
  status->code = code;
  status->retry_after = record->retry_after;
  status->flags = record->flags;
  tb_copy_span(status->message, sizeof(status->message), record->message);
  if (record->title.length) { tb_copy_span(status->title, sizeof(status->title), record->title); }
  if (record->preview.length) { tb_copy_span(status->preview, sizeof(status->preview), record->preview); }
  if (!same) { notify(tracker); }
}

static bool first_state(const TbResponse *response, TbSendStateRecord *record) {
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  uint8_t type = 0;
  TbCursor body;
  return tb_codec_next(&cursor, &type, &body) && type == TB_RECORD_SEND_STATE && tb_codec_send_state(&body, record);
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbSendTracker *tracker = owner;
  if (sequence != tracker->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  tracker->pending = 0;
  TbSendStatus *status = &tracker->status;
  TbSendStateRecord record;
  if (response->outcome == TB_OUTCOME_RESPONSE && response->result == TB_RESULT_OK && first_state(response, &record) &&
      record.draft_id == status->draft_id) {
    apply(tracker, &record);
    return;
  }
  if (!response->final) { tb_requests_cancel(tracker->requests, sequence); }
  const int32_t error = tb_error_from_response(response);
  if (error == TB_ERROR_NO_RESPONSE || error == TB_ERROR_PHONE_UNREACHABLE) {
    status->phase = TB_SENDING_UNKNOWN;
    status->code = error;
  } else if (!settled(status->phase)) {
    status->phase = TB_SENDING_FAILED;
    status->code = error == TB_RESULT_OK ? TB_RESULT_PROTOCOL_ERROR : error;
    status->flags = error == TB_RESULT_BUSY || error == TB_RESULT_SEND_RATE_LIMITED ? TB_SEND_FLAG_RETRYABLE : 0;
    status->retry_after = (uint16_t)(response->retry_after > UINT16_MAX ? UINT16_MAX : response->retry_after);
  }
  notify(tracker);
}

void tb_send_init(TbSendTracker *tracker, TbRequestLayer *requests, TbSendPorts ports) {
  memset(tracker, 0, sizeof(*tracker));
  tracker->requests = requests;
  tracker->ports = ports;
}

bool tb_send_busy(const TbSendTracker *tracker) {
  return tracker->status.draft_id != 0 && (tracker->status.phase == TB_SENDING_SUBMITTING || tracker->status.phase == TB_SENDING_PENDING);
}

bool tb_send_start(TbSendTracker *tracker, const TbSendTarget *target, uint32_t draft_id, bool again, const char *title,
                   const char *preview) {
  if (draft_id == 0 || !target || !target->account[0] || !target->chat[0]) { return false; }
  const bool same_draft = tracker->status.draft_id == draft_id;
  if (tb_send_busy(tracker)) { return same_draft && !again; }
  if (same_draft && tracker->status.phase == TB_SENDING_SENT) { return false; }
  if (same_draft && again) {
    if (!(tracker->status.flags & TB_SEND_FLAG_RETRYABLE) && tracker->status.phase != TB_SENDING_UNKNOWN) { return false; }
    ++tracker->attempt;
  } else if (!same_draft) {
    tracker->attempt = 0;
  }
  TbSendStatus *status = &tracker->status;
  memset(status, 0, sizeof(*status));
  status->draft_id = draft_id;
  status->phase = TB_SENDING_SUBMITTING;
  status->target = *target;
  snprintf(status->title, sizeof(status->title), "%s", title ? title : "");
  snprintf(status->preview, sizeof(status->preview), "%s", preview ? preview : "");
  if (!submit(tracker, TB_REQUEST_SEND)) {
    status->phase = TB_SENDING_FAILED;
    status->code = tb_error_submit_failed(tracker->requests);
    status->flags = TB_SEND_FLAG_RETRYABLE;
  }
  notify(tracker);
  return true;
}

bool tb_send_check(TbSendTracker *tracker) {
  if (tracker->status.draft_id == 0 || tracker->pending || settled(tracker->status.phase)) { return false; }
  return submit(tracker, TB_REQUEST_SEND_CHECK);
}

bool tb_send_record(TbSendTracker *tracker, const TbSendStateRecord *record, bool restored) {
  TbSendStatus *status = &tracker->status;
  if (record->draft_id == 0) { return false; }
  if (record->draft_id != status->draft_id) {
    if (!restored || tb_send_busy(tracker)) { return false; }
    memset(status, 0, sizeof(*status));
    status->draft_id = record->draft_id;
    status->phase = TB_SENDING_IDLE;
    status->restored = true;
    tracker->attempt = 0;
    tb_copy_span(status->target.account, sizeof(status->target.account), record->account);
    tb_copy_span(status->target.chat, sizeof(status->target.chat), record->chat);
  } else if (!same_target(&status->target, record)) {
    return false;
  }
  apply(tracker, record);
  return true;
}

bool tb_send_payload(TbSendTracker *tracker, const uint8_t *payload, uint16_t length) {
  TbCursor cursor;
  tb_cursor_init(&cursor, payload, length);
  bool any = false;
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    TbSendStateRecord record;
    if (!tb_codec_next(&cursor, &type, &body)) { return any; }
    if ((type == TB_RECORD_SEND_STATE || type == TB_RECORD_PENDING_SEND) && tb_codec_send_state(&body, &record)) {
      any = tb_send_record(tracker, &record, type == TB_RECORD_PENDING_SEND) || any;
    }
  }
  return any;
}

void tb_send_forget(TbSendTracker *tracker, uint32_t draft_id) {
  if (tracker->status.draft_id != draft_id || tb_send_busy(tracker)) { return; }
  memset(&tracker->status, 0, sizeof(tracker->status));
}

void tb_send_close(TbSendTracker *tracker) {
  if (tracker->pending) {
    const uint32_t pending = tracker->pending;
    tracker->pending = 0;
    tb_requests_cancel(tracker->requests, pending);
  }
}
