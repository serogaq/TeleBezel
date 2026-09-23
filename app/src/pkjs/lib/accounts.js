'use strict';
var ACCOUNT_PATTERN = /^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/;

function name(account) {
  if (typeof account.label === 'string' && account.label.trim()) { return account.label; }
  var identity = account.telegram_identity;
  if (identity) {
    var full = [identity.first_name, identity.last_name].filter(function(part) { return typeof part === 'string' && part; }).join(' ');
    if (full) { return full; }
    if (identity.usernames && identity.usernames[0]) { return '@' + identity.usernames[0]; }
  }
  return String(account.id || '').slice(0, 8);
}

function valid(account) { return Boolean(account && ACCOUNT_PATTERN.test(account.id)); }

module.exports = {name: name, valid: valid, PATTERN: ACCOUNT_PATTERN};
