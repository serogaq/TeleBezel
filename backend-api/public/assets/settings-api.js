(function (root, factory) {
  'use strict';
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.TeleBezelSettingsApi = api;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const parse = text => {
    if (!text) return {};
    try {
      const value = JSON.parse(text);
      return value !== null && typeof value === 'object' ? value : null;
    } catch {
      return null;
    }
  };
  const create = ({fetch, csrf, flow, onUnauthorized, onSessionExpired = () => {}, now = Date.now}) => {
    let lastOwnerActivityAt = null;
    let ownerActivityPromise = null;
    const request = async (url, options = {}) => {
      const method = options.method || 'GET';
      if (flow.shouldRecordOwnerActivity(url, method)) await recordOwnerActivity();
      const response = await fetch(url, {...options, credentials: 'same-origin', headers: {
        Accept: 'application/json', 'Content-Type': 'application/json', 'X-CSRF-TOKEN': csrf, ...options.headers
      }});
      const body = parse(await response.text());
      const csrfExpired = response.status === 419 || (response.status === 403 && !body?.error?.code);
      if (!response.ok || body === null) {
        const code = csrfExpired ? 'session.expired' : body?.error?.code || (body === null ? 'response.invalid' : 'request.failed');
        const error = new Error(code);
        error.status = response.status;
        if (csrfExpired) onSessionExpired();
        else if (response.status === 401 && url.startsWith('/v1/owner/') && url !== '/v1/owner/login') onUnauthorized();
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
    const authenticated = () => { lastOwnerActivityAt = now(); };
    return Object.freeze({request, recordOwnerActivity, authenticated});
  };
  return Object.freeze({create});
});
