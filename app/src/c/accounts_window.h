#pragma once
#include <pebble.h>
#include "generated/localization.h"
#include "session.h"

typedef struct {
  void (*activate)(void *context, int index);
  void (*make_default)(void *context, int index);
  void *context;
} TbAccountsActions;

typedef struct {
  Window *window;
  MenuLayer *menu;
  const TbSession *session;
  const TbStrings *strings;
  TbAccountsActions actions;
  int selected;
} TbAccountsWindow;

void tb_accounts_window_init(TbAccountsWindow *view, const TbSession *session, const TbStrings *strings, TbAccountsActions actions);
void tb_accounts_window_deinit(TbAccountsWindow *view);
void tb_accounts_window_reload(TbAccountsWindow *view, int selected);
