(function (root, factory) {
  'use strict';
  const flow = factory();
  if (typeof module === 'object' && module.exports) module.exports = flow;
  if (root) root.TeleBezelSettingsFlow = flow;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';

  const ownerActivityExclusions = new Set([
    '/v1/owner/activity',
    '/v1/owner/login',
    '/v1/owner/bootstrap',
    '/v1/owner/recover'
  ]);

  const shouldRecordOwnerActivity = (url, method = 'GET') => method !== 'GET'
    && url.startsWith('/v1/owner/')
    && !ownerActivityExclusions.has(url);

  const authorizationPayload = (authorization, action, value) => {
    if (!authorization || typeof authorization.authorization_version !== 'string' || !authorization.authorization_version) {
      throw new Error('authorization.invalid_state');
    }
    return {
      action,
      authorization_version: authorization.authorization_version,
      ...(value === null ? {} : {value})
    };
  };

  const accountView = account => {
    const runtime = account.runtime || {};
    const authorizationState = runtime.authorization_state || 'unknown';
    const connectionState = runtime.connection_state || 'unknown';
    const errorCode = account.last_error?.code || null;
    const waitingForRuntime = account.lifecycle === 'provisioning'
      || runtime.available !== true
      || authorizationState === 'awaiting_reconciliation';
    const authorizationComplete = authorizationState === 'ready';
    return {
      authorizationState,
      connectionState,
      errorCode,
      waitingForRuntime,
      canAuthorize: account.lifecycle === 'active' && runtime.available === true
        && !authorizationComplete && authorizationState !== 'unknown',
      shouldPoll: waitingForRuntime && !errorCode,
      buttonLabel: errorCode ? 'Needs attention' : waitingForRuntime ? 'Preparing…'
        : authorizationComplete ? 'Authorized' : 'Authorization'
    };
  };

  return Object.freeze({accountView, authorizationPayload, shouldRecordOwnerActivity});
});
