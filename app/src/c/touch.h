#pragma once
#include <pebble.h>
#include "gesture.h"

typedef void (*TbTouchHandler)(void *context, TbGesture gesture);
typedef void (*TbTouchRaw)(void *context, const TouchEvent *event);

typedef struct {
  Window *window;
  TbGestureTracker tracker;
  AppTimer *hold;
  TbTouchHandler handler;
  TbTouchRaw raw;
  void *context;
} TbTouch;

void tb_touch_init(TbTouch *touch, Window *window, TbTouchHandler handler, TbTouchRaw raw, void *context);
void tb_touch_attach(TbTouch *touch);
void tb_touch_detach(TbTouch *touch);
