#include "history_window.h"
#include <string.h>
#include "errors.h"
#include "format.h"
#include "generated/protocol.h"
#include "theme.h"

#define ROW_TOP 0
#define ROW_FIRST_MESSAGE 1

static uint16_t row_count(const TbHistoryWindow *view) { return (uint16_t)(view->history->count + 2); }
static bool is_message(const TbHistoryWindow *view, uint16_t row) { return row >= ROW_FIRST_MESSAGE && row < ROW_FIRST_MESSAGE + view->history->count; }
static bool is_bottom(const TbHistoryWindow *view, uint16_t row) { return row == ROW_FIRST_MESSAGE + view->history->count; }

static const char *error_line(TbHistoryWindow *view, int32_t error, const char *suffix) {
  const TbStrings *strings = view->strings;
  if (error == TB_RESULT_RATE_LIMITED && view->history->retry_after > 0) {
    char wait[48];
    snprintf(wait, sizeof(wait), strings->wait_seconds, (int)view->history->retry_after);
    snprintf(view->buffer, sizeof(view->buffer), "%s. %s", wait, suffix);
  } else {
    snprintf(view->buffer, sizeof(view->buffer), "%s. %s", tb_error_text(strings, error), suffix);
  }
  return view->buffer;
}

static const char *top_text(TbHistoryWindow *view) {
  const TbHistory *history = view->history;
  const TbStrings *strings = view->strings;
  if (!history->loaded) {
    if (history->error != TB_ERROR_NONE && history->op == TB_HISTORY_NONE) { return error_line(view, history->error, strings->retry_hint); }
    return strings->loading;
  }
  switch (history->top) {
    case TB_TOP_LOADING: return strings->loading;
    case TB_TOP_FAILED:
      return history->top_error != TB_ERROR_NONE ? error_line(view, history->top_error, strings->retry_hint) : strings->load_failed;
    case TB_TOP_WAITING: return strings->loading_telegram;
    case TB_TOP_START:
      if (history->count == 0) { return strings->no_messages; }
      snprintf(view->buffer, sizeof(view->buffer), "%s · %s", strings->history_start, strings->check_again);
      return view->buffer;
    default: return strings->earlier;
  }
}

static const char *bottom_text(TbHistoryWindow *view) {
  const TbHistory *history = view->history;
  const TbStrings *strings = view->strings;
  if (history->truncated) { return strings->newer_hidden; }
  if (history->op == TB_HISTORY_REFRESH && !history->refreshing_silently) { return strings->refreshing; }
  if (history->refresh_error != TB_ERROR_NONE) {
    static char reason[160];
    snprintf(reason, sizeof(reason), "%s: %s", strings->not_updated, tb_error_text(strings, history->refresh_error));
    snprintf(view->buffer, sizeof(view->buffer), "%s. %s", reason, strings->refresh);
    return view->buffer;
  }
  if (history->connection_not_ready) {
    snprintf(view->buffer, sizeof(view->buffer), "%s · %s", strings->telegram_connecting, strings->refresh);
    return view->buffer;
  }
  return strings->refresh;
}

static bool private_chat(const TbHistoryWindow *view) {
  return view->history->chat_type == TB_CHAT_TYPE_PRIVATE || view->history->chat_type == TB_CHAT_TYPE_SECRET;
}

static const char *sender_of(const TbHistoryWindow *view, const TbMessage *message) {
  if (message->flags & TB_MESSAGE_FLAG_OUTGOING) { return view->strings->you; }
  if (private_chat(view)) { return ""; }
  return tb_or_empty(message->sender);
}

static bool starts_day(const TbHistoryWindow *view, uint16_t index) {
  if (index == 0) { return true; }
  return !tb_same_day((time_t)view->history->items[index - 1].date, (time_t)view->history->items[index].date);
}

static char s_content[440];
static char s_text[480];

static void body_text(const TbHistoryWindow *view, const TbMessage *message, char *out, size_t size) {
  char *content = s_content;
  tb_format_content(content, sizeof(s_content), view->strings, message->kind, message->action, message->duration, message->extra, message->text);
  if (message->kind == TB_KIND_SERVICE && message->sender && message->action != TB_ACTION_CUSTOM && message->action != TB_ACTION_TITLE_CHANGED &&
      message->action != TB_ACTION_CHAT_CREATED) {
    snprintf(out, size, "%s %s", message->sender, content);
  } else {
    snprintf(out, size, "%s", content);
  }
}

static int16_t inset(void) { return (int16_t)(tb_theme()->margin + PBL_IF_ROUND_ELSE(12, 4)); }

static int16_t content_width(const TbHistoryWindow *view) { return (int16_t)(view->width - 2 * inset() - 2); }

static int16_t text_limit(const TbHistoryWindow *view) {
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->window));
  return (int16_t)(bounds.size.h * 2 / 3);
}

static int16_t message_height(TbHistoryWindow *view, uint16_t index) {
  TbMessage *message = &view->history->items[index];
  const TbTheme *theme = tb_theme();
  const int16_t day = starts_day(view, index) ? theme->meta_height : 0;
  if (message->height < 0) {
    body_text(view, message, s_text, sizeof(s_text));
    message->height = (int16_t)(tb_theme_text_height(s_text, theme->body, content_width(view), text_limit(view)) + 6);
  }
  return (int16_t)(day + theme->meta_height + message->height);
}

static uint16_t rows(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu; (void)section;
  return row_count(context);
}

static int16_t row_height(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbHistoryWindow *view = context;
  const TbTheme *theme = tb_theme();
  int16_t height;
  if (is_message(view, index->row)) {
    height = message_height(view, (uint16_t)(index->row - ROW_FIRST_MESSAGE));
  } else {
    const char *text = index->row == ROW_TOP ? top_text(view) : bottom_text(view);
    height = (int16_t)(tb_theme_text_height(text, theme->meta_bold, content_width(view), text_limit(view)) + 12);
  }
  return height > theme->row_min ? height : theme->row_min;
}

static void draw_message(TbHistoryWindow *view, GContext *ctx, const Layer *cell, uint16_t index) {
  const TbMessage *message = &view->history->items[index];
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const int16_t left = inset();
  const int16_t width = content_width(view);
  const bool outgoing = (message->flags & TB_MESSAGE_FLAG_OUTGOING) != 0;
  int16_t y = 0;
  if (starts_day(view, index)) {
    static char day[32];
    tb_format_day(day, sizeof(day), view->strings, (time_t)message->date, time(NULL));
    graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
    graphics_draw_text(ctx, day, theme->meta_bold, GRect(left, y - 2, width, theme->meta_height), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    y = (int16_t)(y + theme->meta_height);
  }
  static char stamp[24];
  tb_format_time(stamp, sizeof(stamp), (time_t)message->date, (time_t)message->date, clock_is_24h_style());
  static char meta[48];
  snprintf(meta, sizeof(meta), "%s%s", (message->flags & TB_MESSAGE_FLAG_EDITED) ? "* " : "", stamp);
  graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
  graphics_draw_text(ctx, meta, theme->meta, GRect(left, y - 2, width, theme->meta_height), GTextOverflowModeTrailingEllipsis,
                     outgoing ? GTextAlignmentLeft : GTextAlignmentRight, NULL);
  const char *sender = sender_of(view, message);
  if (*sender) {
    graphics_context_set_text_color(ctx, outgoing ? tb_theme_muted(highlighted) : tb_theme_accent(highlighted));
    graphics_draw_text(ctx, sender, theme->meta_bold, GRect(left + (outgoing ? 40 : 0), y - 2, width - 40, theme->meta_height),
                       GTextOverflowModeTrailingEllipsis, outgoing ? GTextAlignmentRight : GTextAlignmentLeft, NULL);
  }
  y = (int16_t)(y + theme->meta_height);
  body_text(view, message, s_text, sizeof(s_text));
  graphics_context_set_text_color(ctx, tb_theme_text(highlighted));
  graphics_draw_text(ctx, s_text, theme->body, GRect(left, y - 4, width, bounds.size.h - y), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  graphics_context_set_fill_color(ctx, highlighted ? GColorWhite : tb_theme_accent(false));
  if (outgoing) {
    graphics_fill_rect(ctx, GRect(bounds.size.w - inset() + 2, 2, 3, bounds.size.h - 4), 0, GCornerNone);
  }
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  TbHistoryWindow *view = context;
  if (is_message(view, index->row)) {
    draw_message(view, ctx, cell, (uint16_t)(index->row - ROW_FIRST_MESSAGE));
    return;
  }
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const char *text = index->row == ROW_TOP ? top_text(view) : bottom_text(view);
  graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
  graphics_draw_text(ctx, text, theme->meta_bold, GRect(theme->margin + 4, 2, bounds.size.w - 2 * theme->margin - 8, bounds.size.h - 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void remember(TbHistoryWindow *view, uint16_t row) {
  view->selected_row = row;
  view->follow_bottom = row + 2 >= row_count(view);
  if (is_message(view, row)) {
    strncpy(view->selected_id, view->history->items[row - ROW_FIRST_MESSAGE].id, sizeof(view->selected_id) - 1);
    view->selected_id[sizeof(view->selected_id) - 1] = '\0';
  } else {
    view->selected_id[0] = '\0';
  }
  view->anchor_id[0] = '\0';
  if (row == ROW_TOP && view->history->count > 0) {
    strncpy(view->anchor_id, view->history->items[0].id, sizeof(view->anchor_id) - 1);
    view->anchor_id[sizeof(view->anchor_id) - 1] = '\0';
  }
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbHistoryWindow *view = context;
  remember(view, index->row);
  if (is_message(view, index->row)) {
    view->actions.activate(view->actions.context, index->row - ROW_FIRST_MESSAGE);
  } else if (index->row == ROW_TOP) {
    if (!view->history->loaded) { tb_history_retry(view->history); }
    else { tb_history_load_older(view->history); }
  } else if (is_bottom(view, index->row)) {
    tb_history_refresh(view->history);
  }
}

static void select_long_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu; (void)index;
  TbHistoryWindow *view = context;
  tb_history_refresh(view->history);
}

static void selection_changed(MenuLayer *menu, MenuIndex now, MenuIndex before, void *context) {
  (void)menu; (void)before;
  TbHistoryWindow *view = context;
  remember(view, now.row);
  if (now.row == ROW_TOP && view->history->loaded && view->history->top == TB_TOP_MORE) { tb_history_load_older(view->history); }
}

static void restore(TbHistoryWindow *view) {
  if (!view->menu) { return; }
  const uint16_t total = row_count(view);
  uint16_t row = view->selected_row;
  const int found = view->selected_id[0] ? tb_history_find(view->history, view->selected_id) : -1;
  if (!view->placed && view->history->count > 0) {
    row = (uint16_t)(view->history->count);
    view->placed = true;
  } else if (view->follow_bottom && view->history->count > 0) {
    row = (uint16_t)(view->history->count);
  } else if (found >= 0) {
    row = (uint16_t)(found + ROW_FIRST_MESSAGE);
  } else if (row == ROW_TOP && view->anchor_id[0] && view->history->op == TB_HISTORY_NONE) {
    const int anchor = tb_history_find(view->history, view->anchor_id);
    if (anchor > 0) { row = (uint16_t)anchor; }
    view->anchor_id[0] = '\0';
  }
  if (row >= total) { row = total - 1; }
  view->selected_row = row;
  menu_layer_set_selected_index(view->menu, (MenuIndex){0, row}, MenuRowAlignCenter, false);
  if (is_message(view, row)) {
    strncpy(view->selected_id, view->history->items[row - ROW_FIRST_MESSAGE].id, sizeof(view->selected_id) - 1);
    view->selected_id[sizeof(view->selected_id) - 1] = '\0';
  }
}

static void window_load(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);
  view->width = bounds.size.w;
  view->menu = menu_layer_create(bounds);
  tb_theme_menu(view->menu);
  menu_layer_set_callbacks(view->menu, view, (MenuLayerCallbacks){
    .get_num_rows = rows,
    .get_cell_height = row_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
    .selection_changed = selection_changed,
  });
  menu_layer_set_click_config_onto_window(view->menu, window);
  layer_add_child(root, menu_layer_get_layer(view->menu));
  restore(view);
}

static void window_unload(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  menu_layer_destroy(view->menu);
  view->menu = NULL;
  if (view->actions.closed) { view->actions.closed(view->actions.context); }
}

static void window_appear(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  tb_history_set_active(view->history, true);
}

static void window_disappear(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  tb_history_set_active(view->history, false);
}

void tb_history_window_init(TbHistoryWindow *view, TbHistory *history, const TbStrings *strings, TbHistoryActions actions) {
  view->history = history;
  view->strings = strings;
  view->actions = actions;
  tb_history_window_reset(view);
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload, .appear = window_appear,
                                                             .disappear = window_disappear});
}

void tb_history_window_deinit(TbHistoryWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_history_window_reset(TbHistoryWindow *view) {
  view->selected_id[0] = '\0';
  view->anchor_id[0] = '\0';
  view->selected_row = 0;
  view->follow_bottom = true;
  view->placed = false;
}

void tb_history_window_reload(TbHistoryWindow *view) {
  if (!view->menu) { return; }
  menu_layer_reload_data(view->menu);
  restore(view);
}
