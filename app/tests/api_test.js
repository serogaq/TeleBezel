'use strict';
var assert = require('assert');
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var apiFactory = require('../src/pkjs/lib/api');

var configured = {address: 'api.test:443', ssl: true, token: 'tb_token'};
function scripted(status, body, headers) {
  function Xhr() { this.readyState = 0; }
  Xhr.prototype.open = function() {};
  Xhr.prototype.setRequestHeader = function() {};
  Xhr.prototype.getResponseHeader = function(name) { return headers && headers[name] !== undefined ? headers[name] : null; };
  Xhr.prototype.send = function() {
    if (status === null) { this.onerror(); return; }
    this.status = status;
    this.responseText = body;
    this.readyState = 4;
    this.onreadystatechange();
  };
  return Xhr;
}
function run(status, body, headers) {
  var result = null;
  apiFactory.create(scripted(status, body, headers), settings, protocol, function() { return 0; })
    .request(configured, 'GET', '/v1/telegram/accounts', function(value) { result = value; });
  return result;
}

var resync = run(409, '{"error":{"code":"sync.resync_required"}}');
assert.deepStrictEqual([resync.ok, resync.code, resync.action], [false, 'sync.resync_required', 'resync']);
var busy = run(503, '{"error":{"code":"service.busy"}}', {'Retry-After': '7'});
assert.deepStrictEqual([busy.action, busy.retryAfter], ['retry', 7]);
var dated = run(429, '{"error":{"code":"rate_limit.exceeded"}}', {'Retry-After': new Date(30000).toUTCString()});
assert.deepStrictEqual([dated.action, dated.retryAfter], ['retry', 30]);
var limited = run(429, '{"error":{"code":"interest.limit_reached"}}');
assert.strictEqual(limited.retryAfter, 5);
assert.strictEqual(run(404, '{"error":{"code":"message.cache_miss"}}').action, 'fallback');
var unauthorized = run(401, '{"error":{"code":"auth.unauthorized"}}');
assert.strictEqual(unauthorized.action, 'reauth');
var offline = run(null);
assert.deepStrictEqual([offline.ok, offline.status, offline.code, offline.action], [false, 0, 'network.unavailable', 'retry']);
var html = run(502, '<html>Bad gateway</html>');
assert.deepStrictEqual([html.code, html.action], ['response.invalid', 'retry']);
var ok = run(200, '{"data":{"items":[]}}');
assert.deepStrictEqual([ok.ok, ok.data], [true, {items: []}]);
assert.strictEqual(run(200, '<html></html>').code, 'response.invalid');

function recording(status, body) {
  var seen = [];
  function Xhr() { this.readyState = 0; this.headers = {}; seen.push(this); }
  Xhr.prototype.open = function(method, url) { this.method = method; this.url = url; };
  Xhr.prototype.setRequestHeader = function(name, value) { this.headers[name] = value; };
  Xhr.prototype.getResponseHeader = function() { return null; };
  Xhr.prototype.abort = function() { this.aborted = true; };
  Xhr.prototype.send = function(payload) {
    this.body = payload;
    if (status === undefined) { return; }
    this.status = status;
    this.responseText = body;
    this.readyState = 4;
    this.onreadystatechange();
  };
  return {Xhr: Xhr, seen: seen};
}
var chatId = '-1009007199254740993';
var recorded = recording(200, '{"data":{"items":[]}}');
var client = apiFactory.create(recorded.Xhr, settings, protocol);
client.chats(configured, '00112233-4455-4677-8899-aabbccddeeff', {list: 'archive', limit: 20, cursor: 'a+b/c='}, function() {});
assert.strictEqual(recorded.seen[0].url, 'https://api.test:443/v1/telegram/accounts/00112233-4455-4677-8899-aabbccddeeff/chats?list=archive&limit=20&cursor=a%2Bb%2Fc%3D');
client.history(configured, '00112233-4455-4677-8899-aabbccddeeff', chatId, {view_id: 'v', limit: 5, cursor: null}, function() {});
assert.strictEqual(recorded.seen[1].url, 'https://api.test:443/v1/telegram/accounts/00112233-4455-4677-8899-aabbccddeeff/chats/-1009007199254740993/messages?view_id=v&limit=5');
client.updatePreferences(configured, {default_account_id: null}, function() {});
assert.deepStrictEqual([recorded.seen[2].method, recorded.seen[2].body, recorded.seen[2].headers['Content-Type']], ['PUT', '{"default_account_id":null}', 'application/json']);
client.accounts(configured, function() {});
assert.ok(/\/v1\/telegram\/accounts\?per_page=50$/.test(recorded.seen[3].url));
var silent = recording();
var calls = 0;
var handle = apiFactory.create(silent.Xhr, settings, protocol).releaseInterest(configured, '00112233-4455-4677-8899-aabbccddeeff', chatId, 'v', function() { calls++; });
assert.strictEqual(silent.seen[0].method, 'DELETE');
handle.abort();
assert.ok(silent.seen[0].aborted);
silent.seen[0].onerror();
assert.strictEqual(calls, 0, 'an aborted request reported a result');
var parsedIds = run(200, '{"data":{"items":[{"id":"9223372036854775807"}]}}');
assert.strictEqual(parsedIds.data.items[0].id, '9223372036854775807');

process.stdout.write('PKJS API tests passed\n');
