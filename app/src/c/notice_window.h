#pragma once
#include <pebble.h>

typedef void (*TbNoticeAction)(void *context);

typedef struct {
  Window *window;
  ScrollLayer *scroll;
  TextLayer *title;
  TextLayer *body;
  TextLayer *hint;
  char body_text[256];
  const char *title_text;
  const char *hint_text;
  TbNoticeAction action;
  void *context;
} TbNotice;

void tb_notice_init(TbNotice *notice);
void tb_notice_deinit(TbNotice *notice);
void tb_notice_set(TbNotice *notice, const char *title, const char *body, const char *hint, TbNoticeAction action, void *context);
void tb_notice_set_body(TbNotice *notice, const char *first, const char *second);
