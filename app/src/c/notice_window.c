#include "notice_window.h"
#include "theme.h"

static void select_clicked(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbNotice *notice = context;
  if (notice->action) { notice->action(notice->context); }
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_clicked);
  window_set_click_context(BUTTON_ID_SELECT, context);
}

static void layout(TbNotice *notice) {
  if (!notice->scroll) { return; }
  const TbTheme *theme = tb_theme();
  Layer *root = window_get_root_layer(notice->window);
  const GRect bounds = layer_get_bounds(root);
  const int16_t width = bounds.size.w - 2 * theme->margin - 8;
  const int16_t top = PBL_IF_ROUND_ELSE(bounds.size.h / 5, 6);
  text_layer_set_text(notice->title, notice->title_text ? notice->title_text : "");
  text_layer_set_text(notice->body, notice->body_text);
  text_layer_set_text(notice->hint, notice->hint_text ? notice->hint_text : "");
  const int16_t title_h = tb_theme_text_height(notice->title_text, theme->title, width, 200) + 4;
  const int16_t body_h = tb_theme_text_height(notice->body_text, theme->body, width, 2000) + 8;
  const int16_t hint_h = tb_theme_text_height(notice->hint_text, theme->meta, width, 200) + 4;
  layer_set_frame(text_layer_get_layer(notice->title), GRect(theme->margin + 4, top, width, title_h));
  layer_set_frame(text_layer_get_layer(notice->body), GRect(theme->margin + 4, top + title_h, width, body_h));
  layer_set_frame(text_layer_get_layer(notice->hint), GRect(theme->margin + 4, top + title_h + body_h, width, hint_h));
  scroll_layer_set_content_size(notice->scroll, GSize(bounds.size.w, top + title_h + body_h + hint_h + PBL_IF_ROUND_ELSE(bounds.size.h / 4, 8)));
  scroll_layer_set_content_offset(notice->scroll, GPointZero, false);
}

static TextLayer *text(GFont font, GTextAlignment alignment, GColor color) {
  TextLayer *layer = text_layer_create(GRectZero);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, color);
  text_layer_set_font(layer, font);
  text_layer_set_text_alignment(layer, alignment);
  text_layer_set_overflow_mode(layer, GTextOverflowModeWordWrap);
  return layer;
}

static void window_load(Window *window) {
  TbNotice *notice = window_get_user_data(window);
  const TbTheme *theme = tb_theme();
  Layer *root = window_get_root_layer(window);
  notice->scroll = scroll_layer_create(layer_get_bounds(root));
  scroll_layer_set_shadow_hidden(notice->scroll, true);
  scroll_layer_set_callbacks(notice->scroll, (ScrollLayerCallbacks){.click_config_provider = click_config});
  scroll_layer_set_context(notice->scroll, notice);
  scroll_layer_set_click_config_onto_window(notice->scroll, window);
  const GTextAlignment alignment = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  notice->title = text(theme->title, alignment, GColorBlack);
  notice->body = text(theme->body, alignment, GColorBlack);
  notice->hint = text(theme->meta, alignment, tb_theme_muted(false));
  scroll_layer_add_child(notice->scroll, text_layer_get_layer(notice->title));
  scroll_layer_add_child(notice->scroll, text_layer_get_layer(notice->body));
  scroll_layer_add_child(notice->scroll, text_layer_get_layer(notice->hint));
  layer_add_child(root, scroll_layer_get_layer(notice->scroll));
  layout(notice);
}

static void window_unload(Window *window) {
  TbNotice *notice = window_get_user_data(window);
  text_layer_destroy(notice->hint);
  text_layer_destroy(notice->body);
  text_layer_destroy(notice->title);
  scroll_layer_destroy(notice->scroll);
  notice->hint = NULL;
  notice->body = NULL;
  notice->title = NULL;
  notice->scroll = NULL;
}

void tb_notice_init(TbNotice *notice) {
  notice->window = window_create();
  window_set_user_data(notice->window, notice);
  window_set_background_color(notice->window, tb_theme_background());
  window_set_window_handlers(notice->window, (WindowHandlers){.load = window_load, .unload = window_unload});
}

void tb_notice_deinit(TbNotice *notice) {
  window_destroy(notice->window);
  notice->window = NULL;
}

void tb_notice_set(TbNotice *notice, const char *title, const char *body, const char *hint, TbNoticeAction action, void *context) {
  notice->title_text = title;
  notice->hint_text = hint;
  notice->action = action;
  notice->context = context;
  snprintf(notice->body_text, sizeof(notice->body_text), "%s", body ? body : "");
  layout(notice);
}

void tb_notice_set_body(TbNotice *notice, const char *first, const char *second) {
  snprintf(notice->body_text, sizeof(notice->body_text), "%s%s%s", first ? first : "", second && *second ? "\n" : "",
           second ? second : "");
  layout(notice);
}
