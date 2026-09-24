#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "request_layer.h"

#define TB_MAX_ACCOUNTS 8
#define TB_ACCOUNT_NAME_SIZE 49
#define TB_HOST_SIZE 65

typedef enum { TB_SESSION_IDLE, TB_SESSION_HELLO, TB_SESSION_BOOTSTRAP } TbSessionPhase;

typedef struct {
  char id[TB_ACCOUNT_ID_SIZE];
  char name[TB_ACCOUNT_NAME_SIZE];
  uint8_t state;
  uint8_t flags;
} TbAccount;

typedef struct {
  void (*changed)(void *context);
  void *context;
  void (*extra)(void *context, uint8_t type, const uint8_t *record, uint16_t length);
} TbSessionPorts;

typedef struct {
  TbRequestLayer *requests;
  TbSessionPorts ports;
  TbSessionPhase phase;
  uint32_t pending;
  int32_t error;
  uint32_t retry_after;
  bool loaded;
  uint32_t generation;
  TbAccount accounts[TB_MAX_ACCOUNTS];
  uint8_t count;
  char default_account[TB_ACCOUNT_ID_SIZE];
  uint8_t chat_list;
  bool show_archive;
  uint8_t unread_mode;
  char host[TB_HOST_SIZE];
  const char *failure;
} TbSession;

void tb_session_init(TbSession *session, TbRequestLayer *requests, TbSessionPorts ports);
void tb_session_start(TbSession *session, bool transport_ready);
void tb_session_retry(TbSession *session);
void tb_session_ready(TbSession *session, uint32_t sequence);
void tb_session_refresh(TbSession *session);
void tb_session_stop(TbSession *session);
bool tb_session_busy(const TbSession *session);
int tb_session_find(const TbSession *session, const char *account);
int tb_session_initial_account(const TbSession *session);
