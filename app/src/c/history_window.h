#pragma once
#include <pebble.h>
#include "generated/localization.h"
#include "history.h"
#include "notify_view.h"
#include "pull.h"

typedef struct {
  void (*activate)(void *context, int index);
  void (*closed)(void *context);
  void (*compose)(void *context);
  void (*menu)(void *context, int index);
  void *context;
} TbHistoryActions;

typedef struct {
  Window *window;
  MenuLayer *menu;
  Layer *pull_layer;
  TbPull pull;
  AppTimer *pull_timer;
  TbHistory *history;
  TbNotifyView notice;
  const TbStrings *strings;
  TbHistoryActions actions;
  uint8_t send;
  char selected_id[TB_TELEGRAM_ID_SIZE];
  char anchor_id[TB_TELEGRAM_ID_SIZE];
  uint16_t selected_row;
  bool follow_bottom;
  bool placed;
  bool restoring;
  int16_t width;
  char buffer[480];
} TbHistoryWindow;

void tb_history_window_init(TbHistoryWindow *view, TbHistory *history, TbNotify *notify, TbNotifyActivate activate, const TbStrings *strings,
                            TbHistoryActions actions);
void tb_history_window_deinit(TbHistoryWindow *view);
void tb_history_window_reset(TbHistoryWindow *view);
void tb_history_window_reload(TbHistoryWindow *view);
void tb_history_window_set_send(TbHistoryWindow *view, uint8_t send);
void tb_history_window_follow(TbHistoryWindow *view);
bool tb_history_window_writable(const TbHistoryWindow *view);
