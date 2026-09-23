'use strict';
var assert = require('assert');
var text = require('../src/pkjs/lib/text');

function round(value, limit, options) {
  var encoded = text.encode(value, limit === undefined ? 4096 : limit, options);
  return {value: text.decodeUtf8(encoded.bytes), bytes: encoded.bytes.length, truncated: encoded.truncated};
}

assert.strictEqual(round('Привет, мир').value, 'Привет, мир');
assert.strictEqual(round('Привет, мир').bytes, Buffer.byteLength('Привет, мир'));
assert.strictEqual(round('a\r\nb\rc').value, 'a\nb\nc');
assert.strictEqual(round('a\n\n\n\n\nb').value, 'a\n\nb');
assert.strictEqual(round('  x  ').value, 'x');
assert.strictEqual(round('tab\there').value, 'tab here');
assert.strictEqual(round('zero​width‮shift﻿').value, 'zerowidthshift');
assert.strictEqual(round('line\nbreak', 4096, {singleLine: true}).value, 'line break');
assert.strictEqual(round('a   b', 4096, {singleLine: true}).value, 'a b');
assert.strictEqual(round('\uD83D\uDE00').value, '\uD83D\uDE00', 'a supported emoji is kept');
assert.strictEqual(round('❤️').value, '❤', 'the emoji variation selector is dropped');
assert.strictEqual(round('\uD83D\uDC4D\uD83C\uDFFD').value, text.PLACEHOLDER, 'a skin tone sequence becomes one placeholder');
assert.strictEqual(round('\uD83D\uDC68‍\uD83D\uDC69‍\uD83D\uDC67').value, text.PLACEHOLDER, 'a ZWJ family becomes one placeholder');
assert.strictEqual(round('\uD83C\uDDF7\uD83C\uDDFA').value, text.PLACEHOLDER, 'a flag becomes one placeholder');
assert.strictEqual(round('1️⃣').value, '1', 'a keycap keeps its digit');
assert.strictEqual(round('\uD83E\uDD84').value, text.PLACEHOLDER);
assert.strictEqual(round('é').value, 'é', 'composed by NFC');
assert.strictEqual(round('\ud800x').value, text.PLACEHOLDER + 'x', 'a lone surrogate is replaced');
assert.strictEqual(round(null).value, '');

var long = new Array(5000).join('ж');
var cut = round(long, 100);
assert.ok(cut.truncated);
assert.ok(cut.bytes <= 100);
assert.strictEqual(cut.value.slice(-1), '…');
assert.strictEqual(cut.value.slice(0, -1), new Array(cut.value.length).join('ж'));
var tight = round('\uD83D\uDE00\uD83D\uDE00\uD83D\uDE00', 9);
assert.ok(tight.bytes <= 9 && tight.truncated && tight.value === '\uD83D\uDE00…');
assert.strictEqual(round('abc', 2).value, '');
for (var limit = 0; limit < 40; ++limit) {
  var sample = round('Сообщение \uD83D\uDE00 text', limit);
  assert.ok(sample.bytes <= limit, 'limit ' + limit);
  assert.strictEqual(text.decodeUtf8(text.encode(sample.value, 4096).bytes), sample.value);
}

var bytes = text.encode('ab\uD83D\uDE00cd', 100).bytes;
text.splitUtf8(bytes, 3).forEach(function(part) {
  assert.ok(part.length <= 4);
  assert.notStrictEqual(part[0] & 0xC0, 0x80, 'a split started inside a code point');
});
assert.strictEqual(text.splitUtf8(bytes, 3).reduce(function(sum, part) { return sum + part.length; }, 0), bytes.length);

var telegramMax = new Array(5000).join('\uD83D\uDE00');
var encoded = text.encode(telegramMax, 16384);
assert.ok(encoded.bytes.length <= 16384 && encoded.truncated);
