#include "status_view.h"
#include "generated/localization.h"
static TextLayer *s_title_layer;
static TextLayer *s_status_layer;
static TextLayer *s_hint_layer;
static const TbStrings *s_strings;
static TbStatus s_status = TB_STATUS_CHECKING;
static const char *status_text(TbStatus status) {
  switch (status) {
    case TB_STATUS_CONNECTED: return s_strings->connected;
    case TB_STATUS_CONFIG_MISSING: return s_strings->config_missing;
    case TB_STATUS_CONFIG_INVALID: return s_strings->config_invalid;
    case TB_STATUS_BACKEND_UNAVAILABLE: return s_strings->backend_unavailable;
    case TB_STATUS_API_UNAUTHORIZED: return s_strings->api_unauthorized;
    case TB_STATUS_BACKEND_NOT_READY: return s_strings->backend_not_ready;
    case TB_STATUS_PROTOCOL_ERROR: return s_strings->protocol_error;
    case TB_STATUS_PHONE_UNREACHABLE: return s_strings->phone_unreachable;
    default: return s_strings->checking;
  }
}
static bool retryable(TbStatus status) { return status != TB_STATUS_CHECKING && status != TB_STATUS_CONNECTED; }
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int title_y = PBL_IF_ROUND_ELSE(bounds.size.h / 4, bounds.size.h / 5);
  int status_y = PBL_IF_ROUND_ELSE(bounds.size.h / 2 - 10, bounds.size.h / 2 - 16);
  s_title_layer = text_layer_create(GRect(8, title_y, bounds.size.w - 16, 36));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorBlack);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, s_strings->title);
  layer_add_child(root, text_layer_get_layer(s_title_layer));
  s_status_layer = text_layer_create(GRect(12, status_y, bounds.size.w - 24, 64));
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_status_layer, GTextOverflowModeWordWrap);
  text_layer_set_text(s_status_layer, status_text(s_status));
  layer_add_child(root, text_layer_get_layer(s_status_layer));
  s_hint_layer = text_layer_create(GRect(8, bounds.size.h - PBL_IF_ROUND_ELSE(48, 30), bounds.size.w - 16, 24));
  text_layer_set_background_color(s_hint_layer, GColorClear);
  text_layer_set_text_color(s_hint_layer, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_text(s_hint_layer, s_strings->retry_hint);
  layer_set_hidden(text_layer_get_layer(s_hint_layer), !retryable(s_status));
  layer_add_child(root, text_layer_get_layer(s_hint_layer));
}
static void window_unload(Window *window) {
  (void)window;
  text_layer_destroy(s_hint_layer);
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_title_layer);
  s_hint_layer = NULL;
  s_status_layer = NULL;
  s_title_layer = NULL;
}
Window *tb_status_view_create(void) {
  s_strings = tb_localization_current();
  Window *window = window_create();
  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorPastelYellow, GColorWhite));
  window_set_window_handlers(window, (WindowHandlers){.load = window_load, .unload = window_unload});
  return window;
}
void tb_status_view_destroy(Window *window) { window_destroy(window); }
void tb_status_view_set(TbStatus status) {
  s_status = status;
  if (s_status_layer) { text_layer_set_text(s_status_layer, status_text(status)); }
  if (s_hint_layer) { layer_set_hidden(text_layer_get_layer(s_hint_layer), !retryable(status)); }
}
