#include "history.h"
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "errors.h"
#include "generated/protocol.h"
#include "scratch.h"

#define TB_SENDER_LIMIT 32
#define TB_EXTRA_LIMIT 48
#define TB_FORWARD_LIMIT 48
#define TB_REPLY_LIMIT 96
#define TB_BUSY_POSTPONE 1000

static void changed(TbHistory *history) {
  ++history->revision;
  history->ports.changed(history->ports.context);
}

static void free_message(TbHistory *history, TbMessage *item) {
  tb_budget_free(&history->budget, item->sender);
  tb_budget_free(&history->budget, item->forward);
  tb_budget_free(&history->budget, item->extra);
  tb_budget_free(&history->budget, item->text);
  tb_budget_free(&history->budget, item->reply);
  tb_budget_free(&history->budget, item->reply_id);
  tb_budget_free(&history->budget, item->reply_sender);
  memset(item, 0, sizeof(*item));
}

static void clear_items(TbHistory *history) {
  for (uint16_t index = 0; index < history->count; ++index) { free_message(history, &history->items[index]); }
  history->count = 0;
}

static void clear_staging(TbHistory *history) {
  for (uint16_t index = 0; index < history->staging_count; ++index) { free_message(history, &history->staging[index]); }
  history->staging_count = 0;
  history->staging_invalid = false;
}

static uint32_t now(TbHistory *history) { return history->ports.now(history->ports.context); }

static bool due(uint32_t at, uint32_t current) { return at != 0 && (int32_t)(current - at) >= 0; }

static void arm(TbHistory *history) {
  history->ports.cancel(history->ports.context);
  const uint32_t current = now(history);
  const uint32_t candidates[3] = {history->followup_due, history->recheck_due, history->periodic_due};
  bool any = false;
  uint32_t earliest = 0;
  for (int index = 0; index < 3; ++index) {
    if (candidates[index] == 0) { continue; }
    const uint32_t remaining = due(candidates[index], current) ? 0 : candidates[index] - current;
    if (!any || remaining < earliest) { earliest = remaining; any = true; }
  }
  if (any) { history->ports.schedule(history->ports.context, earliest == 0 ? 1 : earliest); }
}

static uint32_t at(TbHistory *history, uint32_t delay) {
  const uint32_t value = now(history) + delay;
  return value == 0 ? 1 : value;
}

static void schedule_periodic(TbHistory *history) {
  history->periodic_due = history->active && history->loaded && !history->truncated && history->config.refresh_interval
                            ? at(history, history->config.refresh_interval)
                            : 0;
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response);

static void submit(TbHistory *history, TbHistoryOp op, uint8_t page_op, bool silent) {
  if (history->pending) {
    const uint32_t pending = history->pending;
    history->pending = 0;
    tb_requests_cancel(history->requests, pending);
  }
  clear_staging(history);
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_HISTORY;
  args.page_op = page_op;
  args.page_limit = history->config.page_limit;
  args.text_limit = history->config.text_limit;
  strncpy(args.account, history->account, sizeof(args.account) - 1);
  strncpy(args.chat, history->chat, sizeof(args.chat) - 1);
  history->op = op;
  history->op_page = page_op;
  history->refreshing_silently = silent;
  if (op == TB_HISTORY_OLDER) { history->top = TB_TOP_LOADING; }
  if (op == TB_HISTORY_FIRST) { history->error = TB_ERROR_NONE; }
  history->pending = tb_requests_submit(history->requests, &args, history->config.timeout, 2, false, completed, history);
  if (!history->pending) {
    const int32_t error = tb_error_submit_failed(history->requests);
    history->op = TB_HISTORY_NONE;
    if (!history->loaded) { history->error = error; }
    else if (op == TB_HISTORY_OLDER) { history->top = TB_TOP_FAILED; history->top_error = error; }
    else { history->refresh_error = error; }
  }
  changed(history);
}

static bool stage(TbHistory *history, const TbMessageRecord *record) {
  if (history->staging_count >= history->config.page_limit) { return true; }
  TbMessage *item = &history->staging[history->staging_count];
  memset(item, 0, sizeof(*item));
  if (!tb_copy_id(item->id, sizeof(item->id), record->id, false) || !tb_parse_id(item->id, &item->key)) { return false; }
  item->date = record->date;
  item->flags = record->flags;
  item->kind = record->kind;
  item->action = record->action;
  item->duration = record->duration;
  item->sender = tb_budget_copy(&history->budget, record->sender, TB_SENDER_LIMIT, true);
  item->forward = tb_budget_copy(&history->budget, record->forward, TB_FORWARD_LIMIT, true);
  item->extra = tb_budget_copy(&history->budget, record->extra, TB_EXTRA_LIMIT, true);
  item->text = tb_budget_copy(&history->budget, record->text, history->config.text_limit, true);
  if (record->reply_id.length) {
    uint8_t *quote = (uint8_t *)tb_scratch(TB_SCRATCH_SHORT);
    size_t used = 0;
    const size_t sender = tb_utf8_fit(record->reply_sender.data, record->reply_sender.length, 33);
    memcpy(quote, record->reply_sender.data, sender);
    used = sender;
    if (sender && record->reply_text.length && used + 2 < TB_REPLY_LIMIT) {
      quote[used++] = ':';
      quote[used++] = ' ';
    }
    const size_t text = tb_utf8_fit(record->reply_text.data, record->reply_text.length, TB_REPLY_LIMIT - used);
    memcpy(quote + used, record->reply_text.data, text);
    used += text;
    item->reply = tb_budget_copy(&history->budget, (TbSpan){quote, (uint16_t)used}, TB_REPLY_LIMIT, true);
    item->reply_id = tb_budget_copy(&history->budget, record->reply_id, TB_TELEGRAM_ID_SIZE - 1, true);
    item->reply_sender = tb_budget_copy(&history->budget, record->reply_sender, TB_SENDER_LIMIT, true);
  }
  item->height = -1;
  if ((record->text.length && !item->text) || (record->sender.length && !item->sender) || (record->forward.length && !item->forward)) {
    free_message(history, item);
    history->staging_invalid = true;
    return true;
  }
  ++history->staging_count;
  return true;
}

static bool parse(TbHistory *history, const TbResponse *response) {
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    TbMessageRecord record;
    if (!tb_codec_next(&cursor, &type, &body) || type != TB_RECORD_MESSAGE || !tb_codec_message(&body, &record)) { return false; }
    if (!stage(history, &record)) { return false; }
  }
  return true;
}

static void sort_staging(TbHistory *history) {
  for (uint16_t index = 1; index < history->staging_count; ++index) {
    const TbMessage item = history->staging[index];
    uint16_t slot = index;
    while (slot > 0 && history->staging[slot - 1].key > item.key) {
      history->staging[slot] = history->staging[slot - 1];
      --slot;
    }
    history->staging[slot] = item;
  }
}

static bool over_limits(const TbHistory *history, uint16_t count) {
  return count > history->config.capacity || history->budget.used > history->budget.limit;
}

static void evict_newest(TbHistory *history) {
  while (history->count > 0 && over_limits(history, history->count)) {
    free_message(history, &history->items[history->count - 1]);
    --history->count;
    history->truncated = true;
  }
}

static uint16_t kept_before(const TbHistory *history, int64_t key) {
  uint16_t keep = history->count;
  while (keep > 0 && history->items[keep - 1].key >= key) { --keep; }
  return keep;
}

static void remove_from(TbHistory *history, uint16_t keep) {
  for (uint16_t index = keep; index < history->count; ++index) { free_message(history, &history->items[index]); }
  history->count = keep;
}

static void append_staging(TbHistory *history) {
  for (uint16_t index = 0; index < history->staging_count; ++index) {
    if ((history->count > 0 && history->staging[index].key <= history->items[history->count - 1].key) ||
        history->count >= history->config.capacity) {
      free_message(history, &history->staging[index]);
      continue;
    }
    history->items[history->count++] = history->staging[index];
    memset(&history->staging[index], 0, sizeof(TbMessage));
  }
  history->staging_count = 0;
}

static void prepend_staging(TbHistory *history) {
  const int64_t oldest = history->count ? history->items[0].key : INT64_MAX;
  uint16_t accepted = 0;
  for (uint16_t index = 0; index < history->staging_count; ++index) {
    if (history->staging[index].key < oldest && accepted < history->config.capacity) {
      history->staging[accepted++] = history->staging[index];
    } else {
      free_message(history, &history->staging[index]);
    }
  }
  history->staging_count = 0;
  if (accepted == 0) { return; }
  const uint16_t room = (uint16_t)(history->config.capacity - accepted);
  while (history->count > room) {
    free_message(history, &history->items[history->count - 1]);
    --history->count;
    history->truncated = true;
  }
  memmove(history->items + accepted, history->items, (size_t)history->count * sizeof(TbMessage));
  memcpy(history->items, history->staging, (size_t)accepted * sizeof(TbMessage));
  memset(history->staging, 0, (size_t)accepted * sizeof(TbMessage));
  history->count = (uint16_t)(history->count + accepted);
}

static void set_top_from(TbHistory *history, uint32_t flags, bool older) {
  history->top_error = TB_ERROR_NONE;
  if (flags & TB_FLAG_HAS_MORE) {
    history->top = TB_TOP_MORE;
    history->top_op = TB_PAGE_OP_NEXT;
    history->rechecks = 0;
    history->recheck_due = 0;
    return;
  }
  const bool waiting = (flags & TB_FLAG_REFRESH_PENDING) != 0;
  if (flags & TB_FLAG_PARTIAL) {
    history->top_op = older ? TB_PAGE_OP_RETRY : TB_PAGE_OP_REFRESH;
    history->top = waiting ? TB_TOP_WAITING : TB_TOP_FAILED;
  } else if (flags & TB_FLAG_LOCAL_EXHAUSTED) {
    history->top_op = older ? TB_PAGE_OP_RECHECK : TB_PAGE_OP_REFRESH;
    history->top = waiting ? TB_TOP_WAITING : TB_TOP_START;
  } else {
    history->top_op = older ? TB_PAGE_OP_RECHECK : TB_PAGE_OP_REFRESH;
    history->top = TB_TOP_UNKNOWN;
  }
  if (history->top == TB_TOP_WAITING && older) {
    if (history->rechecks < 2) {
      history->recheck_due = at(history, history->config.followup_delays[history->rechecks]);
      ++history->rechecks;
    } else {
      history->top = (flags & TB_FLAG_PARTIAL) ? TB_TOP_FAILED : TB_TOP_START;
    }
  }
}

static void merge_newest(TbHistory *history, uint32_t flags, bool first) {
  if (first || history->count == 0) {
    clear_items(history);
    append_staging(history);
    history->truncated = false;
    set_top_from(history, flags, false);
    return;
  }
  if (history->staging_count == 0) { return; }
  const int64_t newest = history->items[history->count - 1].key;
  const int64_t smallest = history->staging[0].key;
  uint16_t keep = history->count;
  bool contiguous = true;
  if (smallest <= newest) {
    keep = kept_before(history, smallest);
  } else if (history->staging_count >= history->config.page_limit) {
    contiguous = false;
  }
  if (!contiguous || over_limits(history, (uint16_t)(keep + history->staging_count))) {
    clear_staging(history);
    history->truncated = true;
    return;
  }
  remove_from(history, keep);
  append_staging(history);
}

static void finish_newest(TbHistory *history, uint32_t flags, bool first) {
  merge_newest(history, flags, first);
  history->loaded = true;
  history->refreshed_at = now(history);
  history->refresh_error = TB_ERROR_NONE;
  if ((flags & TB_FLAG_REFRESH_PENDING) && history->followup_chain && history->followups < 2) {
    history->followup_due = at(history, history->config.followup_delays[history->followups]);
    ++history->followups;
  } else {
    history->followup_due = 0;
    history->followup_chain = false;
  }
  schedule_periodic(history);
}

static void finish(TbHistory *history, const TbResponse *response) {
  const TbHistoryOp op = history->op;
  history->pending = 0;
  history->op = TB_HISTORY_NONE;
  history->error = TB_ERROR_NONE;
  history->resynced = false;
  history->connection_not_ready = (response->flags & TB_FLAG_CONNECTION_NOT_READY) != 0;
  sort_staging(history);
  if (op == TB_HISTORY_OLDER) {
    prepend_staging(history);
    set_top_from(history, response->flags, true);
    evict_newest(history);
  } else {
    finish_newest(history, response->flags, op == TB_HISTORY_FIRST);
    evict_newest(history);
  }
  if (history->truncated) { history->periodic_due = 0; }
  arm(history);
  changed(history);
}

static void fail(TbHistory *history, int32_t error, uint32_t retry_after) {
  const TbHistoryOp op = history->op;
  history->pending = 0;
  history->op = TB_HISTORY_NONE;
  clear_staging(history);
  history->retry_after = retry_after;
  if (error == TB_RESULT_CURSOR_LOST && !history->resynced) {
    history->resynced = true;
    history->followups = 0;
    history->followup_chain = true;
    history->rechecks = 0;
    submit(history, TB_HISTORY_FIRST, TB_PAGE_OP_FIRST, history->loaded);
    return;
  }
  if (!history->loaded || tb_error_is_account_level(error) || tb_error_is_connection_level(error) ||
      error == TB_RESULT_CHAT_NOT_FOUND) {
    history->error = error;
  } else if (op == TB_HISTORY_OLDER) {
    history->top = TB_TOP_FAILED;
    history->top_error = error;
  } else {
    history->refresh_error = error;
  }
  if (history->loaded && history->top == TB_TOP_LOADING) {
    history->top = TB_TOP_FAILED;
    history->top_error = error;
  }
  if (history->loaded && (error == TB_RESULT_CURSOR_LOST || op == TB_HISTORY_FIRST) && history->top == TB_TOP_FAILED) {
    history->top_op = TB_PAGE_OP_FIRST;
  }
  schedule_periodic(history);
  arm(history);
  changed(history);
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbHistory *history = owner;
  if (sequence != history->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  if (response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK) {
    if (!response->final) {
      history->pending = 0;
      tb_requests_cancel(history->requests, sequence);
    }
    fail(history, tb_error_from_response(response), response->retry_after);
    return;
  }
  if (!parse(history, response)) {
    if (!response->final) {
      history->pending = 0;
      tb_requests_cancel(history->requests, sequence);
    }
    fail(history, TB_RESULT_PROTOCOL_ERROR, 0);
    return;
  }
  if (response->final) {
    if (history->staging_invalid) {
      clear_staging(history);
      fail(history, TB_ERROR_OUT_OF_MEMORY, 0);
      return;
    }
    finish(history, response);
  }
}

bool tb_history_init(TbHistory *history, TbRequestLayer *requests, TbViewPorts ports, TbHistoryConfig config) {
  memset(history, 0, sizeof(*history));
  history->requests = requests;
  history->ports = ports;
  history->config = config;
  history->budget.limit = config.text_budget;
  history->items = calloc(config.capacity, sizeof(TbMessage));
  history->staging = calloc(config.page_limit, sizeof(TbMessage));
  return history->items && history->staging;
}

void tb_history_deinit(TbHistory *history) {
  tb_history_close(history);
  clear_items(history);
  free(history->items);
  free(history->staging);
  history->items = NULL;
  history->staging = NULL;
}

bool tb_history_is(const TbHistory *history, const char *account, const char *chat) {
  return history->loaded && strcmp(history->account, account) == 0 && strcmp(history->chat, chat) == 0;
}

void tb_history_open(TbHistory *history, const char *account, const char *chat, uint8_t chat_type, bool saved) {
  const bool same = tb_history_is(history, account, chat) && !history->truncated;
  tb_history_close(history);
  history->active = true;
  history->chat_type = chat_type;
  history->saved = saved;
  history->followups = 0;
  history->followup_chain = true;
  history->rechecks = 0;
  history->resynced = false;
  history->retry_after = 0;
  if (same) {
    submit(history, TB_HISTORY_FIRST, TB_PAGE_OP_FIRST, true);
    return;
  }
  clear_items(history);
  strncpy(history->account, account, sizeof(history->account) - 1);
  history->account[sizeof(history->account) - 1] = '\0';
  strncpy(history->chat, chat, sizeof(history->chat) - 1);
  history->chat[sizeof(history->chat) - 1] = '\0';
  history->loaded = false;
  history->truncated = false;
  history->top = TB_TOP_LOADING;
  history->error = TB_ERROR_NONE;
  history->top_error = TB_ERROR_NONE;
  history->refresh_error = TB_ERROR_NONE;
  submit(history, TB_HISTORY_FIRST, TB_PAGE_OP_FIRST, false);
}

void tb_history_load_older(TbHistory *history) {
  if (!history->loaded || history->op != TB_HISTORY_NONE) { return; }
  switch (history->top) {
    case TB_TOP_MORE:
      submit(history, TB_HISTORY_OLDER, TB_PAGE_OP_NEXT, false);
      break;
    case TB_TOP_FAILED:
    case TB_TOP_WAITING:
    case TB_TOP_START:
    case TB_TOP_UNKNOWN:
      history->recheck_due = 0;
      if (history->top_op == TB_PAGE_OP_FIRST) {
        history->resynced = false;
        history->followups = 0;
        history->followup_chain = true;
        submit(history, TB_HISTORY_FIRST, TB_PAGE_OP_FIRST, true);
      } else if (history->top_op == TB_PAGE_OP_REFRESH) {
        history->followups = 0;
        history->followup_chain = true;
        submit(history, TB_HISTORY_REFRESH, TB_PAGE_OP_REFRESH, false);
      } else {
        if (history->top != TB_TOP_WAITING) { history->rechecks = 0; }
        submit(history, TB_HISTORY_OLDER, history->top_op, false);
      }
      break;
    default:
      break;
  }
}

void tb_history_refresh(TbHistory *history) {
  if (history->op != TB_HISTORY_NONE || !history->account[0]) { return; }
  history->followups = 0;
  history->followup_chain = true;
  if (!history->loaded || history->truncated) {
    history->resynced = false;
    submit(history, TB_HISTORY_FIRST, TB_PAGE_OP_FIRST, history->loaded);
  } else {
    submit(history, TB_HISTORY_REFRESH, TB_PAGE_OP_REFRESH, false);
  }
}

void tb_history_retry(TbHistory *history) {
  if (history->op != TB_HISTORY_NONE || !history->account[0]) { return; }
  if (!history->loaded) {
    history->error = TB_ERROR_NONE;
    history->resynced = false;
    history->followups = 0;
    history->followup_chain = true;
    submit(history, TB_HISTORY_FIRST, TB_PAGE_OP_FIRST, false);
  } else if (history->top == TB_TOP_FAILED) {
    tb_history_load_older(history);
  } else {
    tb_history_refresh(history);
  }
}

void tb_history_set_active(TbHistory *history, bool active) {
  if (!history->account[0]) { return; }
  history->active = active;
  if (!active) {
    history->periodic_due = 0;
    history->followup_due = 0;
    arm(history);
    return;
  }
  if (history->loaded && history->op == TB_HISTORY_NONE && !history->truncated &&
      (uint32_t)(now(history) - history->refreshed_at) >= history->config.refresh_interval) {
    tb_history_refresh(history);
    return;
  }
  schedule_periodic(history);
  arm(history);
}

void tb_history_timer(TbHistory *history) {
  const uint32_t current = now(history);
  const bool followup = due(history->followup_due, current);
  const bool periodic = due(history->periodic_due, current);
  const bool recheck = due(history->recheck_due, current);
  if (!followup && !periodic && !recheck) {
    arm(history);
    return;
  }
  if (history->op != TB_HISTORY_NONE) {
    if (followup) { history->followup_due = at(history, TB_BUSY_POSTPONE); }
    if (periodic) { history->periodic_due = at(history, TB_BUSY_POSTPONE); }
    if (recheck) { history->recheck_due = at(history, TB_BUSY_POSTPONE); }
    arm(history);
    return;
  }
  if (recheck) {
    history->recheck_due = 0;
    submit(history, TB_HISTORY_OLDER, history->top_op, false);
    return;
  }
  history->followup_due = 0;
  history->periodic_due = 0;
  if (periodic && !followup) {
    history->followups = 0;
    history->followup_chain = false;
  }
  if (!history->active) {
    arm(history);
    return;
  }
  submit(history, TB_HISTORY_REFRESH, TB_PAGE_OP_REFRESH, true);
}

void tb_history_close(TbHistory *history) {
  if (history->pending) {
    const uint32_t pending = history->pending;
    history->pending = 0;
    tb_requests_cancel(history->requests, pending);
  }
  clear_staging(history);
  history->op = TB_HISTORY_NONE;
  history->active = false;
  history->followup_due = 0;
  history->recheck_due = 0;
  history->periodic_due = 0;
  history->ports.cancel(history->ports.context);
}

int tb_history_find(const TbHistory *history, const char *id) {
  for (int index = 0; index < history->count; ++index) {
    if (strcmp(history->items[index].id, id) == 0) { return index; }
  }
  return -1;
}
