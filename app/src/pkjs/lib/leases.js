'use strict';
var STORAGE_KEY = 'telebezel.view.v1';
var UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

function uuid(random) {
  var hex = '';
  for (var index = 0; index < 32; ++index) {
    var value = Math.floor(random() * 16);
    if (index === 12) { value = 4; }
    if (index === 16) { value = 8 + (value & 3); }
    hex += value.toString(16);
  }
  return hex.slice(0, 8) + '-' + hex.slice(8, 12) + '-' + hex.slice(12, 16) + '-' + hex.slice(16, 20) + '-' + hex.slice(20);
}

function create(storage, random) {
  random = random || Math.random;
  var active = null;

  function viewId() {
    var stored = null;
    try { stored = storage.getItem(STORAGE_KEY); } catch (_error) { stored = null; }
    if (typeof stored === 'string' && UUID_PATTERN.test(stored)) { return stored; }
    var created = uuid(random);
    try { storage.setItem(STORAGE_KEY, created); } catch (_error) { return created; }
    return created;
  }

  function same(left, right) { return left && right && left.account === right.account && left.chat === right.chat; }

  return {
    viewId: viewId,
    open: function(account, chat, release) {
      var next = {account: account, chat: chat};
      if (active && !same(active, next)) { release(active); }
      active = next;
    },
    close: function(account, chat, release) {
      var target = {account: account, chat: chat};
      if (same(active, target)) {
        active = null;
        release(target);
      }
    },
    reset: function() { active = null; },
    active: function() { return active; }
  };
}

module.exports = {create: create, uuid: uuid, STORAGE_KEY: STORAGE_KEY};
