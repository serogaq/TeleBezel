#include "notify_view.h"
#include "icons.h"
#include "theme.h"

int16_t tb_notify_view_height(void) {
  return (int16_t)(2 * tb_theme()->meta_height + 8);
}

static GRect banner(const TbNotifyView *view, GRect bounds) {
  const int16_t height = tb_notify_view_height();
#if defined(PBL_ROUND)
  const int16_t width = (int16_t)(bounds.size.w * 3 / 4);
  return GRect((bounds.size.w - width) / 2, view->top, width, height);
#else
  return GRect(4, view->top + 2, bounds.size.w - 8, height);
#endif
}

static GColor fill_of(TbNotifyLevel level) {
  switch (level) {
    case TB_NOTIFY_ERROR: return GColorDarkCandyAppleRed;
    case TB_NOTIFY_WARNING: return GColorWindsorTan;
    default: return GColorOxfordBlue;
  }
}

static void draw(Layer *layer, GContext *ctx) {
  TbNotifyView *view = *(TbNotifyView **)layer_get_data(layer);
  const TbNotification *item = tb_notify_top(view->notify);
  if (!item) { return; }
  const TbTheme *theme = tb_theme();
  const GRect bounds = layer_get_bounds(layer);
  const bool focused = view->notify->focused;
  const GColor ink = GColorWhite;
  graphics_context_set_fill_color(ctx, fill_of(item->level));
  graphics_fill_rect(ctx, bounds, 6, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, focused ? 3 : 1);
  graphics_draw_round_rect(ctx, focused ? grect_inset(bounds, GEdgeInsets(1)) : bounds, 6);
  const int16_t icon = 12;
  const int16_t left = (int16_t)(PBL_IF_ROUND_ELSE(10, 4));
  const GRect glyph = GRect(left, (int16_t)((theme->meta_height - icon) / 2 + 3), icon, icon);
  if (item->action == TB_NOTIFY_WAIT || (item->level != TB_NOTIFY_INFO && item->action == TB_NOTIFY_CHECK)) { tb_icon_clock(ctx, glyph, ink); }
  else if (item->level == TB_NOTIFY_INFO) { tb_icon_check(ctx, glyph, ink); }
  else { tb_icon_alert(ctx, glyph, ink); }
  const int16_t text_left = (int16_t)(left + icon + 4);
  const int16_t width = (int16_t)(bounds.size.w - text_left - 4);
  graphics_context_set_text_color(ctx, ink);
  graphics_draw_text(ctx, item->title, theme->meta_bold, GRect(text_left, 0, width, theme->meta_height), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, item->body, theme->meta, GRect(text_left, theme->meta_height, width, theme->meta_height + 2),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

void tb_notify_view_init(TbNotifyView *view, TbNotify *notify, TbNotifyActivate activate, void *context) {
  view->notify = notify;
  view->layer = NULL;
  view->top = 0;
  view->activate = activate;
  view->context = context;
}

void tb_notify_view_attach(TbNotifyView *view, Layer *root, int16_t top) {
  view->top = top;
  if (!view->layer) {
    view->layer = layer_create_with_data(banner(view, layer_get_bounds(root)), sizeof(TbNotifyView *));
    if (!view->layer) { return; }
    *(TbNotifyView **)layer_get_data(view->layer) = view;
    layer_set_update_proc(view->layer, draw);
    layer_add_child(root, view->layer);
  }
  tb_notify_view_refresh(view);
}

void tb_notify_view_detach(TbNotifyView *view) {
  if (!view->layer) { return; }
  layer_remove_from_parent(view->layer);
  layer_destroy(view->layer);
  view->layer = NULL;
}

void tb_notify_view_refresh(TbNotifyView *view) {
  if (!view->layer) { return; }
  Layer *parent = layer_get_window(view->layer) ? window_get_root_layer(layer_get_window(view->layer)) : NULL;
  if (parent) { layer_set_frame(view->layer, banner(view, layer_get_bounds(parent))); }
  layer_set_hidden(view->layer, tb_notify_top(view->notify) == NULL);
  layer_mark_dirty(view->layer);
}

bool tb_notify_view_visible(const TbNotifyView *view) { return view->layer && tb_notify_top(view->notify) != NULL; }

bool tb_notify_view_hit(const TbNotifyView *view, int16_t y) {
  if (!tb_notify_view_visible(view)) { return false; }
  const GRect frame = layer_get_frame(view->layer);
  return y >= frame.origin.y && y < frame.origin.y + frame.size.h;
}

void tb_notify_view_activate(TbNotifyView *view) {
  const TbNotification *item = tb_notify_top(view->notify);
  if (item && view->activate) { view->activate(view->context, item); }
}
