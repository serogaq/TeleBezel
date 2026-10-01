#pragma once
#include <pebble.h>
#include "generated/localization.h"
#include "history.h"
#include "media.h"
#include "message_text.h"
#include "touch.h"

typedef struct {
  void (*menu)(void *context);
  void (*view)(void *context);
  void *context;
} TbReaderActions;

typedef struct {
  Window *window;
  ScrollLayer *scroll;
  TextLayer *header;
  Layer *media_box;
  TextLayer *media_line;
  TextLayer *quote_label;
  TextLayer *quote_body;
  TextLayer *body;
  TextLayer *footer;
  TbMessageText *text;
  TbMessageText *quote;
  TbMedia *media;
  const TbStrings *strings;
  TbReaderActions actions;
  TbMessage message;
  char sender[49];
  char forward[49];
  char reply_sender[49];
  char quote_label_text[96];
  char extra[49];
  char reply[97];
  char header_text[224];
  char footer_text[160];
  char media_text[96];
  char *fallback;
  TbTouch touch;
  bool focused;
} TbReaderWindow;

void tb_reader_window_init(TbReaderWindow *view, TbMessageText *text, TbMessageText *quote, TbMedia *media, const TbStrings *strings,
                           TbReaderActions actions);
void tb_reader_window_deinit(TbReaderWindow *view);
void tb_reader_window_show(TbReaderWindow *view, const TbMessage *message, const char *sender, const char *forward);
void tb_reader_window_reload(TbReaderWindow *view);
