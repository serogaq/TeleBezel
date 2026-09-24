#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "request_layer.h"
#include "send_tracker.h"
#include "text.h"
#include "view_ports.h"

#define TB_COMPOSE_NAME_SIZE 33
#define TB_COMPOSE_QUOTE_SIZE 65
#define TB_COMPOSE_DRAFT_LIMIT 1024
#define TB_COMPOSE_UNITS_LIMIT 4096

typedef enum { TB_TEMPLATES_NONE, TB_TEMPLATES_LOADING, TB_TEMPLATES_READY, TB_TEMPLATES_FAILED } TbTemplatesState;
typedef enum { TB_DRAFT_NONE, TB_DRAFT_LOADING, TB_DRAFT_READY, TB_DRAFT_FAILED } TbDraftState;

typedef struct {
  TbSendTarget send;
  char account_name[TB_COMPOSE_NAME_SIZE];
  char chat_title[TB_COMPOSE_NAME_SIZE];
  char reply_sender[TB_COMPOSE_NAME_SIZE];
  char reply_text[TB_COMPOSE_QUOTE_SIZE];
} TbComposeTarget;

typedef struct {
  uint8_t capacity;
  uint8_t preview_limit;
  uint16_t text_limit;
  uint32_t timeout;
} TbComposeConfig;

typedef struct {
  TbRequestLayer *requests;
  TbViewPorts ports;
  TbComposeConfig config;
  TbComposeTarget target;
  bool open;
  TbTemplatesState templates;
  bool templates_stale;
  int32_t templates_error;
  uint32_t templates_rev;
  uint8_t template_count;
  uint8_t template_total;
  char *previews;
  uint32_t templates_pending;
  TbDraftState draft;
  int32_t draft_error;
  uint32_t draft_id;
  uint16_t draft_bytes;
  uint16_t draft_units;
  bool too_long;
  char *text;
  uint16_t text_length;
  uint8_t *dictated;
  uint16_t dictated_length;
  uint32_t draft_pending;
  uint32_t revision;
} TbCompose;

void tb_compose_init(TbCompose *compose, TbRequestLayer *requests, TbViewPorts ports, TbComposeConfig config);
bool tb_compose_open(TbCompose *compose, const TbComposeTarget *target);
void tb_compose_load_templates(TbCompose *compose, bool refresh);
const char *tb_compose_template(const TbCompose *compose, uint8_t index);
bool tb_compose_pick(TbCompose *compose, uint8_t index);
bool tb_compose_dictated(TbCompose *compose, const char *text, size_t length);
bool tb_compose_sendable(const TbCompose *compose);
void tb_compose_drop_draft(TbCompose *compose);
void tb_compose_close(TbCompose *compose, bool keep_draft);
uint16_t tb_utf8_units(const char *text, size_t length);
