#include "history_window.h"
#include <string.h>
#include "diag.h"
#include "errors.h"
#include "format.h"
#include "generated/protocol.h"
#include "icons.h"
#include "scratch.h"
#include "theme.h"

#define ROW_TOP 0
#define ROW_FIRST_MESSAGE 1
#define PULL_DISTANCE 60
#define PULL_MAX PBL_IF_ROUND_ELSE(30, 26)

bool tb_history_window_writable(const TbHistoryWindow *view) {
  return view->send == TB_CAN_SEND_ALLOWED || view->send == TB_CAN_SEND_UNKNOWN;
}

static bool has_status(const TbHistoryWindow *view) {
  const TbHistory *history = view->history;
  return history->truncated || (history->op == TB_HISTORY_REFRESH && !history->refreshing_silently) || history->refresh_error != TB_ERROR_NONE ||
         history->connection_not_ready;
}
static uint16_t tail_rows(const TbHistoryWindow *view) { return (uint16_t)((has_status(view) ? 1 : 0) + (tb_history_window_writable(view) ? 1 : 0)); }
static uint16_t row_count(const TbHistoryWindow *view) { return (uint16_t)(view->history->count + 1 + tail_rows(view)); }
static bool is_message(const TbHistoryWindow *view, uint16_t row) { return row >= ROW_FIRST_MESSAGE && row < ROW_FIRST_MESSAGE + view->history->count; }
static bool is_refresh(const TbHistoryWindow *view, uint16_t row) { return has_status(view) && row == ROW_FIRST_MESSAGE + view->history->count; }
static bool is_write(const TbHistoryWindow *view, uint16_t row) {
  return tb_history_window_writable(view) && row == ROW_FIRST_MESSAGE + view->history->count + (has_status(view) ? 1 : 0);
}
static uint16_t last_row(const TbHistoryWindow *view) { return (uint16_t)(row_count(view) - 1); }
static uint16_t newest_row(const TbHistoryWindow *view) { return view->history->count ? view->history->count : ROW_FIRST_MESSAGE; }

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
    char *reason = tb_scratch(TB_SCRATCH_SHORT);
    snprintf(reason, tb_scratch_size(TB_SCRATCH_SHORT), "%s: %s", strings->not_updated, tb_error_text(strings, history->refresh_error));
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
  return !view->history->saved && (view->history->chat_type == TB_CHAT_TYPE_PRIVATE || view->history->chat_type == TB_CHAT_TYPE_SECRET);
}

static const char *sender_of(const TbHistoryWindow *view, const TbMessage *message) {
  if (view->history->saved && message->forward && *message->forward) { return ""; }
  if (message->flags & TB_MESSAGE_FLAG_OUTGOING) { return view->strings->you; }
  if (private_chat(view)) { return ""; }
  return tb_or_empty(message->sender);
}

static bool starts_day(const TbHistoryWindow *view, uint16_t index) {
  if (index == 0) { return true; }
  return !tb_same_day((time_t)view->history->items[index - 1].date, (time_t)view->history->items[index].date);
}

static void body_text(const TbHistoryWindow *view, const TbMessage *message, char *out, size_t size) {
  char *content = tb_scratch(TB_SCRATCH_CONTENT);
  tb_format_content(content, tb_scratch_size(TB_SCRATCH_CONTENT), view->strings, message->kind, message->action, message->duration, message->extra, message->text);
  if (message->kind == TB_KIND_SERVICE && message->sender && message->action != TB_ACTION_CUSTOM && message->action != TB_ACTION_TITLE_CHANGED &&
      message->action != TB_ACTION_CHAT_CREATED) {
    snprintf(out, size, "%s %s", message->sender, content);
  } else {
    snprintf(out, size, "%s", content);
  }
}

static int16_t inset(void) { return (int16_t)(tb_theme()->margin + PBL_IF_ROUND_ELSE(12, 4)); }

static int16_t content_width(const TbHistoryWindow *view) { return (int16_t)(view->width - 2 * inset() - 2); }

static int16_t marker_size(void) { return 12; }

static int16_t text_limit(const TbHistoryWindow *view) {
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->window));
  return (int16_t)(bounds.size.h * 2 / 3);
}

static bool has_forward(const TbMessage *message) { return message->forward && *message->forward; }

static bool has_quote(const TbMessage *message) { return (message->flags & TB_MESSAGE_FLAG_REPLY) && message->reply && *message->reply; }

static int16_t mark_line(GContext *ctx, void (*icon)(GContext *, GRect, GColor), const char *text, GColor color, int16_t left, int16_t width, int16_t y) {
  const TbTheme *theme = tb_theme();
  const int16_t marker = marker_size();
  icon(ctx, GRect(left, (int16_t)(y + (theme->meta_height - marker) / 2), marker, marker), color);
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, theme->meta, GRect(left + marker + 4, y - 2, width - marker - 4, theme->meta_height), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  return (int16_t)(y + theme->meta_height);
}

static int16_t message_height(TbHistoryWindow *view, uint16_t index) {
  TbMessage *message = &view->history->items[index];
  const TbTheme *theme = tb_theme();
  const int16_t day = starts_day(view, index) ? theme->meta_height : 0;
  if (message->height < 0) {
    body_text(view, message, tb_scratch(TB_SCRATCH_TEXT), tb_scratch_size(TB_SCRATCH_TEXT));
    message->height = (int16_t)(tb_theme_text_height(tb_scratch(TB_SCRATCH_TEXT), theme->body, content_width(view), text_limit(view)) + 6);
  }
  const int16_t extra = (int16_t)((has_forward(message) ? theme->meta_height : 0) + (has_quote(message) ? theme->meta_height : 0));
  return (int16_t)(day + theme->meta_height + extra + message->height);
}

static uint16_t rows(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu; (void)section;
  return row_count(context);
}

static const char *tail_text(TbHistoryWindow *view, uint16_t row) { return is_write(view, row) ? view->strings->write : bottom_text(view); }

static int16_t row_height(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbHistoryWindow *view = context;
  const TbTheme *theme = tb_theme();
  int16_t height;
  if (is_message(view, index->row)) {
    height = message_height(view, (uint16_t)(index->row - ROW_FIRST_MESSAGE));
  } else {
    const char *text = index->row == ROW_TOP ? top_text(view) : tail_text(view, index->row);
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
  const int16_t marker = marker_size();
  int16_t y = 0;
  if (starts_day(view, index)) {
    char day[32];
    tb_format_day(day, sizeof(day), view->strings, (time_t)message->date, time(NULL));
    graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
    graphics_draw_text(ctx, day, theme->meta_bold, GRect(left, y - 2, width, theme->meta_height), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
    y = (int16_t)(y + theme->meta_height);
  }
  char stamp[24];
  tb_format_time(stamp, sizeof(stamp), (time_t)message->date, (time_t)message->date, clock_is_24h_style());
  char meta[48];
  snprintf(meta, sizeof(meta), "%s%s", (message->flags & TB_MESSAGE_FLAG_EDITED) ? "* " : "", stamp);
  int16_t meta_left = left;
  const bool pending = outgoing && (message->flags & TB_MESSAGE_FLAG_PENDING);
  const bool failed = outgoing && (message->flags & TB_MESSAGE_FLAG_FAILED);
  if (pending || failed) {
    const GRect glyph = GRect(left, (int16_t)(y + (theme->meta_height - marker) / 2), marker, marker);
    if (failed) { tb_icon_alert(ctx, glyph, highlighted ? GColorWhite : GColorRed); }
    else { tb_icon_clock(ctx, glyph, tb_theme_muted(highlighted)); }
    meta_left = (int16_t)(left + marker + 3);
  }
  graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
  graphics_draw_text(ctx, meta, theme->meta, GRect(meta_left, y - 2, width - (meta_left - left), theme->meta_height), GTextOverflowModeTrailingEllipsis,
                     outgoing ? GTextAlignmentLeft : GTextAlignmentRight, NULL);
  const char *sender = sender_of(view, message);
  if (*sender) {
    graphics_context_set_text_color(ctx, outgoing ? tb_theme_muted(highlighted) : tb_theme_accent(highlighted));
    graphics_draw_text(ctx, sender, theme->meta_bold, GRect(left + (outgoing ? 40 : 0), y - 2, width - 40, theme->meta_height),
                       GTextOverflowModeTrailingEllipsis, outgoing ? GTextAlignmentRight : GTextAlignmentLeft, NULL);
  }
  y = (int16_t)(y + theme->meta_height);
  if (has_forward(message)) { y = mark_line(ctx, tb_icon_forward, message->forward, tb_theme_accent(highlighted), left, width, y); }
  if (has_quote(message)) { y = mark_line(ctx, tb_icon_reply, message->reply, tb_theme_muted(highlighted), left, width, y); }
  body_text(view, message, tb_scratch(TB_SCRATCH_TEXT), tb_scratch_size(TB_SCRATCH_TEXT));
  graphics_context_set_text_color(ctx, tb_theme_text(highlighted));
  graphics_draw_text(ctx, tb_scratch(TB_SCRATCH_TEXT), theme->body, GRect(left, y - 4, width, bounds.size.h - y), GTextOverflowModeTrailingEllipsis,
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
  const char *text = index->row == ROW_TOP ? top_text(view) : tail_text(view, index->row);
  GRect box = GRect(theme->margin + 4, 2, bounds.size.w - 2 * theme->margin - 8, bounds.size.h - 4);
  if (is_write(view, index->row)) {
    const int16_t icon = marker_size();
    const GSize size = graphics_text_layout_get_content_size(text, theme->title, box, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    const int16_t total = (int16_t)(icon + 5 + size.w);
    const int16_t start = (int16_t)(box.origin.x + (box.size.w - total) / 2);
    tb_icon_pencil(ctx, GRect(start, (int16_t)((bounds.size.h - icon) / 2), icon, icon), tb_theme_accent(highlighted));
    graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
    graphics_draw_text(ctx, text, theme->title, GRect(start + icon + 5, (bounds.size.h - theme->title_height) / 2 - 2, size.w + 4, theme->title_height),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
  graphics_draw_text(ctx, text, theme->meta_bold, box, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void remember(TbHistoryWindow *view, uint16_t row) {
  view->selected_row = row;
  view->follow_bottom = row >= newest_row(view);
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

static void activate_row(TbHistoryWindow *view, uint16_t row) {
  remember(view, row);
  if (is_message(view, row)) {
    view->actions.activate(view->actions.context, row - ROW_FIRST_MESSAGE);
  } else if (row == ROW_TOP) {
    if (!view->history->loaded) { tb_history_retry(view->history); }
    else { tb_history_load_older(view->history); }
  } else if (is_refresh(view, row)) {
    tb_history_refresh(view->history);
  } else if (is_write(view, row)) {
    view->actions.compose(view->actions.context);
  }
}

static void selection_changed(MenuLayer *menu, MenuIndex now, MenuIndex before, void *context) {
  (void)menu; (void)before;
  TbHistoryWindow *view = context;
  if (view->restoring) { return; }
  remember(view, now.row);
  if (now.row == ROW_TOP && view->history->loaded && view->history->top == TB_TOP_MORE) { tb_history_load_older(view->history); }
}

static void restore(TbHistoryWindow *view) {
  if (!view->menu) { return; }
  const uint16_t total = row_count(view);
  uint16_t row = view->selected_row;
  const int found = view->selected_id[0] ? tb_history_find(view->history, view->selected_id) : -1;
  if (!view->placed && view->history->count > 0) {
    row = newest_row(view);
    view->placed = true;
  } else if (view->follow_bottom && view->history->count > 0 && row <= newest_row(view)) {
    row = newest_row(view);
  } else if (found >= 0) {
    row = (uint16_t)(found + ROW_FIRST_MESSAGE);
  } else if (row == ROW_TOP && view->anchor_id[0] && view->history->op == TB_HISTORY_NONE) {
    const int anchor = tb_history_find(view->history, view->anchor_id);
    if (anchor > 0) { row = (uint16_t)anchor; }
    view->anchor_id[0] = '\0';
  }
  if (row >= total) { row = total - 1; }
  view->selected_row = row;
  view->restoring = true;
  menu_layer_set_selected_index(view->menu, (MenuIndex){0, row}, MenuRowAlignCenter, false);
  view->restoring = false;
  if (is_message(view, row)) {
    strncpy(view->selected_id, view->history->items[row - ROW_FIRST_MESSAGE].id, sizeof(view->selected_id) - 1);
    view->selected_id[sizeof(view->selected_id) - 1] = '\0';
  }
}

static uint16_t s_pull_level;

static void draw_pull(Layer *layer, GContext *ctx) {
  if (s_pull_level == 0) { return; }
  const GRect bounds = layer_get_bounds(layer);
  int16_t size = (int16_t)(1 + (int32_t)(PULL_MAX - 1) * s_pull_level / TB_PULL_FULL);
  if (size > bounds.size.h - 2) { size = (int16_t)(bounds.size.h - 2); }
  const GColor color = tb_theme_accent(false);
  const GPoint center = GPoint(bounds.size.w / 2, (int16_t)(bounds.size.h - size / 2 - PBL_IF_ROUND_ELSE(8, 2)));
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, center, (uint16_t)(size / 2 + 2));
  if (size < 4) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, GRect(center.x - size / 2, center.y - size / 2, size, size), 0, GCornerNone);
    return;
  }
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, size >= 20 ? 2 : 1);
  graphics_context_set_antialiased(ctx, true);
  graphics_draw_circle(ctx, center, (uint16_t)(size / 2));
  tb_icon_fill(ctx, GRect(center.x - size / 2, center.y - size / 2, size, size), color, s_pull_level);
}

static void pull_changed(void *context) {
  TbHistoryWindow *view = context;
  s_pull_level = view->pull.level;
  if (view->pull_layer) { layer_mark_dirty(view->pull_layer); }
}

static void pull_trigger(void *context) {
  TbHistoryWindow *view = context;
  view->follow_bottom = true;
  tb_history_refresh(view->history);
}

static void pull_fired(void *context) {
  TbHistoryWindow *view = context;
  view->pull_timer = NULL;
  tb_pull_tick(&view->pull);
}

static bool pull_schedule(void *context, uint32_t milliseconds) {
  TbHistoryWindow *view = context;
  if (view->pull_timer) { app_timer_cancel(view->pull_timer); }
  view->pull_timer = app_timer_register(milliseconds, pull_fired, view);
  return view->pull_timer != NULL;
}

static void pull_cancel(void *context) {
  TbHistoryWindow *view = context;
  if (view->pull_timer) {
    app_timer_cancel(view->pull_timer);
    view->pull_timer = NULL;
  }
}

static uint32_t pull_now(void *context) {
  (void)context;
  time_t seconds = 0;
  uint16_t milliseconds = 0;
  time_ms(&seconds, &milliseconds);
  return (uint32_t)seconds * 1000u + milliseconds;
}

static AppTimer *s_up_repeat;

static uint16_t selected(const TbHistoryWindow *view) { return menu_layer_get_selected_index(view->menu).row; }

static void up_repeat(void *context) {
  TbHistoryWindow *view = context;
  s_up_repeat = NULL;
  if (!view->menu || selected(view) == 0) { return; }
  menu_layer_set_selected_next(view->menu, true, MenuRowAlignCenter, true);
  s_up_repeat = app_timer_register(100, up_repeat, view);
}

static void up_pressed(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbHistoryWindow *view = context;
  if (s_up_repeat) {
    app_timer_cancel(s_up_repeat);
    s_up_repeat = NULL;
  }
  if (view->notice.notify->focused) { return; }
  if (selected(view) == 0) {
    if (tb_notify_view_visible(&view->notice)) { tb_notify_focus(view->notice.notify, true); }
    return;
  }
  menu_layer_set_selected_next(view->menu, true, MenuRowAlignCenter, true);
  s_up_repeat = app_timer_register(400, up_repeat, view);
}

static void up_released(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (s_up_repeat) {
    app_timer_cancel(s_up_repeat);
    s_up_repeat = NULL;
  }
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbHistoryWindow *view = context;
  if (view->notice.notify->focused) {
    tb_notify_focus(view->notice.notify, false);
    return;
  }
  if (selected(view) == last_row(view)) {
    tb_pull_press(&view->pull);
    return;
  }
  menu_layer_set_selected_next(view->menu, false, MenuRowAlignCenter, true);
}

static void select_single(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbHistoryWindow *view = context;
  if (view->notice.notify->focused) {
    tb_notify_view_activate(&view->notice);
    return;
  }
  activate_row(view, selected(view));
}

static void select_long(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbHistoryWindow *view = context;
  const uint16_t row = selected(view);
  if (is_message(view, row)) {
    remember(view, row);
    view->actions.menu(view->actions.context, row - ROW_FIRST_MESSAGE);
  }
}

static void menu_select(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbHistoryWindow *view = context;
  activate_row(view, index->row);
}

static void menu_select_long(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbHistoryWindow *view = context;
  if (is_message(view, index->row)) {
    remember(view, index->row);
    view->actions.menu(view->actions.context, index->row - ROW_FIRST_MESSAGE);
  }
}

static void click_config(void *context) {
  window_set_click_context(BUTTON_ID_DOWN, context);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_raw_click_subscribe(BUTTON_ID_UP, up_pressed, up_released, context);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}

#if defined(PBL_TOUCH)
static TbHistoryWindow *s_touch_view;
static int16_t s_touch_start;
static bool s_touch_armed;

static bool at_bottom(const TbHistoryWindow *view) {
  ScrollLayer *scroll = menu_layer_get_scroll_layer(view->menu);
  const GSize content = scroll_layer_get_content_size(scroll);
  const GRect frame = layer_get_frame(scroll_layer_get_layer(scroll));
  return scroll_layer_get_content_offset(scroll).y + content.h <= frame.size.h + 2;
}

static void touched(const TouchEvent *event, void *context) {
  (void)context;
  TbHistoryWindow *view = s_touch_view;
  if (!view || !view->menu || event->non_navigational) { return; }
  if (event->type == TouchEvent_Touchdown) {
    if (tb_notify_view_hit(&view->notice, event->y)) {
      s_touch_armed = false;
      tb_notify_view_activate(&view->notice);
      return;
    }
    s_touch_start = event->y;
    s_touch_armed = at_bottom(view);
    return;
  }
  if (!s_touch_armed) { return; }
  if (event->type == TouchEvent_PositionUpdate) {
    const int32_t distance = s_touch_start - event->y;
    if (distance > 0 || view->pull.phase == TB_PULL_DRAGGING) {
      tb_pull_drag(&view->pull, (uint16_t)(distance > 0 ? distance * TB_PULL_FULL / PULL_DISTANCE : 0));
    }
  } else if (event->type == TouchEvent_Liftoff) {
    s_touch_armed = false;
    tb_pull_release(&view->pull);
  }
}
#endif

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
    .select_click = menu_select,
    .select_long_click = menu_select_long,
    .selection_changed = selection_changed,
  });
  window_set_click_config_provider_with_context(window, click_config, view);
  layer_add_child(root, menu_layer_get_layer(view->menu));
  const int16_t pull_height = (int16_t)(PULL_MAX + 12);
  view->pull_layer = layer_create(GRect(0, bounds.size.h - pull_height, bounds.size.w, pull_height));
  layer_set_update_proc(view->pull_layer, draw_pull);
  layer_add_child(root, view->pull_layer);
  tb_notify_view_attach(&view->notice, root, PBL_IF_ROUND_ELSE(44, 0));
  restore(view);
  tb_diag_event("window_push", "history");
}

static void window_unload(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  tb_notify_view_detach(&view->notice);
  layer_destroy(view->pull_layer);
  view->pull_layer = NULL;
  menu_layer_destroy(view->menu);
  view->menu = NULL;
  if (view->actions.closed) { view->actions.closed(view->actions.context); }
  tb_diag_event("window_pop", "history");
}

static void window_appear(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  tb_history_set_active(view->history, true);
  tb_notify_view_refresh(&view->notice);
#if defined(PBL_TOUCH)
  if (touch_service_is_enabled()) {
    s_touch_view = view;
    touch_service_subscribe(touched, NULL);
  }
#endif
}

static void window_disappear(Window *window) {
  TbHistoryWindow *view = window_get_user_data(window);
  tb_history_set_active(view->history, false);
  tb_pull_reset(&view->pull);
  s_pull_level = 0;
  tb_notify_focus(view->notice.notify, false);
  up_released(NULL, view);
#if defined(PBL_TOUCH)
  if (s_touch_view == view) {
    touch_service_unsubscribe();
    s_touch_view = NULL;
  }
#endif
}

void tb_history_window_init(TbHistoryWindow *view, TbHistory *history, TbNotify *notify, TbNotifyActivate activate, const TbStrings *strings,
                            TbHistoryActions actions) {
  view->history = history;
  view->strings = strings;
  view->actions = actions;
  view->send = TB_CAN_SEND_UNKNOWN;
  view->pull_timer = NULL;
  view->pull_layer = NULL;
  tb_pull_init(&view->pull, (TbPullPorts){pull_changed, pull_trigger, pull_schedule, pull_cancel, pull_now, view});
  tb_notify_view_init(&view->notice, notify, activate, actions.context);
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
  view->restoring = true;
  menu_layer_reload_data(view->menu);
  view->restoring = false;
  restore(view);
  tb_notify_view_refresh(&view->notice);
}

void tb_history_window_set_send(TbHistoryWindow *view, uint8_t send) { view->send = send; }

void tb_history_window_follow(TbHistoryWindow *view) { view->follow_bottom = true; }
