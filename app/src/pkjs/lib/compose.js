'use strict';
var ACCOUNT_PATTERN = /^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/;
var CHAT_PATTERN = /^-?[1-9][0-9]{0,18}$/;
var MESSAGE_PATTERN = /^[1-9][0-9]{0,18}$/;
var SENDS_KEY = 'telebezel-sends';
var TEMPLATES_KEY = 'telebezel-templates';
var COUNTER_KEY = 'telebezel-draft-counter';
var ENTRY_TTL_MS = 3600000;
var TRACK_WINDOW_MS = 120000;
var TRACK_DELAYS_MS = [2000, 4000, 8000, 15000, 30000];
var POST_WINDOW_MS = 20000;
var POST_ATTEMPTS = 3;
var MAX_TEXT_UNITS = 4096;
var MAX_DICTATION_BYTES = 1024;
var PREVIEW_CHARS = 40;

function canonical(value) {
  var result = typeof value === 'string' ? value : '';
  if (typeof result.normalize === 'function') {
    try { result = result.normalize('NFC'); } catch (_error) { result = String(result); }
  }
  result = result.replace(/\r\n?/g, '\n');
  var kept = '';
  for (var index = 0; index < result.length; ++index) {
    var code = result.charCodeAt(index);
    if (code === 0x0A || !((code < 0x20) || (code >= 0x7F && code <= 0x9F))) { kept += result.charAt(index); }
  }
  return kept.trim();
}

function utf8Bytes(value) {
  var bytes = 0;
  for (var index = 0; index < value.length; ++index) {
    var code = value.charCodeAt(index);
    if (code < 0x80) { bytes += 1; }
    else if (code < 0x800) { bytes += 2; }
    else if (code >= 0xD800 && code <= 0xDBFF && index + 1 < value.length) { bytes += 4; ++index; }
    else { bytes += 3; }
  }
  return bytes;
}

function fingerprint(value) {
  var source = [value.address, value.ssl ? 1 : 0, value.token].join('|');
  var hash = 5381;
  for (var index = 0; index < source.length; ++index) { hash = ((hash * 33) ^ source.charCodeAt(index)) >>> 0; }
  return hash.toString(16);
}

function create(options) {
  var protocol = options.protocol;
  var codec = options.codec;
  var now = options.now || Date.now;
  var setTimer = options.setTimeout || setTimeout;
  var clearTimer = options.clearTimeout || clearTimeout;
  var random = options.random || Math.random;
  var drafts = {};
  var entries = [];
  var trackers = {};
  var loaded = false;

  function log(line) { if (options.log) { options.log('TeleBezel ' + line); } }

  function read(key) {
    try {
      var raw = options.storage.getItem(key);
      return raw ? JSON.parse(raw) : null;
    } catch (_error) {
      return null;
    }
  }

  function write(key, value) {
    try {
      if (value === null) { options.storage.removeItem(key); }
      else { options.storage.setItem(key, JSON.stringify(value)); }
    } catch (_error) {
      log('storage write failed for ' + key);
    }
  }

  function currentSettings() {
    var value = options.settings.load(options.storage);
    return options.settings.validate(value).ok ? value : null;
  }

  function load() {
    if (loaded) { return; }
    loaded = true;
    var value = currentSettings();
    var stored = read(SENDS_KEY);
    var mark = value ? fingerprint(value) : '';
    entries = (Array.isArray(stored) ? stored : []).filter(function(entry) {
      return entry && typeof entry.key === 'string' && entry.fingerprint === mark && now() - entry.created < ENTRY_TTL_MS &&
        entry.state !== 'sent' && ACCOUNT_PATTERN.test(entry.account || '') && CHAT_PATTERN.test(entry.chat || '');
    });
    entries.forEach(function(entry) {
      entry.waiters = [];
      entry.restored = true;
      if (entry.state === 'dispatching') { entry.state = 'unknown'; }
      if (!drafts[entry.draftId]) {
        drafts[entry.draftId] = {id: entry.draftId, account: entry.account, chat: entry.chat, reply: entry.reply || '', title: entry.title,
          text: entry.text, units: entry.text.length, bytes: utf8Bytes(entry.text), tooLong: false, attempts: {}};
      }
      drafts[entry.draftId].attempts[entry.attempt] = entry;
    });
    persist();
  }

  function persist() {
    var kept = entries.filter(function(entry) { return entry.state !== 'sent' && !entry.discarded && now() - entry.created < ENTRY_TTL_MS; });
    write(SENDS_KEY, kept.length ? kept.map(function(entry) {
      return {draftId: entry.draftId, attempt: entry.attempt, key: entry.key, operationId: entry.operationId || null, state: entry.state,
        error: entry.error || null, retryAfter: entry.retryAfter || 0, retryable: Boolean(entry.retryable),
        replyDropped: Boolean(entry.replyDropped), messageId: entry.messageId || null, account: entry.account, chat: entry.chat,
        reply: entry.reply || '', title: entry.title || '', text: entry.text, created: entry.created, tracked: entry.tracked || entry.created,
        fingerprint: entry.fingerprint};
    }) : null);
  }

  function nextDraftId() {
    var counter = read(COUNTER_KEY);
    var value = (typeof counter === 'number' && counter > 0 && counter < 0xFFFFFFF0 ? counter : 0) + 1;
    write(COUNTER_KEY, value);
    return value;
  }

  function newKey() {
    var alphabet = '0123456789abcdefghijklmnopqrstuvwxyz';
    var value = 'tbs-' + now().toString(36) + '-';
    for (var index = 0; index < 20; ++index) { value += alphabet.charAt(Math.floor(random() * alphabet.length)); }
    return value;
  }

  function respond(sequence, code, records, flags) {
    var chunks = code === protocol.result.ok ? codec.pack(records || [], options.budget()) : [[]];
    chunks.forEach(function(chunk, index) {
      var message = {RESPONSE_KIND: protocol.response.data, REQUEST_SEQ: sequence, RESULT_CODE: code, CHUNK_INDEX: index,
        CHUNK_TOTAL: chunks.length, PAGE_FLAGS: flags || 0};
      if (chunk.length > 0) { message.PAYLOAD = chunk; }
      options.transport.send(message, sequence);
    });
    if (code !== protocol.result.ok) { log('compose request ' + sequence + ' result ' + code); }
  }

  function push(records) {
    if (!records.length) { return; }
    codec.pack(records, options.budget()).forEach(function(chunk) {
      options.transport.send({RESPONSE_KIND: protocol.response.push, REQUEST_SEQ: 0, RESULT_CODE: protocol.result.ok, CHUNK_INDEX: 0,
        CHUNK_TOTAL: 1, PAGE_FLAGS: 0, PAYLOAD: chunk});
    });
  }

  function resultCode(error) {
    var codes = protocol.result;
    switch (error) {
      case 'message.send_forbidden': return codes.send_forbidden;
      case 'message.reply_unavailable': return codes.reply_unavailable;
      case 'message.text_too_long': return codes.text_too_long;
      case 'message.send_rate_limited': case 'rate_limit.exceeded': return codes.send_rate_limited;
      case 'chat.not_found': return codes.chat_not_found;
      case 'authorization.invalid_state': return codes.account_needs_login;
      case 'operation.outcome_unknown': return codes.send_unknown;
      case 'service.busy': case 'service.stopping': case 'service.tdlib_unavailable': return codes.busy;
      default: return error ? codes.backend_unavailable : codes.ok;
    }
  }

  function stateCode(state) {
    var states = protocol.send_state;
    if (state === 'sent') { return states.sent; }
    if (state === 'failed') { return states.failed; }
    if (state === 'unknown') { return states.unknown; }
    return states.pending;
  }

  function record(entry, restored) {
    var flags = protocol.send_flag;
    var code = entry.state === 'unknown' ? protocol.result.send_unknown : entry.state === 'failed' ? resultCode(entry.error) : protocol.result.ok;
    return codec.sendState({
      draftId: entry.draftId,
      state: stateCode(entry.state),
      code: code,
      retryAfter: entry.retryAfter || 0,
      flags: (entry.retryable ? flags.retryable : 0) | (entry.replyDropped ? flags.reply_dropped : 0) | (restored ? flags.restored : 0),
      account: entry.account,
      chat: entry.chat,
      message: entry.messageId || '',
      title: entry.title || '',
      preview: entry.text.length > PREVIEW_CHARS ? entry.text.slice(0, PREVIEW_CHARS) : entry.text
    }, restored);
  }

  function apply(entry, operation) {
    if (!operation || typeof operation !== 'object') { return false; }
    var before = entry.state + '|' + (entry.error || '');
    if (typeof operation.id === 'string') { entry.operationId = operation.id; }
    if (typeof operation.operation_id === 'string') { entry.operationId = operation.operation_id; }
    var state = operation.state;
    if (['pending', 'sent', 'failed', 'unknown'].indexOf(state) === -1) { return false; }
    if (entry.state === 'sent' || (entry.state === 'failed' && state !== 'failed')) { return false; }
    entry.state = state;
    var error = operation.error && typeof operation.error === 'object' ? operation.error : null;
    entry.error = error && typeof error.code === 'string' ? error.code : state === 'unknown' ? 'operation.outcome_unknown' : null;
    entry.retryAfter = error && Number.isInteger(error.retry_after) ? error.retry_after : 0;
    entry.retryable = operation.retryable === true;
    entry.replyDropped = operation.reply_dropped === true;
    if (typeof operation.message_id === 'string') { entry.messageId = operation.message_id; }
    return before !== entry.state + '|' + (entry.error || '');
  }

  function settle(entry) {
    var waiters = entry.waiters || [];
    entry.waiters = [];
    waiters.forEach(function(sequence) { respond(sequence, protocol.result.ok, [record(entry, false)], 0); });
  }

  function forget(entry) {
    entries = entries.filter(function(item) { return item !== entry; });
    var draft = drafts[entry.draftId];
    if (draft && draft.attempts[entry.attempt] === entry) { delete draft.attempts[entry.attempt]; }
  }

  function fatal(result) {
    var codes = protocol.result;
    if (result.status === 401 || result.code === 'auth.unauthorized') { return codes.api_unauthorized; }
    if (result.code === 'auth.insufficient_scope' || (result.status === 403 && !/^message\./.test(result.code || ''))) { return codes.wrong_token_type; }
    if (result.code === 'account.not_found' || result.code === 'account.gone') { return codes.account_gone; }
    if (result.code === 'authorization.invalid_state') { return codes.account_needs_login; }
    if (result.code === 'chat.not_found') { return codes.chat_not_found; }
    if (result.code === 'message.text_too_long') { return codes.text_too_long; }
    if (result.code === 'operation.conflict' || result.code === 'request.invalid' || result.code === 'request.invalid_idempotency_key') {
      return codes.protocol_error;
    }
    return null;
  }

  function lost(result) {
    return result.status === 0 || result.status === 502 || result.status === 503 || result.status === 504 || result.code === 'response.invalid';
  }

  function dispatch(entry, value, done) {
    var started = now();
    var attempt = 0;
    function post() {
      ++attempt;
      var body = {text: entry.text};
      if (entry.reply) { body.reply_to_message_id = entry.reply; }
      options.api.sendMessage(value, entry.account, entry.chat, body, entry.key, function(result) {
        if (result.ok) {
          apply(entry, result.data.operation);
          done(null);
          return;
        }
        if (lost(result)) {
          if (attempt < POST_ATTEMPTS && now() - started < POST_WINDOW_MS) {
            setTimer(post, 1000 * attempt);
            return;
          }
          entry.state = 'unknown';
          entry.error = 'operation.outcome_unknown';
          done(null);
          return;
        }
        if (result.status === 429) {
          entry.state = 'failed';
          entry.error = 'message.send_rate_limited';
          entry.retryAfter = result.retryAfter || 5;
          entry.retryable = true;
          done(null);
          return;
        }
        var code = fatal(result);
        entry.state = 'failed';
        entry.error = result.code || 'message.send_failed';
        entry.retryable = code === null && entry.error !== 'message.send_forbidden' && entry.error !== 'message.reply_unavailable';
        done(code);
      });
    }
    post();
  }

  function track(account) {
    var tracker = trackers[account];
    if (tracker && tracker.timer) { return; }
    tracker = trackers[account] = tracker || {step: 0, timer: null};
    var active = entries.filter(function(entry) {
      return entry.account === account && (entry.state === 'pending' || entry.state === 'unknown') && now() - (entry.tracked || entry.created) < TRACK_WINDOW_MS;
    });
    if (!active.length) {
      delete trackers[account];
      return;
    }
    var delay = TRACK_DELAYS_MS[Math.min(tracker.step, TRACK_DELAYS_MS.length - 1)];
    ++tracker.step;
    tracker.timer = setTimer(function() {
      tracker.timer = null;
      var value = currentSettings();
      if (!value) { delete trackers[account]; return; }
      options.updates.poll(value, account, function(outcome) {
        push(consume(value, account, outcome, true));
        if (trackers[account] === tracker) { track(account); }
      });
    }, delay);
  }

  function check(value, entry, done) {
    if (!entry.operationId) {
      dispatch(entry, value, function() { persist(); done(); });
      return;
    }
    options.api.sendStatus(value, entry.account, entry.operationId, function(result) {
      if (result.ok) { apply(entry, result.data.operation); }
      else if (result.status === 404 && entry.state !== 'sent') {
        entry.state = 'unknown';
        entry.error = 'operation.outcome_unknown';
      }
      persist();
      done();
    });
  }

  function consume(value, account, outcome, background) {
    load();
    var changed = [];
    if (outcome && outcome.ok) {
      outcome.events.forEach(function(event) {
        if (!event || event.type !== 'send_changed') { return; }
        entries.forEach(function(entry) {
          if (entry.operationId && entry.operationId === event.operation_id && apply(entry, event)) { changed.push(entry); }
        });
      });
    }
    if (outcome && outcome.resynced) {
      entries.filter(function(entry) {
        return entry.account === account && entry.operationId && (entry.state === 'pending' || entry.state === 'unknown') && !entry.checking;
      }).forEach(function(entry) {
        entry.checking = true;
        var before = entry.state;
        check(value, entry, function() {
          entry.checking = false;
          if (entry.state !== before) { push([record(entry, false)]); }
        });
      });
    }
    persist();
    if (background) {
      changed.forEach(function(entry) { log('send ' + entry.draftId + ' settled ' + entry.state); });
    }
    return changed.map(function(entry) { return record(entry, false); });
  }

  function templates(sequence, payload) {
    var value = currentSettings();
    if (!value) { respond(sequence, protocol.result.config_missing); return; }
    var previewLimit = Math.max(16, Math.min(96, Number.isInteger(payload.TEXT_LIMIT) ? payload.TEXT_LIMIT : 48));
    function answer(snapshot, stale) {
      var records = [codec.templates({revision: snapshot.revision, count: snapshot.items.length, flags: stale ? 1 : 0})];
      snapshot.items.forEach(function(item, index) {
        records.push(codec.template({index: index, length: utf8Bytes(item), preview: item}, previewLimit));
      });
      respond(sequence, protocol.result.ok, records, stale ? protocol.flag.stale : 0);
    }
    options.api.quickReplies(value, function(result) {
      var cached = read(TEMPLATES_KEY);
      var usable = cached && cached.fingerprint === fingerprint(value) && Array.isArray(cached.items) ? cached : null;
      if (!result.ok || !Array.isArray(result.data.items)) {
        if (usable) { answer(usable, true); return; }
        respond(sequence, result.ok ? protocol.result.protocol_error : options.failureCode(result));
        return;
      }
      var snapshot = {
        revision: Number.isInteger(result.data.revision) ? result.data.revision >>> 0 : 0,
        items: result.data.items.slice(0, 50).map(function(item) { return canonical(item && item.text); }).filter(function(text) { return text !== ''; }),
        fingerprint: fingerprint(value)
      };
      write(TEMPLATES_KEY, snapshot);
      answer(snapshot, false);
    });
  }

  function target(payload) {
    var account = payload.ACCOUNT_ID;
    var chat = payload.ENTITY_ID;
    var reply = payload.MESSAGE_ID || '';
    if (!ACCOUNT_PATTERN.test(account || '') || !CHAT_PATTERN.test(chat || '') || (reply && !MESSAGE_PATTERN.test(reply))) { return null; }
    return {account: account, chat: chat, reply: reply};
  }

  function draft(sequence, payload) {
    load();
    var destination = target(payload);
    if (!destination) { options.reject(sequence, payload, ['ACCOUNT_ID', 'ENTITY_ID', 'MESSAGE_ID']); return; }
    var source = null;
    var raw = '';
    if (Array.isArray(payload.PAYLOAD)) {
      if (payload.PAYLOAD.length === 0) { options.reject(sequence, payload, ['PAYLOAD']); return; }
      if (payload.PAYLOAD.length > MAX_DICTATION_BYTES) { respond(sequence, protocol.result.text_too_long); return; }
      raw = options.text.decodeUtf8(payload.PAYLOAD);
      source = 'dictation';
    } else {
      var snapshot = read(TEMPLATES_KEY);
      var value = currentSettings();
      var index = payload.TEMPLATE_INDEX;
      if (!snapshot || !value || snapshot.fingerprint !== fingerprint(value) || !Array.isArray(snapshot.items) ||
          (snapshot.revision >>> 0) !== (payload.TEMPLATES_REV >>> 0) || !Number.isInteger(index) || !snapshot.items[index]) {
        respond(sequence, protocol.result.cursor_lost);
        return;
      }
      raw = snapshot.items[index];
      source = 'template';
    }
    var textValue = canonical(raw);
    if (!textValue) { respond(sequence, protocol.result.protocol_error); return; }
    var id = nextDraftId();
    var created = {id: id, account: destination.account, chat: destination.chat, reply: destination.reply, source: source,
      title: options.title(destination.account, destination.chat), text: textValue, units: textValue.length, bytes: utf8Bytes(textValue),
      tooLong: textValue.length > MAX_TEXT_UNITS, attempts: {}};
    drafts[id] = created;
    var limit = Math.max(16, Math.min(8192, Number.isInteger(payload.TEXT_LIMIT) ? payload.TEXT_LIMIT : 1024));
    var shown = options.text.encode(textValue, limit);
    var parts = options.text.splitUtf8(shown.bytes, options.budget() - 16);
    respond(sequence, protocol.result.ok, [codec.draft({id: id, bytes: created.bytes, units: created.units,
      flags: created.tooLong ? protocol.draft_flag.too_long : 0})].concat(parts.map(codec.text)), shown.truncated ? protocol.flag.truncated : 0);
  }

  function sameTarget(found, payload) {
    return payload.ACCOUNT_ID === found.account && payload.ENTITY_ID === found.chat && (payload.MESSAGE_ID || '') === (found.reply || '');
  }

  function send(sequence, payload) {
    load();
    var found = drafts[payload.DRAFT_ID];
    if (!found) { respond(sequence, protocol.result.draft_lost); return; }
    if (!sameTarget(found, payload)) { options.reject(sequence, payload, ['ACCOUNT_ID', 'ENTITY_ID', 'MESSAGE_ID', 'DRAFT_ID']); return; }
    if (found.tooLong) { respond(sequence, protocol.result.text_too_long); return; }
    var attempt = Number.isInteger(payload.ATTEMPT) && payload.ATTEMPT >= 0 ? payload.ATTEMPT & 0xFF : 0;
    var existing = found.attempts[attempt];
    if (existing) {
      if (existing.state === 'dispatching') { existing.waiters.push(sequence); }
      else { respond(sequence, protocol.result.ok, [record(existing, false)], 0); }
      return;
    }
    if (entries.some(function(entry) { return entry.state === 'dispatching'; })) { respond(sequence, protocol.result.busy); return; }
    var value = currentSettings();
    if (!value) { respond(sequence, protocol.result.config_missing); return; }
    var entry = {draftId: found.id, attempt: attempt, key: newKey(), operationId: null, state: 'dispatching', error: null, retryAfter: 0,
      retryable: false, replyDropped: false, messageId: null, account: found.account, chat: found.chat, reply: found.reply,
      title: found.title, text: found.text, created: now(), tracked: now(), fingerprint: fingerprint(value), waiters: [sequence]};
    found.attempts[attempt] = entry;
    entries.push(entry);
    persist();
    function post() {
      dispatch(entry, value, function(fatalCode) {
        if (fatalCode !== null) {
          var waiters = entry.waiters;
          entry.waiters = [];
          forget(entry);
          persist();
          waiters.forEach(function(waiting) { respond(waiting, fatalCode); });
          return;
        }
        persist();
        settle(entry);
        if (entry.state === 'pending' || entry.state === 'unknown') { track(entry.account); }
      });
    }
    if (options.updates.primed(found.account)) { post(); return; }
    options.updates.poll(value, found.account, function(outcome) {
      push(consume(value, found.account, outcome, true));
      post();
    });
  }

  function latest(found) {
    var best = null;
    Object.keys(found.attempts).forEach(function(key) {
      var entry = found.attempts[key];
      if (!best || entry.created >= best.created) { best = entry; }
    });
    return best;
  }

  function sendCheck(sequence, payload) {
    load();
    var found = drafts[payload.DRAFT_ID];
    var entry = found ? latest(found) : null;
    if (!entry) { respond(sequence, protocol.result.draft_lost); return; }
    if (entry.state === 'dispatching') { entry.waiters.push(sequence); return; }
    var value = currentSettings();
    if (!value) { respond(sequence, protocol.result.config_missing); return; }
    if (entry.state === 'sent' || entry.state === 'failed') { respond(sequence, protocol.result.ok, [record(entry, false)], 0); return; }
    check(value, entry, function() {
      respond(sequence, protocol.result.ok, [record(entry, false)], 0);
      if (entry.state === 'pending' || entry.state === 'unknown') {
        entry.tracked = now();
        track(entry.account);
      }
    });
  }

  function discard(sequence, payload) {
    load();
    var found = drafts[payload.DRAFT_ID];
    if (found) {
      Object.keys(found.attempts).forEach(function(key) {
        var entry = found.attempts[key];
        if (entry.state === 'failed' || entry.state === 'sent') { entry.discarded = true; forget(entry); }
      });
      if (Object.keys(found.attempts).length === 0) { delete drafts[found.id]; }
      persist();
    }
    respond(sequence, protocol.result.ok, [], 0);
  }

  function restoredRecords() {
    load();
    var records = [];
    entries.forEach(function(entry) {
      if (entry.state === 'sent') { return; }
      records.push(record(entry, true));
      if ((entry.state === 'pending' || entry.state === 'unknown') && !trackers[entry.account]) {
        entry.tracked = now();
        track(entry.account);
      }
    });
    return records;
  }

  function reset(value) {
    load();
    var mark = value ? fingerprint(value) : '';
    if (entries.length && entries[0].fingerprint === mark) { return; }
    Object.keys(trackers).forEach(function(account) { if (trackers[account].timer) { clearTimer(trackers[account].timer); } });
    trackers = {};
    drafts = {};
    entries = [];
    write(SENDS_KEY, null);
    write(TEMPLATES_KEY, null);
    options.updates.reset();
  }

  return {
    templates: templates,
    draft: draft,
    send: send,
    sendCheck: sendCheck,
    discard: discard,
    consume: consume,
    restoredRecords: restoredRecords,
    reset: reset
  };
}

module.exports = {create: create, canonical: canonical, fingerprint: fingerprint};
