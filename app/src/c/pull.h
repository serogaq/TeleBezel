#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB_PULL_FULL 1000
#define TB_PULL_HALF 500
#define TB_PULL_FRAME 33
#define TB_PULL_WINDOW 450
#define TB_PULL_TAP_RISE 150
#define TB_PULL_TAP_FALL 300
#define TB_PULL_DOUBLE_RISE 120
#define TB_PULL_HOLD 120
#define TB_PULL_FADE 300
#define TB_PULL_RETURN 250

typedef enum {
  TB_PULL_IDLE,
  TB_PULL_RISING_HALF,
  TB_PULL_WAITING,
  TB_PULL_RISING_FULL,
  TB_PULL_HOLDING,
  TB_PULL_FALLING,
  TB_PULL_DRAGGING
} TbPullPhase;

typedef struct {
  void (*changed)(void *context);
  void (*trigger)(void *context);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  uint32_t (*now)(void *context);
  void *context;
} TbPullPorts;

typedef struct {
  TbPullPorts ports;
  TbPullPhase phase;
  uint16_t level;
  uint16_t from;
  uint16_t to;
  uint32_t started;
  uint32_t duration;
  uint32_t armed_until;
} TbPull;

void tb_pull_init(TbPull *pull, TbPullPorts ports);
void tb_pull_press(TbPull *pull);
void tb_pull_drag(TbPull *pull, uint16_t level);
void tb_pull_release(TbPull *pull);
void tb_pull_tick(TbPull *pull);
void tb_pull_reset(TbPull *pull);
