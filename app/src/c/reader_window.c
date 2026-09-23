#include "reader_window.h"
#include <stdlib.h>
#include <string.h>
#include "errors.h"
#include "format.h"
#include "generated/protocol.h"
#include "theme.h"

static const char *body_value(TbReaderWindow *view) {
  const TbMessageText *text = view->text;
  if (text->loading && text->length == 0) { return view->strings->loading; }
  if (text->length > 0) { return text->text; }
  return view->fallback ? view->fallback : "";
}

static void compose(TbReaderWindow *view) {
  const TbStrings *strings = view->strings;
  const TbMessage *message = &view->message;
  char stamp[24];
  char day[32];
  tb_format_time(stamp, sizeof(stamp), (time_t)message->date, (time_t)message->date, clock_is_24h_style());
  tb_format_day(day, sizeof(day), strings, (time_t)message->date, time(NULL));
  const char *sender = (message->flags & TB_MESSAGE_FLAG_OUTGOING) ? strings->you : view->private_chat ? "" : tb_or_empty(message->sender);
  char label[96] = "";
  if (message->kind != TB_KIND_TEXT) {
    tb_format_content(label, sizeof(label), strings, message->kind, message->action, message->duration, view->extra, "");
  }
  snprintf(view->header_text, sizeof(view->header_text), "%s%s%s %s%s%s%s%s", sender, *sender ? "\n" : "", day, stamp,
           (message->flags & TB_MESSAGE_FLAG_EDITED) ? " · " : "", (message->flags & TB_MESSAGE_FLAG_EDITED) ? strings->edited : "",
           *label ? "\n" : "", label);
  view->footer_text[0] = '\0';
  const TbMessageText *text = view->text;
  if (!text->loading && text->error != TB_ERROR_NONE) {
    snprintf(view->footer_text, sizeof(view->footer_text), "%s. %s", tb_error_text(strings, text->error),
             view->fallback && *view->fallback ? strings->preview_only : strings->retry_hint);
  } else if (!text->loading && text->truncated) {
    snprintf(view->footer_text, sizeof(view->footer_text), "%s", strings->continued_on_phone);
  }
}

static void layout(TbReaderWindow *view) {
  if (!view->scroll) { return; }
  compose(view);
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->window));
  const int16_t width = bounds.size.w - 2 * theme->margin - 8;
  const int16_t left = theme->margin + 4;
  const int16_t top = PBL_IF_ROUND_ELSE(24, 2);
  const char *body = body_value(view);
  text_layer_set_text(view->header, view->header_text);
  text_layer_set_text(view->body, body);
  text_layer_set_text(view->footer, view->footer_text);
  const int16_t header_h = tb_theme_text_height(view->header_text, theme->meta_bold, width, 200) + 4;
  const int16_t body_h = tb_theme_text_height(body, theme->body, width, 30000) + 8;
  const int16_t footer_h = view->footer_text[0] ? tb_theme_text_height(view->footer_text, theme->meta, width, 200) + 4 : 0;
  layer_set_frame(text_layer_get_layer(view->header), GRect(left, top, width, header_h));
  layer_set_frame(text_layer_get_layer(view->body), GRect(left, top + header_h, width, body_h));
  layer_set_frame(text_layer_get_layer(view->footer), GRect(left, top + header_h + body_h, width, footer_h));
  scroll_layer_set_content_size(view->scroll, GSize(bounds.size.w, top + header_h + body_h + footer_h + PBL_IF_ROUND_ELSE(40, 8)));
}

static void select_clicked(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbReaderWindow *view = context;
  if (view->text->error != TB_ERROR_NONE) { tb_message_text_retry(view->text); }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_clicked);
  window_set_click_context(BUTTON_ID_SELECT, context);
}

static TextLayer *text(GFont font, GColor color) {
  TextLayer *layer = text_layer_create(GRectZero);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, color);
  text_layer_set_font(layer, font);
  text_layer_set_overflow_mode(layer, GTextOverflowModeWordWrap);
  return layer;
}

static void window_load(Window *window) {
  TbReaderWindow *view = window_get_user_data(window);
  const TbTheme *theme = tb_theme();
  Layer *root = window_get_root_layer(window);
  view->scroll = scroll_layer_create(layer_get_bounds(root));
  scroll_layer_set_shadow_hidden(view->scroll, true);
  scroll_layer_set_callbacks(view->scroll, (ScrollLayerCallbacks){.click_config_provider = click_config});
  scroll_layer_set_context(view->scroll, view);
  scroll_layer_set_click_config_onto_window(view->scroll, window);
  view->header = text(theme->meta_bold, tb_theme_accent(false));
  view->body = text(theme->body, GColorBlack);
  view->footer = text(theme->meta, tb_theme_muted(false));
  scroll_layer_add_child(view->scroll, text_layer_get_layer(view->header));
  scroll_layer_add_child(view->scroll, text_layer_get_layer(view->body));
  scroll_layer_add_child(view->scroll, text_layer_get_layer(view->footer));
  layer_add_child(root, scroll_layer_get_layer(view->scroll));
#if defined(PBL_ROUND)
  scroll_layer_set_paging(view->scroll, true);
#endif
  layout(view);
}

static void window_unload(Window *window) {
  TbReaderWindow *view = window_get_user_data(window);
  tb_message_text_close(view->text);
  text_layer_destroy(view->footer);
  text_layer_destroy(view->body);
  text_layer_destroy(view->header);
  scroll_layer_destroy(view->scroll);
  view->footer = NULL;
  view->body = NULL;
  view->header = NULL;
  view->scroll = NULL;
  free(view->fallback);
  view->fallback = NULL;
}

void tb_reader_window_init(TbReaderWindow *view, TbMessageText *text, const TbStrings *strings) {
  memset(view, 0, sizeof(*view));
  view->text = text;
  view->strings = strings;
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_background_color(view->window, tb_theme_background());
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload});
}

void tb_reader_window_deinit(TbReaderWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_reader_window_show(TbReaderWindow *view, const TbMessage *message, bool private_chat) {
  free(view->fallback);
  view->fallback = NULL;
  view->message = *message;
  view->message.sender = view->sender;
  view->message.extra = NULL;
  view->message.text = NULL;
  view->private_chat = private_chat;
  snprintf(view->sender, sizeof(view->sender), "%s", tb_or_empty(message->sender));
  snprintf(view->extra, sizeof(view->extra), "%s", tb_or_empty(message->extra));
  const char *preview = tb_or_empty(message->text);
  view->fallback = malloc(strlen(preview) + 1);
  if (view->fallback) { strcpy(view->fallback, preview); }
}

void tb_reader_window_reload(TbReaderWindow *view) {
  if (!view->scroll) { return; }
  const GPoint offset = scroll_layer_get_content_offset(view->scroll);
  layout(view);
  scroll_layer_set_content_offset(view->scroll, offset, false);
}
