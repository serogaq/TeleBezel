'use strict';
var assert = require('assert');
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var text = require('../src/pkjs/lib/text');
var codecFactory = require('../src/pkjs/lib/codec');
var leasesFactory = require('../src/pkjs/lib/leases');
var transportFactory = require('../src/pkjs/lib/transport');
var readerFactory = require('../src/pkjs/lib/reader');
var compose = require('../src/pkjs/lib/compose');
var apiModule = require('../src/pkjs/lib/api');
var collect = require('./helpers/decode').collect;
var decode = require('./helpers/decode').decode;

var ACCOUNT = '00112233-4455-4677-8899-aabbccddeeff';
var CHAT = '-1009007199254740993';
var R = protocol.request;
var TOKEN = 'tb_' + new Array(44).join('a');

function harness(shared) {
  var memory = shared || {};
  var storage = {getItem: function(key) { return Object.prototype.hasOwnProperty.call(memory, key) ? memory[key] : null; },
    setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
  if (!shared) { settings.save(storage, {address: 'tg.example:443', ssl: true, token: TOKEN}); }
  var messages = [];
  var logs = [];
  var calls = [];
  var clock = {now: 1000, timers: []};
  var pebble = {sendAppMessage: function(message, success) { messages.push(message); success(); }};
  function api(name) {
    return function() {
      var args = Array.prototype.slice.call(arguments);
      var call = {name: name, args: args, callback: args[args.length - 1]};
      calls.push(call);
      return {abort: function() {}};
    };
  }
  var names = ['preferences', 'accounts', 'chats', 'history', 'updates', 'message', 'releaseInterest', 'updatePreferences', 'quickReplies',
    'sendMessage', 'sendStatus'];
  var fakeApi = {};
  names.forEach(function(name) { fakeApi[name] = api(name); });
  var reader = readerFactory.create({api: fakeApi, settings: settings, storage: storage, protocol: protocol, codec: codecFactory.create(protocol),
    text: text, transport: transportFactory.create(pebble), leases: leasesFactory.create(storage, function() { return 0.5; }),
    now: function() { return clock.now; }, random: function() { return 0.25; }, log: function(line) { logs.push(line); },
    setTimeout: function(callback, delay) { var timer = {callback: callback, at: clock.now + delay}; clock.timers.push(timer); return timer; },
    clearTimeout: function(timer) { clock.timers = clock.timers.filter(function(item) { return item !== timer; }); }});
  reader.setInboxSize(2048);
  var sequence = 0;
  return {
    reader: reader, calls: calls, messages: messages, logs: logs, memory: memory, storage: storage,
    request: function(payload) { payload.REQUEST_SEQ = ++sequence; reader.handle(payload); return sequence; },
    named: function(name) { return calls.filter(function(call) { return call.name === name; }); },
    reply: function(name, result) {
      var matching = calls.filter(function(call) { return call.name === name && !call.answered; });
      var call = matching[0];
      call.answered = true;
      call.callback(result);
      return call;
    },
    advance: function(ms) {
      clock.now += ms;
      var due = clock.timers.filter(function(timer) { return timer.at <= clock.now; });
      clock.timers = clock.timers.filter(function(timer) { return timer.at > clock.now; });
      due.forEach(function(timer) { timer.callback(); });
    },
    response: function(id) { return collect(protocol, messages, id); },
    pushed: function() {
      var records = [];
      messages.filter(function(message) { return message.RESPONSE_KIND === protocol.response.push; }).forEach(function(message) {
        records = records.concat(decode(protocol, message.PAYLOAD));
      });
      return records;
    }
  };
}
function ok(data) { return {ok: true, status: 200, data: data}; }
function failure(status, code, retryAfter) {
  return {ok: false, status: status, code: code, retryAfter: retryAfter === undefined ? null : retryAfter, action: apiModule.classify(protocol, status, code), data: null};
}
function operation(state, extra) {
  var value = {id: 'op-1', state: state, chat_id: CHAT, reply_to_message_id: null, message_id: state === 'sent' ? '900' : null, error: null,
    retryable: false, reply_dropped: false};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return ok({operation: value});
}
function bytes(value) { return Array.prototype.slice.call(Buffer.from(value, 'utf8')); }
function draftFrom(h, textValue, reply) {
  var id = h.request({REQUEST_KIND: R.draft, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: reply || '', PAYLOAD: bytes(textValue), TEXT_LIMIT: 512});
  return h.response(id);
}
function primed(h) {
  h.reply('updates', ok({events: [], cursor: 'c0', has_more: false, status: {connection: 'ready', proxy: false}}));
}

assert.strictEqual(compose.canonical('  Hi\r\nthere\u0007​  '), 'Hi\nthere​');
assert.strictEqual(compose.canonical('é'), 'é');

var templates = harness();
var listing = templates.request({REQUEST_KIND: R.templates, PAGE_OP: 0, TEXT_LIMIT: 20});
templates.reply('quickReplies', ok({items: [{id: 'a', text: 'On my way', position: 0}, {id: 'b', text: 'Позвоню позже, сейчас на встрече', position: 1}], revision: 7}));
var list = templates.response(listing);
assert.deepStrictEqual(list.records[0], {type: 'templates', revision: 7, count: 2, flags: 0});
assert.strictEqual(list.records[2].length, Buffer.byteLength('Позвоню позже, сейчас на встрече'));
assert.ok(Buffer.byteLength(list.records[2].preview) <= 20);
var stale = templates.request({REQUEST_KIND: R.templates, PAGE_OP: 4});
templates.reply('quickReplies', failure(0, 'network.unavailable'));
assert.strictEqual(templates.response(stale).flags & protocol.flag.stale, protocol.flag.stale, 'a failed refresh serves the last snapshot as stale');
assert.strictEqual(templates.response(stale).records[0].flags, 1);
var chosen = templates.request({REQUEST_KIND: R.draft, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, TEMPLATE_INDEX: 1, TEMPLATES_REV: 7});
var picked = templates.response(chosen);
assert.strictEqual(picked.records[0].type, 'draft');
assert.strictEqual(text.decodeUtf8(picked.records[1].bytes), 'Позвоню позже, сейчас на встрече');
var outdated = templates.request({REQUEST_KIND: R.draft, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, TEMPLATE_INDEX: 0, TEMPLATES_REV: 6});
assert.strictEqual(templates.response(outdated).code, protocol.result.cursor_lost, 'a template chosen from an older list is refused');

var sending = harness();
var created = draftFrom(sending, 'Привет\r\nс часов', '55');
var draftId = created.records[0].id;
assert.strictEqual(text.decodeUtf8(created.records[1].bytes), 'Привет\nс часов');
var first = sending.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: '55', DRAFT_ID: draftId, ATTEMPT: 0});
primed(sending);
var post = sending.named('sendMessage')[0];
assert.deepStrictEqual(post.args[3], {text: 'Привет\nс часов', reply_to_message_id: '55'});
var key = post.args[4];
assert.ok(/^tbs-/.test(key));
var repeat = sending.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: '55', DRAFT_ID: draftId, ATTEMPT: 0});
assert.strictEqual(sending.named('sendMessage').length, 1, 'a repeated send of the same attempt is not posted twice');
sending.reply('sendMessage', operation('sent'));
[first, repeat].forEach(function(id) {
  var state = sending.response(id).records[0];
  assert.strictEqual(state.type, 'send_state');
  assert.strictEqual(state.state, protocol.send_state.sent);
  assert.strictEqual(state.message, '900');
  assert.strictEqual(state.draftId, draftId);
});
assert.strictEqual(sending.memory['telebezel-sends'], undefined, 'a sent message leaves nothing behind');
var wrong = sending.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: '42', MESSAGE_ID: '55', DRAFT_ID: draftId, ATTEMPT: 1});
assert.strictEqual(sending.response(wrong).code, protocol.result.protocol_error, 'a send aimed elsewhere than the draft is refused');
assert.ok(sending.logs.join('\n').indexOf('Привет') === -1, 'message text never reaches the log');

var lost = harness();
var lostDraft = draftFrom(lost, 'Maybe lost').records[0].id;
var lostSend = lost.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: lostDraft, ATTEMPT: 0});
primed(lost);
lost.reply('sendMessage', failure(0, 'network.unavailable'));
lost.advance(1000);
lost.reply('sendMessage', failure(504, 'response.invalid'));
lost.advance(2000);
var keys = lost.named('sendMessage').map(function(call) { return call.args[4]; });
assert.strictEqual(keys.length, 3);
assert.ok(keys[0] === keys[1] && keys[1] === keys[2], 'every retry of a lost response reuses the idempotency key');
lost.reply('sendMessage', failure(0, 'network.unavailable'));
var unknown = lost.response(lostSend).records[0];
assert.strictEqual(unknown.state, protocol.send_state.unknown);
assert.strictEqual(unknown.code, protocol.result.send_unknown);
var checking = lost.request({REQUEST_KIND: R.send_check, DRAFT_ID: lostDraft});
assert.strictEqual(lost.named('sendMessage')[3].args[4], keys[0], 'checking an answer that never came repeats the same key');
lost.reply('sendMessage', operation('sent'));
assert.strictEqual(lost.response(checking).records[0].state, protocol.send_state.sent);

var refused = harness();
var refusedDraft = draftFrom(refused, 'No rights').records[0].id;
var forbidden = refused.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: refusedDraft, ATTEMPT: 0});
primed(refused);
refused.reply('sendMessage', failure(403, 'auth.insufficient_scope'));
assert.strictEqual(refused.response(forbidden).code, protocol.result.wrong_token_type);
var limited = refused.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: refusedDraft, ATTEMPT: 1});
refused.reply('sendMessage', failure(429, 'message.send_rate_limited', 30));
var limitedState = refused.response(limited).records[0];
assert.deepStrictEqual([limitedState.state, limitedState.code, limitedState.retryAfter, limitedState.flags & protocol.send_flag.retryable],
  [protocol.send_state.failed, protocol.result.send_rate_limited, 30, protocol.send_flag.retryable]);
var denied = refused.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: refusedDraft, ATTEMPT: 2});
refused.reply('sendMessage', operation('failed', {error: {code: 'message.reply_unavailable', retry_after: null}}));
assert.strictEqual(refused.response(denied).records[0].code, protocol.result.reply_unavailable);
var blocked = refused.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: refusedDraft, ATTEMPT: 3});
refused.reply('sendMessage', failure(403, 'message.send_forbidden'));
var blockedState = refused.response(blocked).records[0];
assert.deepStrictEqual([blockedState.state, blockedState.code, blockedState.flags & protocol.send_flag.retryable],
  [protocol.send_state.failed, protocol.result.send_forbidden, 0], 'a refused send is a result, not a token problem');
var hugeDraft = refused.request({REQUEST_KIND: R.draft, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, PAYLOAD: bytes(new Array(1026).join('a'))});
assert.strictEqual(refused.response(hugeDraft).code, protocol.result.text_too_long, 'dictation longer than the watch buffer is refused, not cut');
var gone = refused.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: 99999, ATTEMPT: 0});
assert.strictEqual(refused.response(gone).code, protocol.result.draft_lost);

var later = harness();
var laterDraft = draftFrom(later, 'Late result').records[0].id;
var laterSend = later.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: laterDraft, ATTEMPT: 0});
primed(later);
later.reply('sendMessage', operation('pending'));
assert.strictEqual(later.response(laterSend).records[0].state, protocol.send_state.pending);
var busyDraft = draftFrom(later, 'Second').records[0].id;
later.advance(2000);
var poll = later.named('updates')[1];
assert.deepStrictEqual(poll.args[2], {cursor: 'c0', types: 'send,connection'}, 'tracking reuses the cursor taken before sending');
later.reply('updates', ok({events: [{type: 'send_changed', sequence: 3, operation_id: 'op-1', chat_id: CHAT, message_id: '901', state: 'sent',
  retryable: false, reply_dropped: true, error: null}], cursor: 'c1', has_more: false, status: {connection: 'ready', proxy: false}}));
var pushed = later.pushed();
assert.strictEqual(pushed.length, 1);
assert.deepStrictEqual([pushed[0].type, pushed[0].state, pushed[0].message, pushed[0].flags & protocol.send_flag.reply_dropped],
  ['send_state', protocol.send_state.sent, '901', protocol.send_flag.reply_dropped]);
later.advance(60000);
assert.strictEqual(later.named('updates').length, 2, 'tracking stops once nothing is pending');
assert.ok(busyDraft > laterDraft);

var restart = harness();
var restartDraft = draftFrom(restart, 'Across restart').records[0].id;
restart.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: restartDraft, ATTEMPT: 0});
primed(restart);
restart.reply('sendMessage', operation('pending'));
var stored = JSON.parse(restart.memory['telebezel-sends']);
assert.strictEqual(stored.length, 1);
assert.strictEqual(stored[0].operationId, 'op-1');
var reopened = harness(restart.memory);
var boot = reopened.request({REQUEST_KIND: R.bootstrap});
reopened.reply('preferences', ok({id: 'd', name: 'Watch', locale: 'auto', default_account_id: null, chat_list: 'main'}));
reopened.reply('accounts', ok([]));
var restored = reopened.response(boot).records.filter(function(item) { return item.type === 'pending_send'; });
assert.strictEqual(restored.length, 1);
assert.deepStrictEqual([restored[0].draftId, restored[0].state, restored[0].flags & protocol.send_flag.restored, restored[0].preview],
  [restartDraft, protocol.send_state.pending, protocol.send_flag.restored, 'Across restart']);
reopened.advance(2000);
reopened.reply('updates', ok({events: [], cursor: 'r1', has_more: false, status: {connection: 'ready', proxy: false}}));
assert.strictEqual(reopened.named('sendStatus').length, 1, 'a journal restarted without a cursor is reconciled once through the send status');
reopened.reply('sendStatus', operation('failed', {error: {code: 'message.send_forbidden', retry_after: null}}));
assert.strictEqual(reopened.pushed()[0].code, protocol.result.send_forbidden);
var retry = reopened.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: restartDraft, ATTEMPT: 1});
assert.strictEqual(reopened.named('sendMessage')[0].args[3].text, 'Across restart', 'a restored draft can be sent again as a new attempt');
assert.notStrictEqual(reopened.named('sendMessage')[0].args[4], stored[0].key);
reopened.reply('sendMessage', operation('sent', {id: 'op-2'}));
assert.strictEqual(reopened.response(retry).records[0].state, protocol.send_state.sent);
var discard = reopened.request({REQUEST_KIND: R.draft_discard, DRAFT_ID: restartDraft});
assert.strictEqual(reopened.response(discard).code, protocol.result.ok);
assert.strictEqual(reopened.memory['telebezel-sends'], undefined);

var switching = harness();
var switchDraft = draftFrom(switching, 'Pending').records[0].id;
switching.request({REQUEST_KIND: R.send, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, DRAFT_ID: switchDraft, ATTEMPT: 0});
primed(switching);
switching.reply('sendMessage', operation('pending'));
switching.reader.reset();
assert.ok(switching.memory['telebezel-sends'], 'a reset against the same server keeps tracking');
settings.save(switching.storage, {address: 'other.example:443', ssl: true, token: TOKEN});
switching.reader.reset();
assert.strictEqual(switching.memory['telebezel-sends'], undefined, 'another server forgets pending sends');

process.stdout.write('PKJS compose tests passed\n');
