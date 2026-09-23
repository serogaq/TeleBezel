'use strict';
var assert = require('assert');
var transport = require('../src/pkjs/lib/transport');

function fakePebble() {
  var pending = [];
  return {
    sent: [],
    pending: pending,
    sendAppMessage: function(message, success, failure) { this.sent.push(message); pending.push({success: success, failure: failure}); },
    ack: function() { pending.shift().success(); },
    nack: function() { pending.shift().failure(); }
  };
}

var pebble = fakePebble();
var queue = transport.create(pebble);
queue.send({id: 1}, 7);
queue.send({id: 2}, 7);
queue.send({id: 3}, 8);
assert.strictEqual(pebble.sent.length, 1, 'only one AppMessage may be in flight');
pebble.ack();
assert.strictEqual(pebble.sent[1].id, 2);
pebble.nack();
assert.strictEqual(pebble.sent[2].id, 2, 'a rejected chunk is retried');
pebble.nack();
assert.strictEqual(pebble.sent[3].id, 2);
pebble.nack();
assert.strictEqual(pebble.sent[4].id, 3, 'after the retry budget the stream is dropped');
pebble.ack();
assert.strictEqual(queue.pending(), 0);

queue.send({id: 4}, 9);
queue.send({id: 5}, 9);
queue.send({id: 6}, 10);
queue.cancel(9);
pebble.ack();
assert.strictEqual(pebble.sent[pebble.sent.length - 1].id, 6, 'a cancelled stream keeps only its in-flight chunk');
pebble.ack();
assert.strictEqual(queue.pending(), 0);
