#pragma once
#include <pebble.h>
#include "generated/localization.h"
#include "touch.h"
#include "media.h"

typedef struct {
  void (*menu)(void *context);
  void *context;
} TbMediaActions;

typedef struct {
  Window *window;
  Layer *canvas;
  TbMedia *media;
  const TbStrings *strings;
  TbMediaActions actions;
  uint8_t kind;
  bool overlay;
  uint8_t phase;
  AppTimer *overlay_timer;
  TbTouch touch;
} TbMediaWindow;

void tb_media_window_init(TbMediaWindow *view, TbMedia *media, const TbStrings *strings, TbMediaActions actions);
void tb_media_window_deinit(TbMediaWindow *view);
void tb_media_window_show(TbMediaWindow *view, uint8_t kind);
void tb_media_window_reload(TbMediaWindow *view);
void tb_media_status(const TbMedia *media, const TbStrings *strings, uint8_t kind, bool compact, char *out, size_t size);
