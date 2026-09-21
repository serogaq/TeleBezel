'use strict';
var assert = require('assert');
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var runtime = require('../src/pkjs/lib/runtime');

function makeHarness() {
  var events = {};
  var messages = [];
  var calls = [];
  var failNext = false;
  var memory = {};
  var storage = {getItem: function(key) { return memory[key] || null; }, setItem: function(key, value) { memory[key] = value; }, removeItem: function(key) { delete memory[key]; }};
  var pebble = {
    addEventListener: function(name, callback) { events[name] = callback; },
    sendAppMessage: function(message, success, failure) {
      messages.push(message);
      if (failNext) { failNext = false; failure(); } else { success(); }
    },
    openURL: function() {}
  };
  function Clay() {}
  Clay.prototype.setSettings = function() {};
  Clay.prototype.generateUrl = function() { return 'https://config.test'; };
  Clay.prototype.getSettings = function() { return {CONFIG_ADDRESS: 'api.test:443', CONFIG_SSL: true, CONFIG_TOKEN: 'tb_new'}; };
  runtime.create({Pebble: pebble, Clay: Clay, storage: storage, protocol: protocol, settings: settings,
    api: {checkStatus: function(value, callback) { calls.push({value: value, callback: callback}); }},
    configPage: {build: function() { return []; }}, localization: {resolve: function() { return {}; }},
    locales: {en: {}}, getLocale: function() { return 'en'; }}).register();
  return {events: events, messages: messages, calls: calls, storage: storage,
    failNext: function() { failNext = true; }};
}

var h = makeHarness();
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.hello, REQUEST_SEQ: 1}});
assert.strictEqual(h.messages.length, 0, 'hello before PKJS ready must wait');
h.events.ready();
assert.strictEqual(h.messages[0].RESPONSE_KIND, protocol.response.ready);
assert.strictEqual(h.messages[0].REQUEST_SEQ, 1);
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.status, REQUEST_SEQ: 2}});
assert.strictEqual(h.messages[1].RESULT_CODE, protocol.result.config_missing);
h.events.showConfiguration();
h.events.webviewclosed({response: 'saved'});
assert.strictEqual(h.messages[2].RESPONSE_KIND, protocol.response.refresh);
assert.strictEqual(h.calls.length, 1, 'valid save must recheck');
h.calls[0].callback({code: protocol.result.ok});
assert.strictEqual(h.messages[3].RESULT_CODE, protocol.result.ok);
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.status, REQUEST_SEQ: 3}});
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.status, REQUEST_SEQ: 4}});
h.calls[1].callback({code: protocol.result.backend_unavailable});
assert.strictEqual(h.messages.length, 4, 'late old result must not reach the watch');
h.failNext();
h.calls[2].callback({code: protocol.result.ok});
assert.strictEqual(h.messages.length, 6, 'failed watch delivery retries once');
assert.strictEqual(h.messages[5].REQUEST_SEQ, 4);

process.stdout.write('PKJS lifecycle tests passed\n');
