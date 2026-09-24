#include "compose_window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "action_menu.h"
#include "diag.h"
#include "format.h"
#include "generated/protocol.h"
#include "icons.h"
#include "theme.h"

#define SECTION_TOP 0
#define SECTION_TEMPLATES 1
#define ACTION_REDICTATE 1
#define ACTION_OTHER 2
#define ACTION_CANCEL 3
#define ACTION_SEND_AGAIN 4
#define ACTION_CHECK 5
#define ACTION_OPEN_CHAT 6
#define ACTION_SEND_ANYWAY 7
#define TB_RESULT_CLOSE_MS 1500

typedef struct {
  char target[160];
  char section[160];
  char review_footer[128];
  char result_title[48];
  char result_body[200];
  char result_hint[64];
} TbComposeTexts;

static TbComposeTexts *s_texts;

static void release_texts(const TbComposeView *view) {
  if (view->menu_window || view->review_window || view->result_window) { return; }
  free(s_texts);
  s_texts = NULL;
}

static bool in_stack(Window *window) { return window && window_stack_contains_window(window); }

static void describe_target(const TbComposeView *view) {
  const TbComposeTarget *target = &view->compose->target;
  const char *account = target->account_name[0] ? target->account_name : "";
  int used = snprintf(s_texts->target, sizeof(s_texts->target), "%s%s%s", account, *account ? " · " : "", target->chat_title);
  if (target->send.reply[0] && used > 0 && (size_t)used < sizeof(s_texts->target)) {
    snprintf(s_texts->target + used, sizeof(s_texts->target) - (size_t)used, "\n%s %s%s%s", view->strings->in_reply_to, target->reply_sender,
             target->reply_sender[0] && target->reply_text[0] ? ": " : "", target->reply_text);
  }
}

static uint16_t template_rows(const TbComposeView *view) {
  const TbCompose *compose = view->compose;
  if (compose->templates == TB_TEMPLATES_READY && compose->template_count > 0) { return compose->template_count; }
  return 1;
}

static uint16_t menu_sections(MenuLayer *menu, void *context) {
  (void)menu; (void)context;
  return 2;
}

static uint16_t menu_rows(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu;
  TbComposeView *view = context;
  if (section == SECTION_TOP) { return view->dictation ? 1 : 0; }
  return template_rows(view);
}

static const char *section_text(TbComposeView *view, uint16_t section) {
  if (section == SECTION_TOP) {
    describe_target(view);
    return s_texts->target;
  }
  const TbCompose *compose = view->compose;
  char count[32];
  snprintf(count, sizeof(count), view->strings->quick_replies, (int)compose->template_total);
  snprintf(s_texts->section, sizeof(s_texts->section), "%s%s%s", count, compose->templates_stale ? " · " : "",
           compose->templates_stale ? view->strings->templates_stale : "");
  return s_texts->section;
}

static int16_t text_width(const TbComposeView *view) {
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->menu_window));
  return (int16_t)(bounds.size.w - 2 * tb_theme()->margin - 8);
}

static int16_t menu_header_height(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu;
  TbComposeView *view = context;
  const TbTheme *theme = tb_theme();
  const char *text = section_text(view, section);
  const int16_t height = (int16_t)(tb_theme_text_height(text, theme->meta_bold, text_width(view), 120) + 6);
  return (int16_t)(section == SECTION_TOP ? height + PBL_IF_ROUND_ELSE(18, 0) : height);
}

static void menu_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  TbComposeView *view = context;
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(cell);
  const char *text = section_text(view, section);
  graphics_context_set_fill_color(ctx, section == SECTION_TOP ? GColorWhite : GColorLightGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_text_color(ctx, section == SECTION_TOP ? tb_theme_accent(false) : GColorBlack);
  const int16_t top = (int16_t)(section == SECTION_TOP ? PBL_IF_ROUND_ELSE(16, 0) : 0);
  graphics_draw_text(ctx, text, theme->meta_bold, GRect(theme->margin + 4, top, text_width(view), bounds.size.h - top),
                     GTextOverflowModeTrailingEllipsis, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

static const char *template_row_text(TbComposeView *view, uint16_t row) {
  const TbCompose *compose = view->compose;
  if (compose->templates == TB_TEMPLATES_READY && compose->template_count > 0) { return tb_compose_template(compose, (uint8_t)row); }
  if (compose->templates == TB_TEMPLATES_LOADING || compose->templates == TB_TEMPLATES_NONE) { return view->strings->loading; }
  if (compose->templates == TB_TEMPLATES_FAILED) {
    snprintf(s_texts->section, sizeof(s_texts->section), "%s. %s", tb_error_text(view->strings, compose->templates_error), view->strings->retry_hint);
    return s_texts->section;
  }
  return view->strings->templates_empty;
}

static int16_t menu_row_height(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbComposeView *view = context;
  const TbTheme *theme = tb_theme();
  if (index->section == SECTION_TOP) { return (int16_t)(theme->title_height + 10 > theme->row_min ? theme->title_height + 10 : theme->row_min); }
  const bool real = view->compose->templates == TB_TEMPLATES_READY && view->compose->template_count > 0;
  const int16_t height = (int16_t)(tb_theme_text_height(template_row_text(view, index->row), real ? theme->body : theme->meta, text_width(view),
                                                        real ? 3 * theme->body_line : 6 * theme->meta_height) + 8);
  return height > theme->row_min ? height : theme->row_min;
}

static void menu_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  TbComposeView *view = context;
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const int16_t left = (int16_t)(theme->margin + 4);
  if (index->section == SECTION_TOP) {
    const int16_t icon = 16;
    tb_icon_mic(ctx, GRect(left, (bounds.size.h - icon) / 2, icon, icon), tb_theme_accent(highlighted));
    graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
    graphics_draw_text(ctx, view->strings->dictate, theme->title, GRect(left + icon + 6, (bounds.size.h - theme->title_height) / 2 - 2,
                       bounds.size.w - left - icon - 10, theme->title_height), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  const bool real = view->compose->templates == TB_TEMPLATES_READY && view->compose->template_count > 0;
  graphics_context_set_text_color(ctx, real ? tb_theme_text(highlighted) : tb_theme_muted(highlighted));
  graphics_draw_text(ctx, template_row_text(view, index->row), real ? theme->body : theme->meta,
                     GRect(left, 0, bounds.size.w - 2 * left, bounds.size.h - 2), GTextOverflowModeTrailingEllipsis,
                     PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft), NULL);
}

static void menu_select(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbComposeView *view = context;
  if (index->section == SECTION_TOP) {
    view->actions.dictate(view->actions.context);
    return;
  }
  TbCompose *compose = view->compose;
  if (compose->templates == TB_TEMPLATES_READY && compose->template_count > 0) {
    if (tb_compose_pick(compose, (uint8_t)index->row)) { tb_compose_view_review(view); }
    return;
  }
  if (compose->templates != TB_TEMPLATES_LOADING) { tb_compose_load_templates(compose, true); }
}

static void menu_load(Window *window) {
  TbComposeView *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->menu = menu_layer_create(layer_get_bounds(root));
  tb_theme_menu(view->menu);
  menu_layer_set_callbacks(view->menu, view, (MenuLayerCallbacks){
    .get_num_sections = menu_sections,
    .get_num_rows = menu_rows,
    .get_header_height = menu_header_height,
    .draw_header = menu_header,
    .get_cell_height = menu_row_height,
    .draw_row = menu_draw_row,
    .select_click = menu_select,
  });
  menu_layer_set_click_config_onto_window(view->menu, window);
  layer_add_child(root, menu_layer_get_layer(view->menu));
  if (!view->dictation) { menu_layer_set_selected_index(view->menu, (MenuIndex){SECTION_TEMPLATES, 0}, MenuRowAlignNone, false); }
  tb_diag_periodic(true);
  tb_diag_event("window_push", "compose");
}

static void menu_unload(Window *window) {
  TbComposeView *view = window_get_user_data(window);
  menu_layer_destroy(view->menu);
  view->menu = NULL;
  window_destroy(window);
  view->menu_window = NULL;
  release_texts(view);
  if (!view->review_window && !view->result_window) {
    tb_diag_periodic(false);
    view->actions.finished(view->actions.context, false);
  }
  tb_diag_event("window_pop", "compose");
}

static TextLayer *text(GFont font, GColor color, GTextAlignment alignment) {
  TextLayer *layer = text_layer_create(GRectZero);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, color);
  text_layer_set_font(layer, font);
  text_layer_set_overflow_mode(layer, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(layer, alignment);
  return layer;
}

static const char *review_body_text(TbComposeView *view) {
  const TbCompose *compose = view->compose;
  if (compose->draft == TB_DRAFT_LOADING || compose->draft == TB_DRAFT_NONE) { return view->strings->loading; }
  if (compose->draft == TB_DRAFT_FAILED) { return tb_error_text(view->strings, compose->draft_error); }
  if (compose->too_long) { return ""; }
  return compose->text ? compose->text : "";
}

static void review_footer_text(TbComposeView *view) {
  const TbCompose *compose = view->compose;
  s_texts->review_footer[0] = '\0';
  if (compose->draft != TB_DRAFT_READY) { return; }
  if (compose->too_long) {
    snprintf(s_texts->review_footer, sizeof(s_texts->review_footer), view->strings->too_long, (int)compose->draft_units);
    return;
  }
  char count[24];
  snprintf(count, sizeof(count), view->strings->chars, (int)compose->draft_units);
  char hint[40];
  snprintf(hint, sizeof(hint), view->strings->select_hint, view->strings->send);
  snprintf(s_texts->review_footer, sizeof(s_texts->review_footer), "%s\n%s%s%s", count, view->busy_notice ? view->strings->previous_sending : hint,
           view->busy_notice ? "" : "\n", view->busy_notice ? "" : view->strings->menu_hint);
}

static void review_layout(TbComposeView *view) {
  if (!view->review_scroll) { return; }
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->review_window));
  describe_target(view);
  review_footer_text(view);
  TbPageLine lines[TB_PAGE_LINES] = {{view->review_header, s_texts->target, theme->meta_bold, 200, 4},
                                     {view->review_body, review_body_text(view), theme->body, 30000, 8},
                                     {view->review_footer, s_texts->review_footer, theme->meta, 200, 4}};
  const int16_t bottom = tb_theme_page(bounds, lines, TB_PAGE_LINES, PBL_IF_ROUND_ELSE(36, 2));
  scroll_layer_set_content_size(view->review_scroll, GSize(bounds.size.w, bottom + PBL_IF_ROUND_ELSE(40, 8)));
}

static void review_send(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbComposeView *view = context;
  if (!tb_compose_sendable(view->compose)) {
    if (view->compose->draft == TB_DRAFT_FAILED && view->compose->dictated == NULL) { vibes_short_pulse(); }
    return;
  }
  if (tb_send_busy(view->tracker) && view->tracker->status.draft_id != view->compose->draft_id) {
    view->busy_notice = true;
    review_layout(view);
    vibes_short_pulse();
    return;
  }
  view->busy_notice = false;
  if (view->actions.send(view->actions.context, TB_COMPOSE_SEND)) { tb_compose_view_result(view); }
}

static void review_chosen(void *context, uint8_t action) {
  TbComposeView *view = context;
  if (action == ACTION_REDICTATE) {
    view->actions.dictate(view->actions.context);
  } else if (action == ACTION_OTHER) {
    tb_compose_drop_draft(view->compose);
    if (in_stack(view->review_window)) { window_stack_remove(view->review_window, true); }
  } else if (action == ACTION_CANCEL) {
    tb_compose_view_close_all(view);
  }
}

static void review_menu(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbComposeView *view = context;
  const char *labels[3];
  uint8_t actions[3];
  uint8_t count = 0;
  if (view->dictation) {
    labels[count] = view->strings->redictate;
    actions[count++] = ACTION_REDICTATE;
  }
  labels[count] = view->strings->other_template;
  actions[count++] = ACTION_OTHER;
  labels[count] = view->strings->cancel;
  actions[count++] = ACTION_CANCEL;
  tb_actions_open(labels, actions, count, review_chosen, view);
}

static void review_back(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbComposeView *view = context;
  tb_compose_drop_draft(view->compose);
  if (in_stack(view->review_window)) { window_stack_remove(view->review_window, true); }
}

static void review_clicks(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, review_send);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, review_menu, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, review_back);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_set_click_context(BUTTON_ID_BACK, context);
}

static void review_load(Window *window) {
  TbComposeView *view = window_get_user_data(window);
  const TbTheme *theme = tb_theme();
  Layer *root = window_get_root_layer(window);
  view->review_scroll = scroll_layer_create(layer_get_bounds(root));
  scroll_layer_set_shadow_hidden(view->review_scroll, true);
  scroll_layer_set_callbacks(view->review_scroll, (ScrollLayerCallbacks){.click_config_provider = review_clicks});
  scroll_layer_set_context(view->review_scroll, view);
  scroll_layer_set_click_config_onto_window(view->review_scroll, window);
  view->review_header = text(theme->meta_bold, tb_theme_accent(false), PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  view->review_body = text(theme->body, GColorBlack, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  view->review_footer = text(theme->meta, tb_theme_muted(false), PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  scroll_layer_add_child(view->review_scroll, text_layer_get_layer(view->review_header));
  scroll_layer_add_child(view->review_scroll, text_layer_get_layer(view->review_body));
  scroll_layer_add_child(view->review_scroll, text_layer_get_layer(view->review_footer));
  layer_add_child(root, scroll_layer_get_layer(view->review_scroll));
  review_layout(view);
  tb_diag_event("window_push", "review");
}

static void review_unload(Window *window) {
  TbComposeView *view = window_get_user_data(window);
  text_layer_destroy(view->review_footer);
  text_layer_destroy(view->review_body);
  text_layer_destroy(view->review_header);
  scroll_layer_destroy(view->review_scroll);
  view->review_footer = NULL;
  view->review_body = NULL;
  view->review_header = NULL;
  view->review_scroll = NULL;
  window_destroy(window);
  view->review_window = NULL;
  view->busy_notice = false;
  release_texts(view);
  if (!view->menu_window && !view->result_window) {
    tb_diag_periodic(false);
    view->actions.finished(view->actions.context, false);
  }
  tb_diag_event("window_pop", "review");
}

static const TbSendStatus *current(const TbComposeView *view) {
  const TbSendStatus *status = &view->tracker->status;
  return status->draft_id == view->result_draft ? status : NULL;
}

static void result_describe(TbComposeView *view) {
  const TbStrings *strings = view->strings;
  const TbSendStatus *status = current(view);
  s_texts->result_hint[0] = '\0';
  s_texts->result_body[0] = '\0';
  if (!status || status->phase == TB_SENDING_SUBMITTING || status->phase == TB_SENDING_PENDING || status->phase == TB_SENDING_IDLE) {
    snprintf(s_texts->result_title, sizeof(s_texts->result_title), "%s", strings->sending);
    snprintf(s_texts->result_body, sizeof(s_texts->result_body), "%s", view->compose->target.chat_title);
    snprintf(s_texts->result_hint, sizeof(s_texts->result_hint), "%s", strings->sending_continues);
    return;
  }
  if (status->phase == TB_SENDING_SENT) {
    snprintf(s_texts->result_title, sizeof(s_texts->result_title), "%s", strings->sent);
    snprintf(s_texts->result_body, sizeof(s_texts->result_body), "%s", (status->flags & TB_SEND_FLAG_REPLY_DROPPED) ? strings->sent_no_reply : status->title);
    return;
  }
  if (status->phase == TB_SENDING_FAILED) {
    snprintf(s_texts->result_title, sizeof(s_texts->result_title), "%s", strings->not_sent);
    if (status->retry_after > 0) {
      char wait[32];
      snprintf(wait, sizeof(wait), strings->wait_seconds, (int)status->retry_after);
      snprintf(s_texts->result_body, sizeof(s_texts->result_body), "%s. %s", tb_error_text(strings, status->code), wait);
    } else {
      snprintf(s_texts->result_body, sizeof(s_texts->result_body), "%s", tb_error_text(strings, status->code));
    }
    if (status->flags & TB_SEND_FLAG_RETRYABLE) { snprintf(s_texts->result_hint, sizeof(s_texts->result_hint), strings->select_hint, strings->send_again); }
    return;
  }
  snprintf(s_texts->result_title, sizeof(s_texts->result_title), "%s", strings->result_unknown);
  if (view->confirm_anyway) {
    snprintf(s_texts->result_body, sizeof(s_texts->result_body), "%s", strings->duplicate_warning);
    snprintf(s_texts->result_hint, sizeof(s_texts->result_hint), strings->select_hint, strings->send_anyway);
  } else {
    snprintf(s_texts->result_body, sizeof(s_texts->result_body), "%s", strings->send_unknown);
    snprintf(s_texts->result_hint, sizeof(s_texts->result_hint), strings->select_hint, strings->check);
  }
}

static void result_layout(TbComposeView *view) {
  if (!view->result_title) { return; }
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(window_get_root_layer(view->result_window));
  result_describe(view);
  TbPageLine lines[TB_PAGE_LINES] = {{view->result_title, s_texts->result_title, theme->title, 80, 4},
                                     {view->result_body, s_texts->result_body, theme->body, (int16_t)(bounds.size.h / 2), 4},
                                     {view->result_hint, s_texts->result_hint, theme->meta, 60, 4}};
  tb_theme_page(bounds, lines, TB_PAGE_LINES, TB_PAGE_CENTER);
}

static void result_chosen(void *context, uint8_t action) {
  TbComposeView *view = context;
  if (action == ACTION_SEND_AGAIN) {
    view->actions.send(view->actions.context, TB_COMPOSE_SEND_AGAIN);
  } else if (action == ACTION_CHECK) {
    view->actions.check(view->actions.context);
  } else if (action == ACTION_OPEN_CHAT) {
    view->actions.open_chat(view->actions.context);
  } else if (action == ACTION_SEND_ANYWAY) {
    view->confirm_anyway = true;
    result_layout(view);
  } else if (action == ACTION_CANCEL) {
    tb_compose_drop_draft(view->compose);
    tb_compose_view_close_all(view);
  }
}

static void result_select(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbComposeView *view = context;
  const TbSendStatus *status = current(view);
  if (!status) { return; }
  if (status->phase == TB_SENDING_FAILED && (status->flags & TB_SEND_FLAG_RETRYABLE)) {
    view->actions.send(view->actions.context, TB_COMPOSE_SEND_AGAIN);
  } else if (status->phase == TB_SENDING_UNKNOWN) {
    if (view->confirm_anyway) {
      view->confirm_anyway = false;
      view->actions.send(view->actions.context, TB_COMPOSE_SEND_ANYWAY);
    } else {
      view->actions.check(view->actions.context);
    }
  } else if (status->phase == TB_SENDING_SENT) {
    tb_compose_view_close_all(view);
  }
}

static void result_menu(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbComposeView *view = context;
  const TbSendStatus *status = current(view);
  if (!status) { return; }
  const char *labels[3];
  uint8_t actions[3];
  uint8_t count = 0;
  if (status->phase == TB_SENDING_FAILED) {
    if (status->flags & TB_SEND_FLAG_RETRYABLE) {
      labels[count] = view->strings->send_again;
      actions[count++] = ACTION_SEND_AGAIN;
    }
    labels[count] = view->strings->cancel;
    actions[count++] = ACTION_CANCEL;
  } else if (status->phase == TB_SENDING_UNKNOWN) {
    labels[count] = view->strings->check;
    actions[count++] = ACTION_CHECK;
    labels[count] = view->strings->open_chat;
    actions[count++] = ACTION_OPEN_CHAT;
    labels[count] = view->strings->send_anyway;
    actions[count++] = ACTION_SEND_ANYWAY;
  }
  if (count) { tb_actions_open(labels, actions, count, result_chosen, view); }
}

static void result_back(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  tb_compose_view_close_all(context);
}

static void result_clicks(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, result_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, result_menu, NULL);
  window_single_click_subscribe(BUTTON_ID_BACK, result_back);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_set_click_context(BUTTON_ID_BACK, context);
}

static void result_load(Window *window) {
  TbComposeView *view = window_get_user_data(window);
  const TbTheme *theme = tb_theme();
  Layer *root = window_get_root_layer(window);
  view->result_title = text(theme->title, tb_theme_accent(false), GTextAlignmentCenter);
  view->result_body = text(theme->body, GColorBlack, GTextAlignmentCenter);
  view->result_hint = text(theme->meta, tb_theme_muted(false), GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(view->result_title));
  layer_add_child(root, text_layer_get_layer(view->result_body));
  layer_add_child(root, text_layer_get_layer(view->result_hint));
  window_set_click_config_provider_with_context(window, result_clicks, view);
  result_layout(view);
  tb_diag_event("window_push", "result");
}

static void result_unload(Window *window) {
  TbComposeView *view = window_get_user_data(window);
  if (view->auto_close) {
    app_timer_cancel(view->auto_close);
    view->auto_close = NULL;
  }
  text_layer_destroy(view->result_hint);
  text_layer_destroy(view->result_body);
  text_layer_destroy(view->result_title);
  view->result_hint = NULL;
  view->result_body = NULL;
  view->result_title = NULL;
  window_destroy(window);
  view->result_window = NULL;
  view->confirm_anyway = false;
  release_texts(view);
  if (!view->menu_window && !view->review_window) {
    tb_diag_periodic(false);
    const TbSendStatus *status = current(view);
    view->actions.finished(view->actions.context, status && status->phase == TB_SENDING_SENT);
  }
  tb_diag_event("window_pop", "result");
}

static Window *make_window(TbComposeView *view, WindowHandlers handlers) {
  if (!s_texts) { s_texts = calloc(1, sizeof(TbComposeTexts)); }
  if (!s_texts) { return NULL; }
  Window *window = window_create();
  if (!window) { return NULL; }
  window_set_user_data(window, view);
  window_set_background_color(window, tb_theme_background());
  window_set_window_handlers(window, handlers);
  return window;
}

static void auto_closed(void *context) {
  TbComposeView *view = context;
  view->auto_close = NULL;
  tb_compose_view_close_all(view);
}

void tb_compose_view_init(TbComposeView *view, TbCompose *compose, TbSendTracker *tracker, const TbStrings *strings, TbComposeActions actions) {
  memset(view, 0, sizeof(*view));
  view->compose = compose;
  view->tracker = tracker;
  view->strings = strings;
  view->actions = actions;
}

void tb_compose_view_open(TbComposeView *view, bool dictation) {
  view->dictation = dictation;
  if (!view->menu_window) { view->menu_window = make_window(view, (WindowHandlers){.load = menu_load, .unload = menu_unload}); }
  if (view->menu_window && !in_stack(view->menu_window)) { window_stack_push(view->menu_window, true); }
}

void tb_compose_view_review(TbComposeView *view) {
  if (!view->review_window) { view->review_window = make_window(view, (WindowHandlers){.load = review_load, .unload = review_unload}); }
  if (view->review_window && !in_stack(view->review_window)) { window_stack_push(view->review_window, true); }
  review_layout(view);
}

void tb_compose_view_result(TbComposeView *view) {
  view->result_draft = view->tracker->status.draft_id;
  view->confirm_anyway = false;
  if (!view->result_window) { view->result_window = make_window(view, (WindowHandlers){.load = result_load, .unload = result_unload}); }
  if (view->result_window && !in_stack(view->result_window)) { window_stack_push(view->result_window, true); }
  result_layout(view);
}

void tb_compose_view_reload(TbComposeView *view) {
  if (!s_texts) { return; }
  if (view->menu) { menu_layer_reload_data(view->menu); }
  review_layout(view);
  result_layout(view);
}

void tb_compose_view_status(TbComposeView *view, const TbSendStatus *status) {
  if (!view->result_window || status->draft_id != view->result_draft) { return; }
  result_layout(view);
  if (status->phase == TB_SENDING_SENT && !view->auto_close) {
    vibes_short_pulse();
    view->auto_close = app_timer_register(TB_RESULT_CLOSE_MS, auto_closed, view);
  } else if (status->phase == TB_SENDING_FAILED || status->phase == TB_SENDING_UNKNOWN) {
    vibes_double_pulse();
  }
}

bool tb_compose_view_showing(const TbComposeView *view) {
  return in_stack(view->menu_window) || in_stack(view->review_window) || in_stack(view->result_window);
}

void tb_compose_view_close_all(TbComposeView *view) {
  Window *windows[] = {view->result_window, view->review_window, view->menu_window};
  for (size_t index = 0; index < sizeof(windows) / sizeof(windows[0]); ++index) {
    if (in_stack(windows[index])) { window_stack_remove(windows[index], index == 0); }
  }
}
