#pragma once
#include <pebble.h>
#include "generated/localization.h"
#include "history.h"

typedef struct {
  void (*activate)(void *context, int index);
  void (*closed)(void *context);
  void *context;
} TbHistoryActions;

typedef struct {
  Window *window;
  MenuLayer *menu;
  TbHistory *history;
  const TbStrings *strings;
  TbHistoryActions actions;
  char selected_id[TB_TELEGRAM_ID_SIZE];
  char anchor_id[TB_TELEGRAM_ID_SIZE];
  uint16_t selected_row;
  bool follow_bottom;
  bool placed;
  int16_t width;
  char buffer[480];
} TbHistoryWindow;

void tb_history_window_init(TbHistoryWindow *view, TbHistory *history, const TbStrings *strings, TbHistoryActions actions);
void tb_history_window_deinit(TbHistoryWindow *view);
void tb_history_window_reset(TbHistoryWindow *view);
void tb_history_window_reload(TbHistoryWindow *view);
