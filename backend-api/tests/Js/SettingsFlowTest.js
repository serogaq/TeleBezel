'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const flow = require('../../public/assets/settings-flow');

test('authorization action preserves the server authorization version', () => {
  assert.deepEqual(flow.authorizationPayload({authorization_version: '17'}, 'submit_phone_number', '+995555000000'), {
    action: 'submit_phone_number',
    authorization_version: '17',
    value: '+995555000000'
  });
  assert.throws(() => flow.authorizationPayload({version: '17'}, 'submit_code', '12345'), /authorization.invalid_state/);
});

test('owner mutations record activity without recursively touching auth endpoints', () => {
  assert.equal(flow.shouldRecordOwnerActivity('/v1/owner/telegram/accounts/id/authorization/actions', 'POST'), true);
  assert.equal(flow.shouldRecordOwnerActivity('/v1/owner/activity', 'POST'), false);
  assert.equal(flow.shouldRecordOwnerActivity('/v1/owner/login', 'POST'), false);
  assert.equal(flow.shouldRecordOwnerActivity('/v1/owner/settings', 'GET'), false);
});

test('account controls follow runtime readiness and expose terminal errors', () => {
  assert.deepEqual(flow.accountView({lifecycle: 'provisioning', runtime: {available: false, authorization_state: 'awaiting_reconciliation'}}), {
    authorizationState: 'awaiting_reconciliation', connectionState: 'unknown', errorCode: null,
    waitingForRuntime: true, canAuthorize: false, shouldPoll: true, buttonLabel: 'Preparing…'
  });
  assert.equal(flow.accountView({lifecycle: 'active', runtime: {available: true, authorization_state: 'awaiting_code'}}).canAuthorize, true);
  const failed = flow.accountView({lifecycle: 'provisioning', runtime: {available: false}, last_error: {code: 'configuration.missing'}});
  assert.equal(failed.shouldPoll, false);
  assert.equal(failed.buttonLabel, 'Needs attention');
});
