'use strict';
var http = require('http');
var url = require('url');
var tbi = require('../helpers/tbi');

var TOKEN = 'tb_' + new Array(44).join('e');
var FIRST = '00112233-4455-4677-8899-aabbccddeeff';
var SECOND = '10112233-4455-4677-8899-aabbccddeeff';
var BASE_DATE = 1790150000;
var LONG_TEXT = new Array(40).join('Длинное сообщение с переносами строк и кириллицей. ') + '\nКонец.';

function chat(id, title, type, unread, text, extra) {
  var value = {id: id, type: type, title: title, is_forum: false, is_marked_unread: false, unread_count: unread, unread_mention_count: 0,
    unread_reaction_count: 0, notifications: {use_default_mute_for: true, mute_for: 0}, last_read_inbox_message_id: '0',
    last_read_outbox_message_id: '0', positions: {main: {order: String(1000 - Number(id.replace('-', '')) % 1000), is_pinned: false}},
    last_message: {id: '100', chat_id: id, sender: {type: 'user', id: '7', name: 'Ада', fallback: 'User 7'}, date: BASE_DATE,
      edit_date: 0, is_outgoing: false, author_signature: '', content: {kind: 'text', text: text}}, can_send: {text: true, reason: null}};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}

var main = [
  chat('-1009007199254740993', 'Семья', 'supergroup', 3, 'Кто заберёт детей?'),
  chat('1', 'Ada Lovelace', 'private', 0, 'Список покупок', {is_saved_messages: true, last_message: {id: '100', chat_id: '1', sender: {type: 'user', id: '1', name: 'Ada Lovelace', fallback: 'User 1'}, date: BASE_DATE - 600, edit_date: 0, is_outgoing: true, author_signature: '', content: {kind: 'text', text: 'Список покупок'}}}),
  chat('42', 'Ada Lovelace', 'private', 0, 'See you tomorrow', {last_message: {id: '100', chat_id: '42', sender: {type: 'user', id: '1', name: 'Me', fallback: 'User 1'}, date: BASE_DATE - 3600, edit_date: 0, is_outgoing: true, author_signature: '', content: {kind: 'voice_note', duration: 14, fallback_key: 'message.voice_note'}}}),
  chat('-1001234567890', 'Новости', 'channel', 120, 'Главное за день', {notifications: {use_default_mute_for: false, mute_for: 100000}, can_send: {text: false, reason: 'read_only'}}),
  chat('43', 'Борис', 'private', 1, '', {last_message: {id: '100', chat_id: '43', sender: {type: 'user', id: '43', name: 'Борис', fallback: 'User 43'}, date: BASE_DATE - 86400, edit_date: 0, is_outgoing: false, author_signature: '', content: {kind: 'photo', text: 'Смотри какой закат', fallback_key: 'message.photo'}}})
];
for (var index = 0; index < 21; ++index) { main.push(chat(String(1000 + index), 'Chat ' + (index + 1), 'basic_group', index % 3, 'Message number ' + index)); }
var archive = [chat('2001', 'Старый проект', 'basic_group', 0, 'Архивное сообщение'), chat('2002', 'Bot', 'private', 0, '/start')];

function messages(chatId, sent) {
  var list = [];
  for (var id = 70; id >= 1; --id) {
    var content = {kind: 'text', text: 'Сообщение ' + id + (id % 7 === 0 ? '\nвторая строка' : '')};
    if (id === 70) { content = {kind: 'text', text: LONG_TEXT}; }
    if (id === 69) { content = {kind: 'photo', text: 'Подпись к фото. Длинная подпись канала проверяет, что пункт «Фото» перестаёт быть выделенным, когда текст прокручен вниз, и снова выделяется, когда вы возвращаетесь в самый верх сообщения.', fallback_key: 'message.photo', media: photo(1280, 960)}; }
    if (id === 58 || id === 56) { content = {kind: 'photo', fallback_key: 'message.photo', media: photo(960, 1280, {album_id: '5000'})}; }
    if (id === 57) { content = {kind: 'photo', text: 'Альбом из трёх фото', fallback_key: 'message.photo', media: photo(1280, 720, {album_id: '5000'})}; }
    if (id === 54) { content = {kind: 'photo', text: 'Осторожно, спойлер', fallback_key: 'message.photo', media: photo(1280, 960, {has_spoiler: true})}; }
    if (id === 53) { content = {kind: 'photo', fallback_key: 'message.photo', media: photo(1280, 960, {type: 'none', restriction: 'self_destruct'})}; }
    if (id === 51) { content = {kind: 'text', text: 'https://t.me/news/1 Карусель из превью ссылки', media: photo(1280, 960, {count: 3})}; }
    if (id === 52) { content = {kind: 'video', duration: 42, text: 'Видео', fallback_key: 'message.video', media: photo(640, 360, {type: 'thumbnail'})}; }
    if (id === 68) { content = {kind: 'sticker', fallback_key: 'message.sticker'}; }
    if (id === 67) { content = {kind: 'voice_note', duration: 74, fallback_key: 'message.voice_note'}; }
    if (id === 66) { content = {kind: 'service', action: 'pinned', fallback_key: 'message.service'}; }
    if (id === 65) { content = {kind: 'unsupported', fallback_key: 'message.unsupported'}; }
    list.push({id: String(id), chat_id: chatId, sender: {type: 'user', id: id % 2 ? '7' : '8', name: id % 2 ? 'Ада' : null, fallback: id % 2 ? 'User 7' : 'User 8'},
      date: BASE_DATE - (70 - id) * 3000, edit_date: id === 64 ? BASE_DATE : 0, is_outgoing: id % 5 === 0, author_signature: '',
      reply_to: id === 63 ? {message_id: '62', sender_name: 'Ада', text: 'Сообщение 62'}
        : id === 61 ? {message_id: '70', sender_name: 'Ада', text: LONG_TEXT.slice(0, 100)} : null,
      forward_from: id === 62 ? {type: 'chat', id: '-1001234567890', name: 'Новости', fallback: 'Chat -1001234567890', signature: 'Редактор'} : null,
      sending_state: id === 60 ? 'failed' : id === 55 ? 'pending' : null, content: content});
  }
  if (chatId === '-1001234567890') {
    list.forEach(function(item) {
      item.sender = {type: 'chat', id: chatId, name: 'Новости', fallback: 'Chat ' + chatId};
      item.is_channel_post = true;
      item.author_signature = Number(item.id) % 2 ? 'Редактор' : '';
      item.is_outgoing = false;
      item.sending_state = null;
    });
  }
  if (chatId === '1') {
    list.forEach(function(item) {
      var id = Number(item.id);
      item.sender = {type: 'user', id: '1', name: 'Ada Lovelace', fallback: 'User 1'};
      item.is_outgoing = true;
      item.sending_state = null;
      item.forward_from = id === 69 ? {type: 'chat', id: '-1001234567890', name: 'Новости', fallback: 'Chat -1001234567890', signature: 'Редактор'}
        : id % 3 === 0 ? {type: 'user', id: '7', name: 'Ада', fallback: 'User 7'}
        : id % 3 === 1 ? {type: 'hidden', name: 'Борис', fallback: 'Борис'} : null;
    });
  }
  (sent || []).forEach(function(entry) {
    if (entry.chat !== chatId || !entry.messageId) { return; }
    list.unshift({id: entry.messageId, chat_id: chatId, sender: {type: 'user', id: '1', name: null, fallback: 'User 1'}, date: Math.floor(entry.created / 1000),
      edit_date: 0, is_outgoing: true, author_signature: '', reply_to: entry.reply ? {message_id: entry.reply, sender_name: 'Ада', text: 'Сообщение ' + entry.reply} : null,
      forward_from: null, sending_state: entry.state === 'sent' ? null : 'pending', content: {kind: 'text', text: entry.text}});
  });
  return list;
}

function photo(width, height, extra) {
  var value = {type: 'photo', width: width, height: height, has_spoiler: false, album_id: null, restriction: null};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}

var PALETTE = [0xC0, 0xFF, 0xC7, 0xCB, 0xDB, 0xF0, 0xF4, 0xF8, 0xFC, 0xD8, 0xC4, 0xE4, 0xEA, 0xD5, 0xC8, 0xE0];

function scene(variant) {
  return function(x, y, width, height) {
    var horizon = Math.floor(height * (0.55 + 0.1 * variant));
    var sunX = Math.floor(width * (0.3 + 0.2 * variant));
    var sunY = Math.floor(height * 0.28);
    var dx = x - sunX;
    var dy = y - sunY;
    var radius = Math.floor(Math.min(width, height) / 7);
    if (dx * dx + dy * dy < radius * radius) { return 8; }
    if (y < horizon) { return y < horizon / 3 ? 2 : y < horizon * 2 / 3 ? 3 : 4; }
    var hill = Math.floor(horizon + 18 * Math.sin((x + variant * 40) / 23));
    if (y < hill) { return 13; }
    return ((x >> 2) + (y >> 2)) % 2 ? 11 : 10;
  };
}

function fit(item, spec) {
  var ratio = Math.min(spec.width / item.width, spec.height / item.height);
  return {width: Math.max(1, Math.floor(item.width * ratio)), height: Math.max(1, Math.floor(item.height * ratio))};
}

function envelope(data) { return {data: data, request_id: 'mock'}; }
function page(items, extra) {
  var value = {items: items, stale: true, partial: false, has_more: null, next_cursor: null, retry_cursor: null, updates_cursor: 'u',
    observed_at: BASE_DATE, source: 'tdlib_local', connection: 'ready', local_exhausted: false, fallback_reason: null};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}

function create(options) {
  options = options || {};
  var log = [];
  var faults = {};
  var states = options.connection && options.connection.length ? options.connection.slice() : ['ready'];
  var step = 0;
  var journal = 0;
  var sends = {};
  var sendOrder = [];
  var pendingEvents = [];
  var sendModes = [];
  var newest = {};
  var mediaSeen = {};
  var mediaDelay = options.mediaDelay === undefined ? 1 : options.mediaDelay;
  var templates = {items: [{id: 't1', text: 'Уже еду', position: 0}, {id: 't2', text: 'Перезвоню позже', position: 1}], revision: 3};
  function operation(entry) {
    return {id: entry.id, state: entry.state, chat_id: entry.chat, reply_to_message_id: entry.reply || null, message_id: entry.messageId,
      error: entry.error, retryable: entry.retryable, reply_dropped: false};
  }
  function connection() { return states[Math.min(step, states.length - 1)]; }
  function unread(list) {
    var source = list === 'archive' ? archive : main;
    var loud = source.filter(function(item) { return item.unread_count > 0 && item.notifications.use_default_mute_for !== false; });
    return {chats: loud.length, messages: loud.reduce(function(total, item) { return total + item.unread_count; }, 0)};
  }
  var server = http.createServer(function(request, response) {
    var parsed = url.parse(request.url, true);
    var query = parsed.query;
    log.push({method: request.method, path: parsed.pathname, query: query});
    if (process.env.MOCK_LOG_REQUESTS === '1') { process.stdout.write('request ' + request.method + ' ' + parsed.pathname + '\n'); }
    function send(status, body, headers) {
      response.writeHead(status, Object.assign({'Content-Type': 'application/json'}, headers || {}));
      response.end(JSON.stringify(body));
    }
    if (request.headers.authorization !== 'Bearer ' + (options.token || TOKEN)) { send(401, {error: {code: 'auth.unauthorized'}}); return; }
    var fault = faults[parsed.pathname];
    if (fault) {
      if (fault.once) { delete faults[parsed.pathname]; }
      send(fault.status, {error: {code: fault.code}}, fault.headers);
      return;
    }
    var path = parsed.pathname.split('/').filter(Boolean);
    if (parsed.pathname === '/v1/device/preferences') {
      if (request.method === 'PUT') { send(200, {data: {default_account_id: FIRST}}); return; }
      send(200, {data: {id: 'd', name: 'Emulator', locale: 'auto', default_account_id: options.defaultAccount || null, chat_list: 'main'}});
      return;
    }
    if (parsed.pathname === '/v1/telegram/accounts') {
      send(200, {data: [
        {id: FIRST, label: 'Личный', lifecycle: 'active', runtime: {available: true, authorization_state: 'ready', connection_state: 'ready'}},
        {id: SECOND, label: 'Work', lifecycle: 'active', runtime: {available: true, authorization_state: 'awaiting_code', connection_state: 'ready'}}
      ].slice(0, options.accounts === undefined ? 2 : options.accounts), pagination: {current_page: 1, per_page: 50, total: 2, last_page: 1}, request_id: 'mock'});
      return;
    }
    if (parsed.pathname === '/v1/quick-replies') { send(200, envelope(templates)); return; }
    if (path[3] !== FIRST) { send(409, {error: {code: 'authorization.invalid_state'}}); return; }
    if (path.length === 7 && path[6] === 'messages' && request.method === 'POST') {
      var chunks = [];
      request.on('data', function(chunk) { chunks.push(chunk); });
      request.on('end', function() {
        var body = JSON.parse(Buffer.concat(chunks).toString('utf8'));
        var key = request.headers['idempotency-key'];
        var entry = sends[key];
        var mode = entry ? 'repeat' : sendModes.length ? sendModes.shift() : 'sent';
        if (!entry) {
          if (typeof mode === 'object') { send(mode.status, {error: {code: mode.code}}, mode.headers); return; }
          entry = sends[key] = {id: 'op-' + (sendOrder.length + 1), key: key, chat: path[5], reply: body.reply_to_message_id || null, text: body.text,
            state: mode === 'pending' || mode === 'drop' ? 'pending' : mode === 'reply_unavailable' || mode === 'forbidden' ? 'failed' : 'sent', messageId: null, error: null,
            retryable: false, processed: 1, created: Date.now()};
          if (entry.state === 'sent') { entry.messageId = String(900 + sendOrder.length); }
          if (mode === 'drop') { entry.state = 'sent'; entry.messageId = String(900 + sendOrder.length); }
          if (entry.state === 'failed') { entry.error = {code: mode === 'forbidden' ? 'message.send_forbidden' : 'message.reply_unavailable', retry_after: null}; }
          sendOrder.push(entry);
        }
        if (mode === 'drop') { request.socket.destroy(); return; }
        send(202, envelope({operation: operation(entry)}));
      });
      return;
    }
    if (path.length === 6 && path[4] === 'sends') {
      var known = sendOrder.filter(function(item) { return item.id === path[5]; })[0];
      if (!known) { send(404, {error: {code: 'operation.not_found'}}); return; }
      send(200, envelope({operation: operation(known)}));
      return;
    }
    if (path.length === 5 && path[4] === 'chats') {
      var source = query.list === 'archive' ? archive : main;
      var offset = query.cursor ? Number(query.cursor.split(':')[1]) : 0;
      var limit = Number(query.limit || 20);
      var slice = source.slice(offset, offset + limit);
      var more = offset + limit < source.length;
      send(200, envelope(page(slice, {has_more: more ? true : false, local_exhausted: !more, next_cursor: more ? 'c:' + (offset + limit) : null, source: 'tdlib_memory',
        connection: connection(), unread: unread(query.list)})));
      return;
    }
    if (path.length === 5 && path[4] === 'updates') {
      step += 1;
      journal += 1;
      var delivered = query.cursor ? pendingEvents.splice(0) : [];
      send(200, envelope({events: delivered, cursor: 'u' + journal, has_more: false, connection: connection(), status: {connection: connection(), proxy: Boolean(options.proxy)}}));
      return;
    }
    if (path.length === 7 && path[6] === 'messages') {
      if (!query.view_id) { send(422, {error: {code: 'request.invalid'}}); return; }
      var all = messages(path[5], sendOrder);
      var cursor = query.cursor || query.retry_cursor;
      var refresh = 'unchanged';
      if (options.refresh === 'pending' && !cursor) {
        newest[path[5]] = (newest[path[5]] || 0) + 1;
        if (newest[path[5]] === 1) { all = all.filter(function(item) { return Number(item.id) <= 67; }); }
        if (newest[path[5]] <= 2) { refresh = 'pending'; }
      }
      var anchor = cursor ? Number(cursor.split(':')[1]) : Infinity;
      var older = all.filter(function(item) { return Number(item.id) < anchor; });
      var chunk = older.slice(0, Number(query.limit || 30));
      var last = chunk.length ? Number(chunk[chunk.length - 1].id) : anchor;
      send(200, envelope(page(chunk, {has_more: chunk.length ? true : false, local_exhausted: chunk.length === 0, next_cursor: chunk.length ? 'h:' + last : null,
        refresh: refresh})));
      return;
    }
    if (path.length === 9 && path[6] === 'messages' && path[8] === 'media') {
      var chatMessages = messages(path[5], sendOrder);
      var target = chatMessages.filter(function(item) { return item.id === path[7]; })[0];
      var media = target && target.content.media;
      if (!media) { send(404, {error: {code: 'message.cache_miss'}}); return; }
      var members = media.album_id ? chatMessages.filter(function(item) { return item.content.media && item.content.media.album_id === media.album_id; })
        .sort(function(a, b) { return Number(a.id) - Number(b.id); }) : Array.apply(null, Array(Math.min(10, media.count || 1))).map(function() { return target; });
      var position = Number(query.index || 0);
      var item = members[position];
      if (!item) { send(422, {error: {code: 'request.invalid'}}); return; }
      var descriptor = {index: position, count: members.length, item_message_id: item.id, has_spoiler: item.content.media.has_spoiler, retry_after: null};
      var itemMedia = item.content.media;
      if (itemMedia.restriction) { send(200, envelope(Object.assign({state: 'restricted'}, descriptor))); return; }
      if (itemMedia.has_spoiler && query.reveal !== '1') { send(200, envelope(Object.assign({state: 'spoiler'}, descriptor))); return; }
      var seenKey = path[5] + '|' + item.id + '|' + query.shape;
      mediaSeen[seenKey] = (mediaSeen[seenKey] || 0) + 1;
      if (mediaSeen[seenKey] <= mediaDelay) { send(200, envelope(Object.assign({state: 'downloading', retry_after: 1}, descriptor))); return; }
      var spec = {width: Number(query.width), height: Number(query.height)};
      var size = fit(itemMedia, spec);
      var draw = scene(position + (item.id === '69' ? 0 : 1));
      var budget = Number(query.budget || 28672);
      var bits = 4;
      var encoded = null;
      function render() {
        var shrunk = size;
        return tbi.sample({width: shrunk.width, height: shrunk.height, canvasWidth: spec.width, canvasHeight: spec.height, shape: query.shape === 'round' ? 1 : 0,
          bits: bits, tag: Number(item.id) * 16 + position + bits * 256 + size.width * 4096, palette: bits === 4 ? PALETTE : [0xC0, 0xD5, 0xEA, 0xFF],
          pixel: function(x, y) { var value = draw(x, y, shrunk.width, shrunk.height); return bits === 4 ? value : value % 4; }});
      }
      var full = size;
      encoded = render();
      while (encoded.size > budget && size.width > full.width * 0.75) {
        size = {width: Math.floor(size.width * 0.95), height: Math.floor(size.height * 0.95)};
        encoded = render();
      }
      if (encoded.size > budget) {
        bits = 2;
        size = full;
        encoded = render();
      }
      while (encoded.size > budget && size.width > 16) {
        size = {width: Math.floor(size.width * 0.9), height: Math.floor(size.height * 0.9)};
        encoded = render();
      }
      var tag = Number(item.id) * 16 + position + bits * 256 + size.width * 4096;
      send(200, envelope(Object.assign({state: 'ready', rendition: {tag: tag, width: size.width, height: size.height, shape: query.shape,
        format: 'p' + bits, crc32: encoded.crc, bytes_base64: Buffer.from(encoded.bytes).toString('base64')}}, descriptor)));
      return;
    }
    if (path.length === 8 && path[6] === 'messages') {
      var found = messages(path[5], sendOrder).filter(function(item) { return item.id === path[7]; })[0];
      if (!found) { send(404, {error: {code: 'message.cache_miss'}}); return; }
      send(200, envelope({item: found, partial: false, stale: true, source: 'tdlib_memory', refresh: 'unchanged', fallback_reason: null, updates_cursor: 'u', observed_at: BASE_DATE}));
      return;
    }
    if (path[6] === 'interests') { send(200, envelope({active: false, expires_in: 0})); return; }
    send(404, {error: {code: 'http.not_found'}});
  });
  return {
    server: server,
    log: log,
    fail: function(pathname, status, code, once, headers) { faults[pathname] = {status: status, code: code, once: once, headers: headers}; },
    sends: function() { return sendOrder; },
    nextSend: function(mode) { sendModes.push(mode); },
    settle: function(id, state, error) {
      var entry = sendOrder.filter(function(item) { return item.id === id; })[0];
      entry.state = state;
      entry.messageId = state === 'sent' ? '950' : null;
      entry.error = error || null;
      entry.retryable = state === 'failed';
      pendingEvents.push({type: 'send_changed', sequence: journal + 1, operation_id: id, chat_id: entry.chat, message_id: entry.messageId, state: state,
        error: entry.error, retryable: entry.retryable, reply_dropped: false});
    },
    listen: function(port, callback) { server.listen(port, '127.0.0.1', function() { callback(server.address().port); }); },
    close: function(callback) { server.close(callback); }
  };
}

module.exports = {create: create, TOKEN: TOKEN, FIRST: FIRST, SECOND: SECOND, LONG_TEXT: LONG_TEXT};

if (require.main === module) {
  var instance = create({defaultAccount: process.env.MOCK_DEFAULT_ACCOUNT || null, accounts: process.env.MOCK_ACCOUNTS === undefined ? undefined : Number(process.env.MOCK_ACCOUNTS),
    connection: process.env.MOCK_CONNECTION ? process.env.MOCK_CONNECTION.split(',') : null, proxy: process.env.MOCK_PROXY === '1',
    refresh: process.env.MOCK_REFRESH || null, mediaDelay: process.env.MOCK_MEDIA_DELAY === undefined ? undefined : Number(process.env.MOCK_MEDIA_DELAY)});
  (process.env.MOCK_SEND_MODES ? process.env.MOCK_SEND_MODES.split(',') : []).forEach(function(mode) {
    instance.nextSend(mode === 'rate_limited'
      ? {status: 429, code: 'message.send_rate_limited', headers: {'Retry-After': '30'}} : mode);
  });
  var settleAfter = Number(process.env.MOCK_SETTLE_MS || 0);
  if (settleAfter > 0) {
    setInterval(function() {
      instance.sends().forEach(function(entry) {
        if (entry.state === 'pending' && Date.now() - entry.created >= settleAfter) {
          instance.settle(entry.id, 'sent');
          process.stdout.write('settled ' + entry.id + '\n');
        }
      });
    }, 500).unref();
  }
  instance.listen(Number(process.env.PORT || 8787), function(port) { process.stdout.write('mock api on 127.0.0.1:' + port + '\n'); });
}
