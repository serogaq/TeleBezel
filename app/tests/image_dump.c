#include <stdio.h>
#include <stdlib.h>
#include "image.h"

static int hex(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

int main(void) {
  static char line[200000];
  static uint8_t bytes[100000];
  if (!fgets(line, sizeof(line), stdin)) { return 1; }
  size_t length = 0;
  for (size_t index = 0; line[index] && line[index + 1] && hex(line[index]) >= 0; index += 2) {
    bytes[length++] = (uint8_t)(hex(line[index]) * 16 + hex(line[index + 1]));
  }
  TbImageDecoder decoder;
  tb_image_reset(&decoder);
  size_t offset = 0;
  TbImageStatus status = TB_IMAGE_MORE;
  while (offset < length) {
    uint16_t used = 0;
    const uint16_t chunk = (uint16_t)(length - offset > 1000 ? 1000 : length - offset);
    status = tb_image_feed(&decoder, bytes + offset, chunk, &used);
    offset += used;
    if (status == TB_IMAGE_HEADER_READY) { decoder.pixels = malloc(decoder.size); }
    if (status == TB_IMAGE_BAD || status == TB_IMAGE_DONE) { break; }
  }
  printf("{\"status\":%d,\"width\":%u,\"height\":%u,\"shape\":%u,\"bits\":%u,\"size\":%u,\"tag\":%lu,\"consumed\":%lu}\n", (int)status, decoder.header.width,
         decoder.header.height, decoder.header.shape, decoder.header.bits, decoder.size, (unsigned long)decoder.header.tag, (unsigned long)offset);
  free(decoder.pixels);
  return 0;
}
