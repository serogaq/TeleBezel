#include "chats_window.h"
#include <string.h>
#include "errors.h"
#include "format.h"
#include "generated/protocol.h"
#include "theme.h"

#define ROW_TOGGLE 0
#define ROW_STATUS 1
#define ROW_FIRST_CHAT 2

static bool has_tail(const TbChatsWindow *view) {
  const TbChats *chats = view->chats;
  if (!chats->loaded) { return false; }
  if (chats->count == 0) { return true; }
  return chats->tail != TB_TAIL_END && chats->tail != TB_TAIL_FULL;
}

static uint16_t row_count(const TbChatsWindow *view) { return (uint16_t)(ROW_FIRST_CHAT + view->chats->count + (has_tail(view) ? 1 : 0)); }
static bool is_chat(const TbChatsWindow *view, uint16_t row) { return row >= ROW_FIRST_CHAT && row < ROW_FIRST_CHAT + view->chats->count; }
static bool is_tail(const TbChatsWindow *view, uint16_t row) { return has_tail(view) && row == ROW_FIRST_CHAT + view->chats->count; }

static const char *status_text(TbChatsWindow *view, bool *actionable) {
  const TbChats *chats = view->chats;
  const TbStrings *strings = view->strings;
  *actionable = false;
  if (chats->load == TB_CHATS_FIRST || (chats->load == TB_CHATS_REFRESH && chats->count == 0)) { return strings->loading; }
  if (chats->load == TB_CHATS_REFRESH) { return strings->refreshing; }
  if (chats->error != TB_ERROR_NONE && chats->load == TB_CHATS_IDLE && chats->tail != TB_TAIL_FAILED) {
    *actionable = true;
    if (chats->error == TB_RESULT_RATE_LIMITED && chats->retry_after > 0) {
      char wait[48];
      snprintf(wait, sizeof(wait), strings->wait_seconds, (int)chats->retry_after);
      snprintf(view->buffer, sizeof(view->buffer), "%s%s%s. %s", chats->count ? strings->not_updated : "", chats->count ? ": " : "",
               wait, strings->retry_hint);
    } else {
      snprintf(view->buffer, sizeof(view->buffer), "%s%s%s. %s", chats->count ? strings->not_updated : "", chats->count ? ": " : "",
               tb_error_text(strings, chats->error), strings->retry_hint);
    }
    return view->buffer;
  }
  *actionable = true;
  if (chats->connection_not_ready) {
    snprintf(view->buffer, sizeof(view->buffer), "%s · %s", strings->telegram_connecting, strings->refresh);
    return view->buffer;
  }
  char stamp[16];
  const time_t current = time(NULL);
  tb_format_time(stamp, sizeof(stamp), current - (time_t)((view->chats->ports.now(view->chats->ports.context) - chats->loaded_at) / 1000),
                 current, clock_is_24h_style());
  char updated[48];
  snprintf(updated, sizeof(updated), strings->updated_at, stamp);
  snprintf(view->buffer, sizeof(view->buffer), "%s · %s", updated, strings->refresh);
  return view->buffer;
}

static const char *tail_text(const TbChatsWindow *view) {
  const TbChats *chats = view->chats;
  if (chats->count == 0) { return view->strings->no_chats; }
  if (chats->load == TB_CHATS_MORE) { return view->strings->loading; }
  if (chats->tail == TB_TAIL_FAILED) { return view->strings->load_failed; }
  return view->strings->more_chats;
}

static char s_content[240];
static char s_preview[280];
static char s_title[96];

static void chat_preview(TbChatsWindow *view, const TbChat *chat, char *out, size_t size) {
  char *content = s_content;
  tb_format_content(content, sizeof(s_content), view->strings, chat->preview_kind, chat->preview_action, chat->preview_duration,
                    chat->extra, chat->preview);
  const bool group = chat->type == TB_CHAT_TYPE_BASIC_GROUP || chat->type == TB_CHAT_TYPE_SUPERGROUP;
  if (chat->flags & TB_CHAT_FLAG_PREVIEW_OUTGOING) {
    snprintf(out, size, "%s: %s", view->strings->you, content);
  } else if (group && chat->sender && chat->preview_kind != TB_KIND_SERVICE) {
    snprintf(out, size, "%s: %s", chat->sender, content);
  } else if (chat->preview_kind == TB_KIND_SERVICE && chat->sender) {
    snprintf(out, size, "%s %s", chat->sender, content);
  } else {
    snprintf(out, size, "%s", content);
  }
}

static uint16_t rows(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu; (void)section;
  return row_count(context);
}

static int16_t row_height(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbChatsWindow *view = context;
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->window));
  int16_t height = theme->row_min;
  if (is_chat(view, index->row)) {
    height = (int16_t)(theme->title_height + theme->meta_height + 8);
  } else if (index->row == ROW_STATUS) {
    bool actionable = false;
    const char *text = status_text(view, &actionable);
    height = (int16_t)(tb_theme_text_height(text, theme->meta, bounds.size.w - 2 * theme->margin - 4, bounds.size.h - 20) + 10);
  } else {
    height = (int16_t)(theme->title_height + 8);
  }
  return height > theme->row_min ? height : theme->row_min;
}

static void draw_chat(TbChatsWindow *view, GContext *ctx, const Layer *cell, const TbChat *chat) {
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const int16_t left = theme->margin + 2;
  const int16_t width = bounds.size.w - 2 * theme->margin - 4;
  char badge[16];
  tb_format_badge(badge, sizeof(badge), chat->unread, chat->flags);
  int16_t badge_width = 0;
  if (badge[0]) {
    const GSize size = graphics_text_layout_get_content_size(badge, theme->meta_bold, GRect(0, 0, 60, 30), GTextOverflowModeFill, GTextAlignmentRight);
    badge_width = (int16_t)(size.w + 8);
    const GRect pill = GRect(left + width - badge_width, 4, badge_width, theme->meta_height + 2);
    const bool muted = (chat->flags & TB_CHAT_FLAG_MUTED) != 0;
    graphics_context_set_fill_color(ctx, highlighted ? GColorWhite : muted ? PBL_IF_COLOR_ELSE(GColorLightGray, GColorBlack) : tb_theme_accent(false));
    graphics_fill_rect(ctx, pill, 6, GCornersAll);
    graphics_context_set_text_color(ctx, highlighted ? PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorBlack) : GColorWhite);
    graphics_draw_text(ctx, badge, theme->meta_bold, GRect(pill.origin.x, pill.origin.y - 2, pill.size.w, pill.size.h), GTextOverflowModeFill,
                       GTextAlignmentCenter, NULL);
  }
  int16_t time_width = 0;
#if !defined(PBL_PLATFORM_DIORITE) && !defined(PBL_PLATFORM_FLINT)
  if (chat->last_date) {
    char time_text[16];
    tb_format_time(time_text, sizeof(time_text), (time_t)chat->last_date, time(NULL), clock_is_24h_style());
    time_width = 56;
    graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
    graphics_draw_text(ctx, time_text, theme->meta, GRect(left + width - badge_width - time_width, 4, time_width - 4, theme->meta_height),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
#endif
  snprintf(s_title, sizeof(s_title), "%s%s", chat->type == TB_CHAT_TYPE_CHANNEL ? "» " : "", tb_or_empty(chat->title));
  graphics_context_set_text_color(ctx, tb_theme_text(highlighted));
  graphics_draw_text(ctx, s_title, theme->title, GRect(left, -2, width - badge_width - time_width, theme->title_height),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  chat_preview(view, chat, s_preview, sizeof(s_preview));
  graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
  graphics_draw_text(ctx, s_preview, theme->meta, GRect(left, theme->title_height, width, theme->meta_height + 2),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  TbChatsWindow *view = context;
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const GRect box = GRect(theme->margin + 2, 2, bounds.size.w - 2 * theme->margin - 4, bounds.size.h - 4);
  const GTextAlignment alignment = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  if (is_chat(view, index->row)) {
    draw_chat(view, ctx, cell, &view->chats->items[index->row - ROW_FIRST_CHAT]);
  } else if (index->row == ROW_TOGGLE) {
    graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
    graphics_draw_text(ctx, view->chats->list == TB_LIST_ARCHIVE ? view->strings->open_main : view->strings->open_archive, theme->title,
                       GRect(box.origin.x, 0, box.size.w, box.size.h), GTextOverflowModeTrailingEllipsis, alignment, NULL);
  } else if (index->row == ROW_STATUS) {
    bool actionable = false;
    const char *text = status_text(view, &actionable);
    graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
    graphics_draw_text(ctx, text, theme->meta, box, GTextOverflowModeTrailingEllipsis, alignment, NULL);
  } else {
    graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
    graphics_draw_text(ctx, tail_text(view), theme->title, GRect(box.origin.x, 0, box.size.w, box.size.h), GTextOverflowModeTrailingEllipsis,
                       alignment, NULL);
  }
}

static void remember(TbChatsWindow *view, uint16_t row) {
  view->selected_row = row;
  if (is_chat(view, row)) {
    strncpy(view->selected_id, view->chats->items[row - ROW_FIRST_CHAT].id, sizeof(view->selected_id) - 1);
    view->selected_id[sizeof(view->selected_id) - 1] = '\0';
  } else if (row < ROW_FIRST_CHAT) {
    view->selected_id[0] = '\0';
  }
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbChatsWindow *view = context;
  remember(view, index->row);
  if (is_chat(view, index->row)) {
    view->actions.activate(view->actions.context, index->row - ROW_FIRST_CHAT);
  } else if (index->row == ROW_TOGGLE) {
    tb_chats_window_reset(view);
    tb_chats_set_list(view->chats, view->chats->list == TB_LIST_ARCHIVE ? TB_LIST_MAIN : TB_LIST_ARCHIVE);
  } else if (index->row == ROW_STATUS) {
    tb_chats_retry(view->chats);
  } else if (is_tail(view, index->row)) {
    if (view->chats->count == 0) { tb_chats_refresh(view->chats); }
    else { tb_chats_load_more(view->chats); }
  }
}

static void select_long_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu; (void)index;
  TbChatsWindow *view = context;
  tb_chats_refresh(view->chats);
}

static void selection_changed(MenuLayer *menu, MenuIndex now, MenuIndex before, void *context) {
  (void)menu; (void)before;
  TbChatsWindow *view = context;
  remember(view, now.row);
  if (is_tail(view, now.row) && view->chats->tail == TB_TAIL_MORE) { tb_chats_load_more(view->chats); }
}

static void restore(TbChatsWindow *view) {
  if (!view->menu) { return; }
  uint16_t row = view->selected_row;
  if (view->selected_id[0]) {
    const int found = tb_chats_find(view->chats, view->selected_id);
    if (found >= 0) { row = (uint16_t)(found + ROW_FIRST_CHAT); }
  } else if (!view->placed && view->chats->count > 0) {
    row = ROW_FIRST_CHAT;
  }
  if (view->chats->count > 0) { view->placed = true; }
  const uint16_t total = row_count(view);
  if (row >= total) { row = total - 1; }
  view->selected_row = row;
  menu_layer_set_selected_index(view->menu, (MenuIndex){0, row}, MenuRowAlignCenter, false);
}

static void window_load(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->menu = menu_layer_create(layer_get_bounds(root));
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
  TbChatsWindow *view = window_get_user_data(window);
  menu_layer_destroy(view->menu);
  view->menu = NULL;
}

static void window_appear(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  tb_chats_set_active(view->chats, true);
}

static void window_disappear(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  tb_chats_set_active(view->chats, false);
}

void tb_chats_window_init(TbChatsWindow *view, TbChats *chats, const TbStrings *strings, TbChatsActions actions) {
  view->chats = chats;
  view->strings = strings;
  view->actions = actions;
  tb_chats_window_reset(view);
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload, .appear = window_appear,
                                                             .disappear = window_disappear});
}

void tb_chats_window_deinit(TbChatsWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_chats_window_reset(TbChatsWindow *view) {
  view->selected_id[0] = '\0';
  view->selected_row = 0;
  view->placed = false;
}

void tb_chats_window_reload(TbChatsWindow *view) {
  if (!view->menu) { return; }
  menu_layer_reload_data(view->menu);
  restore(view);
}
