#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB_IMAGE_HEADER_SIZE 22
#define TB_IMAGE_MAX_COLORS 16
#define TB_IMAGE_VERSION 1

typedef struct {
  uint8_t bits;
  uint8_t shape;
  uint8_t colors;
  uint16_t width;
  uint16_t height;
  uint16_t canvas_width;
  uint16_t canvas_height;
  uint32_t tag;
  uint32_t crc;
  uint8_t palette[TB_IMAGE_MAX_COLORS];
} TbImageHeader;

typedef enum { TB_IMAGE_MORE, TB_IMAGE_HEADER_READY, TB_IMAGE_DONE, TB_IMAGE_BAD } TbImageStatus;

typedef struct {
  TbImageHeader header;
  uint8_t head[TB_IMAGE_HEADER_SIZE + TB_IMAGE_MAX_COLORS];
  uint8_t head_length;
  bool header_ready;
  uint8_t *pixels;
  uint16_t size;
  uint16_t filled;
  uint8_t op;
  uint8_t remaining;
  uint16_t distance;
} TbImageDecoder;

typedef struct {
  int16_t x;
  int16_t y;
  uint16_t count;
  uint16_t bytes;
} TbImageRow;

void tb_image_reset(TbImageDecoder *decoder);
TbImageStatus tb_image_feed(TbImageDecoder *decoder, const uint8_t *data, uint16_t length, uint16_t *used);
uint32_t tb_image_size(const TbImageHeader *header);
void tb_image_row(const TbImageHeader *header, uint16_t y, TbImageRow *row);
uint8_t tb_image_pixel(const TbImageHeader *header, const uint8_t *row, uint16_t index);
uint32_t tb_image_crc(uint32_t crc, const uint8_t *data, uint32_t length);
uint16_t tb_image_isqrt(uint32_t value);
