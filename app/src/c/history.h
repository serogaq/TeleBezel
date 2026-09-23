#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "request_layer.h"
#include "text.h"
#include "view_ports.h"

typedef struct {
  char id[TB_TELEGRAM_ID_SIZE];
  int64_t key;
  uint32_t date;
  uint8_t flags;
  uint8_t kind;
  uint8_t action;
  uint16_t duration;
  char *sender;
  char *extra;
  char *text;
  int16_t height;
} TbMessage;

typedef enum { TB_TOP_MORE, TB_TOP_LOADING, TB_TOP_FAILED, TB_TOP_WAITING, TB_TOP_START, TB_TOP_UNKNOWN } TbTop;
typedef enum { TB_HISTORY_NONE, TB_HISTORY_FIRST, TB_HISTORY_REFRESH, TB_HISTORY_OLDER } TbHistoryOp;

typedef struct {
  uint16_t capacity;
  uint8_t page_limit;
  uint16_t text_limit;
  size_t text_budget;
  uint32_t timeout;
  uint32_t refresh_interval;
  uint32_t followup_delays[2];
} TbHistoryConfig;

typedef struct {
  TbRequestLayer *requests;
  TbViewPorts ports;
  TbHistoryConfig config;
  char account[TB_ACCOUNT_ID_SIZE];
  char chat[TB_TELEGRAM_ID_SIZE];
  uint8_t chat_type;
  TbMessage *items;
  uint16_t count;
  TbMessage *staging;
  uint16_t staging_count;
  bool staging_invalid;
  TbBudget budget;
  TbTop top;
  uint8_t top_op;
  TbHistoryOp op;
  uint8_t op_page;
  uint32_t pending;
  bool loaded;
  bool truncated;
  bool connection_not_ready;
  bool active;
  bool refreshing_silently;
  bool resynced;
  int32_t error;
  int32_t top_error;
  int32_t refresh_error;
  uint32_t retry_after;
  uint8_t followups;
  bool followup_chain;
  uint8_t rechecks;
  uint32_t followup_due;
  uint32_t recheck_due;
  uint32_t periodic_due;
  uint32_t refreshed_at;
  uint32_t revision;
} TbHistory;

bool tb_history_init(TbHistory *history, TbRequestLayer *requests, TbViewPorts ports, TbHistoryConfig config);
void tb_history_deinit(TbHistory *history);
void tb_history_open(TbHistory *history, const char *account, const char *chat, uint8_t chat_type);
bool tb_history_is(const TbHistory *history, const char *account, const char *chat);
void tb_history_load_older(TbHistory *history);
void tb_history_refresh(TbHistory *history);
void tb_history_retry(TbHistory *history);
void tb_history_set_active(TbHistory *history, bool active);
void tb_history_timer(TbHistory *history);
void tb_history_close(TbHistory *history);
int tb_history_find(const TbHistory *history, const char *id);
