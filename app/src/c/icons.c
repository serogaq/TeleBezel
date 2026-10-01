#include "icons.h"
#include <string.h>

#define TB_ICON_BYTES 64

void tb_icon(GContext *ctx, uint8_t icon, GPoint origin, GColor color) {
  const ResHandle handle = resource_get_handle(RESOURCE_ID_ICONS);
  uint8_t entry[4];
  if (resource_load_byte_range(handle, 1 + (uint32_t)icon * sizeof(entry), entry, sizeof(entry)) != sizeof(entry)) { return; }
  const uint8_t width = entry[0];
  const uint8_t height = entry[1];
  const uint8_t stride = (uint8_t)((width + 3) / 4);
  const size_t size = (size_t)stride * height;
  uint8_t data[TB_ICON_BYTES];
  if (size > sizeof(data) || resource_load_byte_range(handle, (uint32_t)(entry[2] | entry[3] << 8), data, size) != size) { return; }
  GColor palette[4] = {GColorClear, color, color, color};
  palette[1].a = 1;
  palette[2].a = 2;
  GBitmap *bitmap = gbitmap_create_blank_with_palette(GSize(width, height), GBitmapFormat2BitPalette, palette, false);
  if (!bitmap) { return; }
  uint8_t *pixels = gbitmap_get_data(bitmap);
  const uint16_t row = gbitmap_get_bytes_per_row(bitmap);
  for (uint8_t y = 0; y < height; ++y) { memcpy(pixels + y * row, data + y * stride, stride); }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bitmap, GRect(origin.x, origin.y, width, height));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
  gbitmap_destroy(bitmap);
}
