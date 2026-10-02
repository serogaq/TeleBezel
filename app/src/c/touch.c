#include "touch.h"
#include "pull_view.h"

void tb_touch_init(TbTouch *touch, Window *window, TbTouchHandler handler, TbTouchRaw raw, void *context) {
  touch->window = window;
  touch->handler = handler;
  touch->raw = raw;
  touch->context = context;
  touch->hold = NULL;
  touch->tracker.down = false;
#if defined(PBL_TOUCH)
  if (!raw) { window_set_touch_bridge_disabled(window, true); }
#endif
}

#if defined(PBL_TOUCH)
static TbTouch *s_touch;

static void stop_hold(TbTouch *touch) {
  if (touch->hold) { app_timer_cancel(touch->hold); }
  touch->hold = NULL;
}

static void held(void *context) {
  TbTouch *touch = context;
  touch->hold = NULL;
  const TbGesture gesture = tb_gesture_hold(&touch->tracker, touch->tracker.since + TB_GESTURE_HOLD_MS);
  if (gesture != TB_GESTURE_NONE) { touch->handler(touch->context, gesture); }
}

static void touched(const TouchEvent *event, void *context) {
  (void)context;
  TbTouch *touch = s_touch;
  if (!touch || event->non_navigational || window_stack_get_top_window() != touch->window) { return; }
  if (touch->raw) {
    touch->raw(touch->context, event);
    return;
  }
  TbGesture gesture = TB_GESTURE_NONE;
  if (event->type == TouchEvent_Touchdown) {
    tb_gesture_down(&touch->tracker, event->x, event->y, tb_now_ms(NULL));
    stop_hold(touch);
    touch->hold = app_timer_register(TB_GESTURE_HOLD_MS, held, touch);
  } else if (event->type == TouchEvent_PositionUpdate) {
    tb_gesture_move(&touch->tracker, event->x, event->y);
  } else {
    stop_hold(touch);
    gesture = tb_gesture_up(&touch->tracker, event->x, event->y, tb_now_ms(NULL));
  }
  touch->handler(touch->context, gesture);
}
#endif

void tb_touch_attach(TbTouch *touch) {
#if defined(PBL_TOUCH)
  if (!touch_service_is_enabled()) { return; }
  s_touch = touch;
  touch_service_subscribe(touched, NULL);
#else
  (void)touch;
#endif
}

void tb_touch_detach(TbTouch *touch) {
#if defined(PBL_TOUCH)
  stop_hold(touch);
  touch->tracker.down = false;
  if (s_touch != touch) { return; }
  s_touch = NULL;
  if (touch_service_is_enabled()) { touch_service_unsubscribe(); }
#else
  (void)touch;
#endif
}
