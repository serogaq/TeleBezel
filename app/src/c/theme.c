#include "theme.h"

static TbTheme s_theme;

void tb_theme_init(void) {
  s_theme.title = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  s_theme.body = fonts_get_system_font(FONT_KEY_GOTHIC_24);
  s_theme.meta = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  s_theme.meta_bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  s_theme.title_height = 30;
  s_theme.meta_height = 22;
  s_theme.body_line = 28;
  s_theme.round = PBL_IF_ROUND_ELSE(true, false);
  s_theme.margin = PBL_IF_ROUND_ELSE(22, 4);
#if defined(PBL_TOUCH)
  s_theme.touch = touch_service_is_enabled();
  if (s_theme.touch) { app_touch_navigation_enable(true); }
#else
  s_theme.touch = false;
#endif
  s_theme.row_min = s_theme.touch ? 44 : 28;
}

const TbTheme *tb_theme(void) { return &s_theme; }

GColor tb_theme_background(void) { return GColorWhite; }
GColor tb_theme_text(bool highlighted) { return highlighted ? GColorWhite : GColorBlack; }
GColor tb_theme_muted(bool highlighted) {
  return highlighted ? GColorWhite : GColorDarkGray;
}
GColor tb_theme_accent(bool highlighted) {
  return highlighted ? GColorWhite : GColorCobaltBlue;
}

void tb_theme_menu(MenuLayer *menu) {
  menu_layer_set_normal_colors(menu, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(menu, GColorCobaltBlue, GColorWhite);
#if defined(PBL_ROUND)
  menu_layer_set_center_focused(menu, true);
#endif
}

int16_t tb_theme_text_height(const char *text, GFont font, int16_t width, int16_t limit) {
  if (!text || !*text) { return 0; }
  const GSize size = graphics_text_layout_get_content_size(text, font, GRect(0, 0, width, limit), GTextOverflowModeTrailingEllipsis,
                                                           GTextAlignmentLeft);
  return size.h < limit ? size.h : limit;
}

int16_t tb_theme_page(GRect bounds, TbPageLine *lines, int count, int16_t top) {
  const int16_t width = (int16_t)(bounds.size.w - 2 * s_theme.margin - 8);
  const int16_t left = (int16_t)(s_theme.margin + 4);
  int16_t heights[TB_PAGE_MAX_LINES];
  int16_t total = 0;
  if (count > TB_PAGE_MAX_LINES) { count = TB_PAGE_MAX_LINES; }
  for (int index = 0; index < count; ++index) {
    text_layer_set_text(lines[index].layer, lines[index].text);
    heights[index] = lines[index].text && *lines[index].text
                         ? (int16_t)(tb_theme_text_height(lines[index].text, lines[index].font, width, lines[index].limit) + lines[index].pad)
                         : 0;
    total = (int16_t)(total + heights[index]);
  }
  if (top == TB_PAGE_CENTER) {
    top = (int16_t)((bounds.size.h - total) / 2);
    if (top < 4) { top = 4; }
  }
  for (int index = 0; index < count; ++index) {
    layer_set_frame(text_layer_get_layer(lines[index].layer), GRect(left, top, width, heights[index]));
    top = (int16_t)(top + heights[index]);
  }
  return top;
}
