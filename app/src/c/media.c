#include "media.h"
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "errors.h"
#include "generated/protocol.h"
#include "text.h"

#define TB_MEDIA_RESUMES 2
#define TB_MEDIA_POLL_MIN 1000

static void changed(TbMedia *media) {
  ++media->revision;
  media->ports.changed(media->ports.context);
}

static uint32_t now(TbMedia *media) { return media->ports.now(media->ports.context); }

static void drop_pixels(TbMedia *media) {
  free(media->image.pixels);
  tb_image_reset(&media->image);
  media->received = 0;
  media->tag = 0;
  media->total = 0;
}

static void cancel_pending(TbMedia *media) {
  if (!media->pending) { return; }
  const uint32_t pending = media->pending;
  media->pending = 0;
  tb_requests_cancel(media->requests, pending);
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_MEDIA_CANCEL;
  tb_requests_submit(media->requests, &args, media->config.timeout, 1, false, NULL, NULL);
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response);

static void submit(TbMedia *media, uint32_t offset) {
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_MEDIA;
  strncpy(args.account, media->account, sizeof(args.account) - 1);
  strncpy(args.chat, media->chat, sizeof(args.chat) - 1);
  strncpy(args.message, media->message, sizeof(args.message) - 1);
  args.media_index = media->index;
  uint8_t *spec = args.media_spec;
  spec[0] = (uint8_t)(media->config.width & 0xFF);
  spec[1] = (uint8_t)(media->config.width >> 8);
  spec[2] = (uint8_t)(media->config.height & 0xFF);
  spec[3] = (uint8_t)(media->config.height >> 8);
  spec[4] = (uint8_t)(media->config.shape | (media->reveal ? 0x80 : 0));
  spec[5] = media->config.formats;
  spec[6] = (uint8_t)(media->budget & 0xFF);
  spec[7] = (uint8_t)(media->budget >> 8);
  args.media_offset = offset;
  args.media_tag = offset ? media->tag : 0;
  media->error = TB_ERROR_NONE;
  media->pending = tb_requests_submit(media->requests, &args, media->config.timeout, 2, false, completed, media);
  if (!media->pending) {
    media->phase = TB_MEDIA_ERROR;
    media->error = tb_error_submit_failed(media->requests);
  }
}

static void start(TbMedia *media) {
  cancel_pending(media);
  drop_pixels(media);
  media->ports.cancel(media->ports.context);
  media->due = 0;
  media->resumes = 0;
  const size_t available = media->config.heap_free ? media->config.heap_free() : media->config.max_budget + media->config.reserve;
  size_t budget = available > media->config.reserve ? available - media->config.reserve : 0;
  if (budget > media->config.max_budget) { budget = media->config.max_budget; }
  if (budget < media->config.min_budget) {
    media->phase = TB_MEDIA_NO_MEMORY;
    changed(media);
    return;
  }
  media->budget = (uint16_t)(budget & ~(size_t)1023);
  media->started = now(media);
  media->phase = TB_MEDIA_LOADING;
  submit(media, 0);
  changed(media);
}

static void fail(TbMedia *media, int32_t error, TbRequestOutcome outcome) {
  media->pending = 0;
  const bool transport = outcome == TB_OUTCOME_TIMEOUT || outcome == TB_OUTCOME_UNREACHABLE || outcome == TB_OUTCOME_PROTOCOL;
  if (transport && media->phase == TB_MEDIA_TRANSFER && media->received > 0 && media->resumes < TB_MEDIA_RESUMES) {
    ++media->resumes;
    submit(media, media->received);
    changed(media);
    return;
  }
  media->phase = error == TB_RESULT_MESSAGE_UNAVAILABLE ? TB_MEDIA_UNAVAILABLE : TB_MEDIA_ERROR;
  media->error = error;
  changed(media);
}

static void corrupt(TbMedia *media) {
  media->pending = 0;
  if (!media->crc_retry) {
    media->crc_retry = true;
    start(media);
    return;
  }
  drop_pixels(media);
  media->phase = TB_MEDIA_ERROR;
  media->error = TB_RESULT_PROTOCOL_ERROR;
  changed(media);
}

static void wait_for(TbMedia *media, uint16_t retry_after) {
  const uint32_t current = now(media);
  if ((uint32_t)(current - media->started) >= media->config.wait_limit) {
    media->phase = TB_MEDIA_ERROR;
    media->error = TB_ERROR_NO_RESPONSE;
    return;
  }
  const uint32_t delay = retry_after * 1000u > TB_MEDIA_POLL_MIN ? retry_after * 1000u : TB_MEDIA_POLL_MIN;
  media->phase = TB_MEDIA_WAITING;
  media->due = current + delay;
  media->ports.schedule(media->ports.context, delay);
}

static bool info(TbMedia *media, const TbMediaInfoRecord *record) {
  media->count = record->count ? record->count : 1;
  media->flags = record->flags;
  media->server_state = record->state;
  tb_copy_id(media->item, sizeof(media->item), record->item, false);
  switch (record->state) {
    case TB_MEDIA_STATE_READY:
      if (record->tag != media->tag) {
        free(media->image.pixels);
        tb_image_reset(&media->image);
        media->received = 0;
      }
      media->tag = record->tag;
      media->total = record->total;
      media->phase = TB_MEDIA_TRANSFER;
      return record->total > 0;
    case TB_MEDIA_STATE_DOWNLOADING:
    case TB_MEDIA_STATE_PREPARING:
      wait_for(media, record->retry_after);
      return true;
    case TB_MEDIA_STATE_SPOILER: media->phase = TB_MEDIA_SPOILER; return true;
    case TB_MEDIA_STATE_RESTRICTED: media->phase = TB_MEDIA_RESTRICTED; return true;
    case TB_MEDIA_STATE_UNSUPPORTED: media->phase = TB_MEDIA_UNSUPPORTED; return true;
    case TB_MEDIA_STATE_UNAVAILABLE: media->phase = TB_MEDIA_UNAVAILABLE; return true;
    default: media->phase = TB_MEDIA_NONE; return true;
  }
}

typedef enum { FEED_OK, FEED_PROTOCOL, FEED_CORRUPT, FEED_MEMORY } TbFeed;

static TbFeed feed(TbMedia *media, const TbMediaDataRecord *record) {
  if (media->phase != TB_MEDIA_TRANSFER || record->offset != media->received) { return FEED_PROTOCOL; }
  uint16_t position = 0;
  while (position < record->data.length) {
    uint16_t used = 0;
    const TbImageStatus status = tb_image_feed(&media->image, record->data.data + position, (uint16_t)(record->data.length - position), &used);
    position = (uint16_t)(position + used);
    if (status == TB_IMAGE_BAD) { return FEED_CORRUPT; }
    if (status == TB_IMAGE_HEADER_READY) {
      if (media->image.header.tag != media->tag || media->image.size > media->budget) { return FEED_CORRUPT; }
      media->image.pixels = malloc(media->image.size);
      if (!media->image.pixels) { return FEED_MEMORY; }
    }
    if (status == TB_IMAGE_DONE) { break; }
  }
  if (position != record->data.length) { return FEED_CORRUPT; }
  media->received += record->data.length;
  return FEED_OK;
}

static TbFeed parse(TbMedia *media, const TbResponse *response) {
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  bool first = response->index == 0;
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    if (!tb_codec_next(&cursor, &type, &body)) { return FEED_PROTOCOL; }
    if (type == TB_RECORD_MEDIA_INFO && first) {
      TbMediaInfoRecord record;
      if (!tb_codec_media_info(&body, &record) || !info(media, &record)) { return FEED_PROTOCOL; }
    } else if (type == TB_RECORD_MEDIA_DATA) {
      TbMediaDataRecord record;
      if (!tb_codec_media_data(&body, &record)) { return FEED_PROTOCOL; }
      const TbFeed result = feed(media, &record);
      if (result != FEED_OK) { return result; }
    } else {
      return FEED_PROTOCOL;
    }
    first = false;
  }
  return FEED_OK;
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbMedia *media = owner;
  if (sequence != media->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  if (response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK) {
    if (!response->final) {
      media->pending = 0;
      tb_requests_cancel(media->requests, sequence);
    }
    fail(media, tb_error_from_response(response), response->outcome);
    return;
  }
  const TbFeed result = parse(media, response);
  if (result != FEED_OK) {
    if (!response->final) {
      media->pending = 0;
      tb_requests_cancel(media->requests, sequence);
    }
    if (result == FEED_CORRUPT) {
      corrupt(media);
    } else if (result == FEED_MEMORY) {
      media->pending = 0;
      drop_pixels(media);
      media->phase = TB_MEDIA_NO_MEMORY;
      changed(media);
    } else {
      fail(media, TB_RESULT_PROTOCOL_ERROR, TB_OUTCOME_PROTOCOL);
    }
    return;
  }
  if (response->final) {
    media->pending = 0;
    if (media->phase == TB_MEDIA_TRANSFER) {
      if (media->image.pixels && media->image.filled == media->image.size && media->received == media->total) {
        media->phase = TB_MEDIA_READY;
      } else {
        fail(media, TB_RESULT_PROTOCOL_ERROR, TB_OUTCOME_PROTOCOL);
        return;
      }
    }
  }
  changed(media);
}

void tb_media_init(TbMedia *media, TbRequestLayer *requests, TbViewPorts ports, TbMediaConfig config) {
  memset(media, 0, sizeof(*media));
  media->requests = requests;
  media->ports = ports;
  media->config = config;
}

bool tb_media_bound(const TbMedia *media, const char *account, const char *chat, const char *message) {
  return media->account[0] && strcmp(media->account, account) == 0 && strcmp(media->chat, chat) == 0 && strcmp(media->message, message) == 0;
}

static TbMediaPhase initial_phase(uint8_t flags) {
  if (!(flags & TB_MEDIA_FLAG_IMAGE)) { return (flags & TB_MEDIA_FLAG_RESTRICTED) ? TB_MEDIA_RESTRICTED : TB_MEDIA_NONE; }
  if (flags & TB_MEDIA_FLAG_RESTRICTED) { return TB_MEDIA_RESTRICTED; }
  return (flags & TB_MEDIA_FLAG_SPOILER) ? TB_MEDIA_SPOILER : TB_MEDIA_IDLE;
}

void tb_media_bind(TbMedia *media, const char *account, const char *chat, const char *message, uint8_t flags, uint8_t count) {
  if (tb_media_bound(media, account, chat, message)) { return; }
  tb_media_close(media);
  strncpy(media->account, account, sizeof(media->account) - 1);
  strncpy(media->chat, chat, sizeof(media->chat) - 1);
  strncpy(media->message, message, sizeof(media->message) - 1);
  media->flags = flags;
  media->count = count ? count : 1;
  media->index = 0;
  media->phase = initial_phase(flags);
  changed(media);
}

void tb_media_load(TbMedia *media) {
  if (!media->account[0] || media->phase == TB_MEDIA_NONE || media->phase == TB_MEDIA_RESTRICTED) { return; }
  if (media->phase == TB_MEDIA_SPOILER && !media->reveal) { return; }
  if (media->pending || media->phase == TB_MEDIA_READY || media->phase == TB_MEDIA_WAITING) { return; }
  media->crc_retry = false;
  start(media);
}

void tb_media_show(TbMedia *media, uint8_t index) {
  if (!media->account[0] || index >= media->count || index == media->index) { return; }
  tb_media_release(media);
  media->index = index;
  media->reveal = false;
  media->phase = (media->flags & TB_MEDIA_FLAG_RESTRICTED) ? TB_MEDIA_RESTRICTED : TB_MEDIA_IDLE;
  tb_media_load(media);
  changed(media);
}

void tb_media_reveal(TbMedia *media) {
  if (media->phase != TB_MEDIA_SPOILER) { return; }
  media->reveal = true;
  media->revealed = true;
  media->phase = TB_MEDIA_IDLE;
  media->crc_retry = false;
  start(media);
}

void tb_media_retry(TbMedia *media) {
  if (media->phase != TB_MEDIA_ERROR && media->phase != TB_MEDIA_NO_MEMORY && media->phase != TB_MEDIA_UNAVAILABLE) { return; }
  media->crc_retry = false;
  start(media);
}

void tb_media_timer(TbMedia *media) {
  if (media->phase != TB_MEDIA_WAITING || !media->due) { return; }
  if ((int32_t)(now(media) - media->due) < 0) {
    media->ports.schedule(media->ports.context, media->due - now(media));
    return;
  }
  media->due = 0;
  media->phase = TB_MEDIA_LOADING;
  submit(media, 0);
  changed(media);
}

void tb_media_release(TbMedia *media) {
  cancel_pending(media);
  media->ports.cancel(media->ports.context);
  media->due = 0;
  drop_pixels(media);
  if (media->phase == TB_MEDIA_LOADING || media->phase == TB_MEDIA_WAITING || media->phase == TB_MEDIA_TRANSFER || media->phase == TB_MEDIA_READY) {
    media->phase = TB_MEDIA_IDLE;
  }
}

void tb_media_close(TbMedia *media) {
  tb_media_release(media);
  media->account[0] = '\0';
  media->chat[0] = '\0';
  media->message[0] = '\0';
  media->item[0] = '\0';
  media->index = 0;
  media->count = 0;
  media->flags = 0;
  media->reveal = false;
  media->revealed = false;
  media->phase = TB_MEDIA_NONE;
  media->error = TB_ERROR_NONE;
}

uint8_t tb_media_percent(const TbMedia *media) {
  if (!media->total) { return 0; }
  const uint32_t percent = media->received * 100u / media->total;
  return (uint8_t)(percent > 100 ? 100 : percent);
}

bool tb_media_has_image(const TbMedia *media) { return media->image.pixels != NULL && media->image.header_ready; }

bool tb_media_busy(const TbMedia *media) {
  return media->phase == TB_MEDIA_LOADING || media->phase == TB_MEDIA_WAITING || media->phase == TB_MEDIA_TRANSFER;
}
