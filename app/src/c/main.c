#include <pebble.h>
#include "accounts_window.h"
#include "chats.h"
#include "action_menu.h"
#include "chats_window.h"
#include "compose.h"
#include "compose_window.h"
#include "connection.h"
#include "diag.h"
#include "dictation.h"
#include "errors.h"
#include "format.h"
#include "generated/localization.h"
#include "generated/protocol.h"
#include "history.h"
#include "history_window.h"
#include "message_text.h"
#include "notice_window.h"
#include "notify.h"
#include "reader_window.h"
#include "request_layer.h"
#include "send_tracker.h"
#include "scratch.h"
#include "session.h"
#include "theme.h"

#define TB_PERSIST_LAST_ACCOUNT 1
#define TB_DATA_TIMEOUT 25000
#define TB_OUTBOX_SIZE 1280
#define TB_EVENTS_TIMEOUT 10000

#define TB_INBOX_SIZE 4096
#define TB_CHATS_CAPACITY 80
#define TB_CHATS_PAGE 20
#define TB_CHATS_TEXT 100
#define TB_CHATS_BUDGET 8192
#define TB_HISTORY_CAPACITY 60
#define TB_HISTORY_PAGE 20
#define TB_HISTORY_TEXT 360
#define TB_HISTORY_BUDGET 12288
#define TB_FULL_TEXT 8192
#define TB_QUOTE_TEXT 4096
#define TB_TEMPLATES_CAPACITY 50
#define TB_TEMPLATE_PREVIEW 64
#define TB_DRAFT_TEXT 1100

#define TB_ACTION_NONE 0
#define TB_ACTION_REPLY 1
#define TB_ACTION_OPEN 2
#define TB_ACTION_RETRY 3
#define TB_ACTION_WRITE 4

typedef enum { TB_TIMER_REQUEST, TB_TIMER_CHATS, TB_TIMER_CONNECTION, TB_TIMER_HISTORY, TB_TIMER_NOTIFY, TB_TIMER_COUNT } TbTimerSlot;

typedef struct {
  TbRequestLayer requests;
  TbSession session;
  TbChats chats;
  TbConnection connection;
  TbHistory history;
  TbMessageText text;
  TbMessageText quote;
  char quote_id[TB_TELEGRAM_ID_SIZE];
  TbNotice connect;
  TbNotice info;
  TbAccountsWindow accounts;
  TbChatsWindow chats_view;
  TbHistoryWindow history_view;
  TbReaderWindow reader;
  TbNotify notify;
  TbSendTracker tracker;
  TbCompose compose;
  TbComposeView compose_view;
  TbDictation dictation;
  char info_body[256];
  char notify_body[TB_NOTIFY_BODY_SIZE];
  AppTimer *timers[TB_TIMER_COUNT];
} TbApp;

static TbApp *s_app;
static const TbStrings *s_strings;
static uint32_t s_inbox_size;
static uint32_t s_summary_revision;
static AppTimer *s_reload_timer;
static uint8_t s_reload_pending;
static int s_menu_message = -1;
static bool s_compose_from_reader;
static int s_account = -1;
static uint32_t s_default_request;
static int s_default_index = -1;

static uint32_t now_ms(void *context) {
  (void)context;
  time_t seconds = 0;
  uint16_t milliseconds = 0;
  time_ms(&seconds, &milliseconds);
  return (uint32_t)seconds * 1000u + milliseconds;
}

static void timer_fired(void *context) {
  AppTimer **slot = context;
  *slot = NULL;
  switch ((TbTimerSlot)(slot - s_app->timers)) {
    case TB_TIMER_REQUEST: tb_requests_tick(&s_app->requests); break;
    case TB_TIMER_CHATS: tb_chats_timer(&s_app->chats); break;
    case TB_TIMER_CONNECTION: tb_connection_timer(&s_app->connection); break;
    case TB_TIMER_HISTORY: tb_history_timer(&s_app->history); break;
    case TB_TIMER_NOTIFY: tb_notify_timer(&s_app->notify); break;
    case TB_TIMER_COUNT: break;
  }
}

static void timer_cancel(void *context) {
  AppTimer **slot = context;
  if (*slot) {
    app_timer_cancel(*slot);
    *slot = NULL;
  }
}

static bool timer_schedule(void *context, uint32_t milliseconds) {
  AppTimer **slot = context;
  timer_cancel(slot);
  *slot = app_timer_register(milliseconds, timer_fired, slot);
  return *slot != NULL;
}

static bool write_string(DictionaryIterator *out, uint32_t key, const char *value) {
  return !value[0] || dict_write_cstring(out, key, value) == DICT_OK;
}
static bool write_uint(DictionaryIterator *out, uint32_t key, uint32_t value) {
  return dict_write_int(out, key, &value, sizeof(value), false) == DICT_OK;
}

static TbSendResult send_request(void *context, uint32_t sequence, const TbRequestArgs *args) {
  (void)context;
  if (!connection_service_peek_pebble_app_connection()) { return TB_SEND_UNREACHABLE; }
  DictionaryIterator *out = NULL;
  const AppMessageResult begun = app_message_outbox_begin(&out);
  if (begun == APP_MSG_BUSY) { return TB_SEND_BUSY; }
  if (begun == APP_MSG_NOT_CONNECTED) { return TB_SEND_UNREACHABLE; }
  if (begun != APP_MSG_OK || !out) { return TB_SEND_FAILED; }
  bool ok = dict_write_uint8(out, MESSAGE_KEY_REQUEST_KIND, args->kind) == DICT_OK && write_uint(out, MESSAGE_KEY_REQUEST_SEQ, sequence);
  if (args->kind == TB_REQUEST_HELLO) {
    ok = ok && write_uint(out, MESSAGE_KEY_INBOX_SIZE, s_inbox_size);
  } else {
    ok = ok && write_string(out, MESSAGE_KEY_ACCOUNT_ID, args->account) && write_string(out, MESSAGE_KEY_ENTITY_ID, args->chat) &&
         write_string(out, MESSAGE_KEY_MESSAGE_ID, args->message) && dict_write_uint8(out, MESSAGE_KEY_PAGE_OP, args->page_op) == DICT_OK &&
         dict_write_uint8(out, MESSAGE_KEY_LIST, args->list) == DICT_OK && dict_write_uint8(out, MESSAGE_KEY_PAGE_LIMIT, args->page_limit) == DICT_OK &&
         write_uint(out, MESSAGE_KEY_TEXT_LIMIT, args->text_limit);
    if (args->draft_id) { ok = ok && write_uint(out, MESSAGE_KEY_DRAFT_ID, args->draft_id); }
    if (args->kind == TB_REQUEST_SEND) { ok = ok && dict_write_uint8(out, MESSAGE_KEY_ATTEMPT, args->attempt) == DICT_OK; }
    if (args->kind == TB_REQUEST_DRAFT) {
      if (args->payload && args->payload_length) {
        ok = ok && dict_write_data(out, MESSAGE_KEY_PAYLOAD, args->payload, args->payload_length) == DICT_OK;
      } else {
        ok = ok && dict_write_uint8(out, MESSAGE_KEY_TEMPLATE_INDEX, args->template_index) == DICT_OK &&
             write_uint(out, MESSAGE_KEY_TEMPLATES_REV, args->templates_rev);
      }
    }
  }
  if (!ok) { return TB_SEND_FAILED; }
  tb_diag_outbox(dict_write_end(out));
  const AppMessageResult sent = app_message_outbox_send();
  if (sent == APP_MSG_BUSY) { return TB_SEND_BUSY; }
  return sent == APP_MSG_OK ? TB_SEND_OK : sent == APP_MSG_NOT_CONNECTED ? TB_SEND_UNREACHABLE : TB_SEND_FAILED;
}

static bool integer(const Tuple *tuple, int32_t *out) {
  if (!tuple) { return false; }
  if (tuple->type == TUPLE_INT) {
    if (tuple->length == 1) { *out = tuple->value->int8; }
    else if (tuple->length == 2) { *out = tuple->value->int16; }
    else if (tuple->length == 4) { *out = tuple->value->int32; }
    else { return false; }
    return true;
  }
  if (tuple->type == TUPLE_UINT) {
    if (tuple->length == 1) { *out = tuple->value->uint8; }
    else if (tuple->length == 2) { *out = tuple->value->uint16; }
    else if (tuple->length == 4 && tuple->value->uint32 <= INT32_MAX) { *out = (int32_t)tuple->value->uint32; }
    else { return false; }
    return true;
  }
  return false;
}

static void log_tuple(const char *name, const Tuple *tuple) {
  if (!tuple) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage %s missing", name);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "AppMessage %s type %d length %d", name, (int)tuple->type, (int)tuple->length);
  }
}

static bool in_stack(Window *window) { return window && window_stack_contains_window(window); }

static void show_connect(const char *body, const char *hint, TbNoticeAction action) {
  tb_notice_set(&s_app->connect, s_strings->title, body, hint, action, NULL);
  if (!in_stack(s_app->connect.window)) { window_stack_push(s_app->connect.window, false); }
}

static void close_compose(void) {
  tb_compose_view_close_all(&s_app->compose_view);
  tb_dictation_close(&s_app->dictation);
  tb_compose_close(&s_app->compose, true);
}

static void close_views(void) {
  close_compose();
  tb_notify_reset(&s_app->notify);
  tb_message_text_close(&s_app->text);
  tb_message_text_close(&s_app->quote);
  s_app->quote_id[0] = '\0';
  tb_history_close(&s_app->history);
  tb_connection_close(&s_app->connection);
  tb_chats_close(&s_app->chats);
}

static void reset_to_connect(const char *body, const char *hint, TbNoticeAction action) {
  close_views();
  s_account = -1;
  tb_notice_set(&s_app->connect, s_strings->title, body, hint, action, NULL);
  if (!in_stack(s_app->connect.window)) { window_stack_push(s_app->connect.window, false); }
  Window *windows[] = {s_app->reader.window, s_app->history_view.window, s_app->chats_view.window, s_app->accounts.window, s_app->info.window};
  for (size_t index = 0; index < ARRAY_LENGTH(windows); ++index) {
    if (in_stack(windows[index])) { window_stack_remove(windows[index], false); }
  }
}

static void retry_session(void *context) {
  (void)context;
  tb_session_retry(&s_app->session);
}

static void show_info(const char *title, const char *body) {
  snprintf(s_app->info_body, sizeof(s_app->info_body), "%s", body);
  tb_notice_set(&s_app->info, title, s_app->info_body, s_strings->back_hint, NULL, NULL);
  if (!in_stack(s_app->info.window)) { window_stack_push(s_app->info.window, true); }
}

static void remember_account(const char *id) { persist_write_string(TB_PERSIST_LAST_ACCOUNT, id); }

static int remembered_account(void) {
  char id[TB_ACCOUNT_ID_SIZE] = "";
  if (persist_exists(TB_PERSIST_LAST_ACCOUNT)) { persist_read_string(TB_PERSIST_LAST_ACCOUNT, id, sizeof(id)); }
  const int found = id[0] ? tb_session_find(&s_app->session, id) : -1;
  if (found >= 0) { return found; }
  return s_app->session.default_account[0] ? tb_session_find(&s_app->session, s_app->session.default_account) : 0;
}

static const char *account_explanation(uint8_t state) {
  switch (state) {
    case TB_ACCOUNT_STATE_NEEDS_LOGIN: return s_strings->account_needs_login;
    case TB_ACCOUNT_STATE_LOGGING_OUT: return s_strings->state_logging_out;
    case TB_ACCOUNT_STATE_REMOVING: return s_strings->account_gone;
    default: return s_strings->state_connecting;
  }
}

static void open_account(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_app->session.count) { return; }
  const TbAccount *account = &s_app->session.accounts[index];
  if (account->state != TB_ACCOUNT_STATE_READY) {
    show_info(account->name, account_explanation(account->state));
    return;
  }
  if (s_account != index) { tb_history_close(&s_app->history); }
  s_account = index;
  remember_account(account->id);
  tb_chats_window_reset(&s_app->chats_view);
  tb_chats_window_configure(&s_app->chats_view, s_app->session.show_archive, s_app->session.unread_mode);
  tb_connection_open(&s_app->connection, account->id);
  tb_chats_open(&s_app->chats, account->id, s_app->session.chat_list);
  if (!in_stack(s_app->chats_view.window)) { window_stack_push(s_app->chats_view.window, true); }
}

static void default_saved(void *owner, uint32_t sequence, const TbResponse *response) {
  (void)owner;
  if (sequence != s_default_request || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  s_default_request = 0;
  if (response->outcome == TB_OUTCOME_RESPONSE && response->result == TB_RESULT_OK && s_default_index >= 0 &&
      s_default_index < s_app->session.count) {
    for (int index = 0; index < s_app->session.count; ++index) { s_app->session.accounts[index].flags &= (uint8_t)~TB_ACCOUNT_FLAG_DEFAULT; }
    s_app->session.accounts[s_default_index].flags |= TB_ACCOUNT_FLAG_DEFAULT;
    strncpy(s_app->session.default_account, s_app->session.accounts[s_default_index].id, sizeof(s_app->session.default_account) - 1);
    tb_accounts_window_reload(&s_app->accounts, s_default_index);
    vibes_short_pulse();
  } else {
    vibes_double_pulse();
  }
}

static void make_default(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_app->session.count || s_default_request) { return; }
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_SET_DEFAULT;
  strncpy(args.account, s_app->session.accounts[index].id, sizeof(args.account) - 1);
  s_default_index = index;
  s_default_request = tb_requests_submit(&s_app->requests, &args, TB_DATA_TIMEOUT, 2, false, default_saved, NULL);
  if (!s_default_request) { vibes_double_pulse(); }
}

static void route(void) {
  if (tb_session_busy(&s_app->session)) {
    if (!in_stack(s_app->accounts.window) && !in_stack(s_app->chats_view.window)) { show_connect(s_strings->checking, NULL, NULL); }
    return;
  }
  if (s_app->session.error != TB_ERROR_NONE) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "bootstrap failed: error %d %s", (int)s_app->session.error, s_app->session.failure ? s_app->session.failure : "");
    reset_to_connect(tb_error_text(s_strings, s_app->session.error), s_strings->retry_hint, retry_session);
    return;
  }
  if (!s_app->session.loaded) { return; }
  if (s_app->session.count == 0) {
    char body[160];
    snprintf(body, sizeof(body), "%s\n%s/settings", s_strings->no_accounts, s_app->session.host);
    reset_to_connect(body, s_strings->retry_hint, retry_session);
    return;
  }
  if (in_stack(s_app->accounts.window) || in_stack(s_app->chats_view.window)) {
    tb_accounts_window_reload(&s_app->accounts, s_account >= 0 ? s_account : remembered_account());
    return;
  }
  const int initial = tb_session_initial_account(&s_app->session);
  tb_accounts_window_reload(&s_app->accounts, initial >= 0 ? initial : remembered_account());
  if (s_app->session.count > 1 || initial < 0) { window_stack_push(s_app->accounts.window, false); }
  if (initial >= 0) { open_account(NULL, initial); }
  if (in_stack(s_app->connect.window)) { window_stack_remove(s_app->connect.window, false); }
}

static void session_changed(void *context) {
  (void)context;
  route();
}

static bool handle_fatal(int32_t error) {
  if (tb_error_is_connection_level(error)) {
    reset_to_connect(tb_error_text(s_strings, error), s_strings->retry_hint, retry_session);
    return true;
  }
  if (!tb_error_is_account_level(error)) { return false; }
  const char *name = s_account >= 0 && s_account < s_app->session.count ? s_app->session.accounts[s_account].name : s_strings->title;
  if (s_account >= 0 && s_account < s_app->session.count) {
    s_app->session.accounts[s_account].state = error == TB_RESULT_ACCOUNT_GONE ? TB_ACCOUNT_STATE_REMOVING : TB_ACCOUNT_STATE_NEEDS_LOGIN;
  }
  close_views();
  if (in_stack(s_app->reader.window)) { window_stack_remove(s_app->reader.window, false); }
  if (in_stack(s_app->history_view.window)) { window_stack_remove(s_app->history_view.window, false); }
  if (!in_stack(s_app->accounts.window)) { window_stack_push(s_app->accounts.window, false); }
  if (in_stack(s_app->chats_view.window)) { window_stack_remove(s_app->chats_view.window, false); }
  tb_accounts_window_reload(&s_app->accounts, s_account);
  show_info(name, tb_error_text(s_strings, error));
  return true;
}

#define TB_RELOAD_CHATS 1
#define TB_RELOAD_HISTORY 2
#define TB_RELOAD_READER 4
#define TB_RELOAD_COMPOSE 8
#define TB_RELOAD_NOTIFY 16

static void reload_fired(void *context) {
  (void)context;
  s_reload_timer = NULL;
  const uint8_t pending = s_reload_pending;
  s_reload_pending = 0;
  if (pending & TB_RELOAD_CHATS) { tb_chats_window_reload(&s_app->chats_view); }
  if (pending & TB_RELOAD_HISTORY) { tb_history_window_reload(&s_app->history_view); }
  if (pending & TB_RELOAD_READER) { tb_reader_window_reload(&s_app->reader); }
  if (pending & TB_RELOAD_COMPOSE) { tb_compose_view_reload(&s_app->compose_view); }
  if (pending & TB_RELOAD_NOTIFY) {
    tb_notify_view_refresh(&s_app->chats_view.notice);
    tb_notify_view_refresh(&s_app->history_view.notice);
  }
}

static void reload_later(uint8_t views) {
  s_reload_pending |= views;
  if (!s_reload_timer) { s_reload_timer = app_timer_register(0, reload_fired, NULL); }
}


static void chats_notice(void) {
  if (s_app->chats.error == TB_ERROR_NONE || s_app->chats.load != TB_CHATS_IDLE || s_app->chats.tail == TB_TAIL_FAILED || !s_app->chats.loaded) {
    tb_notify_clear(&s_app->notify, TB_NOTIFY_CHATS);
    return;
  }
  if (s_app->chats.error == TB_RESULT_RATE_LIMITED && s_app->chats.retry_after > 0) {
    snprintf(s_app->notify_body, sizeof(s_app->notify_body), s_strings->wait_seconds, (int)s_app->chats.retry_after);
  } else {
    snprintf(s_app->notify_body, sizeof(s_app->notify_body), "%s", tb_error_text(s_strings, s_app->chats.error));
  }
  tb_notify_post(&s_app->notify, TB_NOTIFY_CHATS, TB_NOTIFY_WARNING, TB_NOTIFY_REFRESH, s_strings->not_updated, s_app->notify_body);
}

static void connection_notice(void) {
  if (s_app->connection.error == TB_CONNECTION_ERROR_CANNOT_CONNECT) {
    tb_notify_post(&s_app->notify, TB_NOTIFY_CONNECTION, TB_NOTIFY_ERROR, TB_NOTIFY_REFRESH, s_strings->cannot_connect,
                   s_app->connection.proxy ? s_strings->try_change_proxy : s_strings->try_enable_proxy);
  } else if (s_app->connection.error == TB_CONNECTION_ERROR_LONG_UPDATE) {
    tb_notify_post(&s_app->notify, TB_NOTIFY_CONNECTION, TB_NOTIFY_WARNING, TB_NOTIFY_REFRESH, s_strings->title, s_strings->long_update);
  } else {
    tb_notify_clear(&s_app->notify, TB_NOTIFY_CONNECTION);
  }
}

static void chats_changed(void *context) {
  (void)context;
  if (s_app->chats.error != TB_ERROR_NONE) { APP_LOG(APP_LOG_LEVEL_WARNING, "chats failed: error %d", (int)s_app->chats.error); }
  if (s_app->chats.error != TB_ERROR_NONE && handle_fatal(s_app->chats.error)) { return; }
  chats_notice();
  if (s_app->chats.summary_revision != s_summary_revision) {
    s_summary_revision = s_app->chats.summary_revision;
    tb_connection_summary(&s_app->connection, s_app->chats.connection, s_app->chats.proxy);
  }
  reload_later(TB_RELOAD_CHATS);
}

static void connection_changed(void *context) {
  (void)context;
  connection_notice();
  reload_later(TB_RELOAD_CHATS);
}

static void connection_reload(void *context) {
  (void)context;
  tb_chats_refresh(&s_app->chats);
}

static void history_changed(void *context) {
  (void)context;
  if (s_app->history.error != TB_ERROR_NONE || s_app->history.top_error != TB_ERROR_NONE || s_app->history.refresh_error != TB_ERROR_NONE) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "history failed: %d/%d/%d", (int)s_app->history.error, (int)s_app->history.top_error, (int)s_app->history.refresh_error);
  }
  if (s_app->history.error != TB_ERROR_NONE) {
    if (handle_fatal(s_app->history.error)) { return; }
    if (s_app->history.error == TB_RESULT_CHAT_NOT_FOUND) {
      tb_history_close(&s_app->history);
      s_app->history.loaded = false;
      if (in_stack(s_app->history_view.window)) { window_stack_remove(s_app->history_view.window, false); }
      show_info(s_strings->title, s_strings->chat_not_found);
      tb_chats_refresh(&s_app->chats);
      return;
    }
  }
  reload_later(TB_RELOAD_HISTORY);
}

static void open_quote(void) {
  if (!s_app->quote_id[0] || s_app->text.loading) { return; }
  tb_message_text_open(&s_app->quote, s_app->history.account, s_app->history.chat, s_app->quote_id);
  s_app->quote_id[0] = '\0';
}

static void text_changed(void *context) {
  (void)context;
  if (s_app->text.error != TB_ERROR_NONE && handle_fatal(s_app->text.error)) { return; }
  open_quote();
  reload_later(TB_RELOAD_READER);
}

static void quote_changed(void *context) {
  (void)context;
  if (s_app->quote.error != TB_ERROR_NONE && handle_fatal(s_app->quote.error)) { return; }
  reload_later(TB_RELOAD_READER);
}

static bool text_schedule(void *context, uint32_t milliseconds) { (void)context; (void)milliseconds; return true; }
static void text_cancel(void *context) { (void)context; }

static void open_chat(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_app->chats.count || s_account < 0) { return; }
  const TbChat *chat = &s_app->chats.items[index];
  if (!tb_history_is(&s_app->history, s_app->chats.account, chat->id)) { tb_history_window_reset(&s_app->history_view); }
  else { s_app->history_view.placed = true; }
  tb_history_window_set_send(&s_app->history_view, chat->send);
  tb_history_open(&s_app->history, s_app->chats.account, chat->id, chat->type, (chat->flags & TB_CHAT_FLAG_SAVED) != 0);
  if (!in_stack(s_app->history_view.window)) { window_stack_push(s_app->history_view.window, true); }
}

static void history_closed(void *context) {
  (void)context;
  if (!s_app->history.account[0] || !s_app->history.chat[0]) { return; }
  tb_history_close(&s_app->history);
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_VIEW_CLOSE;
  strncpy(args.account, s_app->history.account, sizeof(args.account) - 1);
  strncpy(args.chat, s_app->history.chat, sizeof(args.chat) - 1);
  tb_requests_submit(&s_app->requests, &args, TB_DATA_TIMEOUT, 2, false, NULL, NULL);
}

static const char *chat_title_of(const char *chat) {
  const int found = tb_chats_find(&s_app->chats, chat);
  if (found < 0) { return ""; }
  const TbChat *item = &s_app->chats.items[found];
  return (item->flags & TB_CHAT_FLAG_SAVED) ? s_strings->saved_messages : tb_or_empty(item->title);
}

static void open_message(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_app->history.count) { return; }
  const TbMessage *message = &s_app->history.items[index];
  tb_message_text_close(&s_app->text);
  tb_message_text_close(&s_app->quote);
  s_app->text.error = TB_ERROR_NONE;
  s_app->quote.error = TB_ERROR_NONE;
  const bool private_chat = s_app->history.chat_type == TB_CHAT_TYPE_PRIVATE || s_app->history.chat_type == TB_CHAT_TYPE_SECRET;
  const bool forwarded = message->forward && *message->forward;
  const char *name = s_app->history.saved && forwarded ? ""
                     : (message->flags & TB_MESSAGE_FLAG_OUTGOING) ? s_strings->you
                     : message->sender && *message->sender ? message->sender
                     : private_chat ? chat_title_of(s_app->history.chat) : "";
  tb_reader_window_show(&s_app->reader, message, name, forwarded ? message->forward : "");
  snprintf(s_app->quote_id, sizeof(s_app->quote_id), "%s", (message->flags & TB_MESSAGE_FLAG_REPLY) ? tb_or_empty(message->reply_id) : "");
  if (message->flags & TB_MESSAGE_FLAG_TRUNCATED) { tb_message_text_open(&s_app->text, s_app->history.account, s_app->history.chat, message->id); }
  open_quote();
  window_stack_push(s_app->reader.window, true);
}

static const char *send_reason(uint8_t send) {
  switch (send) {
    case TB_CAN_SEND_RESTRICTED: return s_strings->send_restricted;
    case TB_CAN_SEND_NOT_MEMBER: return s_strings->not_member;
    case TB_CAN_SEND_BANNED: return s_strings->banned;
    case TB_CAN_SEND_SECRET_CHAT: return s_strings->secret_chat;
    case TB_CAN_SEND_USER_DELETED: return s_strings->user_deleted;
    default: return s_strings->read_only;
  }
}

static bool history_open_for(const TbSendTarget *target) {
  return in_stack(s_app->history_view.window) && strcmp(s_app->history.account, target->account) == 0 && strcmp(s_app->history.chat, target->chat) == 0;
}

static void compose_changed(void *context) {
  (void)context;
  reload_later(TB_RELOAD_COMPOSE);
}

static void dictated(void *context, TbDictationResult result, const char *text, size_t length) {
  (void)context;
  if (result != TB_DICTATION_OK || length == 0 || !s_app->compose.open) { return; }
  if (tb_compose_dictated(&s_app->compose, text, length)) { tb_compose_view_review(&s_app->compose_view); }
}

static void start_dictation(void *context) {
  (void)context;
  if (!tb_dictation_start(&s_app->dictation)) { vibes_short_pulse(); }
}

static void quote(char *out, size_t size, const char *text) {
  const size_t length = strlen(text);
  const bool cut = length >= size;
  const size_t fit = tb_utf8_fit((const uint8_t *)text, length, cut ? size - 3 : size);
  memcpy(out, text, fit);
  if (cut) { memcpy(out + fit, "\xE2\x80\xA6", 3); }
  out[fit + (cut ? 3 : 0)] = '\0';
  for (char *cursor = out; *cursor; ++cursor) {
    if (*cursor == '\n') { *cursor = ' '; }
  }
}

static void begin_compose(const TbMessage *reply, const char *reply_text) {
  if (!s_app->history.account[0] || !s_app->history.chat[0]) { return; }
  TbComposeTarget target;
  memset(&target, 0, sizeof(target));
  strncpy(target.send.account, s_app->history.account, sizeof(target.send.account) - 1);
  strncpy(target.send.chat, s_app->history.chat, sizeof(target.send.chat) - 1);
  if (s_account >= 0 && s_account < s_app->session.count) { quote(target.account_name, sizeof(target.account_name), s_app->session.accounts[s_account].name); }
  quote(target.chat_title, sizeof(target.chat_title), chat_title_of(s_app->history.chat));
  if (reply) {
    strncpy(target.send.reply, reply->id, sizeof(target.send.reply) - 1);
    quote(target.reply_sender, sizeof(target.reply_sender),
          (reply->flags & TB_MESSAGE_FLAG_OUTGOING) ? s_strings->you : tb_or_empty(reply->sender));
    quote(target.reply_text, sizeof(target.reply_text), reply_text ? reply_text : "");
  }
  if (!tb_compose_open(&s_app->compose, &target)) { return; }
  const bool dictation = tb_dictation_open(&s_app->dictation, dictated, NULL);
  tb_compose_view_open(&s_app->compose_view, dictation);
}

static bool compose_send(void *context, TbComposeSendMode mode) {
  (void)context;
  if (mode == TB_COMPOSE_SEND) {
    char preview[TB_SEND_PREVIEW_SIZE];
    quote(preview, sizeof(preview), s_app->compose.text ? s_app->compose.text : "");
    return tb_send_start(&s_app->tracker, &s_app->compose.target.send, s_app->compose.draft_id, false, s_app->compose.target.chat_title, preview);
  }
  TbSendStatus copy = s_app->tracker.status;
  return tb_send_start(&s_app->tracker, &copy.target, copy.draft_id, true, copy.title, copy.preview);
}

static void compose_check(void *context) {
  (void)context;
  tb_send_check(&s_app->tracker);
}

static void compose_open_chat(void *context) {
  (void)context;
  tb_compose_view_close_all(&s_app->compose_view);
  if (in_stack(s_app->reader.window)) { window_stack_remove(s_app->reader.window, false); }
}

static void compose_finished(void *context, bool sent) {
  (void)context;
  const bool attempted = s_app->tracker.status.draft_id != 0 && s_app->tracker.status.draft_id == s_app->compose.draft_id;
  tb_dictation_close(&s_app->dictation);
  tb_compose_close(&s_app->compose, attempted);
  if (sent) {
    tb_history_window_follow(&s_app->history_view);
    tb_history_refresh(&s_app->history);
  } else if (tb_send_busy(&s_app->tracker)) {
    tb_notify_post(&s_app->notify, TB_NOTIFY_SEND, TB_NOTIFY_INFO, TB_NOTIFY_WAIT, s_app->tracker.status.title, s_strings->sending_continues);
  }
  s_compose_from_reader = false;
}

static void late_result(const TbSendStatus *status) {
  const char *title = status->title[0] ? status->title : s_strings->title;
  switch (status->phase) {
    case TB_SENDING_SENT:
      tb_notify_post(&s_app->notify, TB_NOTIFY_SEND, TB_NOTIFY_INFO, TB_NOTIFY_NO_ACTION, title,
                     (status->flags & TB_SEND_FLAG_REPLY_DROPPED) ? s_strings->sent_no_reply : s_strings->sent);
      if (history_open_for(&status->target)) {
        tb_history_window_follow(&s_app->history_view);
        tb_history_refresh(&s_app->history);
      }
      break;
    case TB_SENDING_FAILED:
      snprintf(s_app->notify_body, sizeof(s_app->notify_body), "%s: %s", s_strings->not_sent, tb_error_text(s_strings, status->code));
      tb_notify_post(&s_app->notify, TB_NOTIFY_SEND, TB_NOTIFY_ERROR, TB_NOTIFY_OPEN_CHAT, title, s_app->notify_body);
      vibes_double_pulse();
      break;
    case TB_SENDING_UNKNOWN:
      tb_notify_post(&s_app->notify, TB_NOTIFY_SEND, TB_NOTIFY_ERROR, TB_NOTIFY_CHECK, title, s_strings->result_unknown);
      vibes_double_pulse();
      break;
    case TB_SENDING_PENDING:
      if (status->restored) {
        char heading[TB_NOTIFY_TITLE_SIZE];
        snprintf(heading, sizeof(heading), s_strings->last_message_in, title);
        tb_notify_post(&s_app->notify, TB_NOTIFY_SEND, TB_NOTIFY_WARNING, TB_NOTIFY_CHECK, heading, status->preview);
      }
      break;
    default:
      break;
  }
}

static void send_changed(void *context, const TbSendStatus *status) {
  (void)context;
  tb_diag_event("send", NULL);
  if (s_app->compose_view.result_window && status->draft_id == s_app->compose_view.result_draft) {
    tb_notify_clear(&s_app->notify, TB_NOTIFY_SEND);
    tb_compose_view_status(&s_app->compose_view, status);
    return;
  }
  late_result(status);
}

static void send_record(uint8_t type, const uint8_t *data, uint16_t length) {
  TbCursor body;
  tb_cursor_init(&body, data, length);
  TbSendStateRecord record;
  if (!tb_codec_send_state(&body, &record)) { return; }
  const bool restored = type == TB_RECORD_PENDING_SEND;
  if (tb_send_record(&s_app->tracker, &record, restored)) { return; }
  TbSendStatus foreign;
  memset(&foreign, 0, sizeof(foreign));
  foreign.draft_id = record.draft_id;
  foreign.phase = record.state == TB_SEND_STATE_SENT ? TB_SENDING_SENT : record.state == TB_SEND_STATE_FAILED ? TB_SENDING_FAILED
                  : record.state == TB_SEND_STATE_UNKNOWN ? TB_SENDING_UNKNOWN : TB_SENDING_PENDING;
  foreign.code = record.code;
  foreign.flags = record.flags;
  foreign.restored = restored;
  tb_copy_span(foreign.target.account, sizeof(foreign.target.account), record.account);
  tb_copy_span(foreign.target.chat, sizeof(foreign.target.chat), record.chat);
  tb_copy_span(foreign.title, sizeof(foreign.title), record.title);
  tb_copy_span(foreign.preview, sizeof(foreign.preview), record.preview);
  late_result(&foreign);
}

static void send_records(const uint8_t *payload, uint16_t length) {
  TbCursor cursor;
  tb_cursor_init(&cursor, payload, length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    if (!tb_codec_next(&cursor, &type, &body)) { return; }
    if (type == TB_RECORD_SEND_STATE || type == TB_RECORD_PENDING_SEND) { send_record(type, body.data, body.length); }
  }
}

static void session_extra(void *context, uint8_t type, const uint8_t *record, uint16_t length) {
  (void)context;
  send_record(type, record, length);
}

static void connection_records(void *context, const uint8_t *payload, uint16_t length) {
  (void)context;
  send_records(payload, length);
}

static void retry_failed(void) {
  TbSendStatus copy = s_app->tracker.status;
  if (tb_send_start(&s_app->tracker, &copy.target, copy.draft_id, true, copy.title, copy.preview)) {
    tb_notify_post(&s_app->notify, TB_NOTIFY_SEND, TB_NOTIFY_INFO, TB_NOTIFY_WAIT, copy.title, s_strings->sending);
  } else {
    vibes_short_pulse();
  }
}

static bool retry_available(const char *message_id) {
  const TbSendStatus *status = &s_app->tracker.status;
  return status->draft_id != 0 && (status->phase == TB_SENDING_FAILED || status->phase == TB_SENDING_UNKNOWN) &&
         (status->flags & TB_SEND_FLAG_RETRYABLE || status->phase == TB_SENDING_UNKNOWN) && message_id && message_id[0] &&
         strcmp(status->message, message_id) == 0 && strcmp(status->target.chat, s_app->history.chat) == 0;
}

static void open_chat_of(const TbSendTarget *target) {
  if (history_open_for(target)) {
    tb_compose_view_close_all(&s_app->compose_view);
    if (in_stack(s_app->reader.window)) { window_stack_remove(s_app->reader.window, false); }
    return;
  }
  if (strcmp(s_app->chats.account, target->account) != 0) { return; }
  const int found = tb_chats_find(&s_app->chats, target->chat);
  if (found >= 0) {
    tb_compose_view_close_all(&s_app->compose_view);
    if (in_stack(s_app->reader.window)) { window_stack_remove(s_app->reader.window, false); }
    if (in_stack(s_app->history_view.window)) { window_stack_remove(s_app->history_view.window, false); }
    open_chat(NULL, found);
  }
}

static void run_notify_action(TbNotifyAction action, TbNotifySource source) {
  switch (action) {
    case TB_NOTIFY_REFRESH:
      tb_notify_clear(&s_app->notify, source);
      tb_connection_refresh(&s_app->connection);
      tb_chats_refresh(&s_app->chats);
      if (in_stack(s_app->history_view.window)) { tb_history_refresh(&s_app->history); }
      break;
    case TB_NOTIFY_CHECK:
      tb_notify_clear(&s_app->notify, source);
      if (!tb_send_check(&s_app->tracker)) { late_result(&s_app->tracker.status); }
      break;
    case TB_NOTIFY_OPEN_CHAT:
      tb_notify_clear(&s_app->notify, source);
      open_chat_of(&s_app->tracker.status.target);
      break;
    default:
      tb_notify_dismiss(&s_app->notify);
      break;
  }
}


static void notify_activate(void *context, const TbNotification *item) {
  (void)context;
  run_notify_action(item->action, item->source);
}

static void notify_changed(void *context) {
  (void)context;
  tb_diag_event("notify", NULL);
  reload_later(TB_RELOAD_NOTIFY);
}

static void write_to_chat(void *context) {
  (void)context;
  if (!tb_history_window_writable(&s_app->history_view)) { return; }
  s_compose_from_reader = false;
  begin_compose(NULL, NULL);
}

static void message_chosen(void *context, uint8_t action) {
  (void)context;
  const int index = s_menu_message;
  if (index < 0 || index >= s_app->history.count) { return; }
  if (action == TB_ACTION_REPLY) {
    s_compose_from_reader = false;
    begin_compose(&s_app->history.items[index], s_app->history.items[index].text);
  } else if (action == TB_ACTION_OPEN) {
    open_message(NULL, index);
  } else if (action == TB_ACTION_RETRY) {
    retry_failed();
  }
}

static void message_menu(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_app->history.count) { return; }
  s_menu_message = index;
  const char *labels[3];
  uint8_t actions[3];
  uint8_t count = 0;
  const bool writable = tb_history_window_writable(&s_app->history_view);
  labels[count] = writable ? s_strings->reply : send_reason(s_app->history_view.send);
  actions[count++] = writable ? TB_ACTION_REPLY : TB_ACTION_NONE;
  labels[count] = s_strings->open;
  actions[count++] = TB_ACTION_OPEN;
  if (retry_available(s_app->history.items[index].id)) {
    labels[count] = s_strings->retry_send;
    actions[count++] = TB_ACTION_RETRY;
  }
  tb_actions_open(labels, actions, count, message_chosen, NULL);
}

static void reader_chosen(void *context, uint8_t action) {
  (void)context;
  if (action == TB_ACTION_REPLY) {
    s_compose_from_reader = true;
    begin_compose(&s_app->reader.message, s_app->reader.fallback);
  } else if (action == TB_ACTION_WRITE) {
    s_compose_from_reader = true;
    begin_compose(NULL, NULL);
  } else if (action == TB_ACTION_RETRY) {
    retry_failed();
  }
}

static void reader_menu(void *context) {
  (void)context;
  const char *labels[3];
  uint8_t actions[3];
  uint8_t count = 0;
  const bool writable = tb_history_window_writable(&s_app->history_view);
  labels[count] = writable ? s_strings->reply : send_reason(s_app->history_view.send);
  actions[count++] = writable ? TB_ACTION_REPLY : TB_ACTION_NONE;
  if (writable) {
    labels[count] = s_strings->write_to_chat;
    actions[count++] = TB_ACTION_WRITE;
  }
  if (retry_available(s_app->reader.message.id)) {
    labels[count] = s_strings->retry_send;
    actions[count++] = TB_ACTION_RETRY;
  }
  tb_actions_open(labels, actions, count, reader_chosen, NULL);
}

static void diag_probe(TbDiagCounters *out) {
  if (!s_app) { return; }
  uint16_t queued = 0;
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    if (s_app->requests.slots[index].active) { ++queued; }
  }
  AppTimer *timers[] = {s_app->timers[TB_TIMER_REQUEST], s_app->timers[TB_TIMER_CHATS], s_app->timers[TB_TIMER_CONNECTION], s_reload_timer, s_app->timers[TB_TIMER_HISTORY], s_app->timers[TB_TIMER_NOTIFY],
                        s_app->chats_view.pull_timer, s_app->history_view.pull_timer, s_app->compose_view.auto_close};
  uint16_t active = 0;
  for (size_t index = 0; index < ARRAY_LENGTH(timers); ++index) {
    if (timers[index]) { ++active; }
  }
  out->queued = queued;
  out->timers = active;
  out->items = (uint16_t)(s_app->history.count + s_app->chats.count);
  out->budget = (uint32_t)(s_app->history.budget.used + s_app->chats.budget.used);
  out->draft_bytes = s_app->compose.text_length;
}

static void settings_changed(void) {
  reset_to_connect(s_strings->checking, NULL, NULL);
  tb_session_refresh(&s_app->session);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  int32_t kind = 0;
  int32_t sequence = 0;
  if (!integer(dict_find(iter, MESSAGE_KEY_RESPONSE_KIND), &kind) || !integer(dict_find(iter, MESSAGE_KEY_REQUEST_SEQ), &sequence)) { return; }
  if (kind == TB_RESPONSE_READY) {
    if (sequence > 0) { tb_session_ready(&s_app->session, (uint32_t)sequence); }
    return;
  }
  if (kind == TB_RESPONSE_REFRESH) {
    settings_changed();
    return;
  }
  if (kind == TB_RESPONSE_PUSH) {
    const Tuple *pushed = dict_find(iter, MESSAGE_KEY_PAYLOAD);
    if (pushed && pushed->type == TUPLE_BYTE_ARRAY) {
      tb_diag_inbox(pushed->length);
      send_records(pushed->value->data, pushed->length);
    }
    return;
  }
  if (kind != TB_RESPONSE_DATA || sequence <= 0) { return; }
  int32_t result = 0;
  int32_t index = 0;
  int32_t total = 0;
  int32_t flags = 0;
  int32_t retry_after = 0;
  TbResponse response;
  memset(&response, 0, sizeof(response));
  if (!integer(dict_find(iter, MESSAGE_KEY_RESULT_CODE), &result) || !integer(dict_find(iter, MESSAGE_KEY_CHUNK_INDEX), &index) ||
      !integer(dict_find(iter, MESSAGE_KEY_CHUNK_TOTAL), &total) || index < 0 || total <= 0 || total > UINT16_MAX) {
    log_tuple("RESULT_CODE", dict_find(iter, MESSAGE_KEY_RESULT_CODE));
    log_tuple("CHUNK_INDEX", dict_find(iter, MESSAGE_KEY_CHUNK_INDEX));
    log_tuple("CHUNK_TOTAL", dict_find(iter, MESSAGE_KEY_CHUNK_TOTAL));
    response.total = 0;
    tb_requests_chunk(&s_app->requests, (uint32_t)sequence, &response);
    return;
  }
  integer(dict_find(iter, MESSAGE_KEY_PAGE_FLAGS), &flags);
  integer(dict_find(iter, MESSAGE_KEY_RETRY_AFTER), &retry_after);
  const Tuple *payload = dict_find(iter, MESSAGE_KEY_PAYLOAD);
  if (payload && payload->type != TUPLE_BYTE_ARRAY) { log_tuple("PAYLOAD", payload); }
  response.result = result;
  response.flags = (uint32_t)flags;
  response.retry_after = retry_after > 0 ? (uint32_t)retry_after : 0;
  response.index = (uint16_t)index;
  response.total = (uint16_t)total;
  if (payload && payload->type == TUPLE_BYTE_ARRAY) {
    response.payload = payload->value->data;
    response.length = payload->length;
  }
  tb_diag_inbox(response.length);
  tb_requests_chunk(&s_app->requests, (uint32_t)sequence, &response);
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  (void)iter; (void)context;
  tb_requests_outbox_ready(&s_app->requests);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  (void)context;
  int32_t sequence = 0;
  if (integer(dict_find(iter, MESSAGE_KEY_REQUEST_SEQ), &sequence) && sequence > 0) {
    tb_requests_send_failed(&s_app->requests, (uint32_t)sequence, reason == APP_MSG_NOT_CONNECTED);
  }
  tb_requests_outbox_ready(&s_app->requests);
}

static bool link_error(int32_t error) { return error == TB_ERROR_PHONE_UNREACHABLE || error == TB_ERROR_NO_RESPONSE; }

static void phone_connection(bool connected) {
  if (!connected) { return; }
  if (!tb_session_busy(&s_app->session) && link_error(s_app->session.error)) {
    tb_session_retry(&s_app->session);
    return;
  }
  if (in_stack(s_app->chats_view.window) && link_error(s_app->chats.error)) { tb_chats_retry(&s_app->chats); }
  if (in_stack(s_app->history_view.window)) {
    if (link_error(s_app->history.error) || link_error(s_app->history.top_error)) { tb_history_retry(&s_app->history); }
    else if (link_error(s_app->history.refresh_error)) { tb_history_refresh(&s_app->history); }
  }
}

static void focus_changed(bool in_focus) {
  Window *top = window_stack_get_top_window();
  if (top == s_app->chats_view.window) {
    tb_chats_set_active(&s_app->chats, in_focus);
    tb_connection_set_active(&s_app->connection, in_focus);
  }
  if (top == s_app->history_view.window) { tb_history_set_active(&s_app->history, in_focus); }
}

__attribute__((noinline)) static bool init(void) {
  s_app = calloc(1, sizeof(TbApp));
  if (!s_app || !tb_scratch_init()) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "no memory for app state");
    return false;
  }
  s_strings = tb_localization_current();
  if (!s_strings) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "no memory for strings");
    return false;
  }
  tb_theme_init();
  tb_requests_init(&s_app->requests, (TbRequestPorts){send_request, timer_schedule, timer_cancel, now_ms, &s_app->timers[TB_TIMER_REQUEST]});
  tb_session_init(&s_app->session, &s_app->requests, (TbSessionPorts){session_changed, NULL, session_extra});
  tb_notify_init(&s_app->notify, (TbNotifyPorts){notify_changed, timer_schedule, timer_cancel, now_ms, &s_app->timers[TB_TIMER_NOTIFY]});
  tb_send_init(&s_app->tracker, &s_app->requests, (TbSendPorts){send_changed, NULL});
  tb_compose_init(&s_app->compose, &s_app->requests, (TbViewPorts){compose_changed, text_schedule, text_cancel, now_ms, NULL},
                  (TbComposeConfig){TB_TEMPLATES_CAPACITY, TB_TEMPLATE_PREVIEW, TB_DRAFT_TEXT, TB_DATA_TIMEOUT});
  tb_compose_view_init(&s_app->compose_view, &s_app->compose, &s_app->tracker, s_strings,
                       (TbComposeActions){start_dictation, compose_send, compose_check, compose_open_chat, compose_finished, NULL});
  const TbChatsConfig chats_config = {TB_CHATS_CAPACITY, TB_CHATS_PAGE, TB_CHATS_TEXT, TB_CHATS_BUDGET, TB_DATA_TIMEOUT, 0, 0};
  const TbHistoryConfig history_config = {TB_HISTORY_CAPACITY, TB_HISTORY_PAGE, TB_HISTORY_TEXT, TB_HISTORY_BUDGET, TB_DATA_TIMEOUT, 60000, {3000, 8000}};
  const bool allocated = tb_chats_init(&s_app->chats, &s_app->requests, (TbViewPorts){chats_changed, timer_schedule, timer_cancel, now_ms, &s_app->timers[TB_TIMER_CHATS]}, chats_config) &&
                         tb_history_init(&s_app->history, &s_app->requests, (TbViewPorts){history_changed, timer_schedule, timer_cancel, now_ms, &s_app->timers[TB_TIMER_HISTORY]},
                                         history_config);
  tb_connection_init(&s_app->connection, &s_app->requests,
                     (TbConnectionPorts){connection_changed, connection_reload, timer_schedule, timer_cancel, now_ms, &s_app->timers[TB_TIMER_CONNECTION],
                                         connection_records},
                     TB_EVENTS_TIMEOUT);
  tb_message_text_init(&s_app->text, &s_app->requests, (TbViewPorts){text_changed, text_schedule, text_cancel, now_ms, NULL}, TB_FULL_TEXT, TB_DATA_TIMEOUT);
  tb_message_text_init(&s_app->quote, &s_app->requests, (TbViewPorts){quote_changed, text_schedule, text_cancel, now_ms, NULL}, TB_QUOTE_TEXT, TB_DATA_TIMEOUT);
  tb_notice_init(&s_app->connect);
  tb_notice_init(&s_app->info);
  tb_accounts_window_init(&s_app->accounts, &s_app->session, s_strings, (TbAccountsActions){open_account, make_default, NULL});
  tb_chats_window_init(&s_app->chats_view, &s_app->chats, &s_app->connection, &s_app->notify, notify_activate, s_strings, (TbChatsActions){open_chat, NULL});
  tb_history_window_init(&s_app->history_view, &s_app->history, &s_app->notify, notify_activate, s_strings,
                         (TbHistoryActions){open_message, history_closed, write_to_chat, message_menu, NULL});
  tb_reader_window_init(&s_app->reader, &s_app->text, &s_app->quote, s_strings, (TbReaderActions){reader_menu, NULL});
  show_connect(s_strings->checking, NULL, NULL);
  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = phone_connection});
  app_focus_service_subscribe_handlers((AppFocusHandlers){.did_focus = focus_changed});
  const uint32_t maximum = app_message_inbox_size_maximum();
  s_inbox_size = maximum < TB_INBOX_SIZE ? maximum : TB_INBOX_SIZE;
  const bool opened = app_message_open(s_inbox_size, TB_OUTBOX_SIZE) == APP_MSG_OK;
  APP_LOG(APP_LOG_LEVEL_INFO, "inbox %lu heap free %lu", (unsigned long)s_inbox_size, (unsigned long)heap_bytes_free());
  if (!allocated) {
    reset_to_connect(s_strings->out_of_memory, NULL, NULL);
    return true;
  }
  tb_session_start(&s_app->session, opened);
  return true;
}

__attribute__((noinline)) static void deinit(void) {
  if (s_reload_timer) { app_timer_cancel(s_reload_timer); }
  close_compose();
  tb_send_close(&s_app->tracker);
  tb_notify_reset(&s_app->notify);
  tb_session_stop(&s_app->session);
  tb_requests_cancel_all(&s_app->requests);
  app_message_deregister_callbacks();
  connection_service_unsubscribe();
  app_focus_service_unsubscribe();
  tb_message_text_close(&s_app->text);
  tb_message_text_close(&s_app->quote);
  tb_connection_close(&s_app->connection);
  tb_history_deinit(&s_app->history);
  tb_chats_deinit(&s_app->chats);
  tb_reader_window_deinit(&s_app->reader);
  tb_history_window_deinit(&s_app->history_view);
  tb_chats_window_deinit(&s_app->chats_view);
  tb_accounts_window_deinit(&s_app->accounts);
  tb_notice_deinit(&s_app->info);
  tb_notice_deinit(&s_app->connect);
  tb_scratch_deinit();
  tb_localization_release();
  s_strings = NULL;
  free(s_app);
  s_app = NULL;
}

int main(void) {
  volatile uint32_t base = 0;
  tb_diag_start((void *)&base, diag_probe);
  if (!init()) { return 0; }
  app_event_loop();
  deinit();
}
