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
var collect = require('../helpers/decode').collect;
var mockApi = require('./mock_api');

var root = path.resolve(__dirname, '../..');
var dump = path.join(root, 'build', 'codec_dump');
childProcess.execFileSync(process.env.CC || 'cc', ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Isrc/c', 'tests/codec_dump.c', 'src/c/codec.c', 'src/c/text.c', '-o', dump], {cwd: root, stdio: 'inherit'});

function hex(bytes) { return Buffer.from(bytes).toString('hex'); }
function utf8(value) { return Buffer.from(value, 'hex').toString('utf8'); }

function watchDecode(response) {
  var lines = response.chunks.filter(function(chunk) { return chunk.PAYLOAD; }).map(function(chunk) { return hex(chunk.PAYLOAD); }).join('\n');
  var output = childProcess.execFileSync(dump, [], {input: lines + '\n'}).toString().trim();
  return output ? output.split('\n').map(function(line) { return JSON.parse(line); }) : [];
}

function harness(address, token) {
  var memory = {};
  var storage = {getItem: function(key) { return Object.prototype.hasOwnProperty.call(memory, key) ? memory[key] : null; },
    setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
  settings.save(storage, {address: address, ssl: false, token: token});
  var events = {};
  var messages = [];
  var pebble = {
    addEventListener: function(name, callback) { events[name] = callback; },
    sendAppMessage: function(message, success) { messages.push(message); setImmediate(success); },
    openURL: function() {}
  };
  var transport = transportFactory.create(pebble);
  var reader = readerFactory.create({api: apiFactory.create(XMLHttpRequest, settings, protocol), settings: settings, storage: storage,
    protocol: protocol, codec: codec, text: text, transport: transport, leases: leasesFactory.create(storage)});
  runtimeFactory.create({Pebble: pebble, Clay: function() {}, storage: storage, protocol: protocol, settings: settings, transport: transport,
    reader: reader, configPage: {build: function() { return []; }}, localization: {resolve: function() { return {}; }}, locales: {en: {}},
    getLocale: function() { return 'en'; }}).register();
  events.ready();
  var sequence = 0;
  function request(payload, timeout) {
    var current = ++sequence;
    payload.REQUEST_SEQ = current;
    events.appmessage({payload: payload});
    return new Promise(function(resolve, reject) {
      var started = Date.now();
      (function poll() {
        var response = collect(protocol, messages, current);
        if (response && response.chunks.length === response.chunks[0].CHUNK_TOTAL) { resolve(response); return; }
        if (Date.now() - started > (timeout || 20000)) { reject(new Error('timeout for request ' + current)); return; }
        setTimeout(poll, 10);
      })();
    });
  }
  request.hello = function() { return request({REQUEST_KIND: protocol.request.hello, INBOX_SIZE: 2048}); };
  return {request: request, messages: messages, storage: storage};
}

function consistent(response) {
  var watch = watchDecode(response);
  assert.strictEqual(watch.length, response.records.length, 'the watch decoder saw a different number of records');
  watch.forEach(function(record, index) {
    var expected = response.records[index];
    assert.ok(!record.error, 'watch decoding failed: ' + JSON.stringify(record));
    assert.strictEqual(record.type, expected.type);
    if (record.id !== undefined) { assert.strictEqual(utf8(record.id), expected.id); }
    if (record.validId !== undefined) { assert.ok(record.validId, 'the watch rejected id ' + expected.id); }
    if (record.text !== undefined) { assert.strictEqual(utf8(record.text), expected.text); }
    if (record.previewText !== undefined) { assert.strictEqual(utf8(record.previewText), expected.previewText); }
  });
  response.chunks.forEach(function(chunk) { assert.ok(!chunk.PAYLOAD || chunk.PAYLOAD.length <= 2048 - 96, 'a chunk exceeded the inbox budget'); });
  return watch;
}

function run() {
  var mock = mockApi.create();
  return new Promise(function(resolve) { mock.listen(0, resolve); }).then(function(port) {
    var h = harness('127.0.0.1:' + port, mockApi.TOKEN);
    var R = protocol.request;
    var OP = protocol.page_op;
    return h.request({REQUEST_KIND: R.hello, INBOX_SIZE: 2048}, 1000).catch(function() { return null; }).then(function() {
      return h.request({REQUEST_KIND: R.bootstrap});
    }).then(function(boot) {
      assert.strictEqual(boot.code, protocol.result.ok);
      consistent(boot);
      var accounts = boot.records.filter(function(record) { return record.type === 'account'; });
      assert.deepStrictEqual(accounts.map(function(account) { return account.state; }), [protocol.account_state.ready, protocol.account_state.needs_login]);
      var chats = [];
      function page(op) {
        return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.FIRST, LIST: protocol.list.main, PAGE_OP: op, PAGE_LIMIT: 10, TEXT_LIMIT: 64}).then(function(response) {
          assert.strictEqual(response.code, protocol.result.ok);
          var watch = consistent(response);
          assert.strictEqual(watch[0].type, 'summary', 'a chat page did not start with the summary');
          assert.deepStrictEqual([watch[0].connection, watch[0].unreadChats, watch[0].unreadMessages], [protocol.connection.ready, 16, 25]);
          chats = chats.concat(response.records.filter(function(record) { return record.type === 'chat'; }));
          return response.flags & protocol.flag.has_more ? page(OP.next) : null;
        });
      }
      return page(OP.first).then(function() {
        assert.strictEqual(chats.length, 26);
        assert.strictEqual(chats[0].id, '-1009007199254740993', 'a 64-bit chat id lost precision');
        assert.strictEqual(chats[1].flags & protocol.chat_flag.saved, protocol.chat_flag.saved, 'saved messages lost their flag');
        assert.strictEqual(chats[2].flags & protocol.chat_flag.saved, 0);
        assert.strictEqual(chats[2].previewKind, protocol.kind.voice_note);
        return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.FIRST, LIST: protocol.list.archive, PAGE_OP: OP.first, PAGE_LIMIT: 10, TEXT_LIMIT: 64});
      });
    }).then(function(archive) {
      assert.strictEqual(archive.records.filter(function(record) { return record.type === 'chat'; }).length, 2);
      var history = [];
      function older(op) {
        return h.request({REQUEST_KIND: R.history, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: '-1009007199254740993', PAGE_OP: op, PAGE_LIMIT: 20, TEXT_LIMIT: 200}).then(function(response) {
          assert.strictEqual(response.code, protocol.result.ok);
          consistent(response);
          history = history.concat(response.records);
          if (response.flags & protocol.flag.has_more) { return older(OP.next); }
          assert.ok(response.flags & protocol.flag.local_exhausted);
          return null;
        });
      }
      return older(OP.first).then(function() {
        assert.strictEqual(history.length, 70);
        assert.deepStrictEqual(history.slice(0, 3).map(function(record) { return record.id; }), ['70', '69', '68']);
        assert.ok(history[0].flags & protocol.message_flag.truncated, 'the long message was not marked truncated');
        assert.strictEqual(history[1].text, 'Подпись к фото');
        return h.request({REQUEST_KIND: R.message, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: '-1009007199254740993', MESSAGE_ID: '70', TEXT_LIMIT: 3072});
      });
    }).then(function(full) {
      var watch = consistent(full);
      var bytes = Buffer.concat(watch.map(function(record) { return Buffer.from(record.bytes, 'hex'); }));
      assert.ok(bytes.length <= 3072 && full.chunks.length > 1);
      assert.strictEqual(bytes.toString('utf8'), text.decodeUtf8(text.encode(mockApi.LONG_TEXT, 3072).bytes));
      return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.SECOND, LIST: protocol.list.main, PAGE_OP: OP.first, PAGE_LIMIT: 10, TEXT_LIMIT: 64});
    }).then(function(login) {
      assert.strictEqual(login.code, protocol.result.account_needs_login);
      mock.fail('/v1/telegram/accounts/' + mockApi.FIRST + '/chats', 503, 'service.busy', true, {'Retry-After': '1'});
      return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.FIRST, LIST: protocol.list.main, PAGE_OP: OP.first, PAGE_LIMIT: 10, TEXT_LIMIT: 64});
    }).then(function(recovered) {
      assert.strictEqual(recovered.code, protocol.result.ok, 'a transient busy answer was not retried');
      mock.fail('/v1/telegram/accounts/' + mockApi.FIRST + '/chats', 429, 'rate_limit.exceeded', true, {'Retry-After': '30'});
      return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.FIRST, LIST: protocol.list.main, PAGE_OP: OP.first, PAGE_LIMIT: 10, TEXT_LIMIT: 64});
    }).then(function(limited) {
      assert.deepStrictEqual([limited.code, limited.chunks[0].RETRY_AFTER], [protocol.result.rate_limited, 30]);
      var first = h.request({REQUEST_KIND: R.history, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: '42', PAGE_OP: OP.first, PAGE_LIMIT: 5, TEXT_LIMIT: 64}, 1500);
      var second = h.request({REQUEST_KIND: R.history, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: '43', PAGE_OP: OP.first, PAGE_LIMIT: 5, TEXT_LIMIT: 64});
      return Promise.all([first.then(function() { return 'delivered'; }, function() { return 'superseded'; }), second]);
    }).then(function(results) {
      assert.strictEqual(results[0], 'superseded', 'a superseded chat response reached the watch');
      assert.strictEqual(results[1].code, protocol.result.ok);
      assert.ok(mock.log.some(function(entry) { return entry.method === 'DELETE' && /\/chats\/42\/interests\//.test(entry.path); }), 'the previous lease was not released');
      h.storage.setItem(settings.STORAGE_KEY, JSON.stringify({address: '127.0.0.1:' + port, ssl: false, token: 'tb_' + new Array(44).join('x')}));
      return h.request({REQUEST_KIND: R.bootstrap});
    }).then(function(revoked) {
      assert.strictEqual(revoked.code, protocol.result.api_unauthorized);
      assert.ok(mock.log.every(function(entry) { return JSON.stringify(entry).indexOf(mockApi.TOKEN) === -1; }));
      return new Promise(function(resolve) { mock.close(resolve); });
    });
  }).then(connectionSequence);
}

function connectionSequence() {
  var mock = mockApi.create({connection: ['connecting', 'updating', 'ready'], proxy: true});
  return new Promise(function(resolve) { mock.listen(0, resolve); }).then(function(port) {
    var h = harness('127.0.0.1:' + port, mockApi.TOKEN);
    var R = protocol.request;
    function chats() {
      return h.request({REQUEST_KIND: R.chats, ACCOUNT_ID: mockApi.FIRST, LIST: protocol.list.main, PAGE_OP: protocol.page_op.first, PAGE_LIMIT: 5, TEXT_LIMIT: 64});
    }
    function events() { return h.request({REQUEST_KIND: R.events, ACCOUNT_ID: mockApi.FIRST}); }
    function status(response) {
      var watch = consistent(response);
      assert.strictEqual(watch.length, 1);
      assert.strictEqual(watch[0].type, 'status');
      return watch[0];
    }
    return h.request({REQUEST_KIND: R.hello, INBOX_SIZE: 2048}, 1000).catch(function() { return null; }).then(chats).then(function(first) {
      var watch = consistent(first);
      assert.deepStrictEqual([watch[0].type, watch[0].connection, watch[0].proxy], ['summary', protocol.connection.connecting, 0],
        'the proxy is unknown until the first status');
      return events();
    }).then(function(updating) {
      assert.deepStrictEqual(status(updating), {type: 'status', connection: protocol.connection.updating, proxy: 1});
      return events();
    }).then(function(ready) {
      assert.deepStrictEqual(status(ready), {type: 'status', connection: protocol.connection.ready, proxy: 1});
      var polls = mock.log.filter(function(entry) { return /\/updates$/.test(entry.path); });
      assert.deepStrictEqual(polls.map(function(entry) { return entry.query.cursor || null; }), [null, 'u1'], 'the events cursor was not carried forward');
      return chats();
    }).then(function(reloaded) {
      var watch = consistent(reloaded);
      assert.deepStrictEqual([watch[0].connection, watch[0].proxy], [protocol.connection.ready, 1]);
      return new Promise(function(resolve) { mock.close(resolve); });
    });
  });
}

run().then(function() {
  process.stdout.write('PKJS to API end-to-end read path passed\n');
}, function(error) {
  process.stderr.write(error.stack + '\n');
  process.exit(1);
});
