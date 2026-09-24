'use strict';
var assert = require('assert');
var childProcess = require('child_process');
var path = require('path');
var protocol = require('../../src/pkjs/lib/protocol.generated');
var settings = require('../../src/pkjs/lib/settings');
var text = require('../../src/pkjs/lib/text');
var codec = require('../../src/pkjs/lib/codec').create(protocol);
var apiFactory = require('../../src/pkjs/lib/api');
var transportFactory = require('../../src/pkjs/lib/transport');
var leasesFactory = require('../../src/pkjs/lib/leases');
var readerFactory = require('../../src/pkjs/lib/reader');
var runtimeFactory = require('../../src/pkjs/lib/runtime');
var XMLHttpRequest = require('../helpers/xhr');
var decode = require('../helpers/decode');
var mockApi = require('./mock_api');

var root = path.resolve(__dirname, '../..');
var dump = path.join(root, 'build', 'codec_dump');
childProcess.execFileSync(process.env.CC || 'cc', ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Isrc/c', 'tests/codec_dump.c', 'src/c/codec.c', 'src/c/text.c', '-o', dump], {cwd: root, stdio: 'inherit'});
var R = protocol.request;
var CHAT = '-1009007199254740993';

function watchDecode(payloads) {
  var lines = payloads.map(function(bytes) { return Buffer.from(bytes).toString('hex'); }).join('\n');
  var output = childProcess.execFileSync(dump, [], {input: lines + '\n'}).toString().trim();
  var records = output ? output.split('\n').map(function(line) { return JSON.parse(line); }) : [];
  records.forEach(function(record) { assert.ok(!record.error, 'the watch could not decode ' + JSON.stringify(record)); });
  return records;
}

function harness(address, memory) {
  var storage = {getItem: function(key) { return Object.prototype.hasOwnProperty.call(memory, key) ? memory[key] : null; },
    setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
  settings.save(storage, {address: address, ssl: false, token: mockApi.TOKEN});
  var events = {};
  var messages = [];
  var logs = [];
  var pebble = {
    addEventListener: function(name, callback) { events[name] = callback; },
    sendAppMessage: function(message, success) { messages.push(message); setImmediate(success); },
    openURL: function() {}
  };
  var transport = transportFactory.create(pebble);
  var log = function(line) { logs.push(line); };
  var reader = readerFactory.create({api: apiFactory.create(XMLHttpRequest, settings, protocol, Date.now, log), settings: settings, storage: storage,
    protocol: protocol, codec: codec, text: text, transport: transport, leases: leasesFactory.create(storage), log: log});
  runtimeFactory.create({Pebble: pebble, Clay: function() {}, storage: storage, protocol: protocol, settings: settings, transport: transport,
    reader: reader, configPage: {build: function() { return []; }}, localization: {resolve: function() { return {}; }}, locales: {en: {}},
    getLocale: function() { return 'en'; }}).register();
  events.ready();
  var sequence = 0;
  function request(payload, timeout) {
    var current = ++sequence;
    payload.REQUEST_SEQ = current;
    events.appmessage({payload: payload});
    return waitFor(function() {
      var response = decode.collect(protocol, messages, current);
      return response && response.chunks.length === response.chunks[0].CHUNK_TOTAL ? response : null;
    }, timeout || 30000, 'request ' + current);
  }
  function pushed() {
    return messages.filter(function(message) { return message.RESPONSE_KIND === protocol.response.push; });
  }
  return {request: request, pushed: pushed, logs: logs, messages: messages};
}

function waitFor(check, timeout, label) {
  return new Promise(function(resolve, reject) {
    var started = Date.now();
    (function poll() {
      var value = check();
      if (value) { resolve(value); return; }
      if (Date.now() - started > timeout) { reject(new Error('timeout waiting for ' + label)); return; }
      setTimeout(poll, 20);
    })();
  });
}

function consistent(response) {
  var watch = watchDecode(response.chunks.filter(function(chunk) { return chunk.PAYLOAD; }).map(function(chunk) { return chunk.PAYLOAD; }));
  assert.strictEqual(watch.length, response.records.length, 'the watch decoder saw a different number of records');
  watch.forEach(function(record, index) { assert.strictEqual(record.type, response.records[index].type); });
  return watch;
}

function state(response) {
  assert.strictEqual(response.code, protocol.result.ok, 'send answered ' + response.code);
  var watch = consistent(response);
  assert.strictEqual(watch.length, 1);
  assert.strictEqual(watch[0].type, 'send_state');
  return response.records[0];
}

function draft(h, payload) {
  payload.REQUEST_KIND = R.draft;
  payload.ACCOUNT_ID = mockApi.FIRST;
  payload.ENTITY_ID = payload.ENTITY_ID || CHAT;
  payload.TEXT_LIMIT = 512;
  return h.request(payload).then(function(response) {
    assert.strictEqual(response.code, protocol.result.ok);
    consistent(response);
    return response.records[0].id;
  });
}

function send(h, id, attempt, reply) {
  return h.request({REQUEST_KIND: R.send, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: CHAT, MESSAGE_ID: reply || '', DRAFT_ID: id, ATTEMPT: attempt || 0});
}

function run() {
  var mock = mockApi.create();
  var memory = {};
  return new Promise(function(resolve) { mock.listen(0, resolve); }).then(function(port) {
    var address = '127.0.0.1:' + port;
    var h = harness(address, memory);
    var templateRevision = 0;
    return h.request({REQUEST_KIND: R.bootstrap}).then(function(boot) {
      assert.strictEqual(boot.code, protocol.result.ok);
      return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.FIRST, LIST: 0, PAGE_OP: 0, PAGE_LIMIT: 10, TEXT_LIMIT: 64});
    }).then(function(chats) {
      var watch = consistent(chats).filter(function(record) { return record.type === 'chat'; });
      assert.strictEqual(watch[0].send, protocol.can_send.allowed);
      assert.strictEqual(watch[3].send, protocol.can_send.read_only, 'a channel is not writable');
      return h.request({REQUEST_KIND: R.history, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: CHAT, PAGE_OP: 0, PAGE_LIMIT: 20, TEXT_LIMIT: 120});
    }).then(function(history) {
      consistent(history);
      var byId = {};
      history.records.forEach(function(record) { byId[record.id] = record; });
      assert.ok(byId['63'].flags & protocol.message_flag.reply);
      assert.deepStrictEqual([byId['63'].replyId, byId['63'].replySender], ['62', 'Ада']);
      assert.ok(byId['60'].flags & protocol.message_flag.failed);
      assert.ok(byId['55'].flags & protocol.message_flag.pending);
      return h.request({REQUEST_KIND: R.templates, PAGE_OP: 0, TEXT_LIMIT: 48});
    }).then(function(listing) {
      var watch = consistent(listing);
      assert.deepStrictEqual(watch.map(function(record) { return record.type; }), ['templates', 'template', 'template']);
      templateRevision = listing.records[0].revision;
      return draft(h, {TEMPLATE_INDEX: 1, TEMPLATES_REV: templateRevision});
    }).then(function(id) {
      return send(h, id).then(function(response) {
        var sent = state(response);
        assert.strictEqual(sent.state, protocol.send_state.sent);
        assert.strictEqual(mock.sends()[0].text, 'Перезвоню позже');
        return send(h, id);
      });
    }).then(function(repeated) {
      assert.strictEqual(state(repeated).state, protocol.send_state.sent);
      assert.strictEqual(mock.sends().length, 1, 'a repeated send reached the API twice');
      mock.nextSend('drop');
      var spoken = Array.prototype.slice.call(Buffer.from('Ответ голосом\nвторая строка', 'utf8'));
      return draft(h, {PAYLOAD: spoken, MESSAGE_ID: '62'});
    }).then(function(id) {
      return send(h, id, 0, '62');
    }).then(function(lost) {
      var recovered = state(lost);
      assert.strictEqual(recovered.state, protocol.send_state.sent, 'a lost response was not recovered with the same key');
      var posts = mock.log.filter(function(entry) { return entry.method === 'POST'; });
      assert.strictEqual(posts.length, 3, 'the lost response was not retried');
      assert.strictEqual(mock.sends().length, 2, 'retrying a lost response created a second message');
      assert.strictEqual(mock.sends()[1].reply, '62');
      assert.strictEqual(mock.sends()[1].text, 'Ответ голосом\nвторая строка');
      mock.nextSend({status: 403, code: 'auth.insufficient_scope'});
      return draft(h, {TEMPLATE_INDEX: 0, TEMPLATES_REV: templateRevision});
    }).then(function(id) {
      return send(h, id).then(function(forbidden) {
        assert.strictEqual(forbidden.code, protocol.result.wrong_token_type);
        mock.nextSend({status: 429, code: 'message.send_rate_limited', headers: {'Retry-After': '17'}});
        return send(h, id, 1);
      }).then(function(limited) {
        var failed = state(limited);
        assert.deepStrictEqual([failed.state, failed.code, failed.retryAfter], [protocol.send_state.failed, protocol.result.send_rate_limited, 17]);
        mock.nextSend('reply_unavailable');
        return send(h, id, 2);
      });
    }).then(function(refused) {
      assert.strictEqual(state(refused).code, protocol.result.reply_unavailable);
      mock.nextSend('pending');
      return draft(h, {TEMPLATE_INDEX: 0, TEMPLATES_REV: templateRevision});
    }).then(function(id) {
      return send(h, id).then(function(pending) {
        assert.strictEqual(state(pending).state, protocol.send_state.pending);
        var operation = mock.sends()[mock.sends().length - 1];
        mock.settle(operation.id, 'sent');
        return waitFor(function() { return h.pushed().length ? h.pushed() : null; }, 8000, 'the late result');
      }).then(function(pushes) {
        var watch = watchDecode(pushes.map(function(message) { return message.PAYLOAD; }));
        assert.strictEqual(watch.length, 1);
        assert.deepStrictEqual([watch[0].type, watch[0].draftId, watch[0].state], ['send_state', id, protocol.send_state.sent]);
        assert.strictEqual(pushes[0].REQUEST_SEQ, 0);
        mock.nextSend('pending');
        return draft(h, {PAYLOAD: Array.prototype.slice.call(Buffer.from('Переживёт перезапуск', 'utf8'))});
      });
    }).then(function(id) {
      return send(h, id).then(function(pending) {
        assert.strictEqual(state(pending).state, protocol.send_state.pending);
        var restarted = harness(address, memory);
        return restarted.request({REQUEST_KIND: R.bootstrap}).then(function(boot) {
          var watch = consistent(boot).filter(function(record) { return record.type === 'pending_send' && record.draftId === id; });
          assert.strictEqual(watch.length, 1, 'a pending send was not offered after a restart');
          assert.strictEqual(watch[0].state, protocol.send_state.pending);
          mock.settle(mock.sends()[mock.sends().length - 1].id, 'failed', {code: 'message.send_forbidden', retry_after: null});
          return waitFor(function() {
            var sends = restarted.pushed();
            return sends.length ? sends : null;
          }, 8000, 'the result after restart');
        }).then(function(pushes) {
          var watch = watchDecode(pushes.map(function(message) { return message.PAYLOAD; }));
          assert.deepStrictEqual([watch[0].state, watch[0].code], [protocol.send_state.failed, protocol.result.send_forbidden]);
          var logged = h.logs.concat(restarted.logs).join('\n');
          ['Перезвоню', 'Ответ голосом', 'Переживёт', mockApi.TOKEN].forEach(function(secret) {
            assert.ok(logged.indexOf(secret) === -1, 'the log contains ' + secret);
          });
        });
      });
    }).then(function() {
      return new Promise(function(resolve) { mock.close(resolve); });
    });
  });
}

run().then(function() {
  process.stdout.write('PKJS to API end-to-end send path passed\n');
}, function(error) {
  process.stderr.write(error.stack + '\n');
  process.exit(1);
});
