'use strict';
var assert = require('assert');
var path = require('path');
var fs = require('fs');
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var text = require('../src/pkjs/lib/text');
var codecFactory = require('../src/pkjs/lib/codec');
var leasesFactory = require('../src/pkjs/lib/leases');
var transportFactory = require('../src/pkjs/lib/transport');
var readerFactory = require('../src/pkjs/lib/reader');
var collect = require('./helpers/decode').collect;

var directory = path.resolve(__dirname, '../../tests/contracts/read-api');
function fixture(name) { return JSON.parse(fs.readFileSync(path.join(directory, name + '.json'), 'utf8')); }

var memory = {};
var storage = {getItem: function(key) { return memory[key] || null; }, setItem: function(key, value) { memory[key] = String(value); }, removeItem: function(key) { delete memory[key]; }};
settings.save(storage, {address: 'api.test:443', ssl: true, token: 'tb_' + new Array(44).join('a')});
var messages = [];
var responses = {chats: fixture('chats'), history: fixture('history')};
function answer(name) {
  return function() {
    var callback = arguments[arguments.length - 1];
    callback({ok: true, status: 200, data: responses[name].data});
    return {abort: function() {}};
  };
}
var reader = readerFactory.create({api: {chats: answer('chats'), history: answer('history'), releaseInterest: function() { return {abort: function() {}}; }},
  settings: settings, storage: storage, protocol: protocol, codec: codecFactory.create(protocol), text: text,
  transport: transportFactory.create({sendAppMessage: function(message, success) { messages.push(message); success(); }}),
  leases: leasesFactory.create(storage)});
var account = '00112233-4455-4677-8899-aabbccddeeff';
reader.handle({REQUEST_KIND: protocol.request.chats, REQUEST_SEQ: 1, ACCOUNT_ID: account, LIST: 0, PAGE_OP: 0});
var chats = collect(protocol, messages, 1);
assert.strictEqual(chats.code, protocol.result.ok);
assert.strictEqual(chats.records[0].id, responses.chats.data.items[0].id);
assert.strictEqual(chats.records[0].title, responses.chats.data.items[0].title);
assert.strictEqual(chats.records[0].unread, responses.chats.data.items[0].unread_count);
assert.strictEqual(chats.records[0].previewSender, responses.chats.data.items[0].last_message.sender.name);
reader.handle({REQUEST_KIND: protocol.request.history, REQUEST_SEQ: 2, ACCOUNT_ID: account, ENTITY_ID: responses.history.data.items[0].chat_id, PAGE_OP: 0});
var history = collect(protocol, messages, 2);
assert.strictEqual(history.code, protocol.result.ok);
assert.strictEqual(history.records.length, responses.history.data.items.length);
responses.history.data.items.forEach(function(item, index) {
  assert.strictEqual(history.records[index].id, item.id);
  assert.strictEqual(history.records[index].kind, protocol.kind[item.content.kind]);
  assert.strictEqual(history.records[index].flags & protocol.message_flag.outgoing, item.is_outgoing ? protocol.message_flag.outgoing : 0);
});
process.stdout.write('PKJS contract fixture tests passed\n');
