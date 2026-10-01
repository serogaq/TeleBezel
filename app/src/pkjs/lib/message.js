'use strict';
var TEXT_KEYS = ['ACCOUNT_ID', 'ENTITY_ID', 'MESSAGE_ID'];
var NUMBER_KEYS = ['REQUEST_KIND', 'REQUEST_SEQ', 'INBOX_SIZE', 'PAGE_OP', 'LIST', 'PAGE_LIMIT', 'TEXT_LIMIT', 'DRAFT_ID', 'TEMPLATE_INDEX',
  'TEMPLATES_REV', 'ATTEMPT', 'MEDIA_INDEX', 'MEDIA_OFFSET', 'MEDIA_TAG'];
var ZERO_KEYS = ['PAGE_OP', 'LIST'];
var BYTE_KEYS = ['PAYLOAD', 'MEDIA_SPEC'];

function text(value) {
  if (typeof value !== 'string') { return value; }
  return value.replace(/\u0000+$/, '').trim();
}

function number(value) {
  if (typeof value === 'number') { return value; }
  if (typeof value === 'boolean') { return value ? 1 : 0; }
  if (typeof value === 'string' && /^-?[0-9]+$/.test(value.trim())) { return Number(value.trim()); }
  return value;
}

function bytes(value) {
  if (Array.isArray(value) || value === null || value === undefined || typeof value === 'string') { return value; }
  var length = Number(value.length);
  if (!Number.isInteger(length) || length < 0 || length > 65536) { return value; }
  var result = [];
  for (var index = 0; index < length; ++index) { result.push(Number(value[index]) & 0xFF); }
  return result;
}

function normalize(payload) {
  var result = {};
  Object.keys(payload || {}).forEach(function(key) { result[key] = payload[key]; });
  TEXT_KEYS.forEach(function(key) { if (Object.prototype.hasOwnProperty.call(result, key)) { result[key] = text(result[key]); } });
  NUMBER_KEYS.forEach(function(key) { if (Object.prototype.hasOwnProperty.call(result, key)) { result[key] = number(result[key]); } });
  ZERO_KEYS.forEach(function(key) { if (result[key] === undefined || result[key] === null) { result[key] = 0; } });
  BYTE_KEYS.forEach(function(key) { if (Object.prototype.hasOwnProperty.call(result, key)) { result[key] = bytes(result[key]); } });
  return result;
}

function describe(payload, keys) {
  return keys.map(function(key) {
    var value = payload[key];
    var shown = typeof value === 'string' ? value.length + ' chars' : JSON.stringify(value);
    return key + '=' + typeof value + ':' + shown;
  }).join(' ');
}

module.exports = {normalize: normalize, describe: describe};
