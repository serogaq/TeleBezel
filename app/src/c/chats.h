#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "request_layer.h"
#include "text.h"
#include "view_ports.h"

typedef struct {
  char id[TB_TELEGRAM_ID_SIZE];
  char *title;
  char *sender;
  char *extra;
  char *preview;
  uint8_t type;
  uint8_t flags;
  uint8_t preview_kind;
  uint8_t preview_action;
  uint16_t preview_duration;
  uint16_t unread;
  uint32_t last_date;
} TbChat;

typedef enum { TB_CHATS_IDLE, TB_CHATS_FIRST, TB_CHATS_MORE, TB_CHATS_REFRESH } TbChatsLoad;
typedef enum { TB_TAIL_MORE, TB_TAIL_UNKNOWN, TB_TAIL_END, TB_TAIL_FULL, TB_TAIL_FAILED } TbChatsTail;

typedef struct {
  uint16_t capacity;
  uint8_t page_limit;
  uint16_t text_limit;
  size_t text_budget;
  uint32_t timeout;
  uint32_t refresh_interval;
  uint32_t stale_after;
} TbChatsConfig;

typedef struct {
  TbRequestLayer *requests;
  TbViewPorts ports;
  TbChatsConfig config;
  char account[TB_ACCOUNT_ID_SIZE];
  uint8_t list;
  TbChat *items;
  uint16_t count;
  TbBudget budget;
  TbChatsLoad load;
  uint32_t pending;
  TbChatsTail tail;
  int32_t error;
  uint32_t retry_after;
  bool connection_not_ready;
  uint8_t connection;
  bool proxy;
  uint32_t unread_chats;
  uint32_t unread_messages;
  uint32_t summary_revision;
  bool loaded;
  bool active;
  bool resynced;
  uint32_t loaded_at;
  uint32_t refresh_due;
} TbChats;

bool tb_chats_init(TbChats *chats, TbRequestLayer *requests, TbViewPorts ports, TbChatsConfig config);
void tb_chats_deinit(TbChats *chats);
void tb_chats_open(TbChats *chats, const char *account, uint8_t list);
void tb_chats_set_list(TbChats *chats, uint8_t list);
void tb_chats_load_more(TbChats *chats);
void tb_chats_refresh(TbChats *chats);
void tb_chats_retry(TbChats *chats);
void tb_chats_set_active(TbChats *chats, bool active);
void tb_chats_timer(TbChats *chats);
void tb_chats_close(TbChats *chats);
int tb_chats_find(const TbChats *chats, const char *id);
