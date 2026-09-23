#include <pebble.h>
#include "accounts_window.h"
#include "chats.h"
#include "chats_window.h"
#include "errors.h"
#include "format.h"
#include "generated/localization.h"
#include "generated/protocol.h"
#include "history.h"
#include "history_window.h"
#include "message_text.h"
#include "notice_window.h"
#include "reader_window.h"
#include "request_layer.h"
#include "session.h"
#include "theme.h"

#define TB_PERSIST_LAST_ACCOUNT 1
#define TB_DATA_TIMEOUT 25000
#define TB_OUTBOX_SIZE 512

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
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
#else
#define TB_INBOX_SIZE 2048
#define TB_CHATS_CAPACITY 30
#define TB_CHATS_PAGE 10
#define TB_CHATS_TEXT 64
#define TB_CHATS_BUDGET 3072
#define TB_HISTORY_CAPACITY 25
#define TB_HISTORY_PAGE 12
#define TB_HISTORY_TEXT 160
#define TB_HISTORY_BUDGET 4096
#define TB_FULL_TEXT 3072
#endif

static const TbStrings *s_strings;
static uint32_t s_inbox_size;
static TbRequestLayer s_requests;
static AppTimer *s_request_timer;
static TbSession s_session;
static TbChats s_chats;
static AppTimer *s_chats_timer;
static TbHistory s_history;
static AppTimer *s_history_timer;
static TbMessageText s_text;
static TbNotice s_connect;
static TbNotice s_info;
static TbAccountsWindow s_accounts;
static TbChatsWindow s_chats_view;
static TbHistoryWindow s_history_view;
static TbReaderWindow s_reader;
static int s_account = -1;
static uint32_t s_default_request;
static int s_default_index = -1;
static char s_info_body[256];

static uint32_t now_ms(void *context) {
  (void)context;
  time_t seconds = 0;
  uint16_t milliseconds = 0;
  time_ms(&seconds, &milliseconds);
  return (uint32_t)seconds * 1000u + milliseconds;
}

static void request_timer_fired(void *context) {
  (void)context;
  s_request_timer = NULL;
  tb_requests_tick(&s_requests);
}
static bool request_schedule(void *context, uint32_t milliseconds) {
  (void)context;
  if (s_request_timer) { app_timer_cancel(s_request_timer); }
  s_request_timer = app_timer_register(milliseconds, request_timer_fired, NULL);
  return s_request_timer != NULL;
}
static void request_cancel(void *context) {
  (void)context;
  if (s_request_timer) { app_timer_cancel(s_request_timer); s_request_timer = NULL; }
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
  }
  if (!ok) { return TB_SEND_FAILED; }
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

static bool in_stack(Window *window) { return window && window_stack_contains_window(window); }

static void show_connect(const char *body, const char *hint, TbNoticeAction action) {
  tb_notice_set(&s_connect, s_strings->title, body, hint, action, NULL);
  if (!in_stack(s_connect.window)) { window_stack_push(s_connect.window, false); }
}

static void close_views(void) {
  tb_message_text_close(&s_text);
  tb_history_close(&s_history);
  tb_chats_close(&s_chats);
}

static void reset_to_connect(const char *body, const char *hint, TbNoticeAction action) {
  close_views();
  s_account = -1;
  tb_notice_set(&s_connect, s_strings->title, body, hint, action, NULL);
  if (!in_stack(s_connect.window)) { window_stack_push(s_connect.window, false); }
  Window *windows[] = {s_reader.window, s_history_view.window, s_chats_view.window, s_accounts.window, s_info.window};
  for (size_t index = 0; index < ARRAY_LENGTH(windows); ++index) {
    if (in_stack(windows[index])) { window_stack_remove(windows[index], false); }
  }
}

static void retry_session(void *context) {
  (void)context;
  tb_session_retry(&s_session);
}

static void show_info(const char *title, const char *body) {
  snprintf(s_info_body, sizeof(s_info_body), "%s", body);
  tb_notice_set(&s_info, title, s_info_body, s_strings->back_hint, NULL, NULL);
  if (!in_stack(s_info.window)) { window_stack_push(s_info.window, true); }
}

static void remember_account(const char *id) { persist_write_string(TB_PERSIST_LAST_ACCOUNT, id); }

static int remembered_account(void) {
  char id[TB_ACCOUNT_ID_SIZE] = "";
  if (persist_exists(TB_PERSIST_LAST_ACCOUNT)) { persist_read_string(TB_PERSIST_LAST_ACCOUNT, id, sizeof(id)); }
  const int found = id[0] ? tb_session_find(&s_session, id) : -1;
  if (found >= 0) { return found; }
  return s_session.default_account[0] ? tb_session_find(&s_session, s_session.default_account) : 0;
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
  if (index < 0 || index >= s_session.count) { return; }
  const TbAccount *account = &s_session.accounts[index];
  if (account->state != TB_ACCOUNT_STATE_READY) {
    show_info(account->name, account_explanation(account->state));
    return;
  }
  if (s_account != index) { tb_history_close(&s_history); }
  s_account = index;
  remember_account(account->id);
  tb_chats_window_reset(&s_chats_view);
  tb_chats_open(&s_chats, account->id, s_session.chat_list);
  if (!in_stack(s_chats_view.window)) { window_stack_push(s_chats_view.window, true); }
}

static void default_saved(void *owner, uint32_t sequence, const TbResponse *response) {
  (void)owner;
  if (sequence != s_default_request || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  s_default_request = 0;
  if (response->outcome == TB_OUTCOME_RESPONSE && response->result == TB_RESULT_OK && s_default_index >= 0 &&
      s_default_index < s_session.count) {
    for (int index = 0; index < s_session.count; ++index) { s_session.accounts[index].flags &= (uint8_t)~TB_ACCOUNT_FLAG_DEFAULT; }
    s_session.accounts[s_default_index].flags |= TB_ACCOUNT_FLAG_DEFAULT;
    strncpy(s_session.default_account, s_session.accounts[s_default_index].id, sizeof(s_session.default_account) - 1);
    tb_accounts_window_reload(&s_accounts, s_default_index);
    vibes_short_pulse();
  } else {
    vibes_double_pulse();
  }
}

static void make_default(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_session.count || s_default_request) { return; }
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_SET_DEFAULT;
  strncpy(args.account, s_session.accounts[index].id, sizeof(args.account) - 1);
  s_default_index = index;
  s_default_request = tb_requests_submit(&s_requests, &args, TB_DATA_TIMEOUT, 2, false, default_saved, NULL);
  if (!s_default_request) { vibes_double_pulse(); }
}

static void route(void) {
  if (tb_session_busy(&s_session)) {
    if (!in_stack(s_accounts.window) && !in_stack(s_chats_view.window)) { show_connect(s_strings->checking, NULL, NULL); }
    return;
  }
  if (s_session.error != TB_ERROR_NONE) {
    reset_to_connect(tb_error_text(s_strings, s_session.error), s_strings->retry_hint, retry_session);
    return;
  }
  if (!s_session.loaded) { return; }
  if (s_session.count == 0) {
    char body[160];
    snprintf(body, sizeof(body), "%s\n%s/settings", s_strings->no_accounts, s_session.host);
    reset_to_connect(body, s_strings->retry_hint, retry_session);
    return;
  }
  if (in_stack(s_accounts.window) || in_stack(s_chats_view.window)) {
    tb_accounts_window_reload(&s_accounts, s_account >= 0 ? s_account : remembered_account());
    return;
  }
  const int initial = tb_session_initial_account(&s_session);
  tb_accounts_window_reload(&s_accounts, initial >= 0 ? initial : remembered_account());
  if (s_session.count > 1 || initial < 0) { window_stack_push(s_accounts.window, false); }
  if (initial >= 0) { open_account(NULL, initial); }
  if (in_stack(s_connect.window)) { window_stack_remove(s_connect.window, false); }
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
  const char *name = s_account >= 0 && s_account < s_session.count ? s_session.accounts[s_account].name : s_strings->title;
  if (s_account >= 0 && s_account < s_session.count) {
    s_session.accounts[s_account].state = error == TB_RESULT_ACCOUNT_GONE ? TB_ACCOUNT_STATE_REMOVING : TB_ACCOUNT_STATE_NEEDS_LOGIN;
  }
  close_views();
  if (in_stack(s_reader.window)) { window_stack_remove(s_reader.window, false); }
  if (in_stack(s_history_view.window)) { window_stack_remove(s_history_view.window, false); }
  if (!in_stack(s_accounts.window)) { window_stack_push(s_accounts.window, false); }
  if (in_stack(s_chats_view.window)) { window_stack_remove(s_chats_view.window, false); }
  tb_accounts_window_reload(&s_accounts, s_account);
  show_info(name, tb_error_text(s_strings, error));
  return true;
}

static void chats_changed(void *context) {
  (void)context;
  if (s_chats.error != TB_ERROR_NONE && handle_fatal(s_chats.error)) { return; }
  tb_chats_window_reload(&s_chats_view);
}

static void history_changed(void *context) {
  (void)context;
  if (s_history.error != TB_ERROR_NONE) {
    if (handle_fatal(s_history.error)) { return; }
    if (s_history.error == TB_RESULT_CHAT_NOT_FOUND) {
      tb_history_close(&s_history);
      s_history.loaded = false;
      if (in_stack(s_history_view.window)) { window_stack_remove(s_history_view.window, false); }
      show_info(s_strings->title, s_strings->chat_not_found);
      tb_chats_refresh(&s_chats);
      return;
    }
  }
  tb_history_window_reload(&s_history_view);
}

static void text_changed(void *context) {
  (void)context;
  if (s_text.error != TB_ERROR_NONE && handle_fatal(s_text.error)) { return; }
  tb_reader_window_reload(&s_reader);
}

static void chats_timer_fired(void *context) {
  (void)context;
  s_chats_timer = NULL;
  tb_chats_timer(&s_chats);
}
static bool chats_schedule(void *context, uint32_t milliseconds) {
  (void)context;
  if (s_chats_timer) { app_timer_cancel(s_chats_timer); }
  s_chats_timer = app_timer_register(milliseconds, chats_timer_fired, NULL);
  return s_chats_timer != NULL;
}
static void chats_cancel(void *context) {
  (void)context;
  if (s_chats_timer) { app_timer_cancel(s_chats_timer); s_chats_timer = NULL; }
}

static void history_timer_fired(void *context) {
  (void)context;
  s_history_timer = NULL;
  tb_history_timer(&s_history);
}
static bool history_schedule(void *context, uint32_t milliseconds) {
  (void)context;
  if (s_history_timer) { app_timer_cancel(s_history_timer); }
  s_history_timer = app_timer_register(milliseconds, history_timer_fired, NULL);
  return s_history_timer != NULL;
}
static void history_cancel(void *context) {
  (void)context;
  if (s_history_timer) { app_timer_cancel(s_history_timer); s_history_timer = NULL; }
}

static bool text_schedule(void *context, uint32_t milliseconds) { (void)context; (void)milliseconds; return true; }
static void text_cancel(void *context) { (void)context; }

static void open_chat(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_chats.count || s_account < 0) { return; }
  const TbChat *chat = &s_chats.items[index];
  if (!tb_history_is(&s_history, s_chats.account, chat->id)) { tb_history_window_reset(&s_history_view); }
  else { s_history_view.placed = true; }
  tb_history_open(&s_history, s_chats.account, chat->id, chat->type);
  if (!in_stack(s_history_view.window)) { window_stack_push(s_history_view.window, true); }
}

static void history_closed(void *context) {
  (void)context;
  if (!s_history.account[0] || !s_history.chat[0]) { return; }
  tb_history_close(&s_history);
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_VIEW_CLOSE;
  strncpy(args.account, s_history.account, sizeof(args.account) - 1);
  strncpy(args.chat, s_history.chat, sizeof(args.chat) - 1);
  tb_requests_submit(&s_requests, &args, TB_DATA_TIMEOUT, 2, false, NULL, NULL);
}

static void open_message(void *context, int index) {
  (void)context;
  if (index < 0 || index >= s_history.count) { return; }
  const TbMessage *message = &s_history.items[index];
  tb_message_text_close(&s_text);
  s_text.error = TB_ERROR_NONE;
  const bool private_chat = s_history.chat_type == TB_CHAT_TYPE_PRIVATE || s_history.chat_type == TB_CHAT_TYPE_SECRET;
  tb_reader_window_show(&s_reader, message, private_chat);
  if (message->flags & TB_MESSAGE_FLAG_TRUNCATED) { tb_message_text_open(&s_text, s_history.account, s_history.chat, message->id); }
  window_stack_push(s_reader.window, true);
}

static void settings_changed(void) {
  reset_to_connect(s_strings->checking, NULL, NULL);
  tb_session_refresh(&s_session);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  int32_t kind = 0;
  int32_t sequence = 0;
  if (!integer(dict_find(iter, MESSAGE_KEY_RESPONSE_KIND), &kind) || !integer(dict_find(iter, MESSAGE_KEY_REQUEST_SEQ), &sequence)) { return; }
  if (kind == TB_RESPONSE_READY) {
    if (sequence > 0) { tb_session_ready(&s_session, (uint32_t)sequence); }
    return;
  }
  if (kind == TB_RESPONSE_REFRESH) {
    settings_changed();
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
    response.total = 0;
    tb_requests_chunk(&s_requests, (uint32_t)sequence, &response);
    return;
  }
  integer(dict_find(iter, MESSAGE_KEY_PAGE_FLAGS), &flags);
  integer(dict_find(iter, MESSAGE_KEY_RETRY_AFTER), &retry_after);
  const Tuple *payload = dict_find(iter, MESSAGE_KEY_PAYLOAD);
  response.result = result;
  response.flags = (uint32_t)flags;
  response.retry_after = retry_after > 0 ? (uint32_t)retry_after : 0;
  response.index = (uint16_t)index;
  response.total = (uint16_t)total;
  if (payload && payload->type == TUPLE_BYTE_ARRAY) {
    response.payload = payload->value->data;
    response.length = payload->length;
  }
  tb_requests_chunk(&s_requests, (uint32_t)sequence, &response);
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
  (void)iter; (void)context;
  tb_requests_outbox_ready(&s_requests);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  (void)context;
  int32_t sequence = 0;
  if (integer(dict_find(iter, MESSAGE_KEY_REQUEST_SEQ), &sequence) && sequence > 0) {
    tb_requests_send_failed(&s_requests, (uint32_t)sequence, reason == APP_MSG_NOT_CONNECTED);
  }
  tb_requests_outbox_ready(&s_requests);
}

static bool link_error(int32_t error) { return error == TB_ERROR_PHONE_UNREACHABLE || error == TB_ERROR_NO_RESPONSE; }

static void phone_connection(bool connected) {
  if (!connected) { return; }
  if (!tb_session_busy(&s_session) && link_error(s_session.error)) {
    tb_session_retry(&s_session);
    return;
  }
  if (in_stack(s_chats_view.window) && link_error(s_chats.error)) { tb_chats_retry(&s_chats); }
  if (in_stack(s_history_view.window)) {
    if (link_error(s_history.error) || link_error(s_history.top_error)) { tb_history_retry(&s_history); }
    else if (link_error(s_history.refresh_error)) { tb_history_refresh(&s_history); }
  }
}

static void focus_changed(bool in_focus) {
  Window *top = window_stack_get_top_window();
  if (top == s_chats_view.window) { tb_chats_set_active(&s_chats, in_focus); }
  if (top == s_history_view.window) { tb_history_set_active(&s_history, in_focus); }
}

static void init(void) {
  s_strings = tb_localization_current();
  tb_theme_init();
  tb_requests_init(&s_requests, (TbRequestPorts){send_request, request_schedule, request_cancel, now_ms, NULL});
  tb_session_init(&s_session, &s_requests, (TbSessionPorts){session_changed, NULL});
  const TbChatsConfig chats_config = {TB_CHATS_CAPACITY, TB_CHATS_PAGE, TB_CHATS_TEXT, TB_CHATS_BUDGET, TB_DATA_TIMEOUT, 60000, 30000};
  const TbHistoryConfig history_config = {TB_HISTORY_CAPACITY, TB_HISTORY_PAGE, TB_HISTORY_TEXT, TB_HISTORY_BUDGET, TB_DATA_TIMEOUT, 60000, {3000, 8000}};
  const bool allocated = tb_chats_init(&s_chats, &s_requests, (TbViewPorts){chats_changed, chats_schedule, chats_cancel, now_ms, NULL}, chats_config) &&
                         tb_history_init(&s_history, &s_requests, (TbViewPorts){history_changed, history_schedule, history_cancel, now_ms, NULL},
                                         history_config);
  tb_message_text_init(&s_text, &s_requests, (TbViewPorts){text_changed, text_schedule, text_cancel, now_ms, NULL}, TB_FULL_TEXT, TB_DATA_TIMEOUT);
  tb_notice_init(&s_connect);
  tb_notice_init(&s_info);
  tb_accounts_window_init(&s_accounts, &s_session, s_strings, (TbAccountsActions){open_account, make_default, NULL});
  tb_chats_window_init(&s_chats_view, &s_chats, s_strings, (TbChatsActions){open_chat, NULL});
  tb_history_window_init(&s_history_view, &s_history, s_strings, (TbHistoryActions){open_message, history_closed, NULL});
  tb_reader_window_init(&s_reader, &s_text, s_strings);
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
    return;
  }
  tb_session_start(&s_session, opened);
}

static void deinit(void) {
  tb_session_stop(&s_session);
  tb_requests_cancel_all(&s_requests);
  app_message_deregister_callbacks();
  connection_service_unsubscribe();
  app_focus_service_unsubscribe();
  tb_message_text_close(&s_text);
  tb_history_deinit(&s_history);
  tb_chats_deinit(&s_chats);
  tb_reader_window_deinit(&s_reader);
  tb_history_window_deinit(&s_history_view);
  tb_chats_window_deinit(&s_chats_view);
  tb_accounts_window_deinit(&s_accounts);
  tb_notice_deinit(&s_info);
  tb_notice_deinit(&s_connect);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
