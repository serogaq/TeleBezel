#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "request_layer.h"
#include "view_ports.h"

typedef struct {
  TbRequestLayer *requests;
  TbViewPorts ports;
  uint16_t limit;
  uint32_t timeout;
  char account[TB_ACCOUNT_ID_SIZE];
  char chat[TB_TELEGRAM_ID_SIZE];
  char message[TB_TELEGRAM_ID_SIZE];
  char *text;
  uint16_t length;
  bool loading;
  bool truncated;
  int32_t error;
  uint32_t pending;
} TbMessageText;

void tb_message_text_init(TbMessageText *reader, TbRequestLayer *requests, TbViewPorts ports, uint16_t limit, uint32_t timeout);
bool tb_message_text_open(TbMessageText *reader, const char *account, const char *chat, const char *message);
void tb_message_text_retry(TbMessageText *reader);
void tb_message_text_close(TbMessageText *reader);
