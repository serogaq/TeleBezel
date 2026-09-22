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
function create(XMLHttpRequestCtor, settingsStore, protocol, now) {
  now = now || Date.now;
  function request(settings, method, path, callback) {
    var done = false;
    var xhr = new XMLHttpRequestCtor();
    function finish(result) { if (!done) { done = true; callback(result); } }
    function failure(status, code, retryAfter) {
      finish({ok: false, status: status, code: code, retryAfter: retryAfter, action: classify(protocol, status, code), data: null});
    }
    try {
      var base = settingsStore.baseUrl(settings);
      if (!base) { failure(0, 'config.invalid', null); return; }
      xhr.open(method, base + path, true);
      xhr.timeout = 12000;
      xhr.setRequestHeader('Authorization', 'Bearer ' + settings.token);
      xhr.setRequestHeader('Accept', 'application/json');
      xhr.onreadystatechange = function() {
        if (xhr.readyState !== 4) { return; }
        var status = xhr.status;
        if (status === 0) { failure(0, 'network.unavailable', null); return; }
        var retryAfter = parseRetryAfter(xhr.getResponseHeader ? xhr.getResponseHeader('Retry-After') : null, now);
        var body = null;
        try { body = JSON.parse(xhr.responseText || ''); } catch (_error) { body = null; }
        if (!body || typeof body !== 'object') { failure(status, 'response.invalid', retryAfter); return; }
        if (status >= 200 && status < 300) {
          if (!body.data || typeof body.data !== 'object') { failure(status, 'response.invalid', retryAfter); return; }
          finish({ok: true, status: status, code: null, retryAfter: null, action: null, data: body.data});
          return;
        }
        var code = body.error && typeof body.error.code === 'string' ? body.error.code : 'request.failed';
        failure(status, code, retryAfter !== null ? retryAfter : status === 429 ? 5 : null);
      };
      xhr.onerror = function() { failure(0, 'network.unavailable', null); };
      xhr.ontimeout = xhr.onerror;
      xhr.send(null);
    } catch (_error) { failure(0, 'network.unavailable', null); }
  }
  function checkStatus(settings, callback) {
    request(settings, 'GET', '/v1/status', function(result) {
      if (result.ok) {
        callback({code: result.data.status === 'ready' ? protocol.result.ok : protocol.result.protocol_error});
      } else if (result.code === 'response.invalid') {
        callback({code: result.status === 503 ? protocol.result.backend_not_ready : protocol.result.protocol_error});
      } else if (result.action === 'reauth') {
        callback({code: protocol.result.api_unauthorized});
      } else if (result.status === 503) {
        callback({code: protocol.result.backend_not_ready});
      } else {
        callback({code: protocol.result.backend_unavailable});
      }
    });
  }
  return {request: request, checkStatus: checkStatus};
}
module.exports = {create: create, classify: classify, parseRetryAfter: parseRetryAfter};
