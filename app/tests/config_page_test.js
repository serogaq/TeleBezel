'use strict';
var assert = require('assert');
var page = require('../src/pkjs/lib/config-page');
var strings = require('../localization/en/settings.json');

function select(items) { return items.filter(function(item) { return item.messageKey === 'CONFIG_DEFAULT_ACCOUNT'; })[0]; }
var chooser = select(page.build(strings, {accounts: [{id: 'a', name: 'Personal'}], defaultAccount: 'a'}));
assert.strictEqual(chooser.type, 'select');
assert.deepStrictEqual(chooser.options, [{label: strings.default_account_none, value: ''}, {label: 'Personal', value: 'a'}]);
assert.strictEqual(chooser.defaultValue, 'a');
var unavailable = page.build(strings, {accountsUnavailable: true});
assert.ok(!select(unavailable));
assert.ok(unavailable.some(function(item) { return item.type === 'text' && item.defaultValue === strings.default_account_unavailable; }));
var fresh = page.build(strings, {});
assert.ok(!select(fresh) && !fresh.some(function(item) { return item.type === 'text'; }));
assert.strictEqual(fresh[fresh.length - 1].type, 'submit');
process.stdout.write('Settings page tests passed\n');
