#include "connection.h"
#include <string.h>
#include "codec.h"
#include "generated/protocol.h"

static void changed(TbConnection *connection) { connection->ports.changed(connection->ports.context); }

static uint32_t limit_of(uint8_t state) { return state == TB_CONNECTION_UPDATING ? TB_UPDATING_LIMIT : TB_CONNECTING_LIMIT; }

static uint32_t delay_of(uint8_t state, uint8_t polls) {
  if (state == TB_CONNECTION_UPDATING) { return TB_UPDATING_INTERVAL + (uint32_t)polls * TB_UPDATING_STEP; }
  return TB_CONNECTING_INTERVAL;
}

static void arm(TbConnection *connection) {
  connection->ports.cancel(connection->ports.context);
  if (!connection->polling || !connection->active || connection->pending) { return; }
  if (connection->window + connection->delay > limit_of(connection->state)) {
    connection->polling = false;
    connection->error = connection->state == TB_CONNECTION_UPDATING ? TB_CONNECTION_ERROR_LONG_UPDATE : TB_CONNECTION_ERROR_CANNOT_CONNECT;
    changed(connection);
    return;
  }
  connection->ports.schedule(connection->ports.context, connection->delay);
}

static void enter(TbConnection *connection, uint8_t state) {
  connection->state = state;
  connection->known = true;
  connection->fresh = false;
  connection->error = TB_CONNECTION_ERROR_NONE;
  connection->window = 0;
  connection->polls = 0;
  connection->polling = state != TB_CONNECTION_READY;
  connection->delay = delay_of(state, 0);
  arm(connection);
}

static void proceed(TbConnection *connection) {
  connection->delay = delay_of(connection->state, connection->polls);
  arm(connection);
}

static void apply(TbConnection *connection, uint8_t state, bool proxy, bool polled) {
  if (state > TB_CONNECTION_READY) { state = TB_CONNECTION_CONNECTING; }
  connection->proxy = proxy;
  if (connection->known && !connection->fresh && state == connection->state) {
    if (polled && connection->polling) { proceed(connection); }
    changed(connection);
    return;
  }
  const bool arrived = polled && connection->known && connection->state != TB_CONNECTION_READY && state == TB_CONNECTION_READY;
  enter(connection, state);
  changed(connection);
  if (arrived) { connection->ports.reload(connection->ports.context); }
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbConnection *connection = owner;
  if (sequence != connection->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  connection->pending = 0;
  if (response->outcome == TB_OUTCOME_RESPONSE && response->result == TB_RESULT_OK) {
    TbCursor cursor;
    tb_cursor_init(&cursor, response->payload, response->length);
    uint8_t type = 0;
    TbCursor body;
    TbStatusRecord status;
    if (tb_codec_next(&cursor, &type, &body) && type == TB_RECORD_STATUS && tb_codec_status(&body, &status)) {
      if (cursor.offset < cursor.length && connection->ports.records) {
        connection->ports.records(connection->ports.context, response->payload + cursor.offset, (uint16_t)(cursor.length - cursor.offset));
      }
      apply(connection, status.connection, status.proxy != 0, true);
      return;
    }
  }
  if (!response->final) { tb_requests_cancel(connection->requests, sequence); }
  if (connection->polling) { proceed(connection); }
}

void tb_connection_init(TbConnection *connection, TbRequestLayer *requests, TbConnectionPorts ports, uint32_t timeout) {
  memset(connection, 0, sizeof(*connection));
  connection->requests = requests;
  connection->ports = ports;
  connection->timeout = timeout;
  connection->state = TB_CONNECTION_CONNECTING;
}

void tb_connection_open(TbConnection *connection, const char *account) {
  const bool active = connection->active;
  tb_connection_close(connection);
  strncpy(connection->account, account, sizeof(connection->account) - 1);
  connection->account[sizeof(connection->account) - 1] = '\0';
  connection->active = active;
}

void tb_connection_summary(TbConnection *connection, uint8_t state, bool proxy) {
  if (!connection->account[0]) { return; }
  apply(connection, state, proxy, false);
}

void tb_connection_refresh(TbConnection *connection) {
  if (!connection->account[0]) { return; }
  if (connection->pending) {
    const uint32_t pending = connection->pending;
    connection->pending = 0;
    tb_requests_cancel(connection->requests, pending);
  }
  connection->ports.cancel(connection->ports.context);
  connection->fresh = true;
  connection->polling = false;
  connection->error = TB_CONNECTION_ERROR_NONE;
  changed(connection);
}

void tb_connection_set_active(TbConnection *connection, bool active) {
  connection->active = active;
  if (!active) {
    connection->ports.cancel(connection->ports.context);
    return;
  }
  arm(connection);
}

void tb_connection_timer(TbConnection *connection) {
  if (!connection->polling || !connection->active || connection->pending || !connection->account[0]) { return; }
  connection->window += connection->delay;
  ++connection->polls;
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = TB_REQUEST_EVENTS;
  strncpy(args.account, connection->account, sizeof(args.account) - 1);
  connection->pending = tb_requests_submit(connection->requests, &args, connection->timeout, 1, false, completed, connection);
  if (!connection->pending) { proceed(connection); }
}

void tb_connection_close(TbConnection *connection) {
  if (connection->pending) {
    const uint32_t pending = connection->pending;
    connection->pending = 0;
    tb_requests_cancel(connection->requests, pending);
  }
  connection->ports.cancel(connection->ports.context);
  connection->account[0] = '\0';
  connection->state = TB_CONNECTION_CONNECTING;
  connection->known = false;
  connection->proxy = false;
  connection->fresh = false;
  connection->active = false;
  connection->polling = false;
  connection->error = TB_CONNECTION_ERROR_NONE;
  connection->window = 0;
  connection->delay = 0;
  connection->polls = 0;
}
