'use strict';
var accountInfo = require('./accounts');
var CLAY_STORAGE_KEY = 'clay-settings';
var ACCOUNTS_TIMEOUT_MS = 5000;
function create(options) {
  var activeClay = null;
  var inboxSize = 0;
  var ready = false;
  var pendingHello = 0;
  var transport = options.transport;
  var reader = options.reader;
  var setTimer = options.setTimeout || setTimeout;
  var clearTimer = options.clearTimeout || clearTimeout;
  var offered = null;
  function sendControl(kind, sequence) {
    transport.send({RESPONSE_KIND: kind, REQUEST_SEQ: sequence, RESULT_CODE: 0});
  }
  function scrubClayToken() {
    try {
      var raw = options.storage.getItem(CLAY_STORAGE_KEY);
      if (!raw) { return; }
      var stored = JSON.parse(raw);
      if (stored && typeof stored === 'object' && Object.prototype.hasOwnProperty.call(stored, 'CONFIG_TOKEN')) {
        delete stored.CONFIG_TOKEN;
        options.storage.setItem(CLAY_STORAGE_KEY, JSON.stringify(stored));
      }
    } catch (_error) {
      options.storage.removeItem(CLAY_STORAGE_KEY);
    }
  }
  function onMessage(event) {
    var payload = event && event.payload ? event.payload : {};
    var kind = payload.REQUEST_KIND;
    var sequence = payload.REQUEST_SEQ;
    if (!Number.isInteger(sequence) || sequence <= 0 || sequence > 2147483647) { return; }
    if (kind === options.protocol.request.hello) {
      if (Number.isInteger(payload.INBOX_SIZE) && payload.INBOX_SIZE > 0) {
        inboxSize = payload.INBOX_SIZE;
        reader.setInboxSize(inboxSize);
      }
      if (ready) { sendControl(options.protocol.response.ready, sequence); }
      else { pendingHello = sequence; }
      return;
    }
    if (!ready) { return; }
    reader.handle(payload);
  }
  function showConfiguration() {
    var saved = options.settings.load(options.storage);
    offered = null;
    if (!options.settings.validate(saved).ok) {
      openClay(saved, {});
      return;
    }
    var opened = false;
    var timer = setTimer(function() { open({accountsUnavailable: true}); }, ACCOUNTS_TIMEOUT_MS);
    function open(state) {
      if (opened) { return; }
      opened = true;
      clearTimer(timer);
      openClay(saved, state);
    }
    options.api.preferences(saved, function(preferences) {
      if (opened) { return; }
      if (!preferences.ok) {
        open({accountsUnavailable: true});
        return;
      }
      options.api.accounts(saved, function(result) {
        if (opened) { return; }
        if (!result.ok || !Array.isArray(result.data)) {
          open({accountsUnavailable: true});
          return;
        }
        var list = result.data.filter(accountInfo.valid).map(function(account) { return {id: account.id, name: accountInfo.name(account)}; });
        var ids = list.map(function(account) { return account.id; });
        var current = typeof preferences.data.default_account_id === 'string' && ids.indexOf(preferences.data.default_account_id) !== -1
          ? preferences.data.default_account_id : '';
        offered = {address: saved.address, ssl: saved.ssl, token: saved.token, value: current, ids: ids};
        open({accounts: list, defaultAccount: current});
      });
    });
  }
  function openClay(saved, state) {
    var strings = options.localization.resolve(options.locales, options.getLocale());
    scrubClayToken();
    state.tokenSaved = saved.token !== '';
    activeClay = new options.Clay(options.configPage.build(strings, state), null, {autoHandleEvents: false});
    var values = options.settings.toClay(saved);
    if (state.accounts) { values.CONFIG_DEFAULT_ACCOUNT = state.defaultAccount || ''; }
    activeClay.setSettings(values);
    options.Pebble.openURL(activeClay.generateUrl());
  }
  function chosenDefault(submitted, value) {
    var chosen = options.settings.clayValue(submitted, 'CONFIG_DEFAULT_ACCOUNT');
    var target = offered;
    offered = null;
    if (!target || typeof chosen !== 'string' || chosen === target.value) { return null; }
    if (chosen !== '' && target.ids.indexOf(chosen) === -1) { return null; }
    if (value.address !== target.address || value.ssl !== target.ssl || value.token !== target.token) { return null; }
    return {default_account_id: chosen || null};
  }
  function webviewClosed(event) {
    if (!activeClay || !event || !event.response) { activeClay = null; offered = null; return; }
    var saved = options.settings.load(options.storage);
    var submitted = null;
    try { submitted = activeClay.getSettings(event.response, false); } catch (_error) { submitted = null; }
    scrubClayToken();
    activeClay = null;
    if (!submitted) { offered = null; return; }
    var value = options.settings.fromClay(submitted, saved);
    options.settings.save(options.storage, value);
    var change = chosenDefault(submitted, value);
    function applied() {
      reader.reset();
      sendControl(options.protocol.response.refresh, 0);
    }
    if (!change) {
      applied();
      return;
    }
    options.api.updatePreferences(value, change, applied);
  }
  function register() {
    options.Pebble.addEventListener('ready', function() {
      ready = true;
      if (pendingHello > 0) { sendControl(options.protocol.response.ready, pendingHello); pendingHello = 0; }
    });
    options.Pebble.addEventListener('appmessage', onMessage);
    options.Pebble.addEventListener('showConfiguration', showConfiguration);
    options.Pebble.addEventListener('webviewclosed', webviewClosed);
  }
  return {register: register, onMessage: onMessage, inboxSize: function() { return inboxSize; }};
}
module.exports = {create: create};
