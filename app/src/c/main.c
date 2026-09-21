#include <pebble.h>
#include "generated/protocol.h"
#include "status_session.h"
#include "status_view.h"

static Window *s_main_window;
static AppTimer *s_timer;
static TbStatusSession s_session;

static void cancel_timer(void *context) {
  (void)context;
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
}
static void on_timeout(void *context) {
  (void)context;
  s_timer = NULL;
  tb_status_session_timeout(&s_session);
}
static bool schedule(void *context, uint32_t milliseconds) {
  (void)context;
  s_timer = app_timer_register(milliseconds, on_timeout, NULL);
  return s_timer != NULL;
}
static void render(void *context, TbStatus status) { (void)context; tb_status_view_set(status); }
static bool send_request(void *context, uint8_t kind, uint32_t sequence) {
  (void)context;
  DictionaryIterator *out = NULL;
  if (app_message_outbox_begin(&out) != APP_MSG_OK || !out) { return false; }
  if (dict_write_uint8(out, MESSAGE_KEY_REQUEST_KIND, kind) != DICT_OK ||
      dict_write_int(out, MESSAGE_KEY_REQUEST_SEQ, &sequence, sizeof(sequence), false) != DICT_OK) { return false; }
  return app_message_outbox_send() == APP_MSG_OK;
}
static bool int32_tuple(const Tuple *tuple) { return tuple && tuple->type == TUPLE_INT && tuple->length == sizeof(int32_t); }
static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  Tuple *kind = dict_find(iter, MESSAGE_KEY_RESPONSE_KIND);
  Tuple *sequence = dict_find(iter, MESSAGE_KEY_REQUEST_SEQ);
  Tuple *result = dict_find(iter, MESSAGE_KEY_RESULT_CODE);
  const bool valid = int32_tuple(kind) && int32_tuple(sequence) && int32_tuple(result);
  tb_status_session_receive(&s_session, valid, valid ? kind->value->int32 : 0, valid ? sequence->value->int32 : 0, valid ? result->value->int32 : 0);
}
static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  (void)reason; (void)context;
  Tuple *kind = dict_find(iter, MESSAGE_KEY_REQUEST_KIND);
  Tuple *sequence = dict_find(iter, MESSAGE_KEY_REQUEST_SEQ);
  if (!kind || kind->type != TUPLE_UINT || kind->length != sizeof(uint8_t) || !sequence || sequence->type != TUPLE_UINT || sequence->length != sizeof(uint32_t)) { return; }
  tb_status_session_failed(&s_session, kind->value->uint8, sequence->value->uint32);
}
static void init(void) {
  const TbStatusPorts ports = {send_request, schedule, cancel_timer, render, NULL};
  tb_status_session_init(&s_session, ports);
  s_main_window = tb_status_view_create();
  window_stack_push(s_main_window, true);
  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_failed(outbox_failed);
  tb_status_session_start(&s_session, app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum()) == APP_MSG_OK);
}
static void deinit(void) {
  tb_status_session_stop(&s_session);
  app_message_deregister_callbacks();
  tb_status_view_destroy(s_main_window);
}
int main(void) { init(); app_event_loop(); deinit(); }
