'use strict';
var assert = require('assert');
var protocol = require('../src/pkjs/lib/protocol.generated');
var codec = require('../src/pkjs/lib/codec').create(protocol);
var decode = require('./helpers/decode').decode;
var fixtures = require('./fixtures/codec.json');

fixtures.forEach(function(fixture) {
  var bytes = fixture.record === 'text' ? codec.text(fixture.input.bytes) : codec[fixture.record](fixture.input, fixture.textLimit);
  assert.strictEqual(Buffer.from(bytes).toString('hex'), fixture.hex, fixture.name);
  var decoded = decode(protocol, bytes)[0];
  Object.keys(fixture.decoded).forEach(function(key) {
    assert.deepStrictEqual(decoded[key], fixture.decoded[key], fixture.name + ' ' + key);
  });
});

var records = [[1, 2, 0, 9, 9], [1, 3, 0, 7, 7, 7], [1, 1, 0, 5]];
var chunks = codec.pack(records, 8);
assert.deepStrictEqual(chunks, [[1, 2, 0, 9, 9], [1, 3, 0, 7, 7, 7], [1, 1, 0, 5]]);
assert.deepStrictEqual(codec.pack([], 100), [[]]);
var big = codec.pack([[1, 5, 0, 1, 2, 3, 4, 5], [1, 0, 0]], 100);
assert.strictEqual(big.length, 1);
var huge = new Array(300).join('я');
var chat = decode(protocol, codec.chat({id: '-1001234567890123', title: huge, type: 2, flags: 0, unread: 70000, lastDate: 1700000000,
  previewKind: 0, previewAction: 0, previewDuration: 0, previewSender: huge, previewExtra: '', previewText: huge}, 50))[0];
assert.strictEqual(chat.unread, 65535);
assert.ok(Buffer.byteLength(chat.title) <= 64 && Buffer.byteLength(chat.previewSender) <= 32 && Buffer.byteLength(chat.previewText) <= 50);
assert.strictEqual(chat.id, '-1001234567890123');
