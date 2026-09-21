(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.TeleBezelSettingsApi = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const create = ({fetch, csrf, flow, onUnauthorized, now = Date.now}) => {
    let lastOwnerActivityAt = null;
    let ownerActivityPromise = null;
    const request = async (url, options = {}) => {
      const method = options.method || 'GET';
      if (flow.shouldRecordOwnerActivity(url, method)) await recordOwnerActivity();
      const response = await fetch(url, {...options, credentials: 'same-origin', headers: {
        Accept: 'application/json', 'Content-Type': 'application/json', 'X-CSRF-TOKEN': csrf, ...options.headers
      }});
      const text = await response.text();
      const body = text ? JSON.parse(text) : {};
      if (!response.ok) {
        const error = new Error(body.error?.code || 'request.failed');
        error.status = response.status;
        if (response.status === 401 && url.startsWith('/v1/owner/') && url !== '/v1/owner/login') onUnauthorized();
        throw error;
      }
      return body.data;
    };
    const recordOwnerActivity = async () => {
      if (lastOwnerActivityAt !== null && now() - lastOwnerActivityAt < 60000) return;
      if (ownerActivityPromise === null) {
        ownerActivityPromise = request('/v1/owner/activity', {method: 'POST', body: '{}'})
          .then(() => { lastOwnerActivityAt = now(); })
          .finally(() => { ownerActivityPromise = null; });
      }
      await ownerActivityPromise;
    };
    return Object.freeze({request, recordOwnerActivity});
  };
  return Object.freeze({create});
});
