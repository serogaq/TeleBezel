'use strict';
var CLAY_STORAGE_KEY = 'clay-settings';
function create(options) {
  var activeClay = null;
  var inboxSize = 0;
  var ready = false;
  var pendingHello = 0;
  var latestStatusSeq = 0;
  var generation = 0;
  function send(kind, sequence, code, retries) {
    options.Pebble.sendAppMessage({RESPONSE_KIND: kind, REQUEST_SEQ: sequence, RESULT_CODE: code},
      function() {}, function() {
        if (retries > 0 && (kind !== options.protocol.response.status || sequence === latestStatusSeq)) {
          send(kind, sequence, code, retries - 1);
        }
      });
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
  function sendStatus(sequence, code) { send(options.protocol.response.status, sequence, code, 1); }
  function checkStatus(sequence, value) {
    var currentGeneration = ++generation;
    options.api.checkStatus(value, function(result) {
      if (currentGeneration === generation && sequence === latestStatusSeq) { sendStatus(sequence, result.code); }
    });
  }
  function onMessage(event) {
    var payload = event && event.payload ? event.payload : {};
    var kind = payload.REQUEST_KIND;
    var sequence = payload.REQUEST_SEQ;
    if (!Number.isInteger(sequence) || sequence <= 0 || sequence > 2147483647) { return; }
    if (kind === options.protocol.request.hello) {
      if (Number.isInteger(payload.INBOX_SIZE) && payload.INBOX_SIZE > 0) { inboxSize = payload.INBOX_SIZE; }
      if (ready) { send(options.protocol.response.ready, sequence, 0, 1); }
      else { pendingHello = sequence; }
      return;
    }
    if (!ready || kind !== options.protocol.request.status) { return; }
    latestStatusSeq = sequence;
    var value = options.settings.load(options.storage);
    var valid = options.settings.validate(value);
    if (!valid.ok) {
      ++generation;
      sendStatus(sequence, valid.missing ? options.protocol.result.config_missing : options.protocol.result.config_invalid);
      return;
    }
    checkStatus(sequence, value);
  }
  function showConfiguration() {
    var saved = options.settings.load(options.storage);
    openClay(saved);
  }
  function openClay(saved) {
    var strings = options.localization.resolve(options.locales, options.getLocale());
    scrubClayToken();
    activeClay = new options.Clay(options.configPage.build(strings, {tokenSaved: saved.token !== ''}), null, {autoHandleEvents: false});
    activeClay.setSettings(options.settings.toClay(saved));
    options.Pebble.openURL(activeClay.generateUrl());
  }
  function webviewClosed(event) {
    if (!activeClay || !event || !event.response) { activeClay = null; return; }
    var saved = options.settings.load(options.storage);
    var submitted = null;
    try { submitted = activeClay.getSettings(event.response, false); } catch (_error) { submitted = null; }
    scrubClayToken();
    activeClay = null;
    if (!submitted) { return; }
    var value = options.settings.fromClay(submitted, saved);
    options.settings.save(options.storage, value);
    ++generation;
    send(options.protocol.response.refresh, 0, 0, 1);
    if (latestStatusSeq <= 0) { return; }
    var valid = options.settings.validate(value);
    if (valid.ok) { checkStatus(latestStatusSeq, value); }
    else { sendStatus(latestStatusSeq, valid.missing ? options.protocol.result.config_missing : options.protocol.result.config_invalid); }
  }
  function register() {
    options.Pebble.addEventListener('ready', function() {
      ready = true;
      if (pendingHello > 0) { send(options.protocol.response.ready, pendingHello, 0, 1); pendingHello = 0; }
    });
    options.Pebble.addEventListener('appmessage', onMessage);
    options.Pebble.addEventListener('showConfiguration', showConfiguration);
    options.Pebble.addEventListener('webviewclosed', webviewClosed);
  }
  return {register: register, onMessage: onMessage, inboxSize: function() { return inboxSize; }};
}
module.exports = {create: create};
