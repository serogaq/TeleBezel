'use strict';
var assert = require('assert');
var protocol = require('../src/pkjs/lib/protocol.generated');
var settings = require('../src/pkjs/lib/settings');
var runtime = require('../src/pkjs/lib/runtime');
var transportFactory = require('../src/pkjs/lib/transport');

function makeHarness() {
  var events = {};
  var messages = [];
  var handled = [];
  var resets = 0;
  var inbox = 0;
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
  var reader = {handle: function(payload) { handled.push(payload); }, reset: function() { resets++; }, setInboxSize: function(size) { inbox = size; }};
  var instance = runtime.create({Pebble: pebble, Clay: Clay, storage: storage, protocol: protocol, settings: settings,
    transport: transportFactory.create(pebble), reader: reader,
    configPage: {build: function() { return []; }}, localization: {resolve: function() { return {}; }},
    locales: {en: {}}, getLocale: function() { return 'en'; }});
  instance.register();
  return {events: events, messages: messages, handled: handled, storage: storage, urls: urls, instance: instance,
    resets: function() { return resets; }, inbox: function() { return inbox; },
    submit: function(values) { submitted = values; },
    failNext: function() { failNext = true; }};
}

var h = makeHarness();
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.hello, REQUEST_SEQ: 1, INBOX_SIZE: 4096}});
assert.strictEqual(h.messages.length, 0, 'hello before PKJS ready must wait');
assert.strictEqual(h.inbox(), 4096);
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.bootstrap, REQUEST_SEQ: 2}});
assert.strictEqual(h.handled.length, 0, 'requests before ready are ignored');
h.events.ready();
assert.strictEqual(h.messages[0].RESPONSE_KIND, protocol.response.ready);
assert.strictEqual(h.messages[0].REQUEST_SEQ, 1);
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.bootstrap, REQUEST_SEQ: 3}});
assert.strictEqual(h.handled[0].REQUEST_SEQ, 3);
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.bootstrap, REQUEST_SEQ: 0}});
h.events.appmessage({payload: {REQUEST_KIND: protocol.request.bootstrap, REQUEST_SEQ: 2147483648}});
assert.strictEqual(h.handled.length, 1, 'invalid sequences are ignored');
h.failNext();
h.events.showConfiguration();
h.events.webviewclosed({response: 'saved'});
assert.strictEqual(h.resets(), 1, 'a settings save resets reads');
var refreshes = h.messages.filter(function(message) { return message.RESPONSE_KIND === protocol.response.refresh; });
assert.strictEqual(refreshes.length, 2, 'a failed refresh delivery is retried');
assert.strictEqual(JSON.parse(h.storage.getItem('clay-settings')).CONFIG_TOKEN, undefined, 'clay storage kept the token');
h.events.showConfiguration();
assert.strictEqual(h.urls[h.urls.length - 1].indexOf('tb_new'), -1, 'the settings page url carried the token');
assert.strictEqual(settings.load(h.storage).token, 'tb_new', 'the token was not saved');

var invalid = makeHarness();
invalid.events.ready();
invalid.submit({CONFIG_ADDRESS: 'https://bad/path', CONFIG_SSL: true, CONFIG_TOKEN: 'tb_kept'});
invalid.events.showConfiguration();
invalid.events.webviewclosed({response: 'saved'});
assert.strictEqual(settings.load(invalid.storage).token, 'tb_kept', 'an invalid address discarded the token');
invalid.events.webviewclosed({response: ''});
assert.strictEqual(invalid.resets(), 1, 'a cancelled settings page must not reset reads');

process.stdout.write('PKJS lifecycle tests passed\n');
