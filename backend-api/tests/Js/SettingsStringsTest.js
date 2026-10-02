'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');
const load = locale => JSON.parse(fs.readFileSync(path.join(root, `localization/${locale}/backend.json`), 'utf8'));

test('English and Russian carry the same strings in the same order', () => {
  assert.deepEqual(Object.keys(load('ru')), Object.keys(load('en')));
  for (const [key, value] of Object.entries(load('ru'))) {
    assert.ok(value.trim(), `empty Russian string ${key}`);
    const placeholders = text => (text.match(/\{\w+\}/g) || []).sort();
    assert.deepEqual(placeholders(value), placeholders(load('en')[key]), `placeholders differ for ${key}`);
  }
});

test('every string the page asks for exists', () => {
  const en = load('en');
  const script = fs.readFileSync(path.join(root, 'public/assets/settings.js'), 'utf8');
  const flow = fs.readFileSync(path.join(root, 'public/assets/settings-flow.js'), 'utf8');
  const view = fs.readFileSync(path.join(root, 'resources/views/settings.blade.php'), 'utf8');
  const used = new Set([
    ...[...script.matchAll(/\bt\('([a-z_]+)'/g)].map(match => match[1]),
    ...[...script.matchAll(/\['([a-z_]+)', '(?:tel|text|password|email)', '([a-z_]+)'\]/g)].flatMap(match => [match[1], match[2]]),
    ...[...flow.matchAll(/'(needs_attention|preparing|authorized|authorization)'/g)].map(match => match[1]),
    ...[...view.matchAll(/\$t\['([a-z_]+)'\]/g)].map(match => match[1])
  ]);
  assert.ok(used.size > 60);
  for (const key of used) assert.ok(Object.hasOwn(en, key), `missing string ${key}`);
});
