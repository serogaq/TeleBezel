#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "request_layer.h"

#define TB_CONNECTING_INTERVAL 2000
#define TB_CONNECTING_LIMIT 14000
#define TB_UPDATING_INTERVAL 2500
#define TB_UPDATING_STEP 250
#define TB_UPDATING_LIMIT 20000

typedef enum { TB_CONNECTION_ERROR_NONE, TB_CONNECTION_ERROR_CANNOT_CONNECT, TB_CONNECTION_ERROR_LONG_UPDATE } TbConnectionError;

typedef struct {
  void (*changed)(void *context);
  void (*reload)(void *context);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  uint32_t (*now)(void *context);
  void *context;
} TbConnectionPorts;

typedef struct {
  TbRequestLayer *requests;
  TbConnectionPorts ports;
  uint32_t timeout;
  char account[TB_ACCOUNT_ID_SIZE];
  uint8_t state;
  bool known;
  bool proxy;
  bool fresh;
  bool active;
  bool polling;
  TbConnectionError error;
  uint32_t pending;
  uint32_t window;
  uint32_t delay;
  uint8_t polls;
} TbConnection;

void tb_connection_init(TbConnection *connection, TbRequestLayer *requests, TbConnectionPorts ports, uint32_t timeout);
void tb_connection_open(TbConnection *connection, const char *account);
void tb_connection_summary(TbConnection *connection, uint8_t state, bool proxy);
void tb_connection_refresh(TbConnection *connection);
void tb_connection_set_active(TbConnection *connection, bool active);
void tb_connection_timer(TbConnection *connection);
void tb_connection_close(TbConnection *connection);
