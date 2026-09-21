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
  await Promise.all([api.request('/v1/owner/settings', {method: 'PUT'}), api.request('/v1/owner/settings', {method: 'PUT'})]);
  assert.equal(calls.filter(call => call.url === '/v1/owner/activity').length, 1);
  assert.equal(calls[1].options.credentials, 'same-origin');
  assert.equal(calls[1].options.headers['X-CSRF-TOKEN'], 'csrf');
  time = 60000;
  await api.request('/v1/owner/settings', {method: 'PUT'});
  assert.equal(calls.filter(call => call.url === '/v1/owner/activity').length, 2);
});

test('failed activity blocks mutation and permits a later retry', async () => {
  const calls = [];
  let unauthorized = 0;
  let expired = true;
  const api = create({csrf: 'csrf', flow, onUnauthorized: () => unauthorized++, fetch: async url => {
    calls.push(url);
    return expired ? response(401, {error: {code: 'auth.required'}}) : response(200, {data: {saved: true}});
  }});
  await assert.rejects(api.request('/v1/owner/settings', {method: 'PUT'}), {message: 'auth.required', status: 401});
  assert.deepEqual(calls, ['/v1/owner/activity']);
  assert.equal(unauthorized, 1);
  expired = false;
  assert.deepEqual(await api.request('/v1/owner/settings', {method: 'PUT'}), {saved: true});
  assert.deepEqual(calls, ['/v1/owner/activity', '/v1/owner/activity', '/v1/owner/settings']);
});
