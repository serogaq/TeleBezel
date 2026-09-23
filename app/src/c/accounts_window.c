#include "accounts_window.h"
#include "format.h"
#include "generated/protocol.h"
#include "theme.h"

static uint16_t rows(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu; (void)section;
  const TbAccountsWindow *view = context;
  return view->session->count;
}

static int16_t row_height(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu; (void)index; (void)context;
  const TbTheme *theme = tb_theme();
  const int16_t height = (int16_t)(theme->title_height + theme->meta_height + 8);
  return height > theme->row_min ? height : theme->row_min;
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  const TbAccountsWindow *view = context;
  if (index->row >= view->session->count) { return; }
  const TbAccount *account = &view->session->accounts[index->row];
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const int16_t width = bounds.size.w - 2 * theme->margin - 4;
  const GTextAlignment alignment = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  const int16_t title_height = theme->title_height;
  graphics_context_set_text_color(ctx, tb_theme_text(highlighted));
  graphics_draw_text(ctx, account->name, theme->title, GRect(theme->margin + 2, 0, width, title_height),
                     GTextOverflowModeTrailingEllipsis, alignment, NULL);
  char subtitle[64];
  const char *state = tb_account_state_text(view->strings, account->state);
  if (account->flags & TB_ACCOUNT_FLAG_DEFAULT) {
    snprintf(subtitle, sizeof(subtitle), "%s · %s", view->strings->default_mark, state);
  } else {
    snprintf(subtitle, sizeof(subtitle), "%s", state);
  }
  graphics_context_set_text_color(ctx, account->state == TB_ACCOUNT_STATE_READY ? tb_theme_muted(highlighted) : tb_theme_accent(highlighted));
  graphics_draw_text(ctx, subtitle, theme->meta, GRect(theme->margin + 2, title_height, width, bounds.size.h - title_height),
                     GTextOverflowModeTrailingEllipsis, alignment, NULL);
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbAccountsWindow *view = context;
  view->selected = index->row;
  if (view->actions.activate) { view->actions.activate(view->actions.context, index->row); }
}

static void select_long_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbAccountsWindow *view = context;
  view->selected = index->row;
  if (view->actions.make_default) { view->actions.make_default(view->actions.context, index->row); }
}

static void selection_changed(MenuLayer *menu, MenuIndex now, MenuIndex before, void *context) {
  (void)menu; (void)before;
  TbAccountsWindow *view = context;
  view->selected = now.row;
}

static void window_load(Window *window) {
  TbAccountsWindow *view = window_get_user_data(window);
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
  if (view->selected > 0 && view->selected < view->session->count) {
    menu_layer_set_selected_index(view->menu, (MenuIndex){0, (uint16_t)view->selected}, MenuRowAlignCenter, false);
  }
}

static void window_unload(Window *window) {
  TbAccountsWindow *view = window_get_user_data(window);
  menu_layer_destroy(view->menu);
  view->menu = NULL;
}

void tb_accounts_window_init(TbAccountsWindow *view, const TbSession *session, const TbStrings *strings, TbAccountsActions actions) {
  view->session = session;
  view->strings = strings;
  view->actions = actions;
  view->selected = 0;
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload});
}

void tb_accounts_window_deinit(TbAccountsWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_accounts_window_reload(TbAccountsWindow *view, int selected) {
  view->selected = selected >= 0 && selected < view->session->count ? selected : 0;
  if (!view->menu) { return; }
  menu_layer_reload_data(view->menu);
  if (view->session->count > 0) {
    menu_layer_set_selected_index(view->menu, (MenuIndex){0, (uint16_t)view->selected}, MenuRowAlignCenter, false);
  }
}
