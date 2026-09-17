'use strict';
function create(XMLHttpRequestCtor, settingsStore, protocol) {
  function checkStatus(settings, callback) {
    var done = false;
    function finish(code) { if (!done) { done = true; callback({code: code}); } }
    var xhr = new XMLHttpRequestCtor();
    try {
      xhr.open('GET', settingsStore.baseUrl(settings) + '/v1/status', true);
      xhr.timeout = 12000;
      xhr.setRequestHeader('Authorization', 'Bearer ' + settings.token);
      xhr.setRequestHeader('Accept', 'application/json');
      xhr.onreadystatechange = function() {
        if (xhr.readyState !== 4) { return; }
        if (xhr.status === 200) {
          try { var body = JSON.parse(xhr.responseText || '{}'); finish(body.data && body.data.status === 'ready' ? protocol.result.ok : protocol.result.protocol_error); }
          catch (_error) { finish(protocol.result.protocol_error); }
        } else if (xhr.status === 401) { finish(protocol.result.api_unauthorized); }
        else if (xhr.status === 503) { finish(protocol.result.backend_not_ready); }
        else { finish(protocol.result.backend_unavailable); }
      };
      xhr.onerror = function() { finish(protocol.result.backend_unavailable); };
      xhr.ontimeout = xhr.onerror;
      xhr.send(null);
    } catch (_error) { finish(protocol.result.backend_unavailable); }
  }
  return {checkStatus: checkStatus};
}
module.exports = {create: create};
