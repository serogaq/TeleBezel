'use strict';
function parseRetryAfter(value, now) {
  if (typeof value !== 'string' || value.trim() === '') { return null; }
  var text = value.trim();
  if (/^[0-9]+$/.test(text)) { return Math.min(Number(text), 86400); }
  var at = Date.parse(text);
  return isNaN(at) ? null : Math.max(0, Math.min(Math.ceil((at - now()) / 1000), 86400));
}
function classify(protocol, status, code) {
  if (code && Object.prototype.hasOwnProperty.call(protocol.errors, code)) { return protocol.errors[code]; }
  if (status === 401 || status === 403) { return 'reauth'; }
  if (status === 0 || status === 429 || status === 502 || status === 503 || status === 504) { return 'retry'; }
  return 'show';
}
function query(params) {
  var parts = [];
  Object.keys(params).forEach(function(key) {
    var value = params[key];
    if (value === null || value === undefined || value === '') { return; }
    parts.push(encodeURIComponent(key) + '=' + encodeURIComponent(String(value)));
  });
  return parts.length ? '?' + parts.join('&') : '';
}
function create(XMLHttpRequestCtor, settingsStore, protocol, now) {
  now = now || Date.now;
  function request(settings, method, path, callback, body) {
    var done = false;
    var xhr = new XMLHttpRequestCtor();
    var handle = {abort: function() {
      if (done) { return; }
      done = true;
      try { xhr.abort(); } catch (_error) { return; }
    }};
    function finish(result) { if (!done) { done = true; callback(result); } }
    function failure(status, code, retryAfter) {
      finish({ok: false, status: status, code: code, retryAfter: retryAfter, action: classify(protocol, status, code), data: null});
    }
    try {
      var base = settingsStore.baseUrl(settings);
      if (!base) { failure(0, 'config.invalid', null); return handle; }
      xhr.open(method, base + path, true);
      xhr.timeout = 12000;
      xhr.setRequestHeader('Authorization', 'Bearer ' + settings.token);
      xhr.setRequestHeader('Accept', 'application/json');
      if (body !== undefined) { xhr.setRequestHeader('Content-Type', 'application/json'); }
      xhr.onreadystatechange = function() {
        if (xhr.readyState !== 4 || done) { return; }
        var status = xhr.status;
        if (status === 0) { failure(0, 'network.unavailable', null); return; }
        var retryAfter = parseRetryAfter(xhr.getResponseHeader ? xhr.getResponseHeader('Retry-After') : null, now);
        var parsed = null;
        try { parsed = JSON.parse(xhr.responseText || ''); } catch (_error) { parsed = null; }
        if (!parsed || typeof parsed !== 'object') { failure(status, 'response.invalid', retryAfter); return; }
        if (status >= 200 && status < 300) {
          if (!parsed.data || typeof parsed.data !== 'object') { failure(status, 'response.invalid', retryAfter); return; }
          finish({ok: true, status: status, code: null, retryAfter: null, action: null, data: parsed.data});
          return;
        }
        var code = parsed.error && typeof parsed.error.code === 'string' ? parsed.error.code : 'request.failed';
        failure(status, code, retryAfter !== null ? retryAfter : status === 429 ? 5 : null);
      };
      xhr.onerror = function() { failure(0, 'network.unavailable', null); };
      xhr.ontimeout = xhr.onerror;
      xhr.send(body === undefined ? null : JSON.stringify(body));
    } catch (_error) { failure(0, 'network.unavailable', null); }
    return handle;
  }
  function account(id) { return '/v1/telegram/accounts/' + encodeURIComponent(id); }
  return {
    request: request,
    preferences: function(settings, callback) { return request(settings, 'GET', '/v1/device/preferences', callback); },
    updatePreferences: function(settings, values, callback) { return request(settings, 'PUT', '/v1/device/preferences', callback, values); },
    accounts: function(settings, callback) { return request(settings, 'GET', '/v1/telegram/accounts' + query({per_page: 50}), callback); },
    chats: function(settings, id, params, callback) { return request(settings, 'GET', account(id) + '/chats' + query(params), callback); },
    history: function(settings, id, chat, params, callback) {
      return request(settings, 'GET', account(id) + '/chats/' + encodeURIComponent(chat) + '/messages' + query(params), callback);
    },
    message: function(settings, id, chat, message, params, callback) {
      return request(settings, 'GET', account(id) + '/chats/' + encodeURIComponent(chat) + '/messages/' + encodeURIComponent(message) + query(params), callback);
    },
    releaseInterest: function(settings, id, chat, view, callback) {
      return request(settings, 'DELETE', account(id) + '/chats/' + encodeURIComponent(chat) + '/interests/' + encodeURIComponent(view), callback);
    }
  };
}
module.exports = {create: create, classify: classify, parseRetryAfter: parseRetryAfter, query: query};
