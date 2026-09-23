'use strict';
var PLACEHOLDER = 0x25AF;
var ELLIPSIS = 0x2026;
var EMOJI = [0x2639, 0x263A, 0x2665, 0x2764, 0x2600, 0x2601, 0x2614, 0x26A1, 0x2B50, 0x2728, 0x270C, 0x2705, 0x274C,
  0x1F44B, 0x1F44C, 0x1F44D, 0x1F44E, 0x1F44F, 0x1F64F, 0x1F389, 0x1F525, 0x1F4A9, 0x1F494, 0x1F495, 0x1F499, 0x1F49C,
  0x1F4AA, 0x1F440, 0x1F914, 0x1F917, 0x1F923];

function inRange(value, low, high) { return value >= low && value <= high; }
function whitelisted(value) { return inRange(value, 0x1F600, 0x1F64F) || EMOJI.indexOf(value) !== -1; }
function isCombining(value) {
  return inRange(value, 0x0300, 0x036F) || inRange(value, 0x1AB0, 0x1AFF) || inRange(value, 0x1DC0, 0x1DFF) ||
    inRange(value, 0x20D0, 0x20FF) || inRange(value, 0xFE20, 0xFE2F);
}
function isSelector(value) { return inRange(value, 0xFE00, 0xFE0F) || inRange(value, 0xE0100, 0xE01EF); }
function isModifier(value) { return inRange(value, 0x1F3FB, 0x1F3FF); }
function isTag(value) { return inRange(value, 0xE0020, 0xE007F); }
function isRegional(value) { return inRange(value, 0x1F1E6, 0x1F1FF); }
function isRemoved(value) {
  return (value < 0x20 && value !== 0x0A && value !== 0x09) || inRange(value, 0x7F, 0x9F) || value === 0xAD ||
    inRange(value, 0x200B, 0x200C) || inRange(value, 0x200E, 0x200F) || inRange(value, 0x202A, 0x202E) ||
    inRange(value, 0x2060, 0x2064) || inRange(value, 0x2066, 0x2069) || value === 0xFEFF;
}
function isPictographic(value) { return value >= 0x1F000 || inRange(value, 0x2600, 0x27BF) || inRange(value, 0x2B00, 0x2BFF); }

function codePoints(value) {
  var points = [];
  for (var index = 0; index < value.length; ++index) {
    var high = value.charCodeAt(index);
    if (high >= 0xD800 && high <= 0xDBFF && index + 1 < value.length) {
      var low = value.charCodeAt(index + 1);
      if (low >= 0xDC00 && low <= 0xDFFF) {
        points.push((high - 0xD800) * 0x400 + (low - 0xDC00) + 0x10000);
        ++index;
        continue;
      }
    }
    points.push(high >= 0xD800 && high <= 0xDFFF ? PLACEHOLDER : high);
  }
  return points;
}

function clusters(points) {
  var result = [];
  var index = 0;
  while (index < points.length) {
    var cluster = [points[index++]];
    if (isRegional(cluster[0]) && index < points.length && isRegional(points[index])) { cluster.push(points[index++]); }
    while (index < points.length) {
      var next = points[index];
      if (isCombining(next) || isSelector(next) || isModifier(next) || isTag(next) || next === 0x20E3) {
        cluster.push(next);
        ++index;
      } else if (next === 0x200D && index + 1 < points.length) {
        cluster.push(next, points[index + 1]);
        index += 2;
      } else {
        break;
      }
    }
    result.push(cluster);
  }
  return result;
}

function resolve(cluster) {
  var base = cluster[0];
  if (base === 0x0D) { return [0x0A]; }
  if (base === 0x09) { return [0x20]; }
  if (isRemoved(base) || isCombining(base) || isSelector(base) || isModifier(base) || isTag(base) || base === 0x200D) {
    return [];
  }
  if (cluster.indexOf(0x20E3) !== -1) { return base < 0x80 ? [base] : [PLACEHOLDER]; }
  var extra = cluster.slice(1).filter(function(value) { return !isSelector(value) && !isCombining(value); });
  if (extra.length > 0 || isRegional(base)) { return [PLACEHOLDER]; }
  if (isPictographic(base)) { return whitelisted(base) ? [base] : [PLACEHOLDER]; }
  if (base > 0xFFFF) { return [PLACEHOLDER]; }
  return [base];
}

function utf8Length(value) { return value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4; }
function pushUtf8(bytes, value) {
  if (value < 0x80) { bytes.push(value); }
  else if (value < 0x800) { bytes.push(0xC0 | (value >> 6), 0x80 | (value & 0x3F)); }
  else if (value < 0x10000) { bytes.push(0xE0 | (value >> 12), 0x80 | ((value >> 6) & 0x3F), 0x80 | (value & 0x3F)); }
  else {
    bytes.push(0xF0 | (value >> 18), 0x80 | ((value >> 12) & 0x3F), 0x80 | ((value >> 6) & 0x3F), 0x80 | (value & 0x3F));
  }
}

function normalizeString(value) {
  var text = typeof value === 'string' ? value : '';
  if (typeof text.normalize === 'function') {
    try { text = text.normalize('NFC'); } catch (_error) { text = String(text); }
  }
  return text.replace(/\r\n?/g, '\n');
}

function units(value, singleLine) {
  var resolved = [];
  clusters(codePoints(normalizeString(value))).forEach(function(cluster) {
    var points = resolve(cluster);
    if (points.length === 0) { return; }
    if (singleLine && points[0] === 0x0A) { points = [0x20]; }
    resolved.push(points);
  });
  var compact = [];
  var newlines = 0;
  var spaces = 0;
  resolved.forEach(function(points) {
    if (points[0] === 0x0A) {
      ++newlines;
      spaces = 0;
      if (newlines <= 2) { compact.push(points); }
      return;
    }
    if (points[0] === 0x20) {
      ++spaces;
      if (singleLine && spaces > 1) { return; }
    } else {
      spaces = 0;
    }
    newlines = 0;
    compact.push(points);
  });
  while (compact.length && (compact[0][0] === 0x0A || compact[0][0] === 0x20)) { compact.shift(); }
  while (compact.length && (compact[compact.length - 1][0] === 0x0A || compact[compact.length - 1][0] === 0x20)) { compact.pop(); }
  return compact;
}

function encode(value, maxBytes, options) {
  options = options || {};
  var list = units(value, Boolean(options.singleLine));
  var bytes = [];
  var truncated = false;
  var limit = Math.max(0, maxBytes);
  for (var index = 0; index < list.length; ++index) {
    var size = 0;
    list[index].forEach(function(point) { size += utf8Length(point); });
    var reserve = index + 1 < list.length ? 3 : 0;
    if (bytes.length + size + reserve > limit) {
      truncated = true;
      break;
    }
    list[index].forEach(function(point) { pushUtf8(bytes, point); });
  }
  if (truncated) {
    while (bytes.length > 0 && bytes.length + 3 > limit) { bytes = dropLastPoint(bytes); }
    while (bytes.length > 0 && (bytes[bytes.length - 1] === 0x20 || bytes[bytes.length - 1] === 0x0A)) { bytes.pop(); }
    if (limit >= 3) { pushUtf8(bytes, ELLIPSIS); }
  }
  return {bytes: bytes, truncated: truncated};
}

function dropLastPoint(bytes) {
  var end = bytes.length - 1;
  while (end > 0 && (bytes[end] & 0xC0) === 0x80) { --end; }
  return bytes.slice(0, end);
}

function splitUtf8(bytes, size) {
  var parts = [];
  var start = 0;
  while (start < bytes.length) {
    var end = Math.min(bytes.length, start + size);
    if (end < bytes.length) {
      while (end > start && (bytes[end] & 0xC0) === 0x80) { --end; }
      if (end === start) {
        end = start + 1;
        while (end < bytes.length && (bytes[end] & 0xC0) === 0x80) { ++end; }
      }
    }
    parts.push(bytes.slice(start, end));
    start = end;
  }
  return parts;
}

function decodeUtf8(bytes) {
  var result = '';
  var index = 0;
  while (index < bytes.length) {
    var first = bytes[index];
    var value = first;
    var extra = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC0 ? 1 : 0;
    value = extra === 3 ? first & 0x07 : extra === 2 ? first & 0x0F : extra === 1 ? first & 0x1F : first;
    for (var step = 1; step <= extra; ++step) { value = (value << 6) | (bytes[index + step] & 0x3F); }
    index += extra + 1;
    if (value > 0xFFFF) {
      value -= 0x10000;
      result += String.fromCharCode(0xD800 + (value >> 10), 0xDC00 + (value & 0x3FF));
    } else {
      result += String.fromCharCode(value);
    }
  }
  return result;
}

module.exports = {encode: encode, splitUtf8: splitUtf8, decodeUtf8: decodeUtf8, PLACEHOLDER: String.fromCharCode(PLACEHOLDER)};
