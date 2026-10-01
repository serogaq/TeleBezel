'use strict';
var assert = require('assert');
var Buffer = require('buffer').Buffer;
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var text = require('../src/pkjs/lib/text');
var codecFactory = require('../src/pkjs/lib/codec');
var media = require('../src/pkjs/lib/media');
var leasesFactory = require('../src/pkjs/lib/leases');
var transportFactory = require('../src/pkjs/lib/transport');
var readerFactory = require('../src/pkjs/lib/reader');
var decode = require('./helpers/decode');
var tbi = require('./helpers/tbi');

var ACCOUNT = '00112233-4455-4677-8899-aabbccddeeff';
var CHAT = '-1009007199254740993';
var SPEC = [4, 1, 4, 1, 1, 6, 0, 112];

[[], [1], [1, 2], [1, 2, 3], [255, 0, 128, 7, 9]].forEach(function(bytes) {
  assert.deepStrictEqual(media.decodeBase64(Buffer.from(bytes).toString('base64')), Array.prototype.slice.call(bytes));
});
assert.strictEqual(media.decodeBase64('abc'), null);
assert.strictEqual(media.decodeBase64('ab=c'), null);
assert.strictEqual(media.decodeBase64('a$bc'), null);
assert.strictEqual(media.decodeBase64(null), null);

var spec = media.parseSpec(protocol, SPEC);
assert.deepStrictEqual(spec, {width: 260, height: 260, shape: 'round', budget: 28672, formats: 'p4,p2', reveal: false});
assert.strictEqual(media.parseSpec(protocol, [200, 0, 228, 0, 0x80, 4, 0, 16]).reveal, true);
assert.strictEqual(media.parseSpec(protocol, [4, 2, 4, 1, 1, 6, 0, 112]), null, 'wider than any supported watch');
assert.strictEqual(media.parseSpec(protocol, [4, 1, 4, 1, 1, 0, 0, 112]), null, 'no formats');
assert.strictEqual(media.parseSpec(protocol, [4, 1, 4, 1, 1, 6, 0, 200]), null, 'budget over the watch limit');
assert.strictEqual(media.parseSpec(protocol, [4, 1, 4, 1, 5, 6, 0, 112]), null, 'unknown shape');
assert.strictEqual(media.parseSpec(protocol, 'x'), null);

assert.strictEqual(media.albumKey('13579'), media.albumKey('13579'));
assert.notStrictEqual(media.albumKey('13579'), media.albumKey('13578'));
assert.strictEqual(media.albumKey('0x1'), 0);
assert.strictEqual(media.albumKey(null), 0);

function post(id, extra) {
  var content = {kind: 'photo', media: {type: 'photo', width: 1280, height: 960, has_spoiler: false, album_id: null, restriction: null}};
  Object.keys(extra || {}).forEach(function(key) { content.media[key] = extra[key]; });
  if (extra && extra.text) { content.text = extra.text; }
  return {id: id, content: content, is_channel_post: true, author_signature: 'Editor'};
}
var flags = protocol.media_flag;
assert.strictEqual(media.flags(protocol, post('1')), flags.image | flags.channel_post);
assert.strictEqual(media.flags(protocol, post('1', {has_spoiler: true, album_id: '77'})), flags.image | flags.channel_post | flags.spoiler | flags.album);
assert.strictEqual(media.flags(protocol, post('1', {restriction: 'self_destruct'})), flags.restricted | flags.channel_post);
assert.strictEqual(media.flags(protocol, post('1', {type: 'none'})), flags.channel_post);
assert.strictEqual(media.flags(protocol, {id: '1', content: {kind: 'text', text: 'x'}}), 0);
var carousel = {id: '5', content: {kind: 'text', text: 'https://t.me/news/1', media: {type: 'photo', count: 3, album_id: null}}};
assert.strictEqual(media.flags(protocol, carousel), flags.image | flags.album, 'a link preview carousel is browsed like an album');
assert.strictEqual(media.group([carousel])[0].count, 3, 'items inside one message are counted');
assert.strictEqual(media.group([post('4', {count: 40})])[0].count, 10, 'at most ten items');
assert.strictEqual(media.flags(protocol, {id: '6', content: {kind: 'paid_media', media: {type: 'photo', count: 2, restriction: 'paid'}}}),
  flags.restricted | flags.album);

var grouped = media.group([post('9'), post('8', {album_id: '77'}), post('7', {album_id: '77', text: 'caption', has_spoiler: true}), post('6', {album_id: '77'}),
  post('5', {album_id: '78'}), post('4')]);
assert.deepStrictEqual(grouped.map(function(entry) { return [entry.message.id, entry.count, entry.spoiler]; }),
  [['9', 1, false], ['7', 3, true], ['5', 1, false], ['4', 1, false]]);
assert.strictEqual(media.group([post('3', {album_id: '1'}), post('2', {album_id: '1'})])[0].message.id, '2', 'without a caption the oldest item stands for the album');

var cache = media.createCache(2);
cache.put({key: 'a', value: 1});
cache.put({key: 'b', value: 2});
assert.strictEqual(cache.get('a').value, 1);
cache.put({key: 'c', value: 3});
assert.strictEqual(cache.get('b'), null, 'least recently used entry is evicted');
assert.strictEqual(cache.size(), 2);
cache.clear();
assert.strictEqual(cache.size(), 0);

var codec = codecFactory.create(protocol);
var image = tbi.sample({width: 200, height: 228, tag: 99}).bytes;
var info = {state: protocol.media_state.ready, index: 0, count: 1, flags: 0, tag: 99, total: image.length, retryAfter: 0, item: '42'};
var parts = media.chunks(codec, info, image, 0, 400);
var joined = [];
parts.forEach(function(payload, index) {
  assert.ok(payload.length <= 400);
  decode.decode(protocol, payload).forEach(function(record, position) {
    if (record.type === 'media_info') { assert.ok(index === 0 && position === 0); }
    if (record.type === 'media_data') {
      assert.strictEqual(record.offset, joined.length);
      joined = joined.concat(record.bytes);
    }
  });
});
assert.deepStrictEqual(joined, image);
var resumed = media.chunks(codec, info, image, 1000, 400);
assert.strictEqual(decode.decode(protocol, resumed[0])[1].offset, 1000);
assert.strictEqual(media.chunks(codec, info, [], 0, 400).length, 1);

function harness() {
  var memory = {};
  var storage = {getItem: function(key) { return Object.prototype.hasOwnProperty.call(memory, key) ? memory[key] : null; }, setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
  settings.save(storage, {address: 'tg.example:443', ssl: true, token: 'tb_' + new Array(44).join('a')});
  var messages = [];
  var held = [];
  var pebble = {hold: false, sendAppMessage: function(message, success) {
    messages.push(message);
    if (this.hold) { held.push(success); } else { success(); }
  }};
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
  var fakeApi = {media: api('media'), preferences: api('preferences'), accounts: api('accounts'), releaseInterest: api('releaseInterest')};
  var transport = transportFactory.create(pebble);
  var reader = readerFactory.create({api: fakeApi, settings: settings, storage: storage, protocol: protocol, codec: codec, text: text, transport: transport,
    leases: leasesFactory.create(storage, function() { return 0.5; }), now: function() { return clock.now; },
    setTimeout: function(callback, delay) { var timer = {callback: callback, at: clock.now + delay}; clock.timers.push(timer); return timer; },
    clearTimeout: function(timer) { clock.timers = clock.timers.filter(function(item) { return item !== timer; }); }});
  reader.setInboxSize(4096);
  return {
    reader: reader, calls: calls, messages: messages, pebble: pebble, held: held, transport: transport,
    request: function(payload) { reader.handle(payload); },
    last: function() { return calls[calls.length - 1]; },
    advance: function(ms) {
      clock.now += ms;
      var due = clock.timers.filter(function(timer) { return timer.at <= clock.now; });
      clock.timers = clock.timers.filter(function(timer) { return timer.at > clock.now; });
      due.forEach(function(timer) { timer.callback(); });
    },
    response: function(sequence) { return decode.collect(protocol, messages, sequence); }
  };
}

function request(sequence, extra) {
  var payload = {REQUEST_KIND: protocol.request.media, REQUEST_SEQ: sequence, ACCOUNT_ID: ACCOUNT, ENTITY_ID: CHAT, MESSAGE_ID: '42', MEDIA_INDEX: 0,
    MEDIA_SPEC: SPEC.slice(), MEDIA_OFFSET: 0, MEDIA_TAG: 0};
  Object.keys(extra || {}).forEach(function(key) { payload[key] = extra[key]; });
  return payload;
}

function ready(bytes, tag) {
  return {ok: true, status: 200, data: {state: 'ready', retry_after: null, index: 0, count: 2, item_message_id: '42', has_spoiler: false,
    rendition: {tag: tag, width: 260, height: 260, shape: 'round', format: 'p4', crc32: 1, bytes_base64: Buffer.from(bytes).toString('base64')}}};
}

function bytesOf(response) {
  var joined = [];
  response.records.forEach(function(record) { if (record.type === 'media_data') { joined = joined.concat(record.bytes); } });
  return joined;
}

var round = tbi.sample({width: 260, height: 260, canvasWidth: 260, canvasHeight: 260, shape: 1, tag: 5}).bytes;
var h = harness();
h.request(request(3));
var call = h.last();
assert.strictEqual(call.name, 'media');
assert.deepStrictEqual(call.args.slice(1, 4), [ACCOUNT, CHAT, '42']);
assert.deepStrictEqual(call.args[4], {index: 0, width: 260, height: 260, shape: 'round', budget: 28672, formats: 'p4,p2', reveal: 0});
call.callback({ok: true, status: 200, data: {state: 'downloading', retry_after: 1, index: 0, count: 2, item_message_id: '42'}});
assert.strictEqual(h.response(3), null, 'a short download is awaited on the phone');
h.advance(1000);
assert.strictEqual(h.calls.filter(function(item) { return item.name === 'media'; }).length, 2);
h.last().callback(ready(round, 5));
var response = h.response(3);
assert.ok(response.complete);
var head = response.records[0];
assert.strictEqual(head.type, 'media_info');
assert.deepStrictEqual([head.state, head.count, head.tag, head.total, head.item], [protocol.media_state.ready, 2, 5, round.length, '42']);
assert.deepStrictEqual(bytesOf(response), round);
assert.ok(response.chunks.every(function(chunk) { return chunk.PAYLOAD.length <= 4000; }));

var before = h.calls.length;
h.request(request(4, {MEDIA_OFFSET: 500, MEDIA_TAG: 5}));
assert.strictEqual(h.calls.length, before, 'a resumed transfer is served from the phone cache');
response = h.response(4);
assert.strictEqual(response.records[1].offset, 500);
assert.deepStrictEqual(bytesOf(response), round.slice(500));
h.request(request(5, {MEDIA_OFFSET: 500, MEDIA_TAG: 6}));
assert.strictEqual(h.response(5).records[1].offset, 0, 'a different tag starts from the beginning');

h.request(request(6, {MEDIA_INDEX: 1}));
h.last().callback({ok: true, status: 200, data: {state: 'preparing', retry_after: 3, index: 1, count: 2, item_message_id: '43'}});
h.advance(3000);
h.last().callback({ok: true, status: 200, data: {state: 'preparing', retry_after: 3, index: 1, count: 2, item_message_id: '43'}});
response = h.response(6);
assert.strictEqual(response.records.length, 1);
assert.deepStrictEqual([response.records[0].state, response.records[0].retryAfter, response.records[0].item], [protocol.media_state.preparing, 3, '43']);

h.request(request(7, {MEDIA_INDEX: 1}));
h.last().callback({ok: true, status: 200, data: {state: 'restricted', index: 1, count: 2, item_message_id: '43'}});
assert.strictEqual(h.response(7).records[0].state, protocol.media_state.restricted);

h.request(request(8, {MEDIA_INDEX: 1}));
h.last().callback({ok: false, status: 409, code: 'authorization.invalid_state', retryAfter: null, action: 'show', data: null});
assert.strictEqual(h.response(8).code, protocol.result.account_needs_login);

h.request(request(9, {MEDIA_INDEX: 1}));
h.last().callback({ok: true, status: 200, data: {state: 'ready', index: 1, count: 2, item_message_id: '43', rendition: {tag: 1, bytes_base64: '!!'}}});
assert.strictEqual(h.response(9).code, protocol.result.protocol_error);

h.request(request(10, {MEDIA_SPEC: [1, 2, 3]}));
assert.strictEqual(h.response(10).code, protocol.result.protocol_error);
h.request(request(11, {MESSAGE_ID: '../../etc'}));
assert.strictEqual(h.response(11).code, protocol.result.protocol_error);

var slow = harness();
slow.reader.setInboxSize(600);
slow.pebble.hold = true;
slow.request(request(20));
slow.last().callback(ready(round, 5));
assert.strictEqual(slow.messages.length, 1, 'one chunk is in flight');
slow.transport.send({RESPONSE_KIND: protocol.response.push, REQUEST_SEQ: 0, PAYLOAD: [1]}, 0);
slow.held.shift()();
assert.strictEqual(slow.messages[1].RESPONSE_KIND, protocol.response.push, 'a send status push does not wait for the whole image');
slow.held.shift()();
slow.request({REQUEST_KIND: protocol.request.media_cancel, REQUEST_SEQ: 21});
slow.held.shift()();
assert.deepStrictEqual(slow.messages.map(function(message) { return message.REQUEST_SEQ; }), [20, 0, 20, 21], 'cancel drops queued image chunks');
slow.held.shift()();
assert.strictEqual(slow.transport.pending(), 0);

var aborted = harness();
aborted.request(request(30));
var first = aborted.last();
aborted.request(request(31, {MEDIA_INDEX: 1}));
assert.ok(first.aborted, 'a newer media request aborts the older download');
aborted.reader.reset();
assert.ok(aborted.last().aborted, 'a settings change aborts the media request');
aborted.request(request(32));
assert.strictEqual(aborted.calls.filter(function(item) { return item.name === 'media'; }).length, 3, 'the cache is cleared with the settings');
var arrayLike = {length: 8};
SPEC.forEach(function(value, index) { arrayLike[index] = value; });
var normalized = require('../src/pkjs/lib/message').normalize({MEDIA_SPEC: arrayLike, PAYLOAD: {length: 2, 0: 65, 1: 66}});
assert.deepStrictEqual(normalized.MEDIA_SPEC, SPEC, 'byte arrays from the emulator bridge are array-like objects');
assert.deepStrictEqual(normalized.PAYLOAD, [65, 66]);
process.stdout.write('PKJS media tests passed\n');
