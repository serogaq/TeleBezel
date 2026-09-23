#pragma once
#include <pebble.h>
#include "generated/localization.h"
#include "history.h"
#include "message_text.h"

typedef struct {
  Window *window;
  ScrollLayer *scroll;
  TextLayer *header;
  TextLayer *body;
  TextLayer *footer;
  TbMessageText *text;
  const TbStrings *strings;
  TbMessage message;
  bool private_chat;
  char sender[33];
  char extra[49];
  char header_text[224];
  char footer_text[160];
  char *fallback;
} TbReaderWindow;

void tb_reader_window_init(TbReaderWindow *view, TbMessageText *text, const TbStrings *strings);
void tb_reader_window_deinit(TbReaderWindow *view);
void tb_reader_window_show(TbReaderWindow *view, const TbMessage *message, bool private_chat);
void tb_reader_window_reload(TbReaderWindow *view);
