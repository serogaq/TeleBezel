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
  var apiCalls = [];
  function call(name) { return function() { var args = Array.prototype.slice.call(arguments); apiCalls.push({name: name, args: args, callback: args[args.length - 1]}); }; }
  var api = {preferences: call('preferences'), accounts: call('accounts'), updatePreferences: call('updatePreferences')};
  var pages = [];
  var timers = [];
  var instance = runtime.create({Pebble: pebble, Clay: Clay, storage: storage, protocol: protocol, settings: settings,
    transport: transportFactory.create(pebble), reader: reader, api: api,
    setTimeout: function(callback) { var timer = {callback: callback}; timers.push(timer); return timer; },
    clearTimeout: function(timer) { timer.cleared = true; },
    configPage: {build: function(_strings, state) { pages.push(state); return []; }}, localization: {resolve: function() { return {}; }},
    locales: {en: {}}, getLocale: function() { return 'en'; }});
  instance.register();
  return {events: events, messages: messages, handled: handled, storage: storage, urls: urls, instance: instance, apiCalls: apiCalls, pages: pages, timers: timers,
    clay: clayStore,
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
h.apiCalls[0].callback({ok: true, data: {default_account_id: null}});
h.apiCalls[1].callback({ok: true, data: []});
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

var FIRST = '00112233-4455-4677-8899-aabbccddeeff';
var SECOND = '10112233-4455-4677-8899-aabbccddeeff';
var chooser = makeHarness();
chooser.events.ready();
settings.save(chooser.storage, {address: 'api.test:443', ssl: true, token: 'tb_kept'});
chooser.events.showConfiguration();
assert.strictEqual(chooser.urls.length, 0, 'the page opened before the accounts were loaded');
chooser.apiCalls[0].callback({ok: true, data: {default_account_id: FIRST}});
chooser.apiCalls[1].callback({ok: true, data: [{id: FIRST, label: 'Personal'}, {id: SECOND, label: '', telegram_identity: {first_name: 'Ada'}}, {id: 'bad'}]});
assert.deepStrictEqual(chooser.pages[0].accounts, [{id: FIRST, name: 'Personal'}, {id: SECOND, name: 'Ada'}]);
assert.strictEqual(chooser.pages[0].defaultAccount, FIRST);
assert.strictEqual(chooser.clay().CONFIG_DEFAULT_ACCOUNT, FIRST, 'the current default was not preselected');
chooser.submit({CONFIG_ADDRESS: 'api.test:443', CONFIG_SSL: true, CONFIG_TOKEN: '', CONFIG_DEFAULT_ACCOUNT: SECOND});
chooser.events.webviewclosed({response: 'saved'});
var update = chooser.apiCalls[2];
assert.strictEqual(update.name, 'updatePreferences');
assert.deepStrictEqual(update.args[1], {default_account_id: SECOND});
assert.strictEqual(chooser.resets(), 0, 'the watch reloaded before the default was stored');
update.callback({ok: true, data: {}});
assert.strictEqual(chooser.resets(), 1);
assert.strictEqual(chooser.messages[chooser.messages.length - 1].RESPONSE_KIND, protocol.response.refresh);

chooser.events.showConfiguration();
chooser.apiCalls[3].callback({ok: true, data: {default_account_id: SECOND}});
chooser.apiCalls[4].callback({ok: true, data: [{id: SECOND, label: 'Work'}]});
chooser.submit({CONFIG_ADDRESS: 'api.test:443', CONFIG_SSL: true, CONFIG_TOKEN: '', CONFIG_DEFAULT_ACCOUNT: ''});
chooser.events.webviewclosed({response: 'saved'});
assert.deepStrictEqual(chooser.apiCalls[5].args[1], {default_account_id: null}, 'clearing the default was not sent');
chooser.apiCalls[5].callback({ok: false, status: 0, code: 'network.unavailable'});
assert.strictEqual(chooser.resets(), 2, 'a failed default update must still reload the watch');

chooser.events.showConfiguration();
chooser.apiCalls[6].callback({ok: true, data: {default_account_id: SECOND}});
chooser.apiCalls[7].callback({ok: true, data: [{id: SECOND, label: 'Work'}]});
chooser.submit({CONFIG_ADDRESS: 'other.test:443', CONFIG_SSL: true, CONFIG_TOKEN: 'tb_other', CONFIG_DEFAULT_ACCOUNT: ''});
chooser.events.webviewclosed({response: 'saved'});
assert.strictEqual(chooser.apiCalls.length, 8, 'a default from the old server was sent to the new one');
assert.strictEqual(chooser.resets(), 3);

var slow = makeHarness();
settings.save(slow.storage, {address: 'api.test:443', ssl: true, token: 'tb_kept'});
slow.events.showConfiguration();
slow.timers[0].callback();
assert.strictEqual(slow.pages[0].accountsUnavailable, true, 'a slow server kept the settings page closed');
slow.apiCalls[0].callback({ok: true, data: {default_account_id: null}});
assert.strictEqual(slow.pages.length, 1, 'a late answer opened the settings page twice');
var failing = makeHarness();
settings.save(failing.storage, {address: 'api.test:443', ssl: true, token: 'tb_kept'});
failing.events.showConfiguration();
failing.apiCalls[0].callback({ok: false, status: 401, code: 'auth.unauthorized'});
assert.strictEqual(failing.pages[0].accountsUnavailable, true);
var fresh = makeHarness();
fresh.events.showConfiguration();
assert.strictEqual(fresh.apiCalls.length, 0, 'an unconfigured phone called the API');
assert.strictEqual(fresh.pages[0].accounts, undefined);

process.stdout.write('PKJS lifecycle tests passed\n');
