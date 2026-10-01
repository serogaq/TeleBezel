#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB_GESTURE_SLOP 12
#define TB_GESTURE_SWIPE 30
#define TB_GESTURE_HOLD_MS 500

typedef enum { TB_GESTURE_NONE, TB_GESTURE_TAP, TB_GESTURE_HOLD, TB_GESTURE_UP, TB_GESTURE_DOWN, TB_GESTURE_LEFT, TB_GESTURE_RIGHT } TbGesture;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t last_x;
  int16_t last_y;
  uint32_t since;
  bool down;
  bool moved;
  bool held;
} TbGestureTracker;

void tb_gesture_down(TbGestureTracker *tracker, int16_t x, int16_t y, uint32_t now);
void tb_gesture_move(TbGestureTracker *tracker, int16_t x, int16_t y);
TbGesture tb_gesture_up(TbGestureTracker *tracker, int16_t x, int16_t y, uint32_t now);
TbGesture tb_gesture_hold(TbGestureTracker *tracker, uint32_t now);
