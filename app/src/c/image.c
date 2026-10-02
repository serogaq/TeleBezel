#include "image.h"
#include <stddef.h>
#include <string.h>

#define OP_NONE 0
#define OP_LITERAL 1
#define OP_REPEAT 2
#define OP_DISTANCE_LOW 3
#define OP_DISTANCE_HIGH 4

void tb_image_reset(TbImageDecoder *decoder) { memset(decoder, 0, sizeof(*decoder)); }

uint16_t tb_image_isqrt(uint32_t value) {
  uint32_t root = 0;
  uint32_t bit = 1u << 30;
  while (bit > value) { bit >>= 2; }
  while (bit) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)root;
}

void tb_image_row(const TbImageHeader *header, uint16_t y, TbImageRow *row) {
  const int16_t left = (int16_t)((header->canvas_width - header->width) / 2);
  const int16_t top = (int16_t)((header->canvas_height - header->height) / 2);
  row->y = (int16_t)(top + y);
  row->x = left;
  row->count = header->width;
  if (header->shape == 1) {
    const int32_t diameter = header->canvas_width;
    const int32_t dy = 2 * row->y + 1 - (int32_t)header->canvas_height;
    int32_t from = 0;
    int32_t to = 0;
    if (dy > -diameter && dy < diameter) {
      const int32_t half = tb_image_isqrt((uint32_t)(diameter * diameter - dy * dy));
      from = (diameter - half - 1) / 2 - 1;
      to = (diameter + half - 1) / 2 + 2;
    }
    if (from < left) { from = left; }
    if (to > left + (int32_t)header->width) { to = left + (int32_t)header->width; }
    row->x = (int16_t)from;
    row->count = (uint16_t)(to > from ? to - from : 0);
  }
  row->bytes = (uint16_t)((row->count * header->bits + 7) / 8);
}

uint32_t tb_image_size(const TbImageHeader *header) {
  uint32_t total = 0;
  TbImageRow row;
  for (uint16_t y = 0; y < header->height; ++y) {
    tb_image_row(header, y, &row);
    total += row.bytes;
  }
  return total;
}

uint8_t tb_image_pixel(const TbImageHeader *header, const uint8_t *row, uint16_t index) {
  const uint16_t bit = (uint16_t)(index * header->bits);
  const uint8_t shift = (uint8_t)(8 - header->bits - (bit & 7));
  return (uint8_t)((row[bit >> 3] >> shift) & ((1u << header->bits) - 1));
}

uint32_t tb_image_crc(uint32_t crc, const uint8_t *data, uint32_t length) {
  crc = ~crc;
  while (length--) {
    crc ^= *data++;
    for (int bit = 0; bit < 8; ++bit) { crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
  }
  return ~crc;
}

static uint16_t le16(const uint8_t *bytes) { return (uint16_t)(bytes[0] | (bytes[1] << 8)); }
static uint32_t le32(const uint8_t *bytes) { return (uint32_t)le16(bytes) | ((uint32_t)le16(bytes + 2) << 16); }

static bool parse_header(TbImageDecoder *decoder) {
  const uint8_t *head = decoder->head;
  TbImageHeader *header = &decoder->header;
  header->bits = head[3];
  header->shape = head[4];
  header->colors = head[5];
  header->width = le16(head + 6);
  header->height = le16(head + 8);
  header->canvas_width = le16(head + 10);
  header->canvas_height = le16(head + 12);
  header->tag = le32(head + 14);
  header->crc = le32(head + 18);
  memcpy(header->palette, head + TB_IMAGE_HEADER_SIZE, header->colors);
  const uint32_t size = tb_image_size(header);
  if (size == 0 || size > UINT16_MAX) { return false; }
  decoder->size = (uint16_t)size;
  return true;
}

static bool header_valid(const uint8_t *head) {
  const uint8_t bits = head[3];
  const uint16_t width = le16(head + 6);
  const uint16_t height = le16(head + 8);
  return head[0] == 'T' && head[1] == 'B' && head[2] == TB_IMAGE_VERSION && (bits == 1 || bits == 2 || bits == 4) && head[4] <= 1 &&
         head[5] >= 1 && head[5] <= (1u << bits) && width > 0 && height > 0 && width <= le16(head + 10) && height <= le16(head + 12);
}

static bool emit(TbImageDecoder *decoder, uint8_t value) {
  if (decoder->filled >= decoder->size) { return false; }
  decoder->pixels[decoder->filled++] = value;
  return true;
}

static bool step(TbImageDecoder *decoder, uint8_t byte) {
  switch (decoder->op) {
    case OP_NONE:
      if (byte < 0x80) {
        decoder->op = OP_LITERAL;
        decoder->remaining = (uint8_t)(byte + 1);
      } else if (byte < 0xC0) {
        decoder->op = OP_REPEAT;
        decoder->remaining = (uint8_t)((byte & 0x3F) + 3);
      } else {
        decoder->op = OP_DISTANCE_LOW;
        decoder->remaining = (uint8_t)((byte & 0x3F) + 3);
      }
      return true;
    case OP_LITERAL:
      if (!emit(decoder, byte)) { return false; }
      if (--decoder->remaining == 0) { decoder->op = OP_NONE; }
      return true;
    case OP_REPEAT:
      while (decoder->remaining) {
        if (!emit(decoder, byte)) { return false; }
        --decoder->remaining;
      }
      decoder->op = OP_NONE;
      return true;
    case OP_DISTANCE_LOW:
      decoder->distance = byte;
      decoder->op = OP_DISTANCE_HIGH;
      return true;
    default:
      decoder->distance = (uint16_t)(decoder->distance | (byte << 8));
      if (decoder->distance == 0 || decoder->distance > decoder->filled) { return false; }
      while (decoder->remaining) {
        if (!emit(decoder, decoder->pixels[decoder->filled - decoder->distance])) { return false; }
        --decoder->remaining;
      }
      decoder->op = OP_NONE;
      return true;
  }
}

TbImageStatus tb_image_feed(TbImageDecoder *decoder, const uint8_t *data, uint16_t length, uint16_t *used) {
  uint16_t offset = 0;
  *used = 0;
  if (!decoder->header_ready) {
    while (offset < length) {
      decoder->head[decoder->head_length++] = data[offset++];
      if (decoder->head_length == TB_IMAGE_HEADER_SIZE && !header_valid(decoder->head)) { return TB_IMAGE_BAD; }
      if (decoder->head_length >= TB_IMAGE_HEADER_SIZE && decoder->head_length == TB_IMAGE_HEADER_SIZE + decoder->head[5]) {
        if (!parse_header(decoder)) { return TB_IMAGE_BAD; }
        decoder->header_ready = true;
        *used = offset;
        return TB_IMAGE_HEADER_READY;
      }
    }
    *used = offset;
    return TB_IMAGE_MORE;
  }
  if (!decoder->pixels) { return TB_IMAGE_BAD; }
  while (offset < length) {
    if (decoder->filled >= decoder->size && decoder->op == OP_NONE) { return TB_IMAGE_BAD; }
    if (!step(decoder, data[offset++])) { return TB_IMAGE_BAD; }
  }
  *used = offset;
  if (decoder->filled < decoder->size || decoder->op != OP_NONE) { return TB_IMAGE_MORE; }
  return tb_image_crc(0, decoder->pixels, decoder->size) == decoder->header.crc ? TB_IMAGE_DONE : TB_IMAGE_BAD;
}
