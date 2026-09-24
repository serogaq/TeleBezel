'use strict';
var text = require('../../src/pkjs/lib/text');

function Reader(bytes) { this.bytes = bytes; this.offset = 0; }
Reader.prototype.u8 = function() { return this.bytes[this.offset++]; };
Reader.prototype.u16 = function() { var value = this.bytes[this.offset] | (this.bytes[this.offset + 1] << 8); this.offset += 2; return value; };
Reader.prototype.u32 = function() {
  var value = (this.bytes[this.offset] | (this.bytes[this.offset + 1] << 8) | (this.bytes[this.offset + 2] << 16)) + this.bytes[this.offset + 3] * 0x1000000;
  this.offset += 4;
  return value;
};
Reader.prototype.raw = function(length) { var value = this.bytes.slice(this.offset, this.offset + length); this.offset += length; return value; };
Reader.prototype.str8 = function() { return text.decodeUtf8(this.raw(this.u8())); };
Reader.prototype.str16 = function() { return text.decodeUtf8(this.raw(this.u16())); };

function decode(protocol, bytes) {
  var types = protocol.record;
  var records = [];
  var reader = new Reader(bytes);
  while (reader.offset < bytes.length) {
    var type = reader.u8();
    var length = reader.u16();
    var body = new Reader(reader.raw(length));
    if (type === types.account) {
      records.push({type: 'account', id: body.str8(), name: body.str8(), state: body.u8(), flags: body.u8()});
    } else if (type === types.prefs) {
      records.push({type: 'prefs', defaultAccount: body.str8(), chatList: body.u8(), host: body.str8(), showArchive: body.u8(), unreadMode: body.u8()});
    } else if (type === types.summary) {
      records.push({type: 'summary', connection: body.u8(), proxy: body.u8(), unreadChats: body.u32(), unreadMessages: body.u32()});
    } else if (type === types.status) {
      records.push({type: 'status', connection: body.u8(), proxy: body.u8()});
    } else if (type === types.chat) {
      records.push({type: 'chat', id: body.str8(), title: body.str8(), chatType: body.u8(), flags: body.u8(), unread: body.u16(),
        lastDate: body.u32(), previewKind: body.u8(), previewAction: body.u8(), previewDuration: body.u16(),
        previewSender: body.str8(), previewExtra: body.str8(), previewText: body.str16(), send: body.u8()});
    } else if (type === types.message) {
      records.push({type: 'message', id: body.str8(), date: body.u32(), flags: body.u8(), kind: body.u8(), action: body.u8(),
        duration: body.u16(), sender: body.str8(), extra: body.str8(), text: body.str16(), replyId: body.str8(),
        replySender: body.str8(), replyText: body.str8(), forwardFrom: body.str8()});
    } else if (type === types.templates) {
      records.push({type: 'templates', revision: body.u32(), count: body.u8(), flags: body.u8()});
    } else if (type === types.template) {
      records.push({type: 'template', index: body.u8(), length: body.u16(), preview: body.str8()});
    } else if (type === types.draft) {
      records.push({type: 'draft', id: body.u32(), bytes: body.u16(), units: body.u16(), flags: body.u8()});
    } else if (type === types.send_state || type === types.pending_send) {
      records.push({type: type === types.send_state ? 'send_state' : 'pending_send', draftId: body.u32(), state: body.u8(),
        code: body.u8(), retryAfter: body.u16(), flags: body.u8(), account: body.str8(), chat: body.str8(), message: body.str8(),
        title: body.str8(), preview: body.str8()});
    } else if (type === types.text) {
      records.push({type: 'text', bytes: body.raw(body.u16())});
    } else {
      throw new Error('unknown record ' + type);
    }
    if (body.offset !== length) { throw new Error('record length mismatch for type ' + type); }
  }
  return records;
}

function collect(protocol, messages, sequence) {
  var chunks = messages.filter(function(message) { return message.RESPONSE_KIND === protocol.response.data && message.REQUEST_SEQ === sequence; });
  if (chunks.length === 0) { return null; }
  var records = [];
  chunks.forEach(function(chunk, index) {
    if (chunk.CHUNK_INDEX !== index || chunk.CHUNK_TOTAL !== chunks[0].CHUNK_TOTAL) { throw new Error('chunk order broken'); }
    if (chunk.PAYLOAD) { records = records.concat(decode(protocol, chunk.PAYLOAD)); }
  });
  if (chunks.length > chunks[0].CHUNK_TOTAL) { throw new Error('too many chunks'); }
  return {complete: chunks.length === chunks[0].CHUNK_TOTAL, code: chunks[0].RESULT_CODE, flags: chunks[0].PAGE_FLAGS, retryAfter: chunks[0].RETRY_AFTER, chunks: chunks, records: records};
}

module.exports = {decode: decode, collect: collect};
