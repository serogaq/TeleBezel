#pragma once
#include <pebble.h>
#include "notify.h"

typedef void (*TbNotifyActivate)(void *context, const TbNotification *notification);

typedef struct {
  TbNotify *notify;
  Layer *layer;
  int16_t top;
  TbNotifyActivate activate;
  void *context;
} TbNotifyView;

void tb_notify_view_init(TbNotifyView *view, TbNotify *notify, TbNotifyActivate activate, void *context);
void tb_notify_view_attach(TbNotifyView *view, Layer *root, int16_t top);
void tb_notify_view_detach(TbNotifyView *view);
void tb_notify_view_refresh(TbNotifyView *view);
bool tb_notify_view_visible(const TbNotifyView *view);
bool tb_notify_view_hit(const TbNotifyView *view, int16_t y);
int16_t tb_notify_view_height(void);
void tb_notify_view_activate(TbNotifyView *view);
