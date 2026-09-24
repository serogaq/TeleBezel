#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TB_NOTIFY_CAPACITY 3
#define TB_NOTIFY_INFO_TTL 5000
#define TB_NOTIFY_TITLE_SIZE 33
#define TB_NOTIFY_BODY_SIZE 97

typedef enum { TB_NOTIFY_INFO, TB_NOTIFY_WARNING, TB_NOTIFY_ERROR } TbNotifyLevel;
typedef enum { TB_NOTIFY_NO_ACTION, TB_NOTIFY_REFRESH, TB_NOTIFY_OPEN_CHAT, TB_NOTIFY_CHECK, TB_NOTIFY_WAIT } TbNotifyAction;
typedef enum { TB_NOTIFY_CONNECTION, TB_NOTIFY_CHATS, TB_NOTIFY_SEND } TbNotifySource;

typedef struct {
  TbNotifySource source;
  TbNotifyLevel level;
  TbNotifyAction action;
  uint32_t serial;
  uint32_t expires;
  char title[TB_NOTIFY_TITLE_SIZE];
  char body[TB_NOTIFY_BODY_SIZE];
} TbNotification;

typedef struct {
  void (*changed)(void *context);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  uint32_t (*now)(void *context);
  void *context;
} TbNotifyPorts;

typedef struct {
  TbNotifyPorts ports;
  TbNotification items[TB_NOTIFY_CAPACITY];
  uint8_t count;
  uint32_t serial;
  bool focused;
} TbNotify;

void tb_notify_init(TbNotify *notify, TbNotifyPorts ports);
bool tb_notify_post(TbNotify *notify, TbNotifySource source, TbNotifyLevel level, TbNotifyAction action, const char *title, const char *body);
void tb_notify_clear(TbNotify *notify, TbNotifySource source);
void tb_notify_dismiss(TbNotify *notify);
const TbNotification *tb_notify_top(const TbNotify *notify);
void tb_notify_focus(TbNotify *notify, bool focused);
void tb_notify_timer(TbNotify *notify);
void tb_notify_reset(TbNotify *notify);
