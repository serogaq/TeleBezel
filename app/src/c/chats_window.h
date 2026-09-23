#pragma once
#include <pebble.h>
#include "chats.h"
#include "generated/localization.h"

typedef struct {
  void (*activate)(void *context, int index);
  void *context;
} TbChatsActions;

typedef struct {
  Window *window;
  MenuLayer *menu;
  TbChats *chats;
  const TbStrings *strings;
  TbChatsActions actions;
  char selected_id[TB_TELEGRAM_ID_SIZE];
  uint16_t selected_row;
  bool placed;
  char buffer[400];
} TbChatsWindow;

void tb_chats_window_init(TbChatsWindow *view, TbChats *chats, const TbStrings *strings, TbChatsActions actions);
void tb_chats_window_deinit(TbChatsWindow *view);
void tb_chats_window_reset(TbChatsWindow *view);
void tb_chats_window_reload(TbChatsWindow *view);
