#pragma once
#include <pebble.h>
#include "compose.h"
#include "generated/localization.h"
#include "send_tracker.h"

typedef enum { TB_COMPOSE_SEND, TB_COMPOSE_SEND_AGAIN, TB_COMPOSE_SEND_ANYWAY } TbComposeSendMode;

typedef struct {
  void (*dictate)(void *context);
  bool (*send)(void *context, TbComposeSendMode mode);
  void (*check)(void *context);
  void (*open_chat)(void *context);
  void (*finished)(void *context, bool sent);
  void *context;
} TbComposeActions;

typedef struct {
  TbCompose *compose;
  TbSendTracker *tracker;
  const TbStrings *strings;
  TbComposeActions actions;
  bool dictation;
  bool confirm_anyway;
  bool busy_notice;
  uint32_t result_draft;
  Window *menu_window;
  MenuLayer *menu;
  Window *review_window;
  ScrollLayer *review_scroll;
  TextLayer *review_header;
  TextLayer *review_body;
  TextLayer *review_footer;
  Window *result_window;
  TextLayer *result_title;
  TextLayer *result_body;
  TextLayer *result_hint;
  AppTimer *auto_close;
} TbComposeView;

void tb_compose_view_init(TbComposeView *view, TbCompose *compose, TbSendTracker *tracker, const TbStrings *strings, TbComposeActions actions);
void tb_compose_view_open(TbComposeView *view, bool dictation);
void tb_compose_view_review(TbComposeView *view);
void tb_compose_view_result(TbComposeView *view);
void tb_compose_view_reload(TbComposeView *view);
void tb_compose_view_status(TbComposeView *view, const TbSendStatus *status);
bool tb_compose_view_showing(const TbComposeView *view);
void tb_compose_view_close_all(TbComposeView *view);
