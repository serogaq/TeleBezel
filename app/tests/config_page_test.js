'use strict';
var assert = require('assert');
var page = require('../src/pkjs/lib/config-page');
var strings = require('../localization/en/settings.json');

function flat(items) { return items.reduce(function(all, item) { return all.concat(item.type === 'section' ? flat(item.items) : [item]); }, []); }
function select(items) { return flat(items).filter(function(item) { return item.messageKey === 'CONFIG_DEFAULT_ACCOUNT'; })[0]; }
var sections = page.build(strings, {accounts: [{id: 'a', name: 'Personal'}], defaultAccount: 'a'}).filter(function(item) { return item.type === 'section'; });
assert.strictEqual(sections.length, 2);
assert.deepStrictEqual(sections[0].items.map(function(item) { return item.messageKey || item.defaultValue; }),
  [strings.section_connection, 'CONFIG_ADDRESS', 'CONFIG_SSL', 'CONFIG_TOKEN']);
assert.deepStrictEqual(sections[1].items.map(function(item) { return item.messageKey || item.defaultValue; }),
  [strings.section_watch, 'CONFIG_DEFAULT_ACCOUNT', 'SHOW_ARCHIVE', 'UNREAD_MODE']);
var chooser = select(page.build(strings, {accounts: [{id: 'a', name: 'Personal'}], defaultAccount: 'a'}));
assert.strictEqual(chooser.type, 'select');
assert.deepStrictEqual(chooser.options, [{label: strings.default_account_none, value: ''}, {label: 'Personal', value: 'a'}]);
assert.strictEqual(chooser.defaultValue, 'a');
var unavailable = flat(page.build(strings, {accountsUnavailable: true}));
assert.ok(!select(unavailable));
assert.ok(unavailable.some(function(item) { return item.type === 'text' && item.defaultValue === strings.default_account_unavailable; }));
var built = page.build(strings, {});
assert.strictEqual(built[built.length - 1].type, 'submit');
var fresh = flat(built);
assert.ok(!select(fresh) && !fresh.some(function(item) { return item.type === 'text'; }));
function byKey(items, key) { return items.filter(function(item) { return item.messageKey === key; })[0]; }
var archiveToggle = byKey(fresh, 'SHOW_ARCHIVE');
assert.strictEqual(archiveToggle.type, 'toggle');
assert.strictEqual(archiveToggle.defaultValue, true);
var unreadMode = byKey(fresh, 'UNREAD_MODE');
assert.deepStrictEqual(unreadMode.options.map(function(option) { return option.value; }), ['chats', 'messages']);
assert.strictEqual(unreadMode.defaultValue, 'chats');
assert.ok(fresh.indexOf(archiveToggle) < fresh.length - 1 && fresh.indexOf(unreadMode) < fresh.length - 1);
process.stdout.write('Settings page tests passed\n');
