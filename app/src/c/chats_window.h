#pragma once
#include <pebble.h>
#include "chats.h"
#include "connection.h"
#include "notify_view.h"
#include "pull.h"
#include "generated/localization.h"

typedef struct {
  void (*activate)(void *context, int index);
  void *context;
} TbChatsActions;

typedef struct {
  Window *window;
  MenuLayer *menu;
  Layer *bar;
  TbPull pull;
  AppTimer *pull_timer;
  TbChats *chats;
  TbConnection *connection;
  TbNotifyView notice;
  const TbStrings *strings;
  TbChatsActions actions;
  bool show_archive;
  uint8_t unread_mode;
  char selected_id[TB_TELEGRAM_ID_SIZE];
  uint16_t selected_row;
  bool placed;
} TbChatsWindow;

void tb_chats_window_init(TbChatsWindow *view, TbChats *chats, TbConnection *connection, TbNotify *notify, TbNotifyActivate activate,
                          const TbStrings *strings, TbChatsActions actions);
void tb_chats_window_deinit(TbChatsWindow *view);
void tb_chats_window_configure(TbChatsWindow *view, bool show_archive, uint8_t unread_mode);
void tb_chats_window_reset(TbChatsWindow *view);
void tb_chats_window_reload(TbChatsWindow *view);
