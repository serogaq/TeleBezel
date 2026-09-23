'use strict';
var STORAGE_KEY = 'telebezel.settings.v1';
var ADDRESS_PATTERN = /^(?:[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?|\[[0-9A-Fa-f:.]+\]):([0-9]{1,5})$/;
function flag(value) { return !(value === false || value === 0 || value === '0' || value === 'false'); }
function normalize(raw) {
  raw = raw || {};
  return {address: typeof raw.address === 'string' ? raw.address.trim() : '', ssl: flag(raw.ssl), token: typeof raw.token === 'string' ? raw.token : '',
    showArchive: flag(raw.showArchive), unreadMode: raw.unreadMode === 'messages' ? 'messages' : 'chats'};
}
function validateEndpoint(raw) {
  var value = normalize(raw);
  if (!value.address) { return {ok: false, missing: true}; }
  if (value.address.indexOf('://') !== -1 || /[/?#\s]/.test(value.address)) { return {ok: false, missing: false}; }
  var match = ADDRESS_PATTERN.exec(value.address);
  var port = match ? Number(match[1]) : 0;
  return match && port >= 1 && port <= 65535 ? {ok: true, settings: value} : {ok: false, missing: false};
}
function validate(raw) { var result = validateEndpoint(raw); return result.ok && !result.settings.token ? {ok: false, missing: true} : result; }
function load(storage) {
  try { var value = storage.getItem(STORAGE_KEY); return normalize(value ? JSON.parse(value) : {}); }
  catch (_error) { return normalize({}); }
}
function save(storage, value) { storage.setItem(STORAGE_KEY, JSON.stringify(normalize(value))); }
function clayValue(values, key) {
  var value = values ? values[key] : undefined;
  return value && typeof value === 'object' && Object.prototype.hasOwnProperty.call(value, 'value') ? value.value : value;
}
function fromClay(values, previous) {
  previous = normalize(previous);
  var showArchive = clayValue(values, 'SHOW_ARCHIVE');
  var unreadMode = clayValue(values, 'UNREAD_MODE');
  return normalize({address: clayValue(values, 'CONFIG_ADDRESS'), ssl: clayValue(values, 'CONFIG_SSL'), token: clayValue(values, 'CONFIG_TOKEN') || previous.token,
    showArchive: showArchive === undefined ? previous.showArchive : showArchive, unreadMode: unreadMode === undefined ? previous.unreadMode : unreadMode});
}
function toClay(value) { value = normalize(value); return {CONFIG_ADDRESS: value.address, CONFIG_SSL: value.ssl, SHOW_ARCHIVE: value.showArchive, UNREAD_MODE: value.unreadMode}; }
function baseUrl(value) { var valid = validateEndpoint(value); return valid.ok ? (valid.settings.ssl ? 'https://' : 'http://') + valid.settings.address : null; }
module.exports = {clayValue: clayValue, STORAGE_KEY: STORAGE_KEY, normalize: normalize, validate: validate, validateEndpoint: validateEndpoint, load: load, save: save, fromClay: fromClay, toClay: toClay, baseUrl: baseUrl};
