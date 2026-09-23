'use strict';
var http = require('http');
var url = require('url');

var TOKEN = 'tb_' + new Array(44).join('e');
var FIRST = '00112233-4455-4677-8899-aabbccddeeff';
var SECOND = '10112233-4455-4677-8899-aabbccddeeff';
var BASE_DATE = 1790150000;
var LONG_TEXT = new Array(40).join('Длинное сообщение с переносами строк, кириллицей и эмодзи 😀. ') + '\nКонец.';

function chat(id, title, type, unread, text, extra) {
  var value = {id: id, type: type, title: title, is_forum: false, is_marked_unread: false, unread_count: unread, unread_mention_count: 0,
    unread_reaction_count: 0, notifications: {use_default_mute_for: true, mute_for: 0}, last_read_inbox_message_id: '0',
    last_read_outbox_message_id: '0', positions: {main: {order: String(1000 - Number(id.replace('-', '')) % 1000), is_pinned: false}},
    last_message: {id: '100', chat_id: id, sender: {type: 'user', id: '7', name: 'Ада', fallback: 'User 7'}, date: BASE_DATE,
      edit_date: 0, is_outgoing: false, author_signature: '', content: {kind: 'text', text: text}}};
  Object.keys(extra || {}).forEach(function(key) { value[key] = extra[key]; });
  return value;
}

var main = [
  chat('-1009007199254740993', 'Семья 👋', 'supergroup', 3, 'Кто заберёт детей?'),
  chat('1', 'Ada Lovelace', 'private', 0, 'Список покупок', {is_saved_messages: true, last_message: {id: '100', chat_id: '1', sender: {type: 'user', id: '1', name: 'Ada Lovelace', fallback: 'User 1'}, date: BASE_DATE - 600, edit_date: 0, is_outgoing: true, author_signature: '', content: {kind: 'text', text: 'Список покупок'}}}),
  chat('42', 'Ada Lovelace', 'private', 0, 'See you tomorrow', {last_message: {id: '100', chat_id: '42', sender: {type: 'user', id: '1', name: 'Me', fallback: 'User 1'}, date: BASE_DATE - 3600, edit_date: 0, is_outgoing: true, author_signature: '', content: {kind: 'voice_note', duration: 14, fallback_key: 'message.voice_note'}}}),
  chat('-1001234567890', 'Новости', 'channel', 120, 'Главное за день', {notifications: {use_default_mute_for: false, mute_for: 100000}}),
  chat('43', 'Борис', 'private', 1, '', {last_message: {id: '100', chat_id: '43', sender: {type: 'user', id: '43', name: 'Борис', fallback: 'User 43'}, date: BASE_DATE - 86400, edit_date: 0, is_outgoing: false, author_signature: '', content: {kind: 'photo', text: 'Смотри какой закат', fallback_key: 'message.photo'}}})
];
for (var index = 0; index < 21; ++index) { main.push(chat(String(1000 + index), 'Chat ' + (index + 1), 'basic_group', index % 3, 'Message number ' + index)); }
var archive = [chat('2001', 'Старый проект', 'basic_group', 0, 'Архивное сообщение'), chat('2002', 'Bot', 'private', 0, '/start')];

function messages(chatId) {
  var list = [];
  for (var id = 70; id >= 1; --id) {
    var content = {kind: 'text', text: 'Сообщение ' + id + (id % 7 === 0 ? '\nвторая строка' : '')};
    if (id === 70) { content = {kind: 'text', text: LONG_TEXT}; }
    if (id === 69) { content = {kind: 'photo', text: 'Подпись к фото', fallback_key: 'message.photo'}; }
    if (id === 68) { content = {kind: 'sticker', emoji: '😀', fallback_key: 'message.sticker'}; }
    if (id === 67) { content = {kind: 'voice_note', duration: 74, fallback_key: 'message.voice_note'}; }
    if (id === 66) { content = {kind: 'service', action: 'pinned', fallback_key: 'message.service'}; }
    if (id === 65) { content = {kind: 'unsupported', fallback_key: 'message.unsupported'}; }
    list.push({id: String(id), chat_id: chatId, sender: {type: 'user', id: id % 2 ? '7' : '8', name: id % 2 ? 'Ада' : null, fallback: id % 2 ? 'User 7' : 'User 8'},
      date: BASE_DATE - (70 - id) * 3000, edit_date: id === 64 ? BASE_DATE : 0, is_outgoing: id % 5 === 0, author_signature: '', content: content});
  }
  return list;
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
    if (path[3] !== FIRST) { send(409, {error: {code: 'authorization.invalid_state'}}); return; }
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
      send(200, envelope({items: [], cursor: 'u' + journal, has_more: false, connection: connection(), status: {connection: connection(), proxy: Boolean(options.proxy)}}));
      return;
    }
    if (path.length === 7 && path[6] === 'messages') {
      if (!query.view_id) { send(422, {error: {code: 'request.invalid'}}); return; }
      var all = messages(path[5]);
      var cursor = query.cursor || query.retry_cursor;
      var anchor = cursor ? Number(cursor.split(':')[1]) : 71;
      var older = all.filter(function(item) { return Number(item.id) < anchor; });
      var chunk = older.slice(0, Number(query.limit || 30));
      var last = chunk.length ? Number(chunk[chunk.length - 1].id) : anchor;
      send(200, envelope(page(chunk, {has_more: chunk.length ? true : false, local_exhausted: chunk.length === 0, next_cursor: chunk.length ? 'h:' + last : null,
        refresh: 'unchanged'})));
      return;
    }
    if (path.length === 8 && path[6] === 'messages') {
      var found = messages(path[5]).filter(function(item) { return item.id === path[7]; })[0];
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
    listen: function(port, callback) { server.listen(port, '127.0.0.1', function() { callback(server.address().port); }); },
    close: function(callback) { server.close(callback); }
  };
}

module.exports = {create: create, TOKEN: TOKEN, FIRST: FIRST, SECOND: SECOND, LONG_TEXT: LONG_TEXT};

if (require.main === module) {
  var instance = create({defaultAccount: process.env.MOCK_DEFAULT_ACCOUNT || null, accounts: process.env.MOCK_ACCOUNTS === undefined ? undefined : Number(process.env.MOCK_ACCOUNTS),
    connection: process.env.MOCK_CONNECTION ? process.env.MOCK_CONNECTION.split(',') : null, proxy: process.env.MOCK_PROXY === '1'});
  instance.listen(Number(process.env.PORT || 8787), function(port) { process.stdout.write('mock api on 127.0.0.1:' + port + '\n'); });
}
