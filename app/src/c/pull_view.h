#pragma once
#include <pebble.h>
#include "notify_view.h"
#include "pull.h"

typedef struct {
  AppTimer *timer;
  TbPull *pull;
} TbPullClock;

typedef struct {
  int16_t start;
  bool armed;
} TbPullTouch;

uint32_t tb_now_ms(void *context);
bool tb_pull_clock_schedule(void *clock, uint32_t milliseconds);
void tb_pull_clock_cancel(void *clock);
void tb_pull_draw(GContext *ctx, GPoint center, GColor color, int16_t size, uint16_t level);
void tb_menu_repeat_start(MenuLayer *menu);
void tb_menu_repeat_stop(void);
void tb_pull_touch(TbPullTouch *touch, TbPull *pull, TbNotifyView *notice, const TouchEvent *event, bool edge, bool upward);
