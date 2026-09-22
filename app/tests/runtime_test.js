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
  var submitted = {CONFIG_ADDRESS: 'api.test:443', CONFIG_SSL: true, CONFIG_TOKEN: 'tb_new'};
  var urls = [];
  function clayStore() { return JSON.parse(storage.getItem('clay-settings') || '{}'); }
  function Clay() {}
  Clay.prototype.setSettings = function(values) {
    var stored = clayStore();
    Object.keys(values).forEach(function(key) { stored[key] = values[key]; });
    storage.setItem('clay-settings', JSON.stringify(stored));
  };
  Clay.prototype.generateUrl = function() {
    var url = 'data:text/html,' + encodeURIComponent(JSON.stringify(clayStore()));
    urls.push(url);
    return url;
  };
  Clay.prototype.getSettings = function() {
    storage.setItem('clay-settings', JSON.stringify(submitted));
    return submitted;
  };
  var instance = runtime.create({Pebble: pebble, Clay: Clay, storage: storage, protocol: protocol, settings: settings,
    api: {checkStatus: function(value, callback) { calls.push({value: value, callback: callback}); }},
    configPage: {build: function() { return []; }}, localization: {resolve: function() { return {}; }},
    locales: {en: {}}, getLocale: function() { return 'en'; }});
  instance.register();
  return {events: events, messages: messages, calls: calls, storage: storage, urls: urls, instance: instance,
    submit: function(values) { submitted = values; },
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

assert.strictEqual(JSON.parse(h.storage.getItem('clay-settings')).CONFIG_TOKEN, undefined, 'clay storage kept the token');
h.events.showConfiguration();
assert.strictEqual(h.urls[h.urls.length - 1].indexOf('tb_new'), -1, 'the settings page url carried the token');
assert.strictEqual(settings.load(h.storage).token, 'tb_new', 'the token was not saved');

var invalid = makeHarness();
invalid.events.ready();
invalid.events.appmessage({payload: {REQUEST_KIND: protocol.request.hello, REQUEST_SEQ: 1, INBOX_SIZE: 8200}});
assert.strictEqual(invalid.instance.inboxSize(), 8200);
invalid.events.appmessage({payload: {REQUEST_KIND: protocol.request.status, REQUEST_SEQ: 2}});
invalid.submit({CONFIG_ADDRESS: 'https://bad/path', CONFIG_SSL: true, CONFIG_TOKEN: 'tb_kept'});
invalid.events.showConfiguration();
invalid.events.webviewclosed({response: 'saved'});
assert.strictEqual(settings.load(invalid.storage).token, 'tb_kept', 'an invalid address discarded the token');
var last = invalid.messages[invalid.messages.length - 1];
assert.deepStrictEqual([last.RESPONSE_KIND, last.REQUEST_SEQ, last.RESULT_CODE], [protocol.response.status, 2, protocol.result.config_invalid]);
assert.strictEqual(invalid.messages[invalid.messages.length - 2].RESPONSE_KIND, protocol.response.refresh);

process.stdout.write('PKJS lifecycle tests passed\n');
