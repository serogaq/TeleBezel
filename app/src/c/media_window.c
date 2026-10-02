#include "media_window.h"
#include <string.h>
#include "generated/protocol.h"
#include "pull_view.h"
#include "theme.h"

#define TB_OVERLAY_MS 2000

static TbMediaWindow *s_touch_view;

void tb_media_status(const TbMedia *media, const TbStrings *strings, uint8_t kind, bool compact, char *out, size_t size) {
  const char *text = compact ? strings->media_short_error : strings->media_error;
  switch (media->phase) {
    case TB_MEDIA_IDLE: text = compact ? strings->media_short_idle : strings->media_idle; break;
    case TB_MEDIA_LOADING: text = strings->loading; break;
    case TB_MEDIA_WAITING:
      if (compact) { text = strings->loading; }
      else { text = media->server_state == TB_MEDIA_STATE_PREPARING ? strings->media_preparing : strings->media_downloading; }
      break;
    case TB_MEDIA_TRANSFER:
      snprintf(out, size, compact ? "%d%%" : strings->media_transfer, tb_media_percent(media));
      return;
    case TB_MEDIA_READY: text = strings->media_ready; break;
    case TB_MEDIA_SPOILER: text = compact ? strings->media_short_spoiler : strings->media_hidden; break;
    case TB_MEDIA_RESTRICTED:
      if (kind == TB_KIND_PAID_MEDIA) { text = compact ? strings->media_short_paid : strings->media_paid; }
      else { text = compact ? strings->media_short_disappearing : strings->media_disappearing_text; }
      break;
    case TB_MEDIA_NONE:
    case TB_MEDIA_UNSUPPORTED: text = compact ? strings->media_short_unsupported : strings->media_unsupported; break;
    case TB_MEDIA_UNAVAILABLE:
      if (compact) { text = strings->media_short_unavailable; }
      else { text = kind == TB_KIND_EXPIRED ? strings->media_expired : strings->media_unavailable; }
      break;
    case TB_MEDIA_NO_MEMORY: text = compact ? strings->media_short_no_memory : strings->media_no_memory; break;
    default: break;
  }
  snprintf(out, size, "%s", text);
}

static void draw_image(TbMediaWindow *view, GContext *ctx, GRect bounds) {
  const TbImageDecoder *image = &view->media->image;
  if (!image->pixels || !image->header_ready) { return; }
  GBitmap *frame = graphics_capture_frame_buffer(ctx);
  if (!frame) { return; }
  const TbImageHeader *header = &image->header;
  const int16_t shift_x = (int16_t)((bounds.size.w - header->canvas_width) / 2);
  const int16_t shift_y = (int16_t)((bounds.size.h - header->canvas_height) / 2);
  uint32_t offset = 0;
  TbImageRow row;
  for (uint16_t y = 0; y < header->height; ++y) {
    tb_image_row(header, y, &row);
    if (offset + row.bytes > image->filled) { break; }
    const int16_t screen_y = (int16_t)(row.y + shift_y);
    if (row.count && screen_y >= 0 && screen_y < bounds.size.h) {
      const GBitmapDataRowInfo info = gbitmap_get_data_row_info(frame, (uint16_t)screen_y);
      const uint8_t *source = image->pixels + offset;
      int16_t from = (int16_t)(row.x + shift_x);
      int16_t to = (int16_t)(from + row.count - 1);
      const int16_t first = from;
      if (from < info.min_x) { from = info.min_x; }
      if (to > info.max_x) { to = info.max_x; }
      for (int16_t x = from; x <= to; ++x) { info.data[x] = header->palette[tb_image_pixel(header, source, (uint16_t)(x - first))]; }
    }
    offset += row.bytes;
  }
  graphics_release_frame_buffer(ctx, frame);
}

static void draw_label(GContext *ctx, GRect bounds, const char *text, int16_t y, bool boxed) {
  const TbTheme *theme = tb_theme();
  const int16_t inset = (int16_t)(theme->round ? 36 : 6);
  const GRect box = GRect(inset, y, (int16_t)(bounds.size.w - 2 * inset), theme->meta_height * 2);
  const GSize size = graphics_text_layout_get_content_size(text, theme->meta_bold, box, GTextOverflowModeWordWrap, GTextAlignmentCenter);
  if (boxed) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect((int16_t)((bounds.size.w - size.w) / 2 - 4), y, (int16_t)(size.w + 8), (int16_t)(size.h + 6)), 3, GCornersAll);
  }
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, theme->meta_bold, GRect(box.origin.x, (int16_t)(y - 1), box.size.w, (int16_t)(size.h + 4)), GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void draw(Layer *layer, GContext *ctx) {
  TbMediaWindow *view = s_touch_view;
  const GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  if (!view) { return; }
  const TbMedia *media = view->media;
  draw_image(view, ctx, bounds);
  const bool ready = media->phase == TB_MEDIA_READY;
  char text[96];
  if (!ready || view->overlay) {
    tb_media_status(media, view->strings, view->kind, false, text, sizeof(text));
    if (!ready) { draw_label(ctx, bounds, text, (int16_t)(tb_media_has_image(media) ? bounds.size.h - 64 : bounds.size.h / 2 - 20), tb_media_has_image(media)); }
  }
  if (media->count > 1 && (view->overlay || !ready)) {
    snprintf(text, sizeof(text), view->strings->media_position, media->index + 1, media->count);
    draw_label(ctx, bounds, text, (int16_t)(tb_theme()->round ? 16 : 4), true);
  }
}

static void overlay_done(void *context) {
  TbMediaWindow *view = context;
  view->overlay_timer = NULL;
  view->overlay = false;
  if (view->canvas) { layer_mark_dirty(view->canvas); }
}

static void flash(TbMediaWindow *view) {
  view->overlay = true;
  if (view->overlay_timer) { app_timer_cancel(view->overlay_timer); }
  view->overlay_timer = app_timer_register(TB_OVERLAY_MS, overlay_done, view);
  if (view->canvas) { layer_mark_dirty(view->canvas); }
}

static void step(TbMediaWindow *view, int delta) {
  TbMedia *media = view->media;
  const int target = media->index + delta;
  if (target < 0 || target >= media->count) {
    vibes_short_pulse();
    return;
  }
  tb_media_show(media, (uint8_t)target);
  flash(view);
}

static void activate(TbMediaWindow *view) {
  if (view->media->phase == TB_MEDIA_SPOILER) { tb_media_reveal(view->media); }
}

static void open_menu(TbMediaWindow *view) {
  if (view->actions.menu) { view->actions.menu(view->actions.context); }
}

static void up_clicked(ClickRecognizerRef recognizer, void *context) { (void)recognizer; step(context, -1); }
static void down_clicked(ClickRecognizerRef recognizer, void *context) { (void)recognizer; step(context, 1); }
static void select_clicked(ClickRecognizerRef recognizer, void *context) { (void)recognizer; activate(context); }
static void select_held(ClickRecognizerRef recognizer, void *context) { (void)recognizer; open_menu(context); }

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_clicked);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_clicked);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_clicked);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_held, NULL);
  (void)context;
}

static void run(void *context, TbGesture gesture) {
  TbMediaWindow *view = context;
  switch (gesture) {
    case TB_GESTURE_TAP: activate(view); break;
    case TB_GESTURE_HOLD:
    case TB_GESTURE_LEFT: open_menu(view); break;
    case TB_GESTURE_RIGHT: window_stack_remove(view->window, true); break;
    case TB_GESTURE_UP: step(view, 1); break;
    case TB_GESTURE_DOWN: step(view, -1); break;
    default: break;
  }
}

static void window_load(Window *window) {
  TbMediaWindow *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(view->canvas, draw);
  layer_add_child(root, view->canvas);
  s_touch_view = view;
  flash(view);
}

static void window_appear(Window *window) {
  TbMediaWindow *view = window_get_user_data(window);
  tb_touch_attach(&view->touch);
}

static void window_disappear(Window *window) {
  TbMediaWindow *view = window_get_user_data(window);
  tb_touch_detach(&view->touch);
}

static void window_unload(Window *window) {
  TbMediaWindow *view = window_get_user_data(window);
  if (view->overlay_timer) { app_timer_cancel(view->overlay_timer); }
  view->overlay_timer = NULL;
  layer_destroy(view->canvas);
  view->canvas = NULL;
  if (s_touch_view == view) { s_touch_view = NULL; }
}

void tb_media_window_init(TbMediaWindow *view, TbMedia *media, const TbStrings *strings, TbMediaActions actions) {
  memset(view, 0, sizeof(*view));
  view->media = media;
  view->strings = strings;
  view->actions = actions;
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_background_color(view->window, GColorBlack);
  window_set_click_config_provider_with_context(view->window, click_config, view);
  tb_touch_init(&view->touch, view->window, run, NULL, view);
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload, .appear = window_appear,
                                                             .disappear = window_disappear});
}

void tb_media_window_deinit(TbMediaWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_media_window_show(TbMediaWindow *view, uint8_t kind) {
  view->kind = kind;
  if (!window_stack_contains_window(view->window)) { window_stack_push(view->window, true); }
}

void tb_media_window_reload(TbMediaWindow *view) {
  if (!view->canvas) { return; }
  if (view->media->phase == TB_MEDIA_READY && view->phase != TB_MEDIA_READY && view->media->count > 1) { flash(view); }
  view->phase = (uint8_t)view->media->phase;
  layer_mark_dirty(view->canvas);
}
