#include "connection.h"
#include "fake.h"

#define ACCOUNT "00112233-4455-4677-8899-aabbccddeeff"

static int s_reloads;
static void fake_reload(void *context) { (void)context; ++s_reloads; }

static void status_record(Buf *buf, uint8_t state, uint8_t proxy) {
  begin(buf, TB_RECORD_STATUS);
  put8(buf, state); put8(buf, proxy);
  end(buf);
}

static int s_polls;

static void poll(Fake *fake, TbRequestLayer *layer, TbConnection *connection, uint8_t state, uint8_t proxy) {
  assert(fake->view_timer);
  fake->now += fake->view_delay;
  fake->view_timer = false;
  const int sends = fake->sends;
  tb_connection_timer(connection);
  assert(fake->sends == sends + 1 && fake_last(fake)->kind == TB_REQUEST_EVENTS && strcmp(fake_last(fake)->account, ACCOUNT) == 0);
  ++s_polls;
  Buf buf = {0};
  status_record(&buf, state, proxy);
  deliver(layer, fake_last_sequence(fake), TB_RESULT_OK, 0, &buf, 0, 1);
}

int main(void) {
  Fake fake = {0};
  fake.next = TB_SEND_OK;
  fake.now = 1000;
  TbRequestLayer layer;
  tb_requests_init(&layer, fake_request_ports(&fake));
  TbConnection connection;
  tb_connection_init(&connection, &layer,
                     (TbConnectionPorts){fake_changed, fake_reload, fake_view_schedule, fake_view_cancel, fake_now, &fake, NULL}, 10000);
  tb_connection_open(&connection, ACCOUNT);
  tb_connection_set_active(&connection, true);
  assert(!connection.known && !fake.view_timer);

  tb_connection_summary(&connection, TB_CONNECTION_READY, false);
  assert(connection.known && connection.state == TB_CONNECTION_READY && !connection.polling && !fake.view_timer);

  tb_connection_refresh(&connection);
  tb_connection_summary(&connection, TB_CONNECTION_CONNECTING, true);
  assert(connection.polling && fake.view_timer && fake.view_delay == 2000);
  for (int index = 0; index < 7; ++index) { poll(&fake, &layer, &connection, TB_CONNECTION_CONNECTING, 1); }
  assert(!connection.polling && connection.error == TB_CONNECTION_ERROR_CANNOT_CONNECT && connection.proxy && !fake.view_timer);
  assert(s_polls == 7);

  tb_connection_summary(&connection, TB_CONNECTION_CONNECTING, true);
  assert(connection.error == TB_CONNECTION_ERROR_CANNOT_CONNECT && !fake.view_timer);

  tb_connection_refresh(&connection);
  assert(connection.error == TB_CONNECTION_ERROR_NONE && connection.fresh);
  tb_connection_summary(&connection, TB_CONNECTION_CONNECTING, false);
  assert(connection.polling && connection.window == 0);
  poll(&fake, &layer, &connection, TB_CONNECTION_CONNECTING, 0);
  poll(&fake, &layer, &connection, TB_CONNECTION_UPDATING, 0);
  assert(connection.state == TB_CONNECTION_UPDATING && connection.window == 0 && fake.view_delay == 2500);
  s_polls = 0;
  const uint32_t expected[] = {2500, 2750, 3000, 3250, 3500, 3750};
  for (int index = 0; index < 6; ++index) {
    assert(fake.view_timer && fake.view_delay == expected[index]);
    poll(&fake, &layer, &connection, TB_CONNECTION_UPDATING, 0);
  }
  assert(s_polls == 6 && connection.error == TB_CONNECTION_ERROR_LONG_UPDATE && !connection.polling && !fake.view_timer);
  assert(s_reloads == 0);

  tb_connection_refresh(&connection);
  tb_connection_summary(&connection, TB_CONNECTION_UPDATING, false);
  poll(&fake, &layer, &connection, TB_CONNECTION_UPDATING, 0);
  tb_connection_set_active(&connection, false);
  assert(!fake.view_timer);
  const int sends = fake.sends;
  tb_connection_timer(&connection);
  assert(fake.sends == sends);
  fake.now += 60000;
  tb_connection_set_active(&connection, true);
  assert(fake.view_timer && fake.view_delay == 2750 && connection.error == TB_CONNECTION_ERROR_NONE);
  poll(&fake, &layer, &connection, TB_CONNECTION_READY, 0);
  assert(connection.state == TB_CONNECTION_READY && !connection.polling && !fake.view_timer && s_reloads == 1);
  tb_connection_summary(&connection, TB_CONNECTION_READY, false);
  assert(s_reloads == 1);

  tb_connection_refresh(&connection);
  tb_connection_summary(&connection, TB_CONNECTION_CONNECTING, false);
  assert(fake.view_timer);
  fake.now += fake.view_delay;
  tb_connection_timer(&connection);
  deliver(&layer, fake_last_sequence(&fake), TB_RESULT_BUSY, 0, NULL, 0, 1);
  assert(connection.polling && fake.view_timer && fake.view_delay == 2000);
  fake.now += fake.view_delay;
  tb_connection_timer(&connection);
  const uint32_t pending = fake_last_sequence(&fake);
  tb_connection_refresh(&connection);
  assert(!tb_requests_pending(&layer, pending) && !connection.polling);

  tb_connection_summary(&connection, TB_CONNECTION_CONNECTING, false);
  tb_connection_close(&connection);
  assert(!connection.account[0] && !fake.view_timer && !connection.known);
  tb_connection_summary(&connection, TB_CONNECTION_CONNECTING, false);
  assert(!connection.known);
  return 0;
}
