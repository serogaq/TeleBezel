#include "gesture.h"

static int16_t distance(int16_t value) { return value < 0 ? (int16_t)-value : value; }

void tb_gesture_down(TbGestureTracker *tracker, int16_t x, int16_t y, uint32_t now) {
  tracker->x = x;
  tracker->y = y;
  tracker->last_x = x;
  tracker->last_y = y;
  tracker->since = now;
  tracker->down = true;
  tracker->moved = false;
  tracker->held = false;
}

void tb_gesture_move(TbGestureTracker *tracker, int16_t x, int16_t y) {
  if (!tracker->down) { return; }
  tracker->last_x = x;
  tracker->last_y = y;
  if (distance((int16_t)(x - tracker->x)) > TB_GESTURE_SLOP || distance((int16_t)(y - tracker->y)) > TB_GESTURE_SLOP) { tracker->moved = true; }
}

TbGesture tb_gesture_hold(TbGestureTracker *tracker, uint32_t now) {
  if (!tracker->down || tracker->moved || tracker->held || now - tracker->since < TB_GESTURE_HOLD_MS) { return TB_GESTURE_NONE; }
  tracker->held = true;
  return TB_GESTURE_HOLD;
}

TbGesture tb_gesture_up(TbGestureTracker *tracker, int16_t x, int16_t y, uint32_t now) {
  if (!tracker->down) { return TB_GESTURE_NONE; }
  tb_gesture_move(tracker, x, y);
  tracker->down = false;
  if (tracker->held) { return TB_GESTURE_NONE; }
  const int16_t dx = (int16_t)(tracker->last_x - tracker->x);
  const int16_t dy = (int16_t)(tracker->last_y - tracker->y);
  if (!tracker->moved) { return now - tracker->since >= TB_GESTURE_HOLD_MS ? TB_GESTURE_HOLD : TB_GESTURE_TAP; }
  if (distance(dx) >= distance(dy)) {
    if (distance(dx) < TB_GESTURE_SWIPE) { return TB_GESTURE_NONE; }
    return dx > 0 ? TB_GESTURE_RIGHT : TB_GESTURE_LEFT;
  }
  if (distance(dy) < TB_GESTURE_SWIPE) { return TB_GESTURE_NONE; }
  return dy > 0 ? TB_GESTURE_DOWN : TB_GESTURE_UP;
}
