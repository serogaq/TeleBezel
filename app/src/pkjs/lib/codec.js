'use strict';
var text = require('./text');

function Writer() { this.bytes = []; }
Writer.prototype.u8 = function(value) { this.bytes.push(value & 0xFF); return this; };
Writer.prototype.u16 = function(value) {
  var clamped = Math.max(0, Math.min(0xFFFF, Math.floor(value) || 0));
  this.bytes.push(clamped & 0xFF, (clamped >> 8) & 0xFF);
  return this;
};
Writer.prototype.u32 = function(value) {
  var clamped = Math.max(0, Math.min(0xFFFFFFFF, Math.floor(value) || 0));
  this.bytes.push(clamped & 0xFF, (clamped >>> 8) & 0xFF, (clamped >>> 16) & 0xFF, (clamped >>> 24) & 0xFF);
  return this;
};
Writer.prototype.raw8 = function(bytes) {
  var length = Math.min(bytes.length, 255);
  this.u8(length);
  for (var index = 0; index < length; ++index) { this.bytes.push(bytes[index]); }
  return this;
};
Writer.prototype.raw16 = function(bytes) {
  this.u16(bytes.length);
  for (var index = 0; index < bytes.length; ++index) { this.bytes.push(bytes[index]); }
  return this;
};
Writer.prototype.str8 = function(value, limit, options) {
  return this.raw8(text.encode(value, Math.min(limit || 255, 255), options).bytes);
};
Writer.prototype.ascii8 = function(value) {
  var bytes = [];
  var source = typeof value === 'string' ? value : '';
  for (var index = 0; index < source.length && index < 255; ++index) { bytes.push(source.charCodeAt(index) & 0x7F); }
  return this.raw8(bytes);
};

function record(type, body) {
  var writer = new Writer();
  writer.u8(type).u16(body.length);
  writer.bytes = writer.bytes.concat(body);
  return writer.bytes;
}

function create(protocol) {
  var types = protocol.record;
  return {
    account: function(value) {
      var body = new Writer()
        .ascii8(value.id).str8(value.name, 48, {singleLine: true}).u8(value.state).u8(value.flags);
      return record(types.account, body.bytes);
    },
    prefs: function(value) {
      var body = new Writer().ascii8(value.defaultAccount).u8(value.chatList).str8(value.host, 64, {singleLine: true})
        .u8(value.showArchive ? 1 : 0).u8(value.unreadMode);
      return record(types.prefs, body.bytes);
    },
    summary: function(value) {
      var body = new Writer().u8(value.connection).u8(value.proxy ? 1 : 0).u32(value.unreadChats).u32(value.unreadMessages);
      return record(types.summary, body.bytes);
    },
    status: function(value) {
      return record(types.status, new Writer().u8(value.connection).u8(value.proxy ? 1 : 0).bytes);
    },
    chat: function(value, textLimit) {
      var body = new Writer()
        .ascii8(value.id).str8(value.title, 64, {singleLine: true}).u8(value.type).u8(value.flags).u16(value.unread)
        .u32(value.lastDate).u8(value.previewKind).u8(value.previewAction).u16(value.previewDuration)
        .str8(value.previewSender, 32, {singleLine: true}).str8(value.previewExtra, 48, {singleLine: true});
      body.raw16(text.encode(value.previewText, textLimit, {singleLine: true}).bytes);
      return record(types.chat, body.bytes);
    },
    message: function(value, textLimit) {
      var encoded = text.encode(value.text, textLimit);
      var flags = value.flags | (encoded.truncated ? protocol.message_flag.truncated : 0);
      var body = new Writer()
        .ascii8(value.id).u32(value.date).u8(flags).u8(value.kind).u8(value.action).u16(value.duration)
        .str8(value.sender, 32, {singleLine: true}).str8(value.extra, 48, {singleLine: true});
      body.raw16(encoded.bytes);
      return record(types.message, body.bytes);
    },
    text: function(bytes) { return record(types.text, new Writer().raw16(bytes).bytes); },
    pack: function(records, budget) {
      var chunks = [];
      var current = [];
      records.forEach(function(item) {
        if (current.length > 0 && current.length + item.length > budget) {
          chunks.push(current);
          current = [];
        }
        current = current.concat(item);
      });
      if (current.length > 0 || chunks.length === 0) { chunks.push(current); }
      return chunks;
    }
  };
}

module.exports = {create: create, Writer: Writer};
