#pragma once
#include <pebble.h>

typedef struct {
  GFont title;
  GFont body;
  GFont meta;
  GFont meta_bold;
  int16_t margin;
  int16_t title_height;
  int16_t meta_height;
  int16_t body_line;
  int16_t row_min;
  bool touch;
  bool round;
} TbTheme;

void tb_theme_init(void);
const TbTheme *tb_theme(void);
GColor tb_theme_background(void);
GColor tb_theme_text(bool highlighted);
GColor tb_theme_muted(bool highlighted);
GColor tb_theme_accent(bool highlighted);
void tb_theme_menu(MenuLayer *menu);
int16_t tb_theme_text_height(const char *text, GFont font, int16_t width, int16_t limit);
