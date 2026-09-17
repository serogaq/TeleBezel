#ifndef TELEBEZEL_STATUS_VIEW_H
#define TELEBEZEL_STATUS_VIEW_H
#include <pebble.h>
#include "status_state.h"
Window *tb_status_view_create(void);
void tb_status_view_destroy(Window *window);
void tb_status_view_set(TbStatus status);
#endif
