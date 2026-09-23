'use strict';
var accountInfo = require('./accounts');
var ACCOUNT_PATTERN = /^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/;
var CHAT_PATTERN = /^-?[1-9][0-9]{0,18}$/;
var MESSAGE_PATTERN = /^[1-9][0-9]{0,18}$/;
var RETRY_WINDOW_MS = 8000;
var RETRY_WAIT_LIMIT_S = 5;
var TEXT_CACHE_LIMIT = 300;
var FULL_TEXT_LIMIT = 16384;
var NEEDS_LOGIN = ['awaiting_phone_number', 'awaiting_code', 'awaiting_password', 'awaiting_email_address',
  'awaiting_email_code', 'awaiting_qr_confirmation', 'registration_required', 'premium_purchase_required', 'closed',
  'error', 'logging_out'];

function create(options) {
  var protocol = options.protocol;
  var codec = options.codec;
  var now = options.now || Date.now;
  var setTimer = options.setTimeout || setTimeout;
  var clearTimer = options.clearTimeout || clearTimeout;
  var inboxSize = 0;
  var generation = 0;
  var slots = {};
  var chatCursors = {};
  var historyCursors = {};
  var texts = {};
  var textOrder = [];

  function budget() {
    var size = inboxSize > 0 ? inboxSize : 2048;
    return Math.max(256, Math.min(size, 8192) - 96);
  }

  function respond(sequence, code, records, flags, retryAfter) {
    var chunks = code === protocol.result.ok ? codec.pack(records || [], budget()) : [[]];
    chunks.forEach(function(chunk, index) {
      var message = {
        RESPONSE_KIND: protocol.response.data,
        REQUEST_SEQ: sequence,
        RESULT_CODE: code,
        CHUNK_INDEX: index,
        CHUNK_TOTAL: chunks.length,
        PAGE_FLAGS: flags || 0
      };
      if (chunk.length > 0) { message.PAYLOAD = chunk; }
      if (retryAfter) { message.RETRY_AFTER = Math.min(retryAfter, 3600); }
      options.transport.send(message, sequence);
    });
  }

  function failureCode(result) {
    var code = result.code;
    var codes = protocol.result;
    if (code === 'config.invalid') { return codes.config_invalid; }
    if (code === 'auth.insufficient_scope') { return codes.wrong_token_type; }
    if (result.status === 401 || code === 'auth.unauthorized') { return codes.api_unauthorized; }
    if (code === 'authorization.invalid_state') { return codes.account_needs_login; }
    if (code === 'account.not_found' || code === 'account.gone') { return codes.account_gone; }
    if (code === 'chat.not_found') { return codes.chat_not_found; }
    if (code === 'rate_limit.exceeded') { return codes.rate_limited; }
    if (code === 'interest.limit_reached') { return codes.too_many_views; }
    if (code === 'message.cache_miss' || code === 'message.not_found') { return codes.message_unavailable; }
    if (result.action === 'resync') { return codes.cursor_lost; }
    if (code === 'network.unavailable') { return codes.backend_unavailable; }
    if (code === 'response.invalid') { return result.status >= 500 ? codes.backend_not_ready : codes.protocol_error; }
    if (result.action === 'retry') { return codes.busy; }
    if (result.status >= 500) { return codes.backend_unavailable; }
    return codes.protocol_error;
  }

  function begin(slotName, sequence) {
    if (slotName) {
      var previous = slots[slotName];
      if (previous) {
        previous.cancelled = true;
        if (previous.handle) { previous.handle.abort(); }
        if (previous.timer) { clearTimer(previous.timer); }
        options.transport.cancel(previous.sequence);
      }
    }
    var slot = {sequence: sequence, generation: generation, started: now(), handle: null, timer: null, cancelled: false};
    if (slotName) { slots[slotName] = slot; }
    return slot;
  }

  function live(slotName, slot) {
    return !slot.cancelled && slot.generation === generation && (!slotName || slots[slotName] === slot);
  }

  function finish(slotName, slot) { if (slotName && slots[slotName] === slot) { delete slots[slotName]; } }

  function call(slotName, slot, operation, done) {
    var retried = false;
    function attempt() {
      slot.timer = null;
      slot.handle = operation(function(result) {
        slot.handle = null;
        if (!live(slotName, slot)) { return; }
        if (!result.ok && result.action === 'retry' && !retried && result.code !== 'rate_limit.exceeded') {
          var wait = result.retryAfter === null || result.retryAfter === undefined ? 1 : result.retryAfter;
          if (wait <= RETRY_WAIT_LIMIT_S && now() - slot.started + wait * 1000 < RETRY_WINDOW_MS + RETRY_WAIT_LIMIT_S * 1000 &&
              now() - slot.started < RETRY_WINDOW_MS) {
            retried = true;
            slot.timer = setTimer(attempt, wait * 1000);
            return;
          }
        }
        done(result);
      });
    }
    attempt();
  }

  function settingsOrFail(sequence) {
    var value = options.settings.load(options.storage);
    var valid = options.settings.validate(value);
    if (!valid.ok) {
      respond(sequence, valid.missing ? protocol.result.config_missing : protocol.result.config_invalid);
      return null;
    }
    return value;
  }

  function fail(sequence, result) {
    respond(sequence, failureCode(result), null, 0, result.code === 'rate_limit.exceeded' || result.code === 'interest.limit_reached' ? result.retryAfter || 5 : 0);
  }

  function accountState(account) {
    var states = protocol.account_state;
    var runtime = account.runtime || {};
    if (account.lifecycle === 'removing' || account.lifecycle === 'removed') { return states.removing; }
    if (account.lifecycle === 'logout_pending') { return states.logging_out; }
    if (account.lifecycle !== 'active' || !runtime.available) { return states.connecting; }
    if (runtime.authorization_state === 'ready') { return states.ready; }
    if (NEEDS_LOGIN.indexOf(runtime.authorization_state) !== -1) { return states.needs_login; }
    return states.connecting;
  }

  function bootstrap(sequence) {
    var slot = begin('bootstrap', sequence);
    var value = settingsOrFail(sequence);
    if (!value) { finish('bootstrap', slot); return; }
    call('bootstrap', slot, function(callback) { return options.api.preferences(value, callback); }, function(preferences) {
      if (!preferences.ok) { finish('bootstrap', slot); fail(sequence, preferences); return; }
      call('bootstrap', slot, function(callback) { return options.api.accounts(value, callback); }, function(accounts) {
        finish('bootstrap', slot);
        if (!accounts.ok) { fail(sequence, accounts); return; }
        if (!Array.isArray(accounts.data)) { respond(sequence, protocol.result.protocol_error); return; }
        var defaultAccount = typeof preferences.data.default_account_id === 'string' ? preferences.data.default_account_id : '';
        var records = [codec.prefs({
          defaultAccount: defaultAccount,
          chatList: preferences.data.chat_list === 'archive' ? protocol.list.archive : protocol.list.main,
          host: value.address
        })];
        accounts.data.filter(function(account) { return account && ACCOUNT_PATTERN.test(account.id); }).slice(0, 8).forEach(function(account) {
          records.push(codec.account({
            id: account.id,
            name: accountInfo.name(account),
            state: accountState(account),
            flags: account.id === defaultAccount ? protocol.account_flag.default : 0
          }));
        });
        respond(sequence, protocol.result.ok, records, 0);
      });
    });
  }

  function describe(message, chatId) {
    var content = message && message.content ? message.content : {};
    var kind = Object.prototype.hasOwnProperty.call(protocol.kind, content.kind) ? protocol.kind[content.kind] : protocol.kind.unsupported;
    var sender = '';
    if (message && typeof message.author_signature === 'string' && message.author_signature) {
      sender = message.author_signature;
    } else if (message && message.sender && !(message.sender.type === 'chat' && message.sender.id === chatId)) {
      sender = message.sender.name || message.sender.fallback || '';
    }
    return {
      kind: kind,
      action: Object.prototype.hasOwnProperty.call(protocol.action, content.action) ? protocol.action[content.action] : protocol.action.none,
      duration: Number.isInteger(content.duration) ? content.duration : 0,
      text: typeof content.text === 'string' ? content.text : '',
      extra: typeof content.title === 'string' && content.title ? content.title : typeof content.emoji === 'string' ? content.emoji : '',
      sender: sender
    };
  }

  function chatRecord(chat, list, textLimit) {
    var flags = protocol.chat_flag;
    var value = 0;
    var notifications = chat.notifications || {};
    if (notifications.use_default_mute_for === false && notifications.mute_for > 0) { value |= flags.muted; }
    if (chat.positions && chat.positions[list] && chat.positions[list].is_pinned) { value |= flags.pinned; }
    if (chat.is_marked_unread) { value |= flags.marked_unread; }
    if (chat.unread_mention_count > 0) { value |= flags.mention; }
    var last = chat.last_message;
    var preview = last ? describe(last, chat.id) : {kind: protocol.kind.none, action: 0, duration: 0, text: '', extra: '', sender: ''};
    if (last && last.is_outgoing) { value |= flags.preview_outgoing; }
    return codec.chat({
      id: chat.id,
      title: typeof chat.title === 'string' ? chat.title : '',
      type: Object.prototype.hasOwnProperty.call(protocol.chat_type, chat.type) ? protocol.chat_type[chat.type] : protocol.chat_type.unknown,
      flags: value,
      unread: chat.unread_count,
      lastDate: last ? last.date : 0,
      previewKind: preview.kind,
      previewAction: preview.action,
      previewDuration: preview.duration,
      previewSender: last && last.is_outgoing ? '' : preview.sender,
      previewExtra: preview.extra,
      previewText: preview.text
    }, textLimit);
  }

  function pageFlags(data) {
    var flags = protocol.flag;
    var value = 0;
    if (data.has_more === true) { value |= flags.has_more; }
    if (data.has_more === null || data.has_more === undefined) { value |= flags.has_more_unknown; }
    if (data.partial === true) { value |= flags.partial; }
    if (data.local_exhausted === true) { value |= flags.local_exhausted; }
    if (data.refresh === 'queued' || data.refresh === 'pending') { value |= flags.refresh_pending; }
    if (typeof data.connection === 'string' && data.connection !== 'ready') { value |= flags.connection_not_ready; }
    return value;
  }

  function limits(payload, defaultPage, defaultText, maxText) {
    var page = Number.isInteger(payload.PAGE_LIMIT) ? payload.PAGE_LIMIT : defaultPage;
    var textLimit = Number.isInteger(payload.TEXT_LIMIT) ? payload.TEXT_LIMIT : defaultText;
    return {page: Math.max(1, Math.min(50, page)), text: Math.max(16, Math.min(maxText, textLimit))};
  }

  function chats(sequence, payload) {
    var ops = protocol.page_op;
    var account = payload.ACCOUNT_ID;
    var list = payload.LIST === protocol.list.archive ? 'archive' : 'main';
    var op = payload.PAGE_OP;
    if (!ACCOUNT_PATTERN.test(account || '')) { respond(sequence, protocol.result.protocol_error); return; }
    var bounds = limits(payload, 20, 80, Math.max(16, Math.min(512, budget() - 200)));
    var key = account + '|' + list;
    var state = chatCursors[key] || {};
    var cursor = null;
    if (op === ops.next) {
      if (!state.next) { respond(sequence, protocol.result.cursor_lost); return; }
      cursor = state.next;
    } else if (op === ops.retry) {
      cursor = state.retry || null;
    } else if (op !== ops.first && op !== ops.refresh) {
      respond(sequence, protocol.result.protocol_error);
      return;
    }
    var value = settingsOrFail(sequence);
    if (!value) { return; }
    var slot = begin('chats', sequence);
    call('chats', slot, function(callback) {
      return options.api.chats(value, account, {list: list, limit: bounds.page, cursor: cursor}, callback);
    }, function(result) {
      finish('chats', slot);
      if (!result.ok) {
        if (failureCode(result) === protocol.result.cursor_lost) { delete chatCursors[key]; }
        fail(sequence, result);
        return;
      }
      var data = result.data;
      chatCursors[key] = {next: data.next_cursor || null, retry: data.partial ? data.retry_cursor || cursor : null};
      var records = (Array.isArray(data.items) ? data.items : []).filter(function(chat) {
        return chat && CHAT_PATTERN.test(chat.id);
      }).map(function(chat) { return chatRecord(chat, list, bounds.text); });
      respond(sequence, protocol.result.ok, records, pageFlags(data));
    });
  }

  function remember(account, chat, message) {
    var key = account + '|' + chat + '|' + message.id;
    if (!Object.prototype.hasOwnProperty.call(texts, key)) { textOrder.push(key); }
    texts[key] = message;
    while (textOrder.length > TEXT_CACHE_LIMIT) { delete texts[textOrder.shift()]; }
  }

  function messageRecord(message, chat, textLimit) {
    var described = describe(message, chat);
    var flags = protocol.message_flag;
    return codec.message({
      id: message.id,
      date: message.date,
      flags: (message.is_outgoing ? flags.outgoing : 0) | (message.edit_date > 0 ? flags.edited : 0),
      kind: described.kind,
      action: described.action,
      duration: described.duration,
      sender: described.sender,
      extra: described.extra,
      text: described.text
    }, textLimit);
  }

  function history(sequence, payload) {
    var ops = protocol.page_op;
    var account = payload.ACCOUNT_ID;
    var chat = payload.ENTITY_ID;
    var op = payload.PAGE_OP;
    if (!ACCOUNT_PATTERN.test(account || '') || !CHAT_PATTERN.test(chat || '')) { respond(sequence, protocol.result.protocol_error); return; }
    var bounds = limits(payload, 20, 300, Math.max(16, Math.min(1024, budget() - 200)));
    var key = account + '|' + chat;
    if (op === ops.first) { historyCursors[key] = {older: false, next: null, retry: null, tail: null}; }
    var state = historyCursors[key];
    if (!state) {
      if (op !== ops.refresh) { respond(sequence, protocol.result.cursor_lost); return; }
      state = historyCursors[key] = {older: false, next: null, retry: null, tail: null};
    }
    var params = {view_id: options.leases.viewId(), limit: bounds.page};
    var used = null;
    if (op === ops.next) {
      if (!state.next) { respond(sequence, protocol.result.cursor_lost); return; }
      used = params.cursor = state.next;
    } else if (op === ops.retry) {
      if (!state.retry) { respond(sequence, protocol.result.cursor_lost); return; }
      used = params.retry_cursor = state.retry;
    } else if (op === ops.recheck) {
      if (!state.tail) { respond(sequence, protocol.result.cursor_lost); return; }
      used = params.cursor = state.tail;
    } else if (op !== ops.first && op !== ops.refresh) {
      respond(sequence, protocol.result.protocol_error);
      return;
    }
    var value = settingsOrFail(sequence);
    if (!value) { return; }
    options.leases.open(account, chat, release(value));
    var slot = begin('history', sequence);
    call('history', slot, function(callback) { return options.api.history(value, account, chat, params, callback); }, function(result) {
      finish('history', slot);
      if (!result.ok) {
        if (failureCode(result) === protocol.result.cursor_lost) { delete historyCursors[key]; }
        fail(sequence, result);
        return;
      }
      var data = result.data;
      var current = historyCursors[key];
      var newest = op === ops.first || op === ops.refresh;
      if (current === state && !(newest && state.older)) {
        if (!newest) {
          state.older = true;
          state.tail = used;
        }
        state.next = data.next_cursor || null;
        state.retry = data.partial ? data.retry_cursor || null : null;
      }
      var items = (Array.isArray(data.items) ? data.items : []).filter(function(message) {
        return message && MESSAGE_PATTERN.test(message.id);
      });
      items.forEach(function(message) { remember(account, chat, message); });
      respond(sequence, protocol.result.ok, items.map(function(message) { return messageRecord(message, chat, bounds.text); }), pageFlags(data));
    });
  }

  function fullText(sequence, payload) {
    var account = payload.ACCOUNT_ID;
    var chat = payload.ENTITY_ID;
    var id = payload.MESSAGE_ID;
    if (!ACCOUNT_PATTERN.test(account || '') || !CHAT_PATTERN.test(chat || '') || !MESSAGE_PATTERN.test(id || '')) {
      respond(sequence, protocol.result.protocol_error);
      return;
    }
    var limit = Math.max(16, Math.min(FULL_TEXT_LIMIT, Number.isInteger(payload.TEXT_LIMIT) ? payload.TEXT_LIMIT : 4096));
    function send(message) {
      var encoded = options.text.encode(describe(message, chat).text, limit);
      var parts = options.text.splitUtf8(encoded.bytes, budget() - 8);
      respond(sequence, protocol.result.ok, parts.map(codec.text), encoded.truncated ? protocol.flag.truncated : 0);
    }
    var cached = texts[account + '|' + chat + '|' + id];
    var slot = begin('message', sequence);
    if (cached) { finish('message', slot); send(cached); return; }
    var value = settingsOrFail(sequence);
    if (!value) { finish('message', slot); return; }
    call('message', slot, function(callback) {
      return options.api.message(value, account, chat, id, {view_id: options.leases.viewId()}, callback);
    }, function(result) {
      finish('message', slot);
      if (!result.ok) { fail(sequence, result); return; }
      if (!result.data.item || typeof result.data.item !== 'object') { respond(sequence, protocol.result.protocol_error); return; }
      remember(account, chat, result.data.item);
      send(result.data.item);
    });
  }

  function release(value) {
    return function(target) {
      options.api.releaseInterest(value, target.account, target.chat, options.leases.viewId(), function() {});
    };
  }

  function viewClose(sequence, payload) {
    var account = payload.ACCOUNT_ID;
    var chat = payload.ENTITY_ID;
    if (ACCOUNT_PATTERN.test(account || '') && CHAT_PATTERN.test(chat || '')) {
      var slot = slots.history;
      if (slot) {
        slot.cancelled = true;
        if (slot.handle) { slot.handle.abort(); }
        if (slot.timer) { clearTimer(slot.timer); }
        delete slots.history;
      }
      var value = options.settings.load(options.storage);
      if (options.settings.validate(value).ok) { options.leases.close(account, chat, release(value)); }
    }
    respond(sequence, protocol.result.ok, [], 0);
  }

  function setDefault(sequence, payload) {
    var account = payload.ACCOUNT_ID;
    if (account && !ACCOUNT_PATTERN.test(account)) { respond(sequence, protocol.result.protocol_error); return; }
    var value = settingsOrFail(sequence);
    if (!value) { return; }
    var slot = begin(null, sequence);
    call(null, slot, function(callback) {
      return options.api.updatePreferences(value, {default_account_id: account || null}, callback);
    }, function(result) {
      if (!result.ok) { fail(sequence, result); return; }
      respond(sequence, protocol.result.ok, [], 0);
    });
  }

  function handle(payload) {
    var sequence = payload.REQUEST_SEQ;
    var kinds = protocol.request;
    switch (payload.REQUEST_KIND) {
      case kinds.bootstrap: bootstrap(sequence); break;
      case kinds.chats: chats(sequence, payload); break;
      case kinds.history: history(sequence, payload); break;
      case kinds.message: fullText(sequence, payload); break;
      case kinds.view_close: viewClose(sequence, payload); break;
      case kinds.set_default: setDefault(sequence, payload); break;
      default: respond(sequence, protocol.result.protocol_error);
    }
  }

  function reset() {
    ++generation;
    Object.keys(slots).forEach(function(name) {
      var slot = slots[name];
      slot.cancelled = true;
      if (slot.handle) { slot.handle.abort(); }
      if (slot.timer) { clearTimer(slot.timer); }
      options.transport.cancel(slot.sequence);
    });
    slots = {};
    chatCursors = {};
    historyCursors = {};
    texts = {};
    textOrder = [];
    options.leases.reset();
  }

  return {
    handle: handle,
    reset: reset,
    setInboxSize: function(size) { inboxSize = size; },
    budget: budget
  };
}

module.exports = {create: create};
