'use strict';
var CACHE_LIMIT = 10;
var BASE64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
var ALBUM_PATTERN = /^-?[0-9]{1,20}$/;

function decodeBase64(value) {
  if (typeof value !== 'string' || value.length % 4 !== 0) { return null; }
  var bytes = [];
  for (var index = 0; index < value.length; index += 4) {
    var chunk = 0;
    var padding = 0;
    for (var part = 0; part < 4; ++part) {
      var character = value.charAt(index + part);
      var digit = BASE64.indexOf(character);
      if (character === '=' && index + 4 === value.length && part >= 2) {
        digit = 0;
        ++padding;
      } else if (digit < 0 || padding > 0) {
        return null;
      }
      chunk = chunk * 64 + digit;
    }
    bytes.push((chunk >> 16) & 0xFF);
    if (padding < 2) { bytes.push((chunk >> 8) & 0xFF); }
    if (padding < 1) { bytes.push(chunk & 0xFF); }
  }
  return bytes;
}

function albumKey(id) {
  if (typeof id !== 'string' || !ALBUM_PATTERN.test(id)) { return 0; }
  var hash = 5381;
  for (var index = 0; index < id.length; ++index) { hash = ((hash * 33) ^ id.charCodeAt(index)) >>> 0; }
  return hash || 1;
}

function parseSpec(protocol, raw) {
  if (!Array.isArray(raw) || raw.length !== 8) { return null; }
  var width = raw[0] | (raw[1] << 8);
  var height = raw[2] | (raw[3] << 8);
  var shape = raw[4] & 0x7F;
  var budget = raw[6] | (raw[7] << 8);
  var formats = [];
  if (raw[5] & protocol.media_format.p4) { formats.push('p4'); }
  if (raw[5] & protocol.media_format.p2) { formats.push('p2'); }
  if (raw[5] & protocol.media_format.p1) { formats.push('p1'); }
  if (width < 16 || width > 260 || height < 16 || height > 260 || formats.length === 0 || budget < 2048 || budget > protocol.limit.media_budget) { return null; }
  if (shape !== protocol.media_shape.rect && shape !== protocol.media_shape.round) { return null; }
  return {width: width, height: height, shape: shape === protocol.media_shape.round ? 'round' : 'rect', budget: budget, formats: formats.join(','),
    reveal: (raw[4] & 0x80) !== 0};
}

function flags(protocol, message) {
  var content = message && message.content ? message.content : {};
  var media = content.media && typeof content.media === 'object' ? content.media : null;
  var value = 0;
  if (message && message.is_channel_post === true) { value |= protocol.media_flag.channel_post; }
  if (!media) { return value; }
  var restricted = media.restriction === 'self_destruct' || media.restriction === 'paid';
  if (restricted) { value |= protocol.media_flag.restricted; }
  if (!restricted && (media.type === 'photo' || media.type === 'thumbnail')) { value |= protocol.media_flag.image; }
  if (media.has_spoiler === true) { value |= protocol.media_flag.spoiler; }
  if (albumKey(media.album_id) || inner(media) > 1) { value |= protocol.media_flag.album; }
  return value;
}

function inner(media) {
  var count = media && typeof media.count === 'number' ? Math.floor(media.count) : 1;
  return Math.max(1, Math.min(10, count || 1));
}

function album(message) {
  var media = message && message.content && message.content.media;
  return media && typeof media === 'object' ? albumKey(media.album_id) : 0;
}

function group(items) {
  var groups = [];
  items.forEach(function(item) {
    var key = album(item);
    var last = groups[groups.length - 1];
    if (key && last && last.album === key) {
      last.members.push(item);
    } else {
      groups.push({album: key, members: [item]});
    }
  });
  return groups.map(function(entry) {
    var captioned = entry.members.filter(function(member) { return member.content && typeof member.content.text === 'string' && member.content.text; })[0];
    var message = captioned || entry.members[entry.members.length - 1];
    return {message: message, count: entry.members.length > 1 ? entry.members.length : inner(message.content && message.content.media),
      spoiler: entry.members.some(function(member) { return member.content && member.content.media && member.content.media.has_spoiler === true; })};
  });
}

function createCache(limit) {
  var entries = [];
  return {
    get: function(key) {
      for (var index = 0; index < entries.length; ++index) {
        if (entries[index].key === key) {
          var entry = entries.splice(index, 1)[0];
          entries.push(entry);
          return entry;
        }
      }
      return null;
    },
    put: function(entry) {
      entries = entries.filter(function(existing) { return existing.key !== entry.key; });
      entries.push(entry);
      while (entries.length > (limit || CACHE_LIMIT)) { entries.shift(); }
    },
    clear: function() { entries = []; },
    size: function() { return entries.length; }
  };
}

function chunks(codec, info, bytes, offset, budget) {
  var messages = [];
  var infoRecord = codec.mediaInfo(info);
  var position = offset;
  var first = true;
  while (first || position < bytes.length) {
    var room = budget - (first ? infoRecord.length : 0) - 7;
    var slice = bytes.slice(position, position + Math.max(0, room));
    var payload = first ? infoRecord.slice() : [];
    if (slice.length > 0) { payload = payload.concat(codec.mediaData(position, slice)); }
    messages.push(payload);
    position += slice.length;
    first = false;
    if (room <= 0) { break; }
  }
  return messages;
}

module.exports = {decodeBase64: decodeBase64, albumKey: albumKey, parseSpec: parseSpec, flags: flags, group: group, inner: inner, createCache: createCache, chunks: chunks,
  CACHE_LIMIT: CACHE_LIMIT};
