#include "icons.h"

static void fill(GContext *ctx, GPoint *points, uint32_t count) {
  GPath path = {.num_points = count, .points = points, .rotation = 0, .offset = GPointZero};
  gpath_draw_filled(ctx, &path);
}

static void pen(GContext *ctx, GColor color, GRect box) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_fill_color(ctx, color);
  graphics_context_set_stroke_width(ctx, box.size.w >= 14 ? 2 : 1);
#if defined(PBL_COLOR)
  graphics_context_set_antialiased(ctx, true);
#else
  graphics_context_set_antialiased(ctx, false);
#endif
}

void tb_icon_refresh(GContext *ctx, GRect box, GColor color) {
  pen(ctx, color, box);
  if (box.size.w >= 11) {
    graphics_context_set_stroke_width(ctx, 2);
    const GRect ring = grect_inset(box, GEdgeInsets(1));
    graphics_draw_arc(ctx, ring, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(80), DEG_TO_TRIGANGLE(350));
    const GPoint tip = gpoint_from_polar(ring, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(350));
    GPoint arrow[] = {{tip.x, tip.y - 3}, {tip.x, tip.y + 3}, {tip.x + 4, tip.y}};
    fill(ctx, arrow, 3);
    return;
  }
  static const char *const pixels[] = {"...###...", ".##...#..", ".#....###", "#......#.", "#........",
                                        "#.......#", ".#.....#.", ".##...##.", "...###..."};
  const int16_t top = (int16_t)(box.origin.y + (box.size.h - 9) / 2);
  for (int16_t row = 0; row < 9; ++row) {
    for (int16_t column = 0; column < 9; ++column) {
      if (pixels[row][column] == '#') { graphics_draw_pixel(ctx, GPoint(box.origin.x + column, top + row)); }
    }
  }
}

#define SUB 16

static GColor blend(GColor color, GColor background) {
#if defined(PBL_COLOR)
  GColor mixed = color;
  mixed.r = (uint8_t)((color.r + background.r + 1) / 2);
  mixed.g = (uint8_t)((color.g + background.g + 1) / 2);
  mixed.b = (uint8_t)((color.b + background.b + 1) / 2);
  return mixed;
#else
  (void)background;
  return color;
#endif
}

static void draw_disc(GContext *ctx, int32_t x, int32_t y, int32_t radius, GColor color, GColor soft) {
  const int32_t limit = radius * radius;
  for (int32_t py = (y - radius) / SUB - 1; py <= (y + radius) / SUB + 1; ++py) {
    for (int32_t px = (x - radius) / SUB - 1; px <= (x + radius) / SUB + 1; ++px) {
      int covered = 0;
      for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
          const int32_t dx = px * SUB + sx * 4 + 2 - x;
          const int32_t dy = py * SUB + sy * 4 + 2 - y;
          if (dx * dx + dy * dy <= limit) { ++covered; }
        }
      }
#if defined(PBL_COLOR)
      if (covered >= 10) {
        graphics_context_set_stroke_color(ctx, color);
      } else if (covered >= 4) {
        graphics_context_set_stroke_color(ctx, soft);
      } else {
        continue;
      }
#else
      (void)soft;
      if (covered < 8) { continue; }
      graphics_context_set_stroke_color(ctx, color);
#endif
      graphics_draw_pixel(ctx, GPoint((int16_t)px, (int16_t)py));
    }
  }
}

void tb_icon_loader(GContext *ctx, GRect box, GColor color) {
  graphics_context_set_antialiased(ctx, false);
  const GColor soft = blend(color, PBL_IF_ROUND_ELSE(GColorWhite, GColorDarkGray));
  const int32_t smallest = PBL_IF_COLOR_ELSE(SUB * 5 / 10, SUB * 6 / 10);
  const int32_t largest = PBL_IF_COLOR_ELSE(SUB * 135 / 100, SUB * 115 / 100);
  const int32_t ring = PBL_IF_COLOR_ELSE(SUB * 42 / 10, SUB * 435 / 100);
  const int32_t cx = box.origin.x * SUB + box.size.w * SUB / 2;
  const int32_t cy = box.origin.y * SUB + box.size.h * SUB / 2;
  for (int step = 0; step < 8; ++step) {
    const int32_t angle = DEG_TO_TRIGANGLE(step * 45);
    const int32_t x = cx + ring * sin_lookup(angle) / TRIG_MAX_RATIO;
    const int32_t y = cy - ring * cos_lookup(angle) / TRIG_MAX_RATIO;
    draw_disc(ctx, x, y, smallest + (largest - smallest) * step / 7, color, soft);
  }
}

void tb_icon_download(GContext *ctx, GRect box, GColor color) {
  pen(ctx, color, box);
  const int16_t middle = box.origin.x + box.size.w / 2;
  const int16_t head = box.size.w / 3;
  const int16_t tip = box.origin.y + box.size.h - head;
  graphics_draw_line(ctx, GPoint(middle, box.origin.y + 1), GPoint(middle, tip - head / 2));
  GPoint arrow[] = {{middle - head, tip - head}, {middle + head, tip - head}, {middle, tip}};
  fill(ctx, arrow, 3);
  const int16_t floor = box.origin.y + box.size.h - 1;
  graphics_draw_line(ctx, GPoint(box.origin.x + 1, floor), GPoint(box.origin.x + box.size.w - 2, floor));
}

void tb_icon_check(GContext *ctx, GRect box, GColor color) {
  pen(ctx, color, box);
  const GPoint start = GPoint(box.origin.x + 1, box.origin.y + box.size.h / 2);
  const GPoint turn = GPoint(box.origin.x + box.size.w * 2 / 5, box.origin.y + box.size.h - 2);
  const GPoint end = GPoint(box.origin.x + box.size.w - 1, box.origin.y + 2);
  graphics_draw_line(ctx, start, turn);
  graphics_draw_line(ctx, turn, end);
}

void tb_icon_bookmark(GContext *ctx, GRect box, GColor color) {
  pen(ctx, color, box);
  const int16_t left = box.origin.x + box.size.w / 6;
  const int16_t right = box.origin.x + box.size.w - box.size.w / 6 - 1;
  const int16_t top = box.origin.y;
  const int16_t bottom = box.origin.y + box.size.h - 1;
  GPoint shape[] = {{left, top}, {right, top}, {right, bottom}, {(left + right) / 2, bottom - box.size.h / 3}, {left, bottom}};
  fill(ctx, shape, 5);
}

void tb_icon_chats(GContext *ctx, GRect box, GColor color) {
  pen(ctx, color, box);
  const int16_t tail = box.size.h / 4;
  const GRect bubble = GRect(box.origin.x, box.origin.y, box.size.w, box.size.h - tail);
  graphics_fill_rect(ctx, bubble, box.size.w >= 11 ? 3 : 2, GCornersAll);
  const int16_t base = (int16_t)(bubble.origin.y + bubble.size.h - 1);
  GPoint point[] = {{box.origin.x + 2, base}, {box.origin.x + 2 + tail + 1, base}, {box.origin.x + 2, box.origin.y + box.size.h - 1}};
  fill(ctx, point, 3);
}

void tb_icon_messages(GContext *ctx, GRect box, GColor color) {
  pen(ctx, color, box);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_antialiased(ctx, false);
  const GRect envelope = GRect(box.origin.x, box.origin.y, box.size.w, box.size.h);
  graphics_draw_rect(ctx, envelope);
  const GPoint middle = GPoint(envelope.origin.x + envelope.size.w / 2, envelope.origin.y + envelope.size.h / 2);
  graphics_draw_line(ctx, envelope.origin, middle);
  graphics_draw_line(ctx, GPoint(envelope.origin.x + envelope.size.w - 1, envelope.origin.y), middle);
}

void tb_icon_fill(GContext *ctx, GRect box, GColor color, uint16_t level) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_antialiased(ctx, false);
  const int32_t size = box.size.h;
  const int32_t center2 = size - 1;
  const int32_t radius2 = size - 1;
  const int32_t surface = (int32_t)(size - (int32_t)level * size / 1000);
  for (int32_t row = size - 1; row >= surface && row >= 0; --row) {
    const int32_t dy = 2 * row - center2;
    for (int32_t column = 0; column < size; ++column) {
      const int32_t dx = 2 * column - center2;
      if (dx * dx + dy * dy <= radius2 * radius2) {
        graphics_draw_pixel(ctx, GPoint((int16_t)(box.origin.x + column), (int16_t)(box.origin.y + row)));
      }
    }
  }
}
