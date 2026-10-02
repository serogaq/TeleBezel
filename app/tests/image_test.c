#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "generated/image_vectors.h"
#include "image.h"

static TbImageStatus decode(const TbImageVector *vector, const uint8_t *data, uint32_t length, uint16_t step, TbImageDecoder *decoder) {
  tb_image_reset(decoder);
  TbImageStatus status = TB_IMAGE_MORE;
  uint32_t offset = 0;
  while (offset < length) {
    uint16_t chunk = (uint16_t)(length - offset < step ? length - offset : step);
    uint16_t done = 0;
    while (done < chunk) {
      uint16_t used = 0;
      status = tb_image_feed(decoder, data + offset + done, (uint16_t)(chunk - done), &used);
      done = (uint16_t)(done + used);
      if (status == TB_IMAGE_BAD) { return status; }
      if (status == TB_IMAGE_HEADER_READY) {
        assert(decoder->size == vector->size);
        decoder->pixels = malloc(decoder->size);
        assert(decoder->pixels);
      }
      if (status == TB_IMAGE_DONE) { break; }
    }
    offset += chunk;
    if (status == TB_IMAGE_DONE && offset < length) { return TB_IMAGE_BAD; }
  }
  return status;
}

static void check_geometry(void) {
  assert(tb_image_isqrt(0) == 0 && tb_image_isqrt(1) == 1 && tb_image_isqrt(67600) == 260 && tb_image_isqrt(67599) == 259);
  TbImageHeader header = {4, 1, 16, 260, 260, 260, 260, 0, 0, {0}};
  TbImageRow row;
  tb_image_row(&header, 130, &row);
  assert(row.x == 0 && row.count == 260 && row.y == 130);
  tb_image_row(&header, 0, &row);
  assert(row.count > 0 && row.count < 60 && row.x > 100);
  header.height = 146;
  tb_image_row(&header, 0, &row);
  assert(row.y == 57 && row.count > 200);
  TbImageHeader rect = {4, 0, 16, 120, 80, 200, 228, 0, 0, {0}};
  tb_image_row(&rect, 3, &row);
  assert(row.x == 40 && row.y == 77 && row.count == 120 && row.bytes == 60);
}

int main(void) {
  check_geometry();
  const uint16_t steps[] = {1, 3, 7, 64, 3990, 60000};
  for (size_t index = 0; index < sizeof(tb_image_vectors) / sizeof(tb_image_vectors[0]); ++index) {
    const TbImageVector *vector = &tb_image_vectors[index];
    for (size_t step = 0; step < sizeof(steps) / sizeof(steps[0]); ++step) {
      TbImageDecoder decoder;
      assert(decode(vector, vector->data, vector->length, steps[step], &decoder) == TB_IMAGE_DONE);
      assert(decoder.header.width == vector->width && decoder.header.height == vector->height && decoder.header.tag == vector->tag);
      assert(decoder.header.bits == vector->bits && decoder.header.shape == vector->shape);
      assert(tb_image_crc(0, decoder.pixels, decoder.size) == vector->crc);
      free(decoder.pixels);
    }
    uint8_t *copy = malloc(vector->length + 1);
    memcpy(copy, vector->data, vector->length);
    TbImageDecoder decoder;
    copy[vector->length - 1] ^= 0x01;
    const TbImageStatus flipped = decode(vector, copy, vector->length, 97, &decoder);
    assert(flipped == TB_IMAGE_BAD || flipped == TB_IMAGE_MORE);
    free(decoder.pixels);
    memcpy(copy, vector->data, vector->length);
    copy[vector->length] = 0;
    assert(decode(vector, copy, vector->length + 1, 1000, &decoder) == TB_IMAGE_BAD);
    free(decoder.pixels);
    assert(decode(vector, vector->data, vector->length - 1, 1000, &decoder) == TB_IMAGE_MORE);
    free(decoder.pixels);
    memcpy(copy, vector->data, vector->length);
    copy[0] = 'X';
    assert(decode(vector, copy, vector->length, 1000, &decoder) == TB_IMAGE_BAD && decoder.pixels == NULL);
    memcpy(copy, vector->data, vector->length);
    copy[3] = 3;
    assert(decode(vector, copy, vector->length, 1000, &decoder) == TB_IMAGE_BAD && decoder.pixels == NULL);
    free(copy);
  }
  const uint8_t far_reference[] = {'T', 'B', 1, 4, 0, 1, 4, 0, 1, 0, 4, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0xC0, 0xC1, 9, 0};
  TbImageDecoder decoder;
  tb_image_reset(&decoder);
  uint16_t used = 0;
  assert(tb_image_feed(&decoder, far_reference, sizeof(far_reference), &used) == TB_IMAGE_HEADER_READY && used == 23);
  uint8_t pixels[2];
  decoder.pixels = pixels;
  assert(tb_image_feed(&decoder, far_reference + used, (uint16_t)(sizeof(far_reference) - used), &used) == TB_IMAGE_BAD);
  const uint8_t huge[] = {'T', 'B', 1, 4, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1, 0, 0, 0, 0, 0, 0, 0, 0xC0};
  tb_image_reset(&decoder);
  assert(tb_image_feed(&decoder, huge, sizeof(huge), &used) == TB_IMAGE_BAD);
  const uint8_t too_wide[] = {'T', 'B', 1, 4, 0, 1, 10, 0, 1, 0, 4, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0xC0};
  tb_image_reset(&decoder);
  assert(tb_image_feed(&decoder, too_wide, sizeof(too_wide), &used) == TB_IMAGE_BAD);
  return 0;
}
