#include "pull_view.h"

static AppTimer *s_repeat;

uint32_t tb_now_ms(void *context) {
  (void)context;
  time_t seconds = 0;
  uint16_t milliseconds = 0;
  time_ms(&seconds, &milliseconds);
  return (uint32_t)seconds * 1000u + milliseconds;
}

static void clock_fired(void *context) {
  TbPullClock *clock = context;
  clock->timer = NULL;
  tb_pull_tick(clock->pull);
}

void tb_pull_clock_cancel(void *context) {
  TbPullClock *clock = context;
  if (clock->timer) {
    app_timer_cancel(clock->timer);
    clock->timer = NULL;
  }
}

bool tb_pull_clock_schedule(void *context, uint32_t milliseconds) {
  TbPullClock *clock = context;
  tb_pull_clock_cancel(clock);
  clock->timer = app_timer_register(milliseconds, clock_fired, clock);
  return clock->timer != NULL;
}

static void fill(GContext *ctx, GRect box, GColor color, uint16_t level) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_antialiased(ctx, false);
  const int32_t size = box.size.h;
  const int32_t center2 = size - 1;
  const int32_t radius2 = size - 1;
  const int32_t surface = (int32_t)(size - (int32_t)level * size / 1000);
  for (int32_t row = size - 1; row >= surface && row >= 0; --row) {
    const int32_t dy = 2 * row - center2;
    for (int32_t column = 0; column < size; ++column) {
      const int32_t dx = 2 * column - center2;
      if (dx * dx + dy * dy <= radius2 * radius2) {
        graphics_draw_pixel(ctx, GPoint((int16_t)(box.origin.x + column), (int16_t)(box.origin.y + row)));
      }
    }
  }
}

void tb_pull_draw(GContext *ctx, GPoint center, GColor color, int16_t size, uint16_t level) {
  if (size < 1) { return; }
  const GRect box = GRect(center.x - size / 2, center.y - size / 2, size, size);
  if (size < 4) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, box, 0, GCornerNone);
    return;
  }
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, size >= 20 ? 2 : 1);
  graphics_context_set_antialiased(ctx, true);
  graphics_draw_circle(ctx, GPoint(box.origin.x + size / 2, box.origin.y + size / 2), (uint16_t)(size / 2));
  fill(ctx, box, color, level);
}

static void repeat(void *context) {
  MenuLayer *menu = context;
  s_repeat = NULL;
  if (menu_layer_get_selected_index(menu).row == 0) { return; }
  menu_layer_set_selected_next(menu, true, MenuRowAlignCenter, true);
  s_repeat = app_timer_register(100, repeat, menu);
}

void tb_menu_repeat_stop(void) {
  if (s_repeat) {
    app_timer_cancel(s_repeat);
    s_repeat = NULL;
  }
}

void tb_menu_repeat_start(MenuLayer *menu) {
  tb_menu_repeat_stop();
  menu_layer_set_selected_next(menu, true, MenuRowAlignCenter, true);
  s_repeat = app_timer_register(400, repeat, menu);
}

#define TB_PULL_DISTANCE 60

void tb_pull_touch(TbPullTouch *touch, TbPull *pull, TbNotifyView *notice, const TouchEvent *event, bool edge, bool upward) {
  if (event->type == TouchEvent_Touchdown) {
    touch->armed = false;
    if (tb_notify_view_hit(notice, event->y)) {
      tb_notify_view_activate(notice);
      return;
    }
    touch->start = event->y;
    touch->armed = edge;
    return;
  }
  if (!touch->armed) { return; }
  if (event->type == TouchEvent_PositionUpdate) {
    const int32_t distance = upward ? touch->start - event->y : event->y - touch->start;
    if (distance > 0 || pull->phase == TB_PULL_DRAGGING) {
      tb_pull_drag(pull, (uint16_t)(distance > 0 ? distance * TB_PULL_FULL / TB_PULL_DISTANCE : 0));
    }
  } else {
    touch->armed = false;
    tb_pull_release(pull);
  }
}
