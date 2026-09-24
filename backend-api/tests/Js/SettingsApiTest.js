'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const {create} = require('../../public/assets/settings-api');
const flow = require('../../public/assets/settings-flow');
const response = (status, body) => ({ok: status < 400, status, text: async () => JSON.stringify(body)});

test('concurrent mutations share activity request and renew it after one minute', async () => {
  const calls = [];
  let time = 0;
  const api = create({csrf: 'csrf', flow, now: () => time, onUnauthorized: () => {}, fetch: async (url, options) => {
    calls.push({url, options});
    return response(200, {data: {ok: true}});
  }});
  await Promise.all([api.request('/v1/settings', {method: 'PUT'}), api.request('/v1/settings', {method: 'PUT'})]);
  assert.equal(calls.filter(call => call.url === '/v1/session/activity').length, 1);
  assert.equal(calls[1].options.credentials, 'same-origin');
  assert.equal(calls[1].options.headers['X-CSRF-TOKEN'], 'csrf');
  time = 60000;
  await api.request('/v1/settings', {method: 'PUT'});
  assert.equal(calls.filter(call => call.url === '/v1/session/activity').length, 2);
});

test('failed activity blocks mutation and permits a later retry', async () => {
  const calls = [];
  let unauthorized = 0;
  let expired = true;
  const api = create({csrf: 'csrf', flow, onUnauthorized: () => unauthorized++, fetch: async url => {
    calls.push(url);
    return expired ? response(401, {error: {code: 'auth.required'}}) : response(200, {data: {saved: true}});
  }});
  await assert.rejects(api.request('/v1/settings', {method: 'PUT'}), {message: 'auth.required', status: 401});
  assert.deepEqual(calls, ['/v1/session/activity']);
  assert.equal(unauthorized, 1);
  expired = false;
  assert.deepEqual(await api.request('/v1/settings', {method: 'PUT'}), {saved: true});
  assert.deepEqual(calls, ['/v1/session/activity', '/v1/session/activity', '/v1/settings']);
});

test('expired csrf session asks for a reload instead of failing silently', async () => {
  let expired = 0;
  let unauthorized = 0;
  const api = create({csrf: 'csrf', flow, onUnauthorized: () => unauthorized++, onSessionExpired: () => expired++, fetch: async () => ({ok: false, status: 419, text: async () => '{"message":"CSRF token mismatch."}'})});
  await assert.rejects(api.request('/v1/settings'), {message: 'session.expired', status: 419});
  assert.equal(expired, 1);
  assert.equal(unauthorized, 0);
});

test('a non-json response is reported as an invalid response', async () => {
  const api = create({csrf: 'csrf', flow, onUnauthorized: () => {}, fetch: async () => ({ok: true, status: 200, text: async () => '<html>proxy error</html>'})});
  await assert.rejects(api.request('/v1/settings'), {message: 'response.invalid'});
});

test('the session csrf value from sign-in is sent with every later request', async () => {
  const calls = [];
  const api = create({csrf: 'laravel', apiCsrf: '', flow, onUnauthorized: () => {}, fetch: async (url, options) => {
    calls.push(options.headers);
    return {ok: true, status: 200, text: async () => '{"data":{}}'};
  }});
  await api.request('/v1/session/login', {method: 'POST'});
  assert.equal(calls[0]['X-TeleBezel-CSRF'], undefined);
  api.authenticated({csrf_token: 'bound'});
  await api.request('/v1/settings');
  assert.equal(calls[1]['X-TeleBezel-CSRF'], 'bound');
  assert.equal(calls[1]['X-CSRF-TOKEN'], 'laravel');
});
