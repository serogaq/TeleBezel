'use strict';
var assert = require('assert');
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var text = require('../src/pkjs/lib/text');
var codecFactory = require('../src/pkjs/lib/codec');
var leasesFactory = require('../src/pkjs/lib/leases');
var transportFactory = require('../src/pkjs/lib/transport');
var readerFactory = require('../src/pkjs/lib/reader');
var collect = require('./helpers/decode').collect;

var ACCOUNT = '00112233-4455-4677-8899-aabbccddeeff';
var OTHER = '10112233-4455-4677-8899-aabbccddeeff';
var CHAT = '-1009007199254740993';

function harness(configured) {
  var memory = {};
  var storage = {getItem: function(key) { return Object.prototype.hasOwnProperty.call(memory, key) ? memory[key] : null; }, setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
  if (configured !== false) { settings.save(storage, {address: 'tg.example:443', ssl: true, token: 'tb_' + new Array(44).join('a')}); }
  var messages = [];
  var pebble = {sendAppMessage: function(message, success) { messages.push(message); success(); }};
  var calls = [];
  var clock = {now: 1000, timers: []};
  function api(name) {
    return function() {
      var args = Array.prototype.slice.call(arguments);
      var call = {name: name, args: args, callback: args[args.length - 1], aborted: false};
      calls.push(call);
      return {abort: function() { call.aborted = true; }};
    };
  }
  var fakeApi = {preferences: api('preferences'), accounts: api('accounts'), chats: api('chats'), history: api('history'),
    message: api('message'), releaseInterest: api('releaseInterest'), updatePreferences: api('updatePreferences')};
  var leases = leasesFactory.create(storage, function() { return 0.5; });
  var reader = readerFactory.create({api: fakeApi, settings: settings, storage: storage, protocol: protocol,
    codec: codecFactory.create(protocol), text: text, transport: transportFactory.create(pebble), leases: leases,
    now: function() { return clock.now; },
    setTimeout: function(callback, delay) { var timer = {callback: callback, at: clock.now + delay}; clock.timers.push(timer); return timer; },
    clearTimeout: function(timer) { clock.timers = clock.timers.filter(function(item) { return item !== timer; }); }});
  reader.setInboxSize(512);
  return {
    reader: reader, calls: calls, messages: messages, clock: clock, storage: storage, leases: leases,
    request: function(payload) { reader.handle(payload); },
    reply: function(name, result, index) {
      var matching = calls.filter(function(call) { return call.name === name; });
      matching[index === undefined ? matching.length - 1 : index].callback(result);
    },
    advance: function(ms) {
      clock.now += ms;
      var due = clock.timers.filter(function(timer) { return timer.at <= clock.now; });
      clock.timers = clock.timers.filter(function(timer) { return timer.at > clock.now; });
      due.forEach(function(timer) { timer.callback(); });
    },
    response: function(sequence) { return collect(protocol, messages, sequence); }
  };
}
function ok(data) { return {ok: true, status: 200, data: data}; }
function failure(status, code, retryAfter) {
  var api = require('../src/pkjs/lib/api');
  return {ok: false, status: status, code: code, retryAfter: retryAfter === undefined ? null : retryAfter, action: api.classify(protocol, status, code), data: null};
}
function chat(id, extra) {
  var value = {id: id, type: 'basic_group', title: 'Chat ' + id, is_marked_unread: false, unread_count: 2, unread_mention_count: 0,
    notifications: {use_default_mute_for: true, mute_for: 0}, positions: {main: {order: '1', is_pinned: false}},
    last_message: {id: '5', chat_id: id, sender: {type: 'user', id: '7', name: 'Ada', fallback: 'User 7'}, date: 1700000000, edit_date: 0,
      is_outgoing: false, author_signature: '', content: {kind: 'text', text: 'hello'}}};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}
function message(id, extra) {
  var value = {id: id, chat_id: CHAT, sender: {type: 'user', id: '7', name: null, fallback: 'User 7'}, date: 1700000000 + Number(id), edit_date: 0,
    is_outgoing: false, author_signature: '', content: {kind: 'text', text: 'Message ' + id}};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}
function page(items, extra) {
  var value = {items: items, stale: true, partial: false, has_more: true, next_cursor: 'next-' + (items.length ? items[items.length - 1].id : 'none'),
    retry_cursor: null, updates_cursor: 'u', observed_at: 1, source: 'tdlib_local', refresh: 'pending', connection: 'ready', local_exhausted: false, fallback_reason: null};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}
var R = protocol.request;
var OP = protocol.page_op;

var unconfigured = harness(false);
unconfigured.request({REQUEST_KIND: R.bootstrap, REQUEST_SEQ: 1});
assert.strictEqual(unconfigured.response(1).code, protocol.result.config_missing);
assert.strictEqual(unconfigured.calls.length, 0);

var boot = harness();
boot.request({REQUEST_KIND: R.bootstrap, REQUEST_SEQ: 2});
boot.reply('preferences', ok({id: 'd', name: 'Watch', locale: 'auto', default_account_id: OTHER, chat_list: 'archive'}));
boot.reply('accounts', ok([
  {id: ACCOUNT, label: '', lifecycle: 'active', runtime: {available: true, authorization_state: 'ready', connection_state: 'ready'},
    telegram_identity: {id: '1', first_name: 'Ада', last_name: 'Лавлейс', usernames: []}},
  {id: OTHER, label: 'Work', lifecycle: 'active', runtime: {available: true, authorization_state: 'awaiting_code', connection_state: 'ready'}},
  {id: 'bad', label: 'Broken', lifecycle: 'active', runtime: {}}
]));
var booted = boot.response(2);
assert.strictEqual(booted.code, protocol.result.ok);
assert.deepStrictEqual(booted.records.map(function(record) { return record.type; }), ['prefs', 'account', 'account']);
assert.strictEqual(booted.records[0].chatList, protocol.list.archive);
assert.strictEqual(booted.records[0].host, 'tg.example:443');
assert.strictEqual(booted.records[1].name, 'Ада Лавлейс');
assert.strictEqual(booted.records[1].state, protocol.account_state.ready);
assert.strictEqual(booted.records[2].state, protocol.account_state.needs_login);
assert.strictEqual(booted.records[2].flags, protocol.account_flag.default);
assert.ok(boot.messages.every(function(item) { return JSON.stringify(item).indexOf('tb_') === -1; }), 'the token crossed to the watch');

var revoked = harness();
revoked.request({REQUEST_KIND: R.bootstrap, REQUEST_SEQ: 3});
revoked.reply('preferences', failure(401, 'auth.unauthorized'));
assert.strictEqual(revoked.response(3).code, protocol.result.api_unauthorized);
var scoped = harness();
scoped.request({REQUEST_KIND: R.bootstrap, REQUEST_SEQ: 3});
scoped.reply('preferences', failure(403, 'auth.insufficient_scope'));
assert.strictEqual(scoped.response(3).code, protocol.result.wrong_token_type);
var offline = harness();
offline.request({REQUEST_KIND: R.bootstrap, REQUEST_SEQ: 3});
offline.reply('preferences', failure(0, 'network.unavailable'));
assert.strictEqual(offline.calls.length, 1);
offline.advance(1000);
assert.strictEqual(offline.calls.length, 2, 'a network failure is retried once');
offline.reply('preferences', failure(0, 'network.unavailable'));
assert.strictEqual(offline.response(3).code, protocol.result.backend_unavailable);

var lists = harness();
lists.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 10, ACCOUNT_ID: ACCOUNT, LIST: protocol.list.main, PAGE_OP: OP.first, PAGE_LIMIT: 2, TEXT_LIMIT: 40});
assert.deepStrictEqual(lists.calls[0].args.slice(1, 3), [ACCOUNT, {list: 'main', limit: 2, cursor: null}]);
lists.reply('chats', ok(page([chat(CHAT), chat('42', {type: 'private', unread_mention_count: 1, notifications: {use_default_mute_for: false, mute_for: 100}})], {has_more: true, next_cursor: 'c1'})));
var listed = lists.response(10);
assert.strictEqual(listed.records.length, 2);
assert.strictEqual(listed.records[0].id, CHAT);
assert.strictEqual(listed.records[0].previewSender, 'Ada');
assert.strictEqual(listed.records[1].flags & protocol.chat_flag.muted, protocol.chat_flag.muted);
assert.strictEqual(listed.records[1].flags & protocol.chat_flag.mention, protocol.chat_flag.mention);
assert.strictEqual(listed.flags & protocol.flag.has_more, protocol.flag.has_more);
lists.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 11, ACCOUNT_ID: ACCOUNT, LIST: protocol.list.main, PAGE_OP: OP.next, PAGE_LIMIT: 2, TEXT_LIMIT: 40});
assert.strictEqual(lists.calls[1].args[2].cursor, 'c1');
lists.reply('chats', failure(409, 'cursor.unusable'));
assert.strictEqual(lists.response(11).code, protocol.result.cursor_lost);
lists.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 12, ACCOUNT_ID: ACCOUNT, LIST: protocol.list.main, PAGE_OP: OP.next});
assert.strictEqual(lists.response(12).code, protocol.result.cursor_lost, 'a lost cursor is not guessed');
lists.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 13, ACCOUNT_ID: ACCOUNT, LIST: protocol.list.archive, PAGE_OP: OP.first});
lists.reply('chats', failure(409, 'authorization.invalid_state'));
assert.strictEqual(lists.response(13).code, protocol.result.account_needs_login);
lists.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 14, ACCOUNT_ID: ACCOUNT, LIST: protocol.list.archive, PAGE_OP: OP.first});
lists.reply('chats', failure(429, 'rate_limit.exceeded', 17));
var limited = lists.response(14);
assert.deepStrictEqual([limited.code, limited.retryAfter], [protocol.result.rate_limited, 17]);
lists.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 15, ACCOUNT_ID: 'not-a-uuid', LIST: 0, PAGE_OP: OP.first});
assert.strictEqual(lists.response(15).code, protocol.result.protocol_error);

var superseded = harness();
superseded.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 20, ACCOUNT_ID: ACCOUNT, LIST: 0, PAGE_OP: OP.first});
superseded.request({REQUEST_KIND: R.chats, REQUEST_SEQ: 21, ACCOUNT_ID: OTHER, LIST: 0, PAGE_OP: OP.first});
assert.ok(superseded.calls[0].aborted, 'a superseded request is aborted');
superseded.reply('chats', ok(page([chat('1')])), 0);
assert.strictEqual(superseded.response(20), null, 'a late response of another account reached the watch');
superseded.reply('chats', ok(page([chat('2')])), 1);
assert.strictEqual(superseded.response(21).records[0].id, '2');

var reading = harness();
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 30, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.first, PAGE_LIMIT: 3, TEXT_LIMIT: 200});
var params = reading.calls[0].args[3];
assert.strictEqual(params.limit, 3);
assert.ok(/^[0-9a-f-]{36}$/.test(params.view_id));
assert.strictEqual(params.cursor, undefined);
var longText = new Array(200).join('слово ');
reading.reply('history', ok(page([message('30', {content: {kind: 'text', text: longText}}), message('29', {is_outgoing: true, edit_date: 5, content: {kind: 'text', text: longText}}),
  message('28', {content: {kind: 'photo', text: 'Подпись', fallback_key: 'message.photo'}})], {next_cursor: 'h28', refresh: 'queued'})));
var first = reading.response(30);
assert.strictEqual(first.records.length, 3);
assert.ok(first.chunks.length > 1, 'a large page is split into chunks');
assert.ok(first.chunks.every(function(chunk) { return !chunk.PAYLOAD || chunk.PAYLOAD.length <= 512 - 96; }));
assert.strictEqual(first.records[0].flags & protocol.message_flag.truncated, protocol.message_flag.truncated);
assert.ok(Buffer.byteLength(first.records[0].text) <= 200);
assert.strictEqual(first.records[1].flags & 3, protocol.message_flag.outgoing | protocol.message_flag.edited);
assert.strictEqual(first.records[2].kind, protocol.kind.photo);
assert.strictEqual(first.records[2].text, 'Подпись');
assert.strictEqual(first.records[0].sender, 'User 7');
assert.strictEqual(first.flags & protocol.flag.refresh_pending, protocol.flag.refresh_pending);

reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 31, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.next, PAGE_LIMIT: 3, TEXT_LIMIT: 64});
assert.strictEqual(reading.calls[1].args[3].cursor, 'h28');
reading.reply('history', ok(page([], {partial: true, has_more: null, next_cursor: null, retry_cursor: 'h28', fallback_reason: 'read.deadline'})));
var partial = reading.response(31);
assert.strictEqual(partial.records.length, 0);
assert.strictEqual(partial.flags & protocol.flag.partial, protocol.flag.partial);
assert.strictEqual(partial.flags & protocol.flag.has_more_unknown, protocol.flag.has_more_unknown);
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 32, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.refresh, PAGE_LIMIT: 3});
reading.reply('history', ok(page([message('31')], {next_cursor: 'h31'})));
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 33, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.retry, PAGE_LIMIT: 3});
assert.strictEqual(reading.calls[3].args[3].retry_cursor, 'h28', 'a newest-page refresh must not replace the older retry cursor');
reading.reply('history', ok(page([], {has_more: false, next_cursor: null, local_exhausted: true, refresh: 'queued'})));
var exhausted = reading.response(33);
assert.strictEqual(exhausted.flags & protocol.flag.has_more, 0);
assert.strictEqual(exhausted.flags & protocol.flag.local_exhausted, protocol.flag.local_exhausted);
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 34, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.recheck, PAGE_LIMIT: 3});
assert.strictEqual(reading.calls[4].args[3].cursor, 'h28', 'a recheck repeats the last older range');
reading.reply('history', ok(page([message('27')], {next_cursor: 'h27'})));
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 35, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.next});
assert.strictEqual(reading.calls[5].args[3].cursor, 'h27');
reading.reply('history', failure(409, 'sync.resync_required'));
assert.strictEqual(reading.response(35).code, protocol.result.cursor_lost);
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 36, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.next});
assert.strictEqual(reading.response(36).code, protocol.result.cursor_lost);

reading.request({REQUEST_KIND: R.message, REQUEST_SEQ: 40, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: '30', TEXT_LIMIT: 4096});
var full = reading.response(40);
assert.strictEqual(reading.calls.filter(function(call) { return call.name === 'message'; }).length, 0, 'a cached message was fetched again');
var joined = [];
full.records.forEach(function(record) { joined = joined.concat(record.bytes); });
assert.strictEqual(text.decodeUtf8(joined), longText.trim());
assert.ok(full.chunks.length > 1);
reading.request({REQUEST_KIND: R.message, REQUEST_SEQ: 41, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: '99', TEXT_LIMIT: 20});
reading.reply('message', failure(404, 'message.cache_miss'));
assert.strictEqual(reading.response(41).code, protocol.result.message_unavailable);
reading.request({REQUEST_KIND: R.message, REQUEST_SEQ: 42, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: '98', TEXT_LIMIT: 20});
reading.reply('message', ok({item: message('98', {content: {kind: 'text', text: 'A fairly long message body'}})}));
var short = reading.response(42);
assert.strictEqual(short.flags & protocol.flag.truncated, protocol.flag.truncated);

var viewId = reading.calls[0].args[3].view_id;
reading.request({REQUEST_KIND: R.history, REQUEST_SEQ: 50, ACCOUNT_ID: ACCOUNT, ENTITY_ID: '42', PAGE_OP: OP.first});
var released = reading.calls.filter(function(call) { return call.name === 'releaseInterest'; });
assert.deepStrictEqual(released[0].args.slice(1, 4), [ACCOUNT, CHAT, viewId], 'opening another chat releases the previous lease');
assert.strictEqual(reading.calls.filter(function(call) { return call.name === 'history'; }).pop().args[3].view_id, viewId, 'the view id is stable');
reading.request({REQUEST_KIND: R.view_close, REQUEST_SEQ: 51, ACCOUNT_ID: ACCOUNT, ENTITY_ID: '42'});
assert.strictEqual(reading.response(51).code, protocol.result.ok);
assert.ok(reading.calls.filter(function(call) { return call.name === 'history'; }).pop().aborted, 'closing a view aborts its read');
released = reading.calls.filter(function(call) { return call.name === 'releaseInterest'; });
assert.strictEqual(released[1].args[2], '42');
assert.strictEqual(reading.leases.viewId(), viewId);
assert.strictEqual(reading.storage.getItem(leasesFactory.STORAGE_KEY), viewId);

var busy = harness();
busy.request({REQUEST_KIND: R.history, REQUEST_SEQ: 60, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.first});
busy.reply('history', failure(503, 'service.busy', 1));
busy.advance(1000);
assert.strictEqual(busy.calls.filter(function(call) { return call.name === 'history'; }).length, 2);
busy.reply('history', failure(503, 'service.busy', 1));
assert.strictEqual(busy.response(60).code, protocol.result.busy);
var views = harness();
views.request({REQUEST_KIND: R.history, REQUEST_SEQ: 61, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.first});
views.reply('history', failure(429, 'interest.limit_reached', 5));
views.advance(5000);
views.reply('history', failure(429, 'interest.limit_reached', 5));
assert.strictEqual(views.response(61).code, protocol.result.too_many_views);

var switching = harness();
switching.request({REQUEST_KIND: R.history, REQUEST_SEQ: 70, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAGE_OP: OP.first});
switching.reader.reset();
switching.reply('history', ok(page([message('1')])));
assert.strictEqual(switching.response(70), null, 'a response after a settings change reached the watch');

var defaults = harness();
defaults.request({REQUEST_KIND: R.set_default, REQUEST_SEQ: 80, ACCOUNT_ID: OTHER});
assert.deepStrictEqual(defaults.calls[0].args[1], {default_account_id: OTHER});
defaults.reply('updatePreferences', ok({default_account_id: OTHER}));
assert.strictEqual(defaults.response(80).code, protocol.result.ok);
defaults.request({REQUEST_KIND: R.set_default, REQUEST_SEQ: 81, ACCOUNT_ID: ''});
assert.deepStrictEqual(defaults.calls[1].args[1], {default_account_id: null});
defaults.request({REQUEST_KIND: 99, REQUEST_SEQ: 82});
assert.strictEqual(defaults.response(82).code, protocol.result.protocol_error);

var variants = harness();
variants.request({REQUEST_KIND: '3', REQUEST_SEQ: '90', ACCOUNT_ID: ACCOUNT + '\u0000', LIST: undefined, PAGE_LIMIT: '5', TEXT_LIMIT: 40});
assert.strictEqual(variants.calls.length, 1, 'a NUL-terminated id or a missing zero field was rejected');
assert.deepStrictEqual(variants.calls[0].args.slice(1, 3), [ACCOUNT, {list: 'main', limit: 5, cursor: null}]);
variants.reply('chats', ok(page([chat('42')])));
assert.strictEqual(variants.response(90).code, protocol.result.ok);
var logged = [];
var rejecting = readerFactory.create({api: {}, settings: settings, storage: variants.storage, protocol: protocol, codec: codecFactory.create(protocol), text: text,
  transport: transportFactory.create({sendAppMessage: function(_message, success) { success(); }}), leases: variants.leases,
  log: function(line) { logged.push(line); }});
rejecting.handle({REQUEST_KIND: protocol.request.chats, REQUEST_SEQ: 91, ACCOUNT_ID: 'broken', PAGE_OP: 0});
assert.ok(/rejected ACCOUNT_ID=string:6 chars/.test(logged[0]), 'the rejected field was not reported: ' + logged[0]);
assert.strictEqual(logged[0].indexOf('broken'), -1, 'a rejected value was logged verbatim');
