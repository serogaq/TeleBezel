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
var imageDump = path.join(root, 'build', 'image_dump');
childProcess.execFileSync(process.env.CC || 'cc', ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Isrc/c', 'tests/codec_dump.c', 'src/c/codec.c', 'src/c/text.c', '-o', dump], {cwd: root, stdio: 'inherit'});
childProcess.execFileSync(process.env.CC || 'cc', ['-std=c99', '-Wall', '-Wextra', '-Werror', '-Isrc/c', 'tests/image_dump.c', 'src/c/image.c', '-o', imageDump], {cwd: root, stdio: 'inherit'});

var CHAT = '-1009007199254740993';
var CHANNEL = '-1001234567890';
var ROUND = [4, 1, 4, 1, 1, 6, 0, 112];
var RECT = [200, 0, 228, 0, 0, 6, 0, 88];

function hex(bytes) { return Buffer.from(bytes).toString('hex'); }

function watchRecords(response) {
  var lines = response.chunks.filter(function(chunk) { return chunk.PAYLOAD; }).map(function(chunk) { return hex(chunk.PAYLOAD); }).join('\n');
  var output = childProcess.execFileSync(dump, [], {input: lines + '\n'}).toString().trim();
  var records = output ? output.split('\n').map(function(line) { return JSON.parse(line); }) : [];
  records.forEach(function(record) { assert.ok(!record.error, 'the watch could not decode ' + JSON.stringify(record)); });
  return records;
}

function image(response) {
  var bytes = [];
  response.records.forEach(function(record) {
    if (record.type === 'media_data') {
      assert.strictEqual(record.offset, bytes.length, 'media data arrived out of order');
      bytes = bytes.concat(record.bytes);
    }
  });
  return JSON.parse(childProcess.execFileSync(imageDump, [], {input: hex(bytes) + '\n'}).toString());
}

function harness(address, token) {
  var memory = {};
  var storage = {getItem: function(key) { return Object.prototype.hasOwnProperty.call(memory, key) ? memory[key] : null; },
    setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
  settings.save(storage, {address: address, ssl: false, token: token, photoMode: 'manual'});
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
  function send(payload) {
    var current = ++sequence;
    payload.REQUEST_SEQ = current;
    events.appmessage({payload: payload});
    return current;
  }
  function wait(current, timeout) {
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
  return {send: send, wait: wait, request: function(payload, timeout) { return wait(send(payload), timeout); }, messages: messages};
}

function mediaRequest(message, spec, extra) {
  var payload = {REQUEST_KIND: protocol.request.media, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: CHAT, MESSAGE_ID: message, MEDIA_INDEX: 0, MEDIA_SPEC: spec,
    MEDIA_OFFSET: 0, MEDIA_TAG: 0};
  Object.keys(extra || {}).forEach(function(key) { payload[key] = extra[key]; });
  return payload;
}

function run() {
  var mock = mockApi.create();
  return new Promise(function(resolve) { mock.listen(0, resolve); }).then(function(port) {
    var h = harness('127.0.0.1:' + port, mockApi.TOKEN);
    var R = protocol.request;
    function mediaCalls() { return mock.log.filter(function(entry) { return /\/media$/.test(entry.path); }).length; }
    return h.request({REQUEST_KIND: R.hello, INBOX_SIZE: 4096}, 1000).catch(function() { return null; }).then(function() {
      return h.request({REQUEST_KIND: R.bootstrap});
    }).then(function(boot) {
      assert.strictEqual(watchRecords(boot)[0].photoMode, protocol.photo_mode.manual);
      return h.request({REQUEST_KIND: R.history, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: CHAT, PAGE_OP: protocol.page_op.first, PAGE_LIMIT: 20, TEXT_LIMIT: 200});
    }).then(function(history) {
      var records = watchRecords(history);
      function byId(id) { return records.filter(function(record) { return Buffer.from(record.id, 'hex').toString() === id; })[0]; }
      var flags = protocol.media_flag;
      assert.strictEqual(byId('69').media, flags.image);
      var album = byId('57');
      assert.ok(album && album.mediaCount === 3 && (album.media & flags.album), 'the album is one row with three photos');
      assert.ok(!byId('56') && !byId('58'), 'album members are folded into the captioned post');
      assert.strictEqual(byId('54').media, flags.image | flags.spoiler);
      assert.strictEqual(byId('53').media, flags.restricted);
      assert.strictEqual(byId('52').media, flags.image);
      assert.ok(byId('51').mediaCount === 3 && byId('51').media === (flags.image | flags.album), 'a link preview carousel is one row with three items');
      return h.request(mediaRequest('69', ROUND));
    }).then(function(response) {
      assert.strictEqual(response.code, protocol.result.ok);
      var info = watchRecords(response)[0];
      assert.deepStrictEqual([info.type, info.state, info.count], ['media_info', protocol.media_state.ready, 1]);
      var decoded = image(response);
      assert.deepStrictEqual([decoded.status, decoded.width, decoded.height, decoded.shape, decoded.tag], [2, 260, 195, 1, info.tag]);
      assert.ok(decoded.size <= 28672 && decoded.consumed === info.total);
      assert.ok(response.chunks.every(function(chunk) { return !chunk.PAYLOAD || chunk.PAYLOAD.length <= 4096 - 96; }));
      var before = mediaCalls();
      return h.request(mediaRequest('69', ROUND)).then(function(again) {
        assert.strictEqual(mediaCalls(), before, 'reopening the photo is served from the phone cache');
        assert.deepStrictEqual(image(again), decoded);
        return h.request(mediaRequest('69', ROUND, {MEDIA_OFFSET: 500, MEDIA_TAG: info.tag}));
      }).then(function(resumed) {
        assert.strictEqual(resumed.records[1].offset, 500, 'a resumed transfer continues from the offset');
      });
    }).then(function() {
      return h.request(mediaRequest('69', RECT));
    }).then(function(rect) {
      var decoded = image(rect);
      assert.deepStrictEqual([decoded.status, decoded.width, decoded.height, decoded.shape], [2, 200, 150, 0]);
      return h.request(mediaRequest('57', ROUND, {MEDIA_INDEX: 2}));
    }).then(function(third) {
      var info = watchRecords(third)[0];
      assert.deepStrictEqual([info.index, info.count, Buffer.from(info.item, 'hex').toString()], [2, 3, '58']);
      assert.strictEqual(image(third).height, 260, 'a portrait photo fills the round screen height');
      return h.request(mediaRequest('51', ROUND, {MEDIA_INDEX: 2}));
    }).then(function(carousel) {
      var info = watchRecords(carousel)[0];
      assert.deepStrictEqual([info.index, info.count, Buffer.from(info.item, 'hex').toString()], [2, 3, '51'], 'carousel items live in one message');
      assert.strictEqual(image(carousel).status, 2);
      return h.request(mediaRequest('54', ROUND));
    }).then(function(spoiler) {
      assert.strictEqual(watchRecords(spoiler)[0].state, protocol.media_state.spoiler);
      assert.strictEqual(spoiler.records.length, 1, 'a hidden spoiler sends no image');
      var revealed = ROUND.slice();
      revealed[4] |= 0x80;
      return h.request(mediaRequest('54', revealed));
    }).then(function(revealed) {
      assert.strictEqual(image(revealed).status, 2);
      return h.request(mediaRequest('53', ROUND));
    }).then(function(restricted) {
      assert.strictEqual(watchRecords(restricted)[0].state, protocol.media_state.restricted);
      return h.request(mediaRequest('404', ROUND));
    }).then(function(missing) {
      assert.strictEqual(missing.code, protocol.result.message_unavailable);
      var started = h.send(mediaRequest('52', ROUND));
      return h.request({REQUEST_KIND: R.media_cancel}).then(function(cancelled) {
        assert.strictEqual(cancelled.code, protocol.result.ok);
        return new Promise(function(resolve) { setTimeout(resolve, 1500); }).then(function() {
          assert.strictEqual(collect(protocol, h.messages, started), null, 'a cancelled request sends nothing');
        });
      });
    }).then(function() {
      return h.request({REQUEST_KIND: R.history, ACCOUNT_ID: mockApi.FIRST, ENTITY_ID: CHANNEL, PAGE_OP: protocol.page_op.first, PAGE_LIMIT: 4, TEXT_LIMIT: 200});
    }).then(function(channel) {
      var posts = watchRecords(channel);
      assert.ok(posts.every(function(post) { return post.media & protocol.media_flag.channel_post; }));
      var signed = posts.filter(function(post) { return Buffer.from(post.signature, 'hex').toString() === 'Редактор'; });
      assert.ok(signed.length > 0, 'channel posts carry the author signature');
    }).then(function() {
      return new Promise(function(resolve) { mock.close(resolve); });
    }, function(error) {
      return new Promise(function(resolve) { mock.close(resolve); }).then(function() { throw error; });
    });
  });
}

run().then(function() {
  process.stdout.write('E2E media path passed\n');
}).catch(function(error) {
  process.stderr.write((error && error.stack) || String(error));
  process.stderr.write('\n');
  process.exit(1);
});
