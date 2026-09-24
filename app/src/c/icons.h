#pragma once
#include <pebble.h>

void tb_icon_refresh(GContext *ctx, GRect box, GColor color);
void tb_icon_fill(GContext *ctx, GRect box, GColor color, uint16_t level);
void tb_icon_loader(GContext *ctx, GRect box, GColor color);
void tb_icon_download(GContext *ctx, GRect box, GColor color);
void tb_icon_check(GContext *ctx, GRect box, GColor color);
void tb_icon_bookmark(GContext *ctx, GRect box, GColor color);
void tb_icon_chats(GContext *ctx, GRect box, GColor color);
void tb_icon_messages(GContext *ctx, GRect box, GColor color);
void tb_icon_pencil(GContext *ctx, GRect box, GColor color);
void tb_icon_mic(GContext *ctx, GRect box, GColor color);
void tb_icon_reply(GContext *ctx, GRect box, GColor color);
void tb_icon_forward(GContext *ctx, GRect box, GColor color);
void tb_icon_clock(GContext *ctx, GRect box, GColor color);
void tb_icon_alert(GContext *ctx, GRect box, GColor color);
