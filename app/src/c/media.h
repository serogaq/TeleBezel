#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "image.h"
#include "request_layer.h"
#include "view_ports.h"

typedef enum {
  TB_MEDIA_NONE,
  TB_MEDIA_IDLE,
  TB_MEDIA_LOADING,
  TB_MEDIA_WAITING,
  TB_MEDIA_TRANSFER,
  TB_MEDIA_READY,
  TB_MEDIA_SPOILER,
  TB_MEDIA_RESTRICTED,
  TB_MEDIA_UNSUPPORTED,
  TB_MEDIA_UNAVAILABLE,
  TB_MEDIA_NO_MEMORY,
  TB_MEDIA_ERROR
} TbMediaPhase;

typedef struct {
  uint16_t width;
  uint16_t height;
  uint8_t shape;
  uint8_t formats;
  uint16_t max_budget;
  uint16_t reserve;
  uint16_t min_budget;
  uint32_t timeout;
  uint32_t wait_limit;
  size_t (*heap_free)(void);
} TbMediaConfig;

typedef struct {
  TbRequestLayer *requests;
  TbViewPorts ports;
  TbMediaConfig config;
  char account[TB_ACCOUNT_ID_SIZE];
  char chat[TB_TELEGRAM_ID_SIZE];
  char message[TB_TELEGRAM_ID_SIZE];
  char item[TB_TELEGRAM_ID_SIZE];
  uint8_t index;
  uint8_t count;
  uint8_t flags;
  uint8_t server_state;
  bool reveal;
  bool revealed;
  TbMediaPhase phase;
  int32_t error;
  uint32_t pending;
  uint32_t tag;
  uint32_t total;
  uint32_t received;
  uint16_t budget;
  uint8_t resumes;
  bool crc_retry;
  uint32_t started;
  uint32_t due;
  TbImageDecoder image;
  uint32_t revision;
} TbMedia;

void tb_media_init(TbMedia *media, TbRequestLayer *requests, TbViewPorts ports, TbMediaConfig config);
void tb_media_bind(TbMedia *media, const char *account, const char *chat, const char *message, uint8_t flags, uint8_t count);
bool tb_media_bound(const TbMedia *media, const char *account, const char *chat, const char *message);
void tb_media_load(TbMedia *media);
void tb_media_show(TbMedia *media, uint8_t index);
void tb_media_reveal(TbMedia *media);
void tb_media_retry(TbMedia *media);
void tb_media_timer(TbMedia *media);
void tb_media_release(TbMedia *media);
void tb_media_close(TbMedia *media);
uint8_t tb_media_percent(const TbMedia *media);
bool tb_media_has_image(const TbMedia *media);
bool tb_media_busy(const TbMedia *media);
