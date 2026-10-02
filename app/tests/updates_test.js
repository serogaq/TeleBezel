'use strict';
var assert = require('assert');
var updates = require('../src/pkjs/lib/updates');

function fakeApi() {
  var calls = [];
  return {
    calls: calls,
    updates: function(value, account, params, callback) {
      var call = {value: value, account: account, params: params, callback: callback, aborted: false};
      calls.push(call);
      return {abort: function() { call.aborted = true; }};
    }
  };
}

var api = fakeApi();
var feed = updates.create({api: api});
var delivered = [];
feed.poll({server: 'old'}, 'a', function(outcome) { delivered.push(['old', outcome.events[0]]); });
feed.reset();
assert.ok(api.calls[0].aborted, 'reset aborts the request in flight');
feed.poll({server: 'new'}, 'a', function(outcome) { delivered.push(['new', outcome.events[0]]); });
assert.strictEqual(api.calls[1].params.cursor, null);
api.calls[0].callback({ok: true, data: {cursor: 'old-cursor', events: ['old-event']}});
assert.deepStrictEqual(delivered, [], 'a response from before reset reaches no callback');
assert.strictEqual(feed.primed('a'), false, 'a response from before reset leaves no cursor');
api.calls[1].callback({ok: true, data: {cursor: 'new-cursor', events: ['new-event']}});
assert.deepStrictEqual(delivered, [['new', 'new-event']]);
feed.poll({server: 'new'}, 'a', function() {});
assert.strictEqual(api.calls[2].params.cursor, 'new-cursor');

api = fakeApi();
feed = updates.create({api: api});
delivered = [];
feed.poll({}, 'a', function(outcome) { delivered.push(['first', outcome.events.length]); });
feed.poll({}, 'a', function(outcome) { delivered.push(['second', outcome.events.length]); });
assert.strictEqual(api.calls.length, 1, 'polls for one account share a request');
api.calls[0].callback({ok: true, data: {cursor: 'c1', events: [1, 2]}});
assert.deepStrictEqual(delivered, [['first', 2], ['second', 2]]);

api = fakeApi();
feed = updates.create({api: api});
var resynced = null;
feed.poll({}, 'a', function() {});
api.calls[0].callback({ok: true, data: {cursor: 'c1', events: []}});
feed.poll({}, 'a', function(outcome) { resynced = outcome.resynced; });
api.calls[1].callback({ok: false, action: 'resync'});
assert.strictEqual(api.calls[2].params.cursor, null, 'a lost cursor is retried without it');
feed.reset();
api.calls[2].callback({ok: true, data: {cursor: 'stale', events: []}});
assert.strictEqual(resynced, null, 'a retry from before reset is dropped too');
assert.strictEqual(feed.primed('a'), false);

process.stdout.write('PKJS updates tests passed\n');
