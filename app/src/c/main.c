#include <pebble.h>
#include "generated/protocol.h"
#include "status_state.h"
#include "status_view.h"

#define TB_HELLO_TIMEOUT_MS 1500
#define TB_STATUS_TIMEOUT_MS 14000
#define TB_HELLO_MAX_ATTEMPTS 3
#define TB_STATUS_MAX_ATTEMPTS 2

static Window *s_main_window;
static TbStatusState s_state;
static AppTimer *s_timer;
static bool s_waiting_ready;
static bool s_waiting_status;
static uint8_t s_hello_attempts;
static uint8_t s_status_attempts;

static void send_hello(void);
static void send_status_request(void);

static void cancel_timer(void) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}

static void finish(int32_t code) {
  cancel_timer();
  s_waiting_ready = false;
  s_waiting_status = false;
  tb_status_state_apply(&s_state, s_state.sequence, code);
  tb_status_view_set(s_state.status);
}

static void on_timeout(void *context) {
  (void)context;
  s_timer = NULL;
  if (s_waiting_ready) {
    if (s_hello_attempts < TB_HELLO_MAX_ATTEMPTS) { send_hello(); }
    else { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
  } else if (s_waiting_status) {
    if (s_status_attempts < TB_STATUS_MAX_ATTEMPTS) { send_status_request(); }
    else { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
  }
}

static bool send_request(uint8_t kind, uint32_t sequence) {
  DictionaryIterator *out = NULL;
  if (app_message_outbox_begin(&out) != APP_MSG_OK || !out) { return false; }
  if (dict_write_uint8(out, MESSAGE_KEY_REQUEST_KIND, kind) != DICT_OK ||
      dict_write_int(out, MESSAGE_KEY_REQUEST_SEQ, &sequence, sizeof(sequence), false) != DICT_OK) {
    return false;
  }
  return app_message_outbox_send() == APP_MSG_OK;
}

static void send_hello(void) {
  ++s_hello_attempts;
  if (!send_request(TB_REQUEST_HELLO, s_state.sequence)) {
    if (s_hello_attempts < TB_HELLO_MAX_ATTEMPTS) { send_hello(); }
    else { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
    return;
  }
  cancel_timer();
  s_timer = app_timer_register(TB_HELLO_TIMEOUT_MS, on_timeout, NULL);
  if (!s_timer) { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
}

static void send_status_request(void) {
  cancel_timer();
  s_waiting_ready = false;
  s_waiting_status = true;
  ++s_status_attempts;
  uint32_t sequence = tb_status_state_begin(&s_state);
  tb_status_view_set(s_state.status);
  if (!send_request(TB_REQUEST_STATUS, sequence)) {
    if (s_status_attempts < TB_STATUS_MAX_ATTEMPTS) { send_status_request(); }
    else { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
    return;
  }
  s_timer = app_timer_register(TB_STATUS_TIMEOUT_MS, on_timeout, NULL);
  if (!s_timer) { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
}

static bool int32_tuple(const Tuple *tuple) {
  return tuple && tuple->type == TUPLE_INT && tuple->length == sizeof(int32_t);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  Tuple *kind = dict_find(iter, MESSAGE_KEY_RESPONSE_KIND);
  Tuple *sequence = dict_find(iter, MESSAGE_KEY_REQUEST_SEQ);
  Tuple *result = dict_find(iter, MESSAGE_KEY_RESULT_CODE);
  if (!int32_tuple(kind) || !int32_tuple(sequence) || !int32_tuple(result)) {
    if (s_waiting_status || s_waiting_ready) { finish(TB_RESULT_PROTOCOL_ERROR); }
    return;
  }
  int32_t kind_value = kind->value->int32;
  int32_t sequence_value = sequence->value->int32;
  if (kind_value == TB_RESPONSE_READY && s_waiting_ready &&
      sequence_value > 0 && (uint32_t)sequence_value == s_state.sequence) {
    s_status_attempts = 0;
    send_status_request();
  } else if (kind_value == TB_RESPONSE_REFRESH && sequence_value == 0 && !s_waiting_ready) {
    s_status_attempts = 0;
    send_status_request();
  } else if (kind_value == TB_RESPONSE_STATUS && s_waiting_status &&
             sequence_value > 0 && (uint32_t)sequence_value == s_state.sequence) {
    finish(result->value->int32);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  (void)reason; (void)context;
  // The outstanding request still has a bounded timeout and one retry.
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  (void)reason; (void)context;
  Tuple *kind = dict_find(iter, MESSAGE_KEY_REQUEST_KIND);
  Tuple *sequence = dict_find(iter, MESSAGE_KEY_REQUEST_SEQ);
  if (!kind || kind->type != TUPLE_UINT || kind->length != sizeof(uint8_t) ||
      !sequence || sequence->type != TUPLE_UINT || sequence->length != sizeof(uint32_t) ||
      sequence->value->uint32 != s_state.sequence) { return; }
  cancel_timer();
  if (kind->value->uint8 == TB_REQUEST_HELLO && s_waiting_ready) {
    if (s_hello_attempts < TB_HELLO_MAX_ATTEMPTS) { send_hello(); }
    else { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
  } else if (kind->value->uint8 == TB_REQUEST_STATUS && s_waiting_status) {
    if (s_status_attempts < TB_STATUS_MAX_ATTEMPTS) { send_status_request(); }
    else { finish(TB_RESULT_BACKEND_UNAVAILABLE); }
  }
}

static void init(void) {
  tb_status_state_init(&s_state);
  s_main_window = tb_status_view_create();
  window_stack_push(s_main_window, true);
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  if (app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum()) != APP_MSG_OK) {
    tb_status_state_begin(&s_state);
    finish(TB_RESULT_BACKEND_UNAVAILABLE);
    return;
  }
  tb_status_state_begin(&s_state);
  s_waiting_ready = true;
  send_hello();
}

static void deinit(void) {
  cancel_timer();
  app_message_deregister_callbacks();
  tb_status_view_destroy(s_main_window);
}

int main(void) { init(); app_event_loop(); deinit(); }
