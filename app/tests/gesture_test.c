#include <assert.h>
#include "gesture.h"

int main(void) {
  TbGestureTracker tracker = {0};
  assert(tb_gesture_up(&tracker, 0, 0, 0) == TB_GESTURE_NONE);
  tb_gesture_down(&tracker, 100, 100, 1000);
  tb_gesture_move(&tracker, 104, 98);
  assert(tb_gesture_hold(&tracker, 1200) == TB_GESTURE_NONE);
  assert(tb_gesture_up(&tracker, 103, 101, 1100) == TB_GESTURE_TAP);

  tb_gesture_down(&tracker, 100, 100, 0);
  assert(tb_gesture_hold(&tracker, TB_GESTURE_HOLD_MS) == TB_GESTURE_HOLD);
  assert(tb_gesture_hold(&tracker, TB_GESTURE_HOLD_MS + 10) == TB_GESTURE_NONE);
  assert(tb_gesture_up(&tracker, 100, 100, 900) == TB_GESTURE_NONE);

  tb_gesture_down(&tracker, 100, 100, 0);
  assert(tb_gesture_up(&tracker, 100, 100, TB_GESTURE_HOLD_MS + 1) == TB_GESTURE_HOLD);

  tb_gesture_down(&tracker, 100, 150, 0);
  tb_gesture_move(&tracker, 102, 120);
  assert(tb_gesture_hold(&tracker, 900) == TB_GESTURE_NONE);
  assert(tb_gesture_up(&tracker, 103, 80, 200) == TB_GESTURE_UP);
  tb_gesture_down(&tracker, 100, 80, 0);
  assert(tb_gesture_up(&tracker, 95, 160, 200) == TB_GESTURE_DOWN);
  tb_gesture_down(&tracker, 40, 100, 0);
  assert(tb_gesture_up(&tracker, 150, 110, 200) == TB_GESTURE_RIGHT);
  tb_gesture_down(&tracker, 150, 100, 0);
  assert(tb_gesture_up(&tracker, 60, 95, 200) == TB_GESTURE_LEFT);
  tb_gesture_down(&tracker, 100, 100, 0);
  assert(tb_gesture_up(&tracker, 120, 110, 200) == TB_GESTURE_NONE);
  return 0;
}
