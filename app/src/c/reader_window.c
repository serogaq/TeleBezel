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

static const char *quote_value(TbReaderWindow *view) {
  const TbMessageText *quote = view->quote;
  if (quote->length > 0) { return quote->text; }
  const size_t prefix = strlen(view->reply_sender);
  const char *snippet = view->reply;
  if (prefix && strncmp(snippet, view->reply_sender, prefix) == 0 && strncmp(snippet + prefix, ": ", 2) == 0) { snippet += prefix + 2; }
  if (!*snippet && quote->loading) { return view->strings->loading; }
  return snippet;
}

static void compose(TbReaderWindow *view) {
  const TbStrings *strings = view->strings;
  const TbMessage *message = &view->message;
  char stamp[24];
  char day[32];
  tb_format_time(stamp, sizeof(stamp), (time_t)message->date, (time_t)message->date, clock_is_24h_style());
  tb_format_day(day, sizeof(day), strings, (time_t)message->date, time(NULL));
  char forward[96] = "";
  if (view->forward[0]) { snprintf(forward, sizeof(forward), strings->forwarded_from, view->forward); }
  char label[96] = "";
  if (message->kind != TB_KIND_TEXT) {
    tb_format_content(label, sizeof(label), strings, message->kind, message->action, message->duration, view->extra, "");
  }
  snprintf(view->header_text, sizeof(view->header_text), "%s%s%s%s%s %s%s%s%s%s", view->sender, view->sender[0] ? "\n" : "", forward,
           *forward ? "\n" : "", day, stamp, (message->flags & TB_MESSAGE_FLAG_EDITED) ? " · " : "",
           (message->flags & TB_MESSAGE_FLAG_EDITED) ? strings->edited : "", *label ? "\n" : "", label);
  view->quote_label_text[0] = '\0';
  if (view->reply[0] || view->reply_sender[0]) {
    snprintf(view->quote_label_text, sizeof(view->quote_label_text), "%s%s%s", strings->in_reply_to, view->reply_sender[0] ? " " : "",
             view->reply_sender);
  }
  view->footer_text[0] = '\0';
  const TbMessageText *text = view->text;
  if (!text->loading && text->error != TB_ERROR_NONE) {
    snprintf(view->footer_text, sizeof(view->footer_text), "%s. %s", tb_error_text(strings, text->error),
             view->fallback && *view->fallback ? strings->preview_only : strings->menu_hint);
  } else if (!text->loading && text->truncated) {
    snprintf(view->footer_text, sizeof(view->footer_text), "%s", strings->continued_on_phone);
  }
}

static void layout(TbReaderWindow *view) {
  if (!view->scroll) { return; }
  compose(view);
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->window));
  const bool quoted = view->quote_label_text[0] != '\0';
  TbPageLine lines[TB_PAGE_MAX_LINES] = {{view->header, view->header_text, theme->meta_bold, 200, 4},
                                         {view->quote_label, view->quote_label_text, theme->meta_bold, 200, 0},
                                         {view->quote_body, quoted ? quote_value(view) : "", theme->meta, 30000, 8},
                                         {view->body, body_value(view), theme->body, 30000, 8},
                                         {view->footer, view->footer_text, theme->meta, 200, 4}};
  const int16_t bottom = tb_theme_page(bounds, lines, TB_PAGE_MAX_LINES, PBL_IF_ROUND_ELSE(36, 2));
  scroll_layer_set_content_size(view->scroll, GSize(bounds.size.w, bottom + PBL_IF_ROUND_ELSE(40, 8)));
}

static void select_clicked(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbReaderWindow *view = context;
  if (view->actions.menu) { view->actions.menu(view->actions.context); }
}

static void select_held(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbReaderWindow *view = context;
  if (view->text->error != TB_ERROR_NONE) { tb_message_text_retry(view->text); }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_clicked);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_held, NULL);
  window_set_click_context(BUTTON_ID_SELECT, context);
}

static TextLayer *text(GFont font, GColor color) {
  TextLayer *layer = text_layer_create(GRectZero);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, color);
  text_layer_set_font(layer, font);
  text_layer_set_overflow_mode(layer, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(layer, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
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
  view->quote_label = text(theme->meta_bold, tb_theme_muted(false));
  view->quote_body = text(theme->meta, tb_theme_muted(false));
  view->body = text(theme->body, GColorBlack);
  view->footer = text(theme->meta, tb_theme_muted(false));
  scroll_layer_add_child(view->scroll, text_layer_get_layer(view->header));
  scroll_layer_add_child(view->scroll, text_layer_get_layer(view->quote_label));
  scroll_layer_add_child(view->scroll, text_layer_get_layer(view->quote_body));
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
  tb_message_text_close(view->quote);
  text_layer_destroy(view->footer);
  text_layer_destroy(view->body);
  text_layer_destroy(view->quote_body);
  text_layer_destroy(view->quote_label);
  text_layer_destroy(view->header);
  scroll_layer_destroy(view->scroll);
  view->footer = NULL;
  view->body = NULL;
  view->quote_body = NULL;
  view->quote_label = NULL;
  view->header = NULL;
  view->scroll = NULL;
  free(view->fallback);
  view->fallback = NULL;
}

void tb_reader_window_init(TbReaderWindow *view, TbMessageText *text, TbMessageText *quote, const TbStrings *strings, TbReaderActions actions) {
  memset(view, 0, sizeof(*view));
  view->text = text;
  view->quote = quote;
  view->strings = strings;
  view->actions = actions;
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_background_color(view->window, tb_theme_background());
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload});
}

void tb_reader_window_deinit(TbReaderWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_reader_window_show(TbReaderWindow *view, const TbMessage *message, const char *sender, const char *forward) {
  free(view->fallback);
  view->fallback = NULL;
  view->message = *message;
  view->message.sender = view->sender;
  view->message.extra = NULL;
  view->message.text = NULL;
  view->message.reply = NULL;
  view->message.forward = NULL;
  view->message.reply_id = NULL;
  view->message.reply_sender = NULL;
  snprintf(view->reply, sizeof(view->reply), "%s", tb_or_empty(message->reply));
  snprintf(view->sender, sizeof(view->sender), "%s", tb_or_empty(sender));
  snprintf(view->forward, sizeof(view->forward), "%s", tb_or_empty(forward));
  snprintf(view->reply_sender, sizeof(view->reply_sender), "%s", tb_or_empty(message->reply_sender));
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
