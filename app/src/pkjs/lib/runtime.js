'use strict';
var CLAY_STORAGE_KEY = 'clay-settings';
function create(options) {
  var activeClay = null;
  var inboxSize = 0;
  var ready = false;
  var pendingHello = 0;
  var transport = options.transport;
  var reader = options.reader;
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
    options.settings.save(options.storage, options.settings.fromClay(submitted, saved));
    reader.reset();
    sendControl(options.protocol.response.refresh, 0);
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
