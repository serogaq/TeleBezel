#include <stdlib.h>
#include "errors.h"
#include "fake.h"
#include "generated/image_vectors.h"
#include "media.h"

#define ACCOUNT "00112233-4455-4677-8899-aabbccddeeff"
#define CHUNK 1000

static size_t s_heap = 60000;
static size_t heap_free(void) { return s_heap; }

static TbMediaConfig config(void) {
  return (TbMediaConfig){260, 260, TB_MEDIA_SHAPE_ROUND, TB_MEDIA_FORMAT_P4 | TB_MEDIA_FORMAT_P2, 28672, 18432, 4096, 25000, 30000, heap_free};
}

static void info_record(Buf *buf, uint8_t state, uint8_t index, uint8_t count, uint32_t tag, uint32_t total, uint16_t retry_after) {
  begin(buf, TB_RECORD_MEDIA_INFO);
  put8(buf, state); put8(buf, index); put8(buf, count); put8(buf, 0); put32(buf, tag); put32(buf, total); put16(buf, retry_after);
  putstr8(buf, "77");
  end(buf);
}

static void data_record(Buf *buf, uint32_t offset, const uint8_t *data, uint16_t length) {
  begin(buf, TB_RECORD_MEDIA_DATA);
  put32(buf, offset);
  memcpy(buf->data + buf->length, data, length);
  buf->length = (uint16_t)(buf->length + length);
  end(buf);
}

static uint16_t chunks_of(uint32_t remaining) { return (uint16_t)((remaining + CHUNK - 1) / CHUNK); }

static void stream(TbRequestLayer *layer, uint32_t sequence, const TbImageVector *vector, uint32_t from, uint32_t until, bool info) {
  const uint16_t total = chunks_of(vector->length - from);
  uint16_t index = 0;
  for (uint32_t offset = from; offset < until; offset += CHUNK, ++index) {
    Buf buf = {0};
    if (index == 0 && info) { info_record(&buf, TB_MEDIA_STATE_READY, 0, 3, vector->tag, vector->length, 0); }
    const uint32_t size = vector->length - offset < CHUNK ? vector->length - offset : CHUNK;
    data_record(&buf, offset, vector->data + offset, (uint16_t)size);
    deliver(layer, sequence, TB_RESULT_OK, 0, &buf, index, total);
  }
}

int main(void) {
  const TbImageVector *vector = &tb_image_vectors[2];
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbMedia media;
  tb_media_init(&media, &layer, fake_view_ports(&fake), config());

  tb_media_bind(&media, ACCOUNT, "-42", "77", TB_MEDIA_FLAG_IMAGE | TB_MEDIA_FLAG_ALBUM, 3);
  assert(media.phase == TB_MEDIA_IDLE && media.count == 3 && fake.sends == 0);
  tb_media_load(&media);
  const TbRequestArgs *sent = fake_last(&fake);
  assert(sent->kind == TB_REQUEST_MEDIA && strcmp(sent->message, "77") == 0 && sent->media_index == 0 && sent->media_offset == 0);
  assert(sent->media_spec[0] == 4 && sent->media_spec[1] == 1 && sent->media_spec[4] == TB_MEDIA_SHAPE_ROUND);
  assert((sent->media_spec[6] | (sent->media_spec[7] << 8)) == 28672);
  assert(media.phase == TB_MEDIA_LOADING && tb_media_busy(&media));
  uint32_t sequence = fake_last_sequence(&fake);
  stream(&layer, sequence, vector, 0, vector->length, true);
  assert(media.phase == TB_MEDIA_READY && tb_media_has_image(&media) && tb_media_percent(&media) == 100);
  assert(tb_image_crc(0, media.image.pixels, media.image.size) == vector->crc && media.tag == vector->tag);

  tb_media_load(&media);
  assert(fake_last_sequence(&fake) == sequence);

  int sends = fake.sends;
  tb_media_show(&media, 1);
  assert(fake.sent[sends].kind == TB_REQUEST_MEDIA && fake_last(&fake)->media_index == 1 && !tb_media_has_image(&media));
  sequence = fake_last_sequence(&fake);
  stream(&layer, sequence, vector, 0, CHUNK * 2, true);
  assert(media.phase == TB_MEDIA_TRANSFER && media.received == CHUNK * 2 && tb_media_percent(&media) > 0);
  fake.now += 30000;
  tb_requests_tick(&layer);
  assert(media.phase == TB_MEDIA_TRANSFER && fake_last(&fake)->media_offset == CHUNK * 2 && fake_last(&fake)->media_tag == vector->tag);
  sequence = fake_last_sequence(&fake);
  const uint32_t rest = vector->length - CHUNK * 2;
  {
    const uint16_t total = chunks_of(rest);
    uint16_t index = 0;
    for (uint32_t offset = CHUNK * 2; offset < vector->length; offset += CHUNK, ++index) {
      Buf buf = {0};
      if (index == 0) { info_record(&buf, TB_MEDIA_STATE_READY, 1, 3, vector->tag, vector->length, 0); }
      const uint32_t size = vector->length - offset < CHUNK ? vector->length - offset : CHUNK;
      data_record(&buf, offset, vector->data + offset, (uint16_t)size);
      deliver(&layer, sequence, TB_RESULT_OK, 0, &buf, index, total);
    }
  }
  assert(media.phase == TB_MEDIA_READY && media.index == 1);

  tb_media_show(&media, 2);
  sequence = fake_last_sequence(&fake);
  Buf waiting = {0};
  info_record(&waiting, TB_MEDIA_STATE_DOWNLOADING, 2, 3, 0, 0, 2);
  deliver(&layer, sequence, TB_RESULT_OK, 0, &waiting, 0, 1);
  assert(media.phase == TB_MEDIA_WAITING && fake.view_timer && fake.view_delay == 2000);
  fake.now += 2000;
  tb_media_timer(&media);
  assert(media.phase == TB_MEDIA_LOADING && fake_last_sequence(&fake) != sequence);
  sequence = fake_last_sequence(&fake);
  fake.now += 40000;
  deliver(&layer, sequence, TB_RESULT_OK, 0, &waiting, 0, 1);
  assert(media.phase == TB_MEDIA_ERROR && media.error == TB_ERROR_NO_RESPONSE);

  tb_media_retry(&media);
  sequence = fake_last_sequence(&fake);
  sends = fake.sends;
  tb_media_bind(&media, ACCOUNT, "-42", "78", TB_MEDIA_FLAG_IMAGE, 1);
  assert(fake.sends == sends + 1 && fake_last(&fake)->kind == TB_REQUEST_MEDIA_CANCEL);
  const uint32_t cancel = fake_last_sequence(&fake);
  deliver(&layer, cancel, TB_RESULT_OK, 0, NULL, 0, 1);
  assert(!tb_requests_chunk(&layer, sequence, &(TbResponse){.result = TB_RESULT_OK, .index = 0, .total = 1}));
  assert(media.phase == TB_MEDIA_IDLE && strcmp(media.message, "78") == 0);

  tb_media_load(&media);
  sequence = fake_last_sequence(&fake);
  uint8_t *broken = malloc(vector->length);
  memcpy(broken, vector->data, vector->length);
  broken[vector->length - 1] ^= 0xFF;
  {
    const TbImageVector copy = {vector->name, broken, vector->length, vector->bits, vector->shape, vector->width, vector->height,
                                vector->canvas_width, vector->canvas_height, vector->tag, vector->size, vector->crc};
    stream(&layer, sequence, &copy, 0, copy.length, true);
    assert(media.phase == TB_MEDIA_LOADING && media.crc_retry && fake_last_sequence(&fake) != sequence);
    sequence = fake_last_sequence(&fake);
    stream(&layer, sequence, &copy, 0, copy.length, true);
    assert(media.phase == TB_MEDIA_ERROR && media.error == TB_RESULT_PROTOCOL_ERROR && !tb_media_has_image(&media));
  }
  free(broken);

  tb_media_retry(&media);
  sequence = fake_last_sequence(&fake);
  Buf gone = {0};
  info_record(&gone, TB_MEDIA_STATE_UNAVAILABLE, 0, 1, 0, 0, 0);
  deliver(&layer, sequence, TB_RESULT_OK, 0, &gone, 0, 1);
  assert(media.phase == TB_MEDIA_UNAVAILABLE);
  tb_media_retry(&media);
  sequence = fake_last_sequence(&fake);
  deliver(&layer, sequence, TB_RESULT_ACCOUNT_NEEDS_LOGIN, 0, NULL, 0, 1);
  assert(media.phase == TB_MEDIA_ERROR && media.error == TB_RESULT_ACCOUNT_NEEDS_LOGIN);

  tb_media_retry(&media);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_MESSAGE_UNAVAILABLE, 0, NULL, 0, 1);
  assert(media.phase == TB_MEDIA_UNAVAILABLE);

  tb_media_bind(&media, ACCOUNT, "-42", "79", TB_MEDIA_FLAG_IMAGE | TB_MEDIA_FLAG_SPOILER, 1);
  sends = fake.sends;
  tb_media_load(&media);
  assert(media.phase == TB_MEDIA_SPOILER && fake.sends == sends);
  tb_media_reveal(&media);
  assert(fake.sends == sends + 1 && (fake_last(&fake)->media_spec[4] & 0x80) != 0);

  tb_media_bind(&media, ACCOUNT, "-42", "80", TB_MEDIA_FLAG_RESTRICTED, 1);
  assert(media.phase == TB_MEDIA_RESTRICTED);
  sends = fake.sends;
  tb_media_load(&media);
  assert(fake.sends == sends);

  tb_media_bind(&media, ACCOUNT, "-42", "81", TB_MEDIA_FLAG_IMAGE, 1);
  s_heap = 20000;
  sends = fake.sends;
  tb_media_load(&media);
  assert(media.phase == TB_MEDIA_NO_MEMORY && fake.sends == sends);
  s_heap = 40000;
  tb_media_retry(&media);
  assert(media.phase == TB_MEDIA_LOADING && (fake_last(&fake)->media_spec[6] | (fake_last(&fake)->media_spec[7] << 8)) == 21504);
  sequence = fake_last_sequence(&fake);
  stream(&layer, sequence, vector, 0, CHUNK, true);
  assert(media.phase == TB_MEDIA_LOADING && fake_last_sequence(&fake) != sequence);
  sequence = fake_last_sequence(&fake);
  stream(&layer, sequence, vector, 0, CHUNK, true);
  assert(media.phase == TB_MEDIA_ERROR && !tb_media_has_image(&media));

  tb_media_close(&media);
  assert(media.phase == TB_MEDIA_NONE && media.account[0] == '\0');
  tb_media_load(&media);
  return 0;
}
