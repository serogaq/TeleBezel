#include "chats.h"
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "errors.h"
#include "generated/protocol.h"

#define TB_TITLE_LIMIT 64
#define TB_SENDER_LIMIT 32
#define TB_EXTRA_LIMIT 48

static void changed(TbChats *chats) { chats->ports.changed(chats->ports.context); }

static void free_item(TbChats *chats, TbChat *item) {
  tb_budget_free(&chats->budget, item->title);
  tb_budget_free(&chats->budget, item->sender);
  tb_budget_free(&chats->budget, item->extra);
  tb_budget_free(&chats->budget, item->preview);
  memset(item, 0, sizeof(*item));
}

static void clear(TbChats *chats) {
  for (uint16_t index = 0; index < chats->count; ++index) { free_item(chats, &chats->items[index]); }
  chats->count = 0;
}

static void schedule(TbChats *chats) {
  chats->ports.cancel(chats->ports.context);
  chats->refresh_due = 0;
  if (!chats->active || !chats->loaded || chats->config.refresh_interval == 0) { return; }
  chats->refresh_due = chats->ports.now(chats->ports.context) + chats->config.refresh_interval;
  chats->ports.schedule(chats->ports.context, chats->config.refresh_interval);
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response);

static void request(TbChats *chats, TbChatsLoad load, uint8_t op) {
  if (chats->pending) { tb_requests_cancel(chats->requests, chats->pending); }
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_CHATS;
  args.page_op = op;
  args.list = chats->list;
  args.page_limit = chats->config.page_limit;
  args.text_limit = chats->config.text_limit;
  strncpy(args.account, chats->account, sizeof(args.account) - 1);
  chats->load = load;
  chats->error = TB_ERROR_NONE;
  chats->pending = tb_requests_submit(chats->requests, &args, chats->config.timeout, 2, false, completed, chats);
  if (!chats->pending) {
    chats->load = TB_CHATS_IDLE;
    chats->error = tb_error_submit_failed(chats->requests);
  }
  changed(chats);
}

static bool append(TbChats *chats, const TbChatRecord *record) {
  if (chats->count >= chats->config.capacity) {
    chats->tail = TB_TAIL_FULL;
    return true;
  }
  TbChat *item = &chats->items[chats->count];
  memset(item, 0, sizeof(*item));
  if (!tb_copy_id(item->id, sizeof(item->id), record->id, true)) { return false; }
  for (uint16_t index = 0; index < chats->count; ++index) {
    if (strcmp(chats->items[index].id, item->id) == 0) { return true; }
  }
  item->title = tb_budget_copy(&chats->budget, record->title, TB_TITLE_LIMIT, false);
  item->sender = tb_budget_copy(&chats->budget, record->preview_sender, TB_SENDER_LIMIT, false);
  item->extra = tb_budget_copy(&chats->budget, record->preview_extra, TB_EXTRA_LIMIT, false);
  item->preview = tb_budget_copy(&chats->budget, record->preview_text, chats->config.text_limit, false);
  if ((record->title.length && !item->title) || (record->preview_text.length && !item->preview)) {
    free_item(chats, item);
    chats->tail = TB_TAIL_FULL;
    return true;
  }
  item->type = record->type;
  item->flags = record->flags;
  item->preview_kind = record->preview_kind;
  item->preview_action = record->preview_action;
  item->preview_duration = record->preview_duration;
  item->unread = record->unread;
  item->last_date = record->last_date;
  ++chats->count;
  return true;
}

static bool parse(TbChats *chats, const TbResponse *response) {
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    TbChatRecord record;
    if (!tb_codec_next(&cursor, &type, &body) || type != TB_RECORD_CHAT || !tb_codec_chat(&body, &record)) { return false; }
    if (chats->tail == TB_TAIL_FULL) { continue; }
    if (!append(chats, &record)) { return false; }
  }
  return true;
}

static TbChatsTail tail_from(uint32_t flags) {
  if (flags & TB_FLAG_HAS_MORE) { return TB_TAIL_MORE; }
  if (flags & TB_FLAG_PARTIAL) { return TB_TAIL_FAILED; }
  if (flags & TB_FLAG_HAS_MORE_UNKNOWN) { return TB_TAIL_UNKNOWN; }
  return TB_TAIL_END;
}

static void fail(TbChats *chats, int32_t error, uint32_t retry_after) {
  const TbChatsLoad load = chats->load;
  chats->pending = 0;
  chats->load = TB_CHATS_IDLE;
  if (error == TB_RESULT_CURSOR_LOST && !chats->resynced) {
    chats->resynced = true;
    request(chats, TB_CHATS_REFRESH, TB_PAGE_OP_FIRST);
    return;
  }
  chats->error = error;
  chats->retry_after = retry_after;
  if (load == TB_CHATS_MORE) { chats->tail = TB_TAIL_FAILED; }
  schedule(chats);
  changed(chats);
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbChats *chats = owner;
  if (sequence != chats->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  if (response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK) {
    if (!response->final) {
      chats->pending = 0;
      tb_requests_cancel(chats->requests, sequence);
    }
    fail(chats, tb_error_from_response(response), response->retry_after);
    return;
  }
  if (response->index == 0) {
    if (chats->load != TB_CHATS_MORE) {
      clear(chats);
      chats->tail = TB_TAIL_UNKNOWN;
    } else if (chats->tail == TB_TAIL_FAILED) {
      chats->tail = TB_TAIL_UNKNOWN;
    }
    if (chats->tail == TB_TAIL_FULL && chats->count < chats->config.capacity) { chats->tail = TB_TAIL_UNKNOWN; }
    chats->connection_not_ready = (response->flags & TB_FLAG_CONNECTION_NOT_READY) != 0;
  }
  if (!parse(chats, response)) {
    if (!response->final) {
      chats->pending = 0;
      tb_requests_cancel(chats->requests, sequence);
    }
    fail(chats, TB_RESULT_PROTOCOL_ERROR, 0);
    return;
  }
  if (!response->final) {
    changed(chats);
    return;
  }
  chats->pending = 0;
  chats->load = TB_CHATS_IDLE;
  chats->error = TB_ERROR_NONE;
  chats->resynced = false;
  if (chats->tail != TB_TAIL_FULL) { chats->tail = tail_from(response->flags); }
  chats->loaded = true;
  chats->loaded_at = chats->ports.now(chats->ports.context);
  schedule(chats);
  changed(chats);
}

bool tb_chats_init(TbChats *chats, TbRequestLayer *requests, TbViewPorts ports, TbChatsConfig config) {
  memset(chats, 0, sizeof(*chats));
  chats->requests = requests;
  chats->ports = ports;
  chats->config = config;
  chats->budget.limit = config.text_budget;
  chats->items = calloc(config.capacity, sizeof(TbChat));
  return chats->items != NULL;
}

void tb_chats_deinit(TbChats *chats) {
  tb_chats_close(chats);
  free(chats->items);
  chats->items = NULL;
}

void tb_chats_open(TbChats *chats, const char *account, uint8_t list) {
  tb_chats_close(chats);
  strncpy(chats->account, account, sizeof(chats->account) - 1);
  chats->account[sizeof(chats->account) - 1] = '\0';
  chats->list = list == TB_LIST_ARCHIVE ? TB_LIST_ARCHIVE : TB_LIST_MAIN;
  chats->active = true;
  request(chats, TB_CHATS_FIRST, TB_PAGE_OP_FIRST);
}

void tb_chats_set_list(TbChats *chats, uint8_t list) {
  char account[TB_ACCOUNT_ID_SIZE];
  strncpy(account, chats->account, sizeof(account));
  tb_chats_open(chats, account, list);
}

void tb_chats_load_more(TbChats *chats) {
  if (chats->load != TB_CHATS_IDLE || !chats->loaded) { return; }
  if (chats->tail == TB_TAIL_MORE) { request(chats, TB_CHATS_MORE, TB_PAGE_OP_NEXT); }
  else if (chats->tail == TB_TAIL_FAILED) { request(chats, TB_CHATS_MORE, TB_PAGE_OP_RETRY); }
  else if (chats->tail == TB_TAIL_UNKNOWN) { request(chats, TB_CHATS_REFRESH, TB_PAGE_OP_FIRST); }
}

void tb_chats_refresh(TbChats *chats) {
  if (chats->load != TB_CHATS_IDLE) { return; }
  request(chats, chats->loaded ? TB_CHATS_REFRESH : TB_CHATS_FIRST, TB_PAGE_OP_FIRST);
}

void tb_chats_retry(TbChats *chats) {
  if (chats->load != TB_CHATS_IDLE) { return; }
  if (chats->tail == TB_TAIL_FAILED && chats->loaded) { tb_chats_load_more(chats); }
  else { tb_chats_refresh(chats); }
}

void tb_chats_set_active(TbChats *chats, bool active) {
  if (!chats->account[0]) { return; }
  chats->active = active;
  if (!active) {
    chats->ports.cancel(chats->ports.context);
    chats->refresh_due = 0;
    return;
  }
  const uint32_t now = chats->ports.now(chats->ports.context);
  if (chats->loaded && chats->load == TB_CHATS_IDLE && (uint32_t)(now - chats->loaded_at) >= chats->config.stale_after) {
    tb_chats_refresh(chats);
  } else {
    schedule(chats);
  }
}

void tb_chats_timer(TbChats *chats) {
  chats->refresh_due = 0;
  if (!chats->active) { return; }
  if (chats->load != TB_CHATS_IDLE) {
    schedule(chats);
    return;
  }
  tb_chats_refresh(chats);
}

void tb_chats_close(TbChats *chats) {
  if (chats->pending) {
    const uint32_t pending = chats->pending;
    chats->pending = 0;
    tb_requests_cancel(chats->requests, pending);
  }
  chats->ports.cancel(chats->ports.context);
  clear(chats);
  chats->account[0] = '\0';
  chats->load = TB_CHATS_IDLE;
  chats->tail = TB_TAIL_UNKNOWN;
  chats->error = TB_ERROR_NONE;
  chats->retry_after = 0;
  chats->loaded = false;
  chats->active = false;
  chats->resynced = false;
  chats->connection_not_ready = false;
  chats->refresh_due = 0;
}

int tb_chats_find(const TbChats *chats, const char *id) {
  for (int index = 0; index < chats->count; ++index) {
    if (strcmp(chats->items[index].id, id) == 0) { return index; }
  }
  return -1;
}
