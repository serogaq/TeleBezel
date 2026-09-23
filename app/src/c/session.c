#include "session.h"
#include <string.h>
#include "codec.h"
#include "errors.h"
#include "generated/protocol.h"
#include "text.h"

#define TB_HELLO_TIMEOUT 1500
#define TB_HELLO_ATTEMPTS 3
#define TB_BOOTSTRAP_TIMEOUT 25000
#define TB_BOOTSTRAP_ATTEMPTS 2

static void changed(TbSession *session) { session->ports.changed(session->ports.context); }

static void settle(TbSession *session, int32_t error) {
  session->phase = TB_SESSION_IDLE;
  session->pending = 0;
  session->error = error;
  changed(session);
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response);

static void submit(TbSession *session, uint8_t kind, uint32_t timeout, uint8_t attempts, bool resend) {
  TbRequestArgs args;
  memset(&args, 0, sizeof(args));
  args.kind = kind;
  session->pending = tb_requests_submit(session->requests, &args, timeout, attempts, resend, completed, session);
  if (!session->pending && session->phase != TB_SESSION_IDLE) { settle(session, tb_error_submit_failed(session->requests)); }
}

static void bootstrap(TbSession *session) {
  if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
  session->pending = 0;
  session->phase = TB_SESSION_BOOTSTRAP;
  session->error = TB_ERROR_NONE;
  changed(session);
  submit(session, TB_REQUEST_BOOTSTRAP, TB_BOOTSTRAP_TIMEOUT, TB_BOOTSTRAP_ATTEMPTS, false);
}

static bool parse(TbSession *session, const TbResponse *response) {
  if (response->index == 0) {
    session->count = 0;
    session->default_account[0] = '\0';
    session->host[0] = '\0';
    session->chat_list = TB_LIST_MAIN;
  }
  TbCursor cursor;
  tb_cursor_init(&cursor, response->payload, response->length);
  while (cursor.offset < cursor.length) {
    uint8_t type = 0;
    TbCursor body;
    if (!tb_codec_next(&cursor, &type, &body)) { return false; }
    if (type == TB_RECORD_PREFS) {
      TbPrefsRecord prefs;
      if (!tb_codec_prefs(&body, &prefs)) { return false; }
      if (!tb_copy_uuid(session->default_account, sizeof(session->default_account), prefs.default_account)) {
        session->default_account[0] = '\0';
      }
      session->chat_list = prefs.chat_list == TB_LIST_ARCHIVE ? TB_LIST_ARCHIVE : TB_LIST_MAIN;
      tb_copy_span(session->host, sizeof(session->host), prefs.host);
    } else if (type == TB_RECORD_ACCOUNT) {
      TbAccountRecord record;
      if (!tb_codec_account(&body, &record)) { return false; }
      if (session->count >= TB_MAX_ACCOUNTS) { continue; }
      TbAccount *account = &session->accounts[session->count];
      if (!tb_copy_uuid(account->id, sizeof(account->id), record.id)) { return false; }
      tb_copy_span(account->name, sizeof(account->name), record.name);
      account->state = record.state;
      account->flags = record.flags;
      ++session->count;
    } else {
      return false;
    }
  }
  return true;
}

static void completed(void *owner, uint32_t sequence, const TbResponse *response) {
  TbSession *session = owner;
  if (sequence != session->pending || response->outcome == TB_OUTCOME_CANCELLED) { return; }
  if (session->phase == TB_SESSION_HELLO) {
    session->pending = 0;
    if (response->outcome == TB_OUTCOME_RESPONSE) { bootstrap(session); }
    else { settle(session, tb_error_from_response(response)); }
    return;
  }
  if (response->outcome != TB_OUTCOME_RESPONSE || response->result != TB_RESULT_OK) {
    if (!response->final) { tb_requests_cancel(session->requests, sequence); }
    session->retry_after = response->retry_after;
    settle(session, tb_error_from_response(response));
    return;
  }
  if (!parse(session, response)) {
    if (!response->final) {
      session->pending = 0;
      tb_requests_cancel(session->requests, sequence);
    }
    session->count = 0;
    settle(session, TB_RESULT_PROTOCOL_ERROR);
    return;
  }
  if (response->final) {
    session->loaded = true;
    ++session->generation;
    settle(session, TB_ERROR_NONE);
  }
}

void tb_session_init(TbSession *session, TbRequestLayer *requests, TbSessionPorts ports) {
  memset(session, 0, sizeof(*session));
  session->requests = requests;
  session->ports = ports;
  session->phase = TB_SESSION_IDLE;
}

void tb_session_start(TbSession *session, bool transport_ready) {
  if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
  session->pending = 0;
  session->loaded = false;
  session->count = 0;
  if (!transport_ready) {
    settle(session, TB_ERROR_NO_RESPONSE);
    return;
  }
  session->phase = TB_SESSION_HELLO;
  session->error = TB_ERROR_NONE;
  changed(session);
  submit(session, TB_REQUEST_HELLO, TB_HELLO_TIMEOUT, TB_HELLO_ATTEMPTS, true);
}

void tb_session_retry(TbSession *session) {
  if (session->phase != TB_SESSION_IDLE) { return; }
  tb_session_start(session, true);
}

void tb_session_ready(TbSession *session, uint32_t sequence) {
  if (session->phase == TB_SESSION_HELLO && sequence == session->pending) {
    tb_requests_response(session->requests, sequence, TB_RESULT_OK);
  } else if (session->phase == TB_SESSION_IDLE && !session->loaded) {
    bootstrap(session);
  }
}

void tb_session_refresh(TbSession *session) {
  if (session->phase == TB_SESSION_HELLO) { return; }
  session->loaded = false;
  session->count = 0;
  bootstrap(session);
}

void tb_session_stop(TbSession *session) {
  if (session->pending) { tb_requests_cancel(session->requests, session->pending); }
  session->pending = 0;
  session->phase = TB_SESSION_IDLE;
}

bool tb_session_busy(const TbSession *session) { return session->phase != TB_SESSION_IDLE; }

int tb_session_find(const TbSession *session, const char *account) {
  for (int index = 0; index < session->count; ++index) {
    if (strcmp(session->accounts[index].id, account) == 0) { return index; }
  }
  return -1;
}

int tb_session_initial_account(const TbSession *session) {
  if (session->default_account[0]) {
    const int index = tb_session_find(session, session->default_account);
    if (index >= 0 && session->accounts[index].state == TB_ACCOUNT_STATE_READY) { return index; }
  }
  if (session->count == 1 && session->accounts[0].state == TB_ACCOUNT_STATE_READY) { return 0; }
  return -1;
}
