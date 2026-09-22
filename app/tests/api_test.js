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

function status(code, body) {
  var value = null;
  apiFactory.create(scripted(code, body), settings, protocol).checkStatus(configured, function(result) { value = result.code; });
  return value;
}
assert.strictEqual(status(200, '{"data":{"status":"ready"}}'), protocol.result.ok);
assert.strictEqual(status(401, '{"error":{"code":"auth.unauthorized"}}'), protocol.result.api_unauthorized);
assert.strictEqual(status(503, '{"error":{"code":"service.tdlib_unavailable"}}'), protocol.result.backend_not_ready);
assert.strictEqual(status(200, 'not json'), protocol.result.protocol_error);
assert.strictEqual(status(null), protocol.result.backend_unavailable);

process.stdout.write('PKJS API tests passed\n');
