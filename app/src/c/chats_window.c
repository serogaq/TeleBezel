#include "chats_window.h"
#include <stdlib.h>
#include <string.h>
#include "errors.h"
#include "format.h"
#include "generated/protocol.h"
#include "diag.h"
#include "icons.h"
#include "scratch.h"
#include "theme.h"

#define PULL_DISTANCE 60
#define BAR_GAP 3
#define BAR_EDGE 7
#define BAR_NUDGE 4
#define BAR_PIECES 40
#define TOGGLE_HEIGHT 19

static TbChatsWindow *s_ticking;

static uint16_t first_chat(const TbChatsWindow *view) { return (uint16_t)(view->show_archive ? 1 : 0); }

static bool has_tail(const TbChatsWindow *view) {
  const TbChats *chats = view->chats;
  if (!chats->loaded || chats->count == 0) { return true; }
  return chats->tail != TB_TAIL_END && chats->tail != TB_TAIL_FULL;
}

static uint16_t row_count(const TbChatsWindow *view) { return (uint16_t)(first_chat(view) + view->chats->count + (has_tail(view) ? 1 : 0)); }
static bool is_toggle(const TbChatsWindow *view, uint16_t row) { return view->show_archive && row == 0; }
static bool is_chat(const TbChatsWindow *view, uint16_t row) { return row >= first_chat(view) && row < first_chat(view) + view->chats->count; }
static bool is_tail(const TbChatsWindow *view, uint16_t row) { return has_tail(view) && row == first_chat(view) + view->chats->count; }

static const char *tail_text(const TbChatsWindow *view) {
  const TbChats *chats = view->chats;
  if (chats->load == TB_CHATS_FIRST || (chats->load == TB_CHATS_REFRESH && chats->count == 0)) { return view->strings->loading; }
  if (!chats->loaded) { return chats->error != TB_ERROR_NONE ? view->strings->load_failed : view->strings->loading; }
  if (chats->count == 0) { return view->strings->no_chats; }
  if (chats->load == TB_CHATS_MORE) { return view->strings->loading; }
  if (chats->tail == TB_TAIL_FAILED) { return view->strings->load_failed; }
  return view->strings->more_chats;
}


static void chat_preview(TbChatsWindow *view, const TbChat *chat, char *out, size_t size) {
  char *content = tb_scratch(TB_SCRATCH_CONTENT);
  tb_format_content(content, tb_scratch_size(TB_SCRATCH_CONTENT), view->strings, chat->preview_kind, chat->preview_action, chat->preview_duration,
                    chat->extra, chat->preview);
  const bool group = chat->type == TB_CHAT_TYPE_BASIC_GROUP || chat->type == TB_CHAT_TYPE_SUPERGROUP;
  if (chat->flags & TB_CHAT_FLAG_PREVIEW_OUTGOING) {
    snprintf(out, size, "%s: %s", view->strings->you, content);
  } else if (group && chat->sender && chat->preview_kind != TB_KIND_SERVICE) {
    snprintf(out, size, "%s: %s", chat->sender, content);
  } else if (chat->preview_kind == TB_KIND_SERVICE && chat->sender) {
    snprintf(out, size, "%s %s", chat->sender, content);
  } else {
    snprintf(out, size, "%s", content);
  }
}

typedef void (*TbIcon)(GContext *ctx, GRect box, GColor color);

static int16_t icon_size(void) { return 14; }

static int16_t glyph_height(void) { return 11; }

static int16_t icon_width(TbIcon icon) { return (int16_t)(glyph_height() + (icon == tb_icon_messages ? glyph_height() / 3 : 0)); }

static int16_t glyph_top(void) { return 7; }

#if !defined(PBL_ROUND)
static int16_t segment_height(void) { return (int16_t)(tb_theme()->meta_height + 4); }
#endif

static int16_t row_height(MenuLayer *menu, MenuIndex *index, void *context);

#if defined(PBL_ROUND)
static int16_t lead(TbChatsWindow *view) {
  const GRect screen = layer_get_bounds(window_get_root_layer(view->window));
  const uint16_t home = view->chats->count > 0 ? first_chat(view) : 0;
  int16_t space = (int16_t)(screen.size.h / 2);
  for (uint16_t row = 0; row <= home; ++row) {
    MenuIndex index = {0, row};
    const int16_t size = row_height(view->menu, &index, view);
    space = (int16_t)(space - (row == home ? size / 2 : size));
  }
  return space > 0 ? space : 0;
}
#endif

#if !defined(PBL_ROUND)
static int16_t pull_gap(void);
#endif

static int16_t header_height(TbChatsWindow *view) {
#if defined(PBL_ROUND)
  return lead(view);
#else
  (void)view;
  return (int16_t)(segment_height() + pull_gap());
#endif
}

typedef struct {
  TbIcon icon;
  const char *text;
  int16_t width;
} TbSegment;

static TbSegment segment(TbIcon icon, const char *text) {
  const TbTheme *theme = tb_theme();
  TbSegment value = {icon, text, icon_width(icon)};
  if (text && *text) {
    const GSize size = graphics_text_layout_get_content_size(text, theme->meta, GRect(0, 0, 200, theme->meta_height), GTextOverflowModeFill,
                                                             GTextAlignmentLeft);
    value.width = (int16_t)(value.width + BAR_GAP + size.w);
  }
  return value;
}

static uint16_t s_pull_level;

#define PULL_MAX PBL_IF_ROUND_ELSE(36, 26)

static int16_t pull_size(void) {
#if defined(PBL_ROUND)
  const int32_t remaining = TB_PULL_FULL - s_pull_level;
  const int32_t eased = TB_PULL_FULL - remaining * remaining / TB_PULL_FULL;
  return (int16_t)(1 + (int32_t)(PULL_MAX - 1) * eased / TB_PULL_FULL);
#else
  return (int16_t)(1 + (int32_t)(PULL_MAX - 1) * s_pull_level / TB_PULL_FULL);
#endif
}

static void draw_pull(GContext *ctx, GPoint center, GColor color, int16_t limit) {
  int16_t size = pull_size();
  if (size > limit) { size = limit; }
  if (size < 1) { return; }
  if (size < 4) {
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, GRect(center.x - size / 2, center.y - size / 2, size, size), 0, GCornerNone);
    return;
  }
  const GRect box = GRect(center.x - size / 2, center.y - size / 2, size, size);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, size >= 20 ? 2 : 1);
  graphics_context_set_antialiased(ctx, true);
  graphics_draw_circle(ctx, GPoint(box.origin.x + size / 2, box.origin.y + size / 2), (uint16_t)(size / 2));
  tb_icon_fill(ctx, box, color, s_pull_level);
}

#if !defined(PBL_ROUND)
static int16_t pull_gap(void) { return (int16_t)((int32_t)s_pull_level * (PULL_MAX + 8) / TB_PULL_FULL); }
#endif

static TbIcon status_icon(const TbConnection *connection) {
  if (!connection->known || connection->state == TB_CONNECTION_CONNECTING) { return tb_icon_loader; }
  return connection->state == TB_CONNECTION_UPDATING ? tb_icon_download : tb_icon_check;
}

#if defined(PBL_ROUND)
typedef struct {
  TbIcon icon;
  char text[6];
  int16_t width;
  int16_t gap;
} TbPiece;

static uint8_t split(TbPiece *pieces, uint8_t count, const TbSegment *value) {
  const TbTheme *theme = tb_theme();
  if (count >= BAR_PIECES) { return count; }
  pieces[count] = (TbPiece){.icon = value->icon, .width = icon_width(value->icon), .gap = BAR_GAP};
  ++count;
  const char *cursor = value->text ? value->text : "";
  while (*cursor && count < BAR_PIECES) {
    if (*cursor == ' ') {
      pieces[count - 1].gap = (int16_t)(pieces[count - 1].gap + theme->meta_height / 4);
      ++cursor;
      continue;
    }
    size_t length = 1;
    while (cursor[length] && (cursor[length] & 0xC0) == 0x80 && length < 4) { ++length; }
    TbPiece *piece = &pieces[count++];
    piece->icon = NULL;
    memcpy(piece->text, cursor, length);
    piece->text[length] = '\0';
    piece->width = graphics_text_layout_get_content_size(piece->text, theme->meta, GRect(0, 0, 60, theme->meta_height), GTextOverflowModeFill,
                                                         GTextAlignmentLeft).w;
    piece->gap = 1;
    cursor += length;
  }
  return count;
}

static int16_t span(const TbPiece *pieces, uint8_t from, uint8_t to) {
  int16_t total = 0;
  for (uint8_t index = from; index < to; ++index) { total = (int16_t)(total + pieces[index].width + (index + 1 < to ? pieces[index].gap : 0)); }
  return total;
}

static int16_t isqrt(int32_t value) {
  if (value <= 0) { return 0; }
  int32_t root = value;
  int32_t next = (root + 1) / 2;
  while (next < root) {
    root = next;
    next = (root + value / root) / 2;
  }
  return (int16_t)root;
}

static int32_t arc_angle(int16_t length, int16_t radius) { return (int32_t)length * TRIG_MAX_ANGLE / (int32_t)(2 * 314 * radius / 100); }

#define GLYPH_PAD 3

static void draw_piece(GContext *ctx, const TbPiece *piece, GPoint origin) {
  if (piece->icon) {
    piece->icon(ctx, GRect(origin.x, origin.y, piece->width, glyph_height()), tb_theme_accent(false));
    return;
  }
  graphics_context_set_text_color(ctx, tb_theme_text(false));
  graphics_draw_text(ctx, piece->text, tb_theme()->meta, GRect(origin.x, origin.y - glyph_top(), piece->width + 2 * GLYPH_PAD, tb_theme()->meta_height),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

#define MASK_SIZE 32

static uint8_t *s_mask;
static uint8_t *s_saved;

static int32_t sample(int32_t x, int32_t y, GSize size) {
  const int32_t left = x >> 8;
  const int32_t top = y >> 8;
  const int32_t fx = x & 0xFF;
  const int32_t fy = y & 0xFF;
  int32_t value = 0;
  for (int dy = 0; dy < 2; ++dy) {
    for (int dx = 0; dx < 2; ++dx) {
      const int32_t px = left + dx;
      const int32_t py = top + dy;
      if (px < 0 || py < 0 || px >= size.w || py >= size.h) { continue; }
      const int32_t weight = (dx ? fx : 256 - fx) * (dy ? fy : 256 - fy);
      value += weight * s_mask[py * MASK_SIZE + px];
    }
  }
  return value >> 16;
}

static uint8_t lightness(GColor8 color) { return (uint8_t)(color.r + color.g + color.b); }

static void rotate_into(GBitmap *frame, GColor ink, GSize size, GPoint pivot, int32_t angle, GPoint spot, int16_t origin_y) {
  const bool dark = ink.r == ink.g && ink.g == ink.b;
  const GColor8 ramp[4] = {GColorWhite, dark ? GColorLightGray : GColorBabyBlueEyes, dark ? GColorDarkGray : GColorPictonBlue, ink};
  const int32_t sine = sin_lookup(angle);
  const int32_t cosine = cos_lookup(angle);
  const int16_t reach = (int16_t)(size.w + size.h);
  for (int16_t y = (int16_t)(spot.y - reach); y <= spot.y + reach; ++y) {
    const int16_t row = (int16_t)(origin_y + y);
    if (row < 0 || row >= 260) { continue; }
    const GBitmapDataRowInfo info = gbitmap_get_data_row_info(frame, (uint16_t)row);
    for (int16_t x = (int16_t)(spot.x - reach); x <= spot.x + reach; ++x) {
      if (x < info.min_x || x > info.max_x) { continue; }
      int32_t total = 0;
      for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
          const int32_t ox = (x - spot.x) * 256 + sx * 64 - 96;
          const int32_t oy = (y - spot.y) * 256 + sy * 64 - 96;
          const int32_t source_x = (ox * cosine + oy * sine) / TRIG_MAX_RATIO + pivot.x * 256;
          const int32_t source_y = (oy * cosine - ox * sine) / TRIG_MAX_RATIO + pivot.y * 256;
          total += sample(source_x, source_y, size);
        }
      }
      const int32_t coverage = total / 16;
      const int level = coverage >= 150 ? 3 : coverage >= 90 ? 2 : coverage >= 40 ? 1 : 0;
      if (level == 0) { continue; }
      const GColor8 color = ramp[level];
      if (lightness(color) < lightness((GColor8){.argb = info.data[x]})) { info.data[x] = color.argb; }
    }
  }
}

static void place(GContext *ctx, const TbPiece *piece, int32_t angle, int16_t track, GPoint center, GPoint scratch, int16_t origin_y) {
  const GPoint spot = GPoint((int16_t)(center.x + track * sin_lookup(angle) / TRIG_MAX_RATIO),
                             (int16_t)(center.y - track * cos_lookup(angle) / TRIG_MAX_RATIO));
  const int16_t half_width = piece->width / 2;
  const int16_t half_height = glyph_height() / 2;
  const GSize size = GSize(piece->width + 2 * GLYPH_PAD, tb_theme()->meta_height + GLYPH_PAD);
  if (size.w > MASK_SIZE || size.h > MASK_SIZE) { return; }
  GBitmap *frame = graphics_capture_frame_buffer(ctx);
  if (!frame) { return; }
  for (int16_t row = 0; row < size.h; ++row) {
    const GBitmapDataRowInfo info = gbitmap_get_data_row_info(frame, (uint16_t)(origin_y + scratch.y + row));
    memcpy(&s_saved[row * MASK_SIZE], &info.data[scratch.x], (size_t)size.w);
  }
  graphics_release_frame_buffer(ctx, frame);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(scratch.x, scratch.y, size.w, size.h), 0, GCornerNone);
  draw_piece(ctx, piece, GPoint(scratch.x + GLYPH_PAD, scratch.y + GLYPH_PAD));
  frame = graphics_capture_frame_buffer(ctx);
  if (!frame) { return; }
  const GColor ink = piece->icon ? tb_theme_accent(false) : tb_theme_text(false);
  const int32_t ink_light = ink.r + ink.g + ink.b;
  for (int16_t row = 0; row < size.h; ++row) {
    const GBitmapDataRowInfo info = gbitmap_get_data_row_info(frame, (uint16_t)(origin_y + scratch.y + row));
    for (int16_t column = 0; column < size.w; ++column) {
      const int16_t x = (int16_t)(scratch.x + column);
      uint8_t value = 0;
      if (x >= info.min_x && x <= info.max_x) {
        const GColor8 color = (GColor8){.argb = info.data[x]};
        const int32_t light = color.r + color.g + color.b;
        value = (uint8_t)(light >= 9 ? 0 : (9 - light) * 255 / (9 - ink_light));
      }
      s_mask[row * MASK_SIZE + column] = value;
      info.data[x] = s_saved[row * MASK_SIZE + column];
    }
  }
  rotate_into(frame, ink, size, GPoint(GLYPH_PAD + half_width, GLYPH_PAD + half_height), angle, spot, origin_y);
  graphics_release_frame_buffer(ctx, frame);
}

static int16_t draw_level(GContext *ctx, const TbPiece *pieces, uint8_t from, uint8_t to, int16_t cx, int16_t cy, int16_t radius) {
  const int16_t width = span(pieces, from, to);
  int16_t x = (int16_t)(cx - width / 2 - BAR_NUDGE);
  const int16_t reach = (int16_t)((cx - x) > (x + width - cx) ? cx - x : x + width - cx);
  const int16_t inner = (int16_t)(radius - BAR_EDGE);
  if (reach >= inner) { return cy - radius; }
  const int16_t top = (int16_t)(cy - isqrt((int32_t)inner * inner - (int32_t)reach * reach));
  for (uint8_t index = from; index < to; ++index) {
    draw_piece(ctx, &pieces[index], GPoint(x, top));
    x = (int16_t)(x + pieces[index].width + pieces[index].gap);
  }
  return (int16_t)(top + glyph_height());
}

static int32_t span_angle(const TbPiece *pieces, uint8_t from, uint8_t to, int16_t track) { return arc_angle(span(pieces, from, to), track); }

static void draw_arc_of(GContext *ctx, const TbPiece *pieces, uint8_t from, uint8_t to, int32_t start, int16_t track, GPoint center,
                        GPoint scratch, int16_t origin_y) {
  int32_t angle = start;
  for (uint8_t index = from; index < to; ++index) {
    const int32_t half = arc_angle((int16_t)(pieces[index].width / 2), track);
    place(ctx, &pieces[index], angle + half, track, center, scratch, origin_y);
    angle += arc_angle((int16_t)(pieces[index].width + pieces[index].gap), track);
  }
}
#else
static void draw_segment(GContext *ctx, const TbSegment *value, GRect box, GTextAlignment alignment) {
  const TbTheme *theme = tb_theme();
  const int16_t size = glyph_height();
  if (box.size.w < size) { return; }
  int16_t width = value->width < box.size.w ? value->width : box.size.w;
  int16_t x = box.origin.x;
  if (alignment == GTextAlignmentCenter) { x = (int16_t)(box.origin.x + (box.size.w - width) / 2); }
  if (alignment == GTextAlignmentRight) { x = (int16_t)(box.origin.x + box.size.w - width); }
  const int16_t text_top = (int16_t)(box.origin.y + (box.size.h - theme->meta_height) / 2 - 3);
  const int16_t icon = icon_width(value->icon);
  value->icon(ctx, GRect(x, text_top + glyph_top(), icon, size), GColorWhite);
  if (!value->text || !*value->text) { return; }
  const int16_t left = (int16_t)(x + icon + BAR_GAP);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, value->text, theme->meta, GRect(left, text_top, width - icon - BAR_GAP, theme->meta_height),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}
#endif

static void bar_segments(TbChatsWindow *view, TbSegment *left, TbSegment *middle, TbSegment *right) {
  const TbChats *chats = view->chats;
  static char updated[32];
  static char clock[12];
  static char counter[12];
  updated[0] = '\0';
  const time_t now = time(NULL);
  if (chats->loaded) {
    const int32_t elapsed = (int32_t)(chats->ports.now(chats->ports.context) - chats->loaded_at);
    const time_t age = elapsed > 0 ? elapsed / 1000 : 0;
    tb_format_ago(updated, sizeof(updated), view->strings, now - age, now, clock_is_24h_style());
  }
  tb_format_clock(clock, sizeof(clock), now, clock_is_24h_style());
  const bool messages = view->unread_mode == TB_UNREAD_MODE_MESSAGES;
  tb_format_count(counter, sizeof(counter), messages ? chats->unread_messages : chats->unread_chats);
  *left = segment(tb_icon_refresh, updated);
  *middle = segment(status_icon(view->connection), clock);
  *right = segment(messages ? tb_icon_messages : tb_icon_chats, counter);
}

#if defined(PBL_ROUND)
static TbChatsWindow *s_bar_view;

static int16_t bar_shift(TbChatsWindow *view) {
  const int16_t offset = (int16_t)scroll_layer_get_content_offset(menu_layer_get_scroll_layer(view->menu)).y;
  return offset < 0 ? offset : 0;
}

static void bar_update(Layer *layer, GContext *ctx) {
  TbChatsWindow *view = s_bar_view;
  if (!view || !view->menu) { return; }
  const GRect screen = layer_get_bounds(layer);
  TbSegment left;
  TbSegment middle;
  TbSegment right;
  bar_segments(view, &left, &middle, &right);
  const int16_t radius = screen.size.w / 2;
  const int16_t shift = bar_shift(view);
  if (shift < -3 * tb_theme()->meta_height) { return; }
  const int16_t cx = screen.size.w / 2;
  const int16_t cy = (int16_t)(screen.size.h / 2 + shift);
  uint8_t *scratch = malloc(2 * MASK_SIZE * MASK_SIZE + BAR_PIECES * sizeof(TbPiece));
  if (!scratch) { return; }
  s_mask = scratch;
  s_saved = scratch + MASK_SIZE * MASK_SIZE;
  TbPiece *pieces = (TbPiece *)(scratch + 2 * MASK_SIZE * MASK_SIZE);
  const uint8_t left_end = split(pieces, 0, &left);
  const uint8_t middle_end = split(pieces, left_end, &middle);
  const uint8_t right_end = split(pieces, middle_end, &right);
  const int16_t track = (int16_t)(radius - BAR_EDGE - glyph_height() / 2);
  const GPoint center = GPoint(cx, cy);
  const GPoint spot = GPoint((int16_t)(cx - MASK_SIZE / 2), (int16_t)(screen.size.h / 2 - MASK_SIZE / 2));
  const int32_t middle_span = span_angle(pieces, left_end, middle_end, track);
  const int32_t middle_start = -middle_span / 2;
  const int32_t spacing = arc_angle((int16_t)(3 * BAR_GAP), track);
  MenuIndex home = {0, view->chats->count > 0 ? first_chat(view) : 0};
  const int16_t strip = (int16_t)(cy - row_height(view->menu, &home, view) / 2);
  const int16_t drop = (int16_t)(cy - strip + glyph_height() / 2);
  const int32_t outer = drop <= 0 ? 0 : drop < track ? atan2_lookup(isqrt((int32_t)track * track - (int32_t)drop * drop), drop) : TRIG_MAX_ANGLE / 4;
  const int32_t inner = -middle_start + spacing;
  const int32_t left_span = span_angle(pieces, 0, left_end, track);
  const int32_t right_span = span_angle(pieces, middle_end, right_end, track);
  const int32_t right_inner = middle_start + middle_span + spacing;
  const int32_t left_start = -(outer + inner) / 2 - left_span / 2;
  const int32_t right_start = (outer + right_inner) / 2 - right_span / 2;
  const int16_t clock_bottom = draw_level(ctx, pieces, left_end, middle_end, cx, cy, radius);
  if (s_pull_level > 0) {
    const int16_t rows_top = (int16_t)(scroll_layer_get_content_offset(menu_layer_get_scroll_layer(view->menu)).y + lead(view));
    draw_pull(ctx, GPoint(cx, (int16_t)((clock_bottom + rows_top) / 2)), tb_theme_accent(false), (int16_t)(rows_top - clock_bottom - 2));
  }
  draw_arc_of(ctx, pieces, 0, left_end, left_start, track, center, spot, 0);
  draw_arc_of(ctx, pieces, middle_end, right_end, right_start, track, center, spot, 0);
  s_mask = NULL;
  s_saved = NULL;
  free(scratch);
}
#endif

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  (void)section;
#if defined(PBL_ROUND)
  (void)ctx; (void)cell; (void)context;
#else
  TbChatsWindow *view = context;
  const GRect bounds = layer_get_bounds(cell);
  TbSegment left;
  TbSegment middle;
  TbSegment right;
  bar_segments(view, &left, &middle, &right);
  const int16_t height = segment_height();
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, height), 0, GCornerNone);
  const int16_t margin = tb_theme()->margin;
  const int16_t right_x = (int16_t)(bounds.size.w - margin - right.width);
  int16_t middle_x = (int16_t)((bounds.size.w - middle.width) / 2);
  const int16_t earliest = (int16_t)(margin + left.width + 2 * BAR_GAP);
  const int16_t latest = (int16_t)(right_x - 2 * BAR_GAP - middle.width);
  if (middle_x < earliest || middle_x > latest) {
    middle_x = (int16_t)((margin + left.width + right_x - middle.width) / 2 - 2);
    if (middle_x < earliest) { middle_x = earliest; }
  }
  draw_segment(ctx, &middle, GRect(middle_x, 0, middle.width, height), GTextAlignmentCenter);
  draw_segment(ctx, &right, GRect(right_x, 0, right.width, height), GTextAlignmentRight);
  draw_segment(ctx, &left, GRect(margin, 0, middle_x - margin - 2 * BAR_GAP, height), GTextAlignmentLeft);
  if (s_pull_level > 0) { draw_pull(ctx, GPoint(bounds.size.w / 2, height + pull_gap() / 2), tb_theme_accent(false), (int16_t)(pull_gap() - 2)); }
#endif
}

static uint16_t sections(MenuLayer *menu, void *context) {
  (void)menu; (void)context;
  return 1;
}

static int16_t header_height_callback(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu; (void)section;
  return header_height(context);
}

static uint16_t rows(MenuLayer *menu, uint16_t section, void *context) {
  (void)menu; (void)section;
  return row_count(context);
}

static int16_t row_height(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbChatsWindow *view = context;
  const TbTheme *theme = tb_theme();
  int16_t height = theme->row_min;
  if (is_chat(view, index->row)) {
    height = (int16_t)(theme->title_height + theme->meta_height + 8);
  } else if (is_toggle(view, index->row)) {
    return TOGGLE_HEIGHT;
  } else {
    height = (int16_t)(theme->title_height + 8);
  }
  const int16_t minimum = is_chat(view, index->row) ? theme->row_min : PBL_IF_ROUND_ELSE(38, theme->row_min);
  return height > minimum ? height : minimum;
}

static void draw_chat(TbChatsWindow *view, GContext *ctx, const Layer *cell, const TbChat *chat) {
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const int16_t left = theme->margin + 2;
  const int16_t width = bounds.size.w - 2 * theme->margin - 4;
  char badge[16];
  tb_format_badge(badge, sizeof(badge), chat->unread, chat->flags);
  int16_t badge_width = 0;
  if (badge[0]) {
    const GSize size = graphics_text_layout_get_content_size(badge, theme->meta_bold, GRect(0, 0, 60, 30), GTextOverflowModeFill, GTextAlignmentRight);
    const int16_t height = (int16_t)(theme->meta_height | 1);
    const int16_t radius = (int16_t)(height / 2);
    const int16_t span = (int16_t)(size.w + radius + 2);
    badge_width = span > height ? span : height;
    const int16_t pill_y = (int16_t)(theme->title_height + (theme->meta_height + 2 - height) / 2 + 1);
    const GRect pill = GRect(left + width - badge_width, pill_y, badge_width, height);
    const bool muted = (chat->flags & TB_CHAT_FLAG_MUTED) != 0;
    graphics_context_set_fill_color(ctx, highlighted ? GColorWhite : muted ? GColorLightGray : tb_theme_accent(false));
    graphics_context_set_antialiased(ctx, true);
    graphics_fill_circle(ctx, GPoint(pill.origin.x + radius, pill.origin.y + radius), (uint16_t)radius);
    graphics_fill_circle(ctx, GPoint(pill.origin.x + pill.size.w - 1 - radius, pill.origin.y + radius), (uint16_t)radius);
    graphics_fill_rect(ctx, GRect(pill.origin.x + radius, pill.origin.y, pill.size.w - 2 * radius, height), 0, GCornerNone);
    graphics_context_set_text_color(ctx, highlighted ? GColorCobaltBlue : GColorWhite);
    graphics_draw_text(ctx, badge, theme->meta_bold,
                       GRect(pill.origin.x, pill.origin.y + (height - glyph_height()) / 2 - glyph_top(), pill.size.w, theme->meta_height),
                       GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
  int16_t reserve = badge_width ? (int16_t)(badge_width + 4) : 0;
  int16_t time_width = 0;
  if (chat->last_date) {
    char time_text[16];
    tb_format_time(time_text, sizeof(time_text), (time_t)chat->last_date, time(NULL), clock_is_24h_style());
    time_width = 56;
    graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
    graphics_draw_text(ctx, time_text, theme->meta, GRect(left + width - time_width, 4, time_width, theme->meta_height),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
  const int16_t preview_reserve = reserve;
  reserve = time_width;
  int16_t title_left = left;
  char *title = tb_scratch(TB_SCRATCH_SHORT);
  const bool saved = (chat->flags & TB_CHAT_FLAG_SAVED) != 0;
  if (saved) {
    const int16_t size = icon_size();
    tb_icon_bookmark(ctx, GRect(left, (theme->title_height - size) / 2 + 1, size, size), tb_theme_accent(highlighted));
    title_left = (int16_t)(left + size + 4);
    snprintf(title, tb_scratch_size(TB_SCRATCH_SHORT), "%s", view->strings->saved_messages);
  } else {
    snprintf(title, tb_scratch_size(TB_SCRATCH_SHORT), "%s%s", chat->type == TB_CHAT_TYPE_CHANNEL ? "» " : "", tb_or_empty(chat->title));
  }
  graphics_context_set_text_color(ctx, tb_theme_text(highlighted));
  graphics_draw_text(ctx, title, theme->title, GRect(title_left, -2, width - reserve - (title_left - left), theme->title_height),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  char *preview = tb_scratch(TB_SCRATCH_TEXT);
  chat_preview(view, chat, preview, tb_scratch_size(TB_SCRATCH_TEXT));
  graphics_context_set_text_color(ctx, tb_theme_muted(highlighted));
  graphics_draw_text(ctx, preview, theme->meta, GRect(left, theme->title_height, width - preview_reserve, theme->meta_height + 2),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  TbChatsWindow *view = context;
  const TbTheme *theme = tb_theme();
  const bool highlighted = menu_cell_layer_is_highlighted(cell);
  const GRect bounds = layer_get_bounds(cell);
  const GRect box = GRect(theme->margin + 2, 2, bounds.size.w - 2 * theme->margin - 4, bounds.size.h - 4);
  const GTextAlignment alignment = PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft);
  if (is_chat(view, index->row)) {
    draw_chat(view, ctx, cell, &view->chats->items[index->row - first_chat(view)]);
  } else if (is_toggle(view, index->row)) {
    graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
    graphics_draw_text(ctx, view->chats->list == TB_LIST_ARCHIVE ? view->strings->open_main : view->strings->open_archive,
                       fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(box.origin.x, (bounds.size.h - 18) / 2, box.size.w, 18),
                       GTextOverflowModeTrailingEllipsis, alignment, NULL);
  } else {
    graphics_context_set_text_color(ctx, tb_theme_accent(highlighted));
    graphics_draw_text(ctx, tail_text(view), theme->title, GRect(box.origin.x, 0, box.size.w, box.size.h), GTextOverflowModeTrailingEllipsis,
                       alignment, NULL);
  }
}

static void remember(TbChatsWindow *view, uint16_t row) {
  view->selected_row = row;
  if (is_chat(view, row)) {
    strncpy(view->selected_id, view->chats->items[row - first_chat(view)].id, sizeof(view->selected_id) - 1);
    view->selected_id[sizeof(view->selected_id) - 1] = '\0';
  } else if (row < first_chat(view)) {
    view->selected_id[0] = '\0';
  }
}

static void refresh(TbChatsWindow *view) {
  tb_connection_refresh(view->connection);
  tb_chats_refresh(view->chats);
}

static void select_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu;
  TbChatsWindow *view = context;
  remember(view, index->row);
  if (is_chat(view, index->row)) {
    view->actions.activate(view->actions.context, index->row - first_chat(view));
  } else if (is_toggle(view, index->row)) {
    tb_chats_window_reset(view);
    tb_chats_set_list(view->chats, view->chats->list == TB_LIST_ARCHIVE ? TB_LIST_MAIN : TB_LIST_ARCHIVE);
  } else if (is_tail(view, index->row)) {
    if (!view->chats->loaded && view->chats->error != TB_ERROR_NONE) { tb_chats_retry(view->chats); }
    else if (view->chats->count == 0) { refresh(view); }
    else { tb_chats_load_more(view->chats); }
  }
}

static void select_long_click(MenuLayer *menu, MenuIndex *index, void *context) {
  (void)menu; (void)index;
  refresh(context);
}

static void selection_changed(MenuLayer *menu, MenuIndex now, MenuIndex before, void *context) {
  (void)menu; (void)before;
  TbChatsWindow *view = context;
  remember(view, now.row);
  if (is_tail(view, now.row) && view->chats->tail == TB_TAIL_MORE) { tb_chats_load_more(view->chats); }
}

static void restore(TbChatsWindow *view) {
  if (!view->menu) { return; }
  uint16_t row = view->selected_row;
  if (view->selected_id[0]) {
    const int found = tb_chats_find(view->chats, view->selected_id);
    if (found >= 0) { row = (uint16_t)(found + first_chat(view)); }
  } else if (!view->placed && view->chats->count > 0) {
    row = first_chat(view);
  }
  if (view->chats->count > 0) { view->placed = true; }
  const uint16_t total = row_count(view);
  if (row >= total) { row = total - 1; }
  view->selected_row = row;
#if defined(PBL_ROUND)
  menu_layer_set_selected_index(view->menu, (MenuIndex){0, row}, MenuRowAlignCenter, false);
#else
  if (row <= first_chat(view)) {
    menu_layer_set_selected_index(view->menu, (MenuIndex){0, row}, MenuRowAlignNone, false);
    scroll_layer_set_content_offset(menu_layer_get_scroll_layer(view->menu), GPointZero, false);
  } else {
    menu_layer_set_selected_index(view->menu, (MenuIndex){0, row}, MenuRowAlignCenter, false);
  }
#endif
}

static void pull_changed(void *context) {
  TbChatsWindow *view = context;
  s_pull_level = view->pull.level;
#if defined(PBL_ROUND)
  if (view->menu) { layer_mark_dirty(menu_layer_get_layer(view->menu)); }
#else
  if (view->menu) { menu_layer_reload_data(view->menu); }
#endif
#if defined(PBL_ROUND)
  if (view->bar) { layer_mark_dirty(view->bar); }
#endif
}

static void pull_trigger(void *context) { refresh(context); }

static void pull_fired(void *context) {
  TbChatsWindow *view = context;
  view->pull_timer = NULL;
  tb_pull_tick(&view->pull);
}

static bool pull_schedule(void *context, uint32_t milliseconds) {
  TbChatsWindow *view = context;
  if (view->pull_timer) { app_timer_cancel(view->pull_timer); }
  view->pull_timer = app_timer_register(milliseconds, pull_fired, view);
  return view->pull_timer != NULL;
}

static void pull_cancel(void *context) {
  TbChatsWindow *view = context;
  if (view->pull_timer) {
    app_timer_cancel(view->pull_timer);
    view->pull_timer = NULL;
  }
}

static uint32_t pull_now(void *context) {
  (void)context;
  time_t seconds = 0;
  uint16_t milliseconds = 0;
  time_ms(&seconds, &milliseconds);
  return (uint32_t)seconds * 1000u + milliseconds;
}

static AppTimer *s_up_repeat;

static void up_repeat(void *context) {
  TbChatsWindow *view = context;
  s_up_repeat = NULL;
  if (!view->menu || menu_layer_get_selected_index(view->menu).row == 0) { return; }
  menu_layer_set_selected_next(view->menu, true, MenuRowAlignCenter, true);
  s_up_repeat = app_timer_register(100, up_repeat, view);
}

static void up_pressed(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbChatsWindow *view = context;
  if (s_up_repeat) {
    app_timer_cancel(s_up_repeat);
    s_up_repeat = NULL;
  }
  if (menu_layer_get_selected_index(view->menu).row == 0) {
    if (tb_notify_view_visible(&view->notice) && !view->notice.notify->focused) {
      tb_notify_focus(view->notice.notify, true);
      return;
    }
    tb_notify_focus(view->notice.notify, false);
    tb_pull_press(&view->pull);
    return;
  }
  menu_layer_set_selected_next(view->menu, true, MenuRowAlignCenter, true);
  s_up_repeat = app_timer_register(400, up_repeat, view);
}

static void up_released(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  if (s_up_repeat) {
    app_timer_cancel(s_up_repeat);
    s_up_repeat = NULL;
  }
}

static void down_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbChatsWindow *view = context;
  if (view->notice.notify->focused) {
    tb_notify_focus(view->notice.notify, false);
    return;
  }
  menu_layer_set_selected_next(view->menu, false, MenuRowAlignCenter, true);
}

static void select_single(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbChatsWindow *view = context;
  if (view->notice.notify->focused) {
    tb_notify_view_activate(&view->notice);
    return;
  }
  MenuIndex index = menu_layer_get_selected_index(view->menu);
  select_click(view->menu, &index, view);
}

static void select_long(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  TbChatsWindow *view = context;
  refresh(view);
}

static void click_config(void *context) {
  window_set_click_context(BUTTON_ID_DOWN, context);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_raw_click_subscribe(BUTTON_ID_UP, up_pressed, up_released, context);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}

#if defined(PBL_TOUCH)
static TbChatsWindow *s_touch_view;
static int16_t s_touch_start;
static bool s_touch_armed;

static void touched(const TouchEvent *event, void *context) {
  (void)context;
  TbChatsWindow *view = s_touch_view;
  if (!view || !view->menu || event->non_navigational) { return; }
  if (event->type == TouchEvent_Touchdown) {
    if (tb_notify_view_hit(&view->notice, event->y)) {
      s_touch_armed = false;
      tb_notify_view_activate(&view->notice);
      return;
    }
    s_touch_start = event->y;
    s_touch_armed = scroll_layer_get_content_offset(menu_layer_get_scroll_layer(view->menu)).y >= 0;
    return;
  }
  if (!s_touch_armed) { return; }
  if (event->type == TouchEvent_PositionUpdate) {
    const int32_t distance = event->y - s_touch_start;
    if (distance > 0 || view->pull.phase == TB_PULL_DRAGGING) {
      tb_pull_drag(&view->pull, (uint16_t)(distance > 0 ? distance * TB_PULL_FULL / PULL_DISTANCE : 0));
    }
  } else if (event->type == TouchEvent_Liftoff) {
    s_touch_armed = false;
    tb_pull_release(&view->pull);
  }
}
#endif

static AppTimer *s_minute;

static void minute_elapsed(void *context);

static void schedule_minute(void) {
  time_t seconds = 0;
  uint16_t milliseconds = 0;
  time_ms(&seconds, &milliseconds);
  const uint32_t remaining = (uint32_t)(60 - seconds % 60) * 1000u - milliseconds;
  s_minute = app_timer_register(remaining + 50, minute_elapsed, NULL);
}

static void minute_elapsed(void *context) {
  (void)context;
  s_minute = NULL;
  if (!s_ticking) { return; }
  if (s_ticking->menu) { layer_mark_dirty(menu_layer_get_layer(s_ticking->menu)); }
  schedule_minute();
}

static void window_load(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  view->menu = menu_layer_create(layer_get_bounds(root));
  tb_theme_menu(view->menu);
  menu_layer_set_callbacks(view->menu, view, (MenuLayerCallbacks){
    .get_num_sections = sections,
    .get_header_height = header_height_callback,
    .draw_header = draw_header,
    .get_num_rows = rows,
    .get_cell_height = row_height,
    .draw_row = draw_row,
    .select_click = select_click,
    .select_long_click = select_long_click,
    .selection_changed = selection_changed,
  });
  window_set_click_config_provider_with_context(window, click_config, view);
  layer_add_child(root, menu_layer_get_layer(view->menu));
#if defined(PBL_ROUND)
  s_bar_view = view;
  view->bar = layer_create(layer_get_bounds(root));
  layer_set_update_proc(view->bar, bar_update);
  layer_add_child(root, view->bar);
  tb_notify_view_attach(&view->notice, root, 46);
#else
  tb_notify_view_attach(&view->notice, root, segment_height());
#endif
  restore(view);
  tb_diag_event("window_push", "chats");
}

static void window_unload(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  tb_notify_view_detach(&view->notice);
  tb_diag_event("window_pop", "chats");
#if defined(PBL_ROUND)
  layer_destroy(view->bar);
  view->bar = NULL;
  s_bar_view = NULL;
#endif
  menu_layer_destroy(view->menu);
  view->menu = NULL;
}

static void window_appear(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  s_ticking = view;
  if (!s_minute) { schedule_minute(); }
  tb_chats_set_active(view->chats, true);
  tb_connection_set_active(view->connection, true);
#if defined(PBL_TOUCH)
  if (touch_service_is_enabled()) {
    s_touch_view = view;
    touch_service_subscribe(touched, NULL);
  }
#endif
}

static void window_disappear(Window *window) {
  TbChatsWindow *view = window_get_user_data(window);
  if (s_minute) {
    app_timer_cancel(s_minute);
    s_minute = NULL;
  }
  s_ticking = NULL;
  tb_chats_set_active(view->chats, false);
  tb_connection_set_active(view->connection, false);
  tb_pull_reset(&view->pull);
  s_pull_level = 0;
  tb_notify_focus(view->notice.notify, false);
  up_released(NULL, view);
#if defined(PBL_TOUCH)
  if (s_touch_view == view) {
    touch_service_unsubscribe();
    s_touch_view = NULL;
  }
#endif
}

void tb_chats_window_init(TbChatsWindow *view, TbChats *chats, TbConnection *connection, TbNotify *notify, TbNotifyActivate activate,
                          const TbStrings *strings, TbChatsActions actions) {
  tb_notify_view_init(&view->notice, notify, activate, actions.context);
  view->chats = chats;
  view->connection = connection;
  view->strings = strings;
  view->actions = actions;
  view->pull_timer = NULL;
  tb_pull_init(&view->pull, (TbPullPorts){pull_changed, pull_trigger, pull_schedule, pull_cancel, pull_now, view});
  view->show_archive = true;
  view->unread_mode = TB_UNREAD_MODE_CHATS;
  tb_chats_window_reset(view);
  view->window = window_create();
  window_set_user_data(view->window, view);
  window_set_window_handlers(view->window, (WindowHandlers){.load = window_load, .unload = window_unload, .appear = window_appear,
                                                             .disappear = window_disappear});
}

void tb_chats_window_deinit(TbChatsWindow *view) {
  window_destroy(view->window);
  view->window = NULL;
}

void tb_chats_window_configure(TbChatsWindow *view, bool show_archive, uint8_t unread_mode) {
  view->show_archive = show_archive;
  view->unread_mode = unread_mode;
}

void tb_chats_window_reset(TbChatsWindow *view) {
  view->selected_id[0] = '\0';
  view->selected_row = 0;
  view->placed = false;
}

void tb_chats_window_reload(TbChatsWindow *view) {
  if (!view->menu) { return; }
  menu_layer_reload_data(view->menu);
  restore(view);
  tb_notify_view_refresh(&view->notice);
}
