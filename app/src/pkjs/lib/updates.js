'use strict';
var TYPES = 'send,connection';

function create(options) {
  var cursors = {};
  var waiting = {};

  function poll(value, account, callback) {
    if (waiting[account]) {
      waiting[account].push(callback);
      return;
    }
    waiting[account] = [callback];
    var resynced = !cursors[account];
    function deliver(outcome) {
      var callbacks = waiting[account] || [];
      delete waiting[account];
      callbacks.forEach(function(done) { done(outcome); });
    }
    function attempt() {
      var cursor = cursors[account] || null;
      options.api.updates(value, account, {cursor: cursor, types: TYPES}, function(result) {
        if (!result.ok && result.action === 'resync' && cursor) {
          delete cursors[account];
          resynced = true;
          attempt();
          return;
        }
        if (!result.ok) {
          deliver({ok: false, result: result, events: [], resynced: resynced});
          return;
        }
        var data = result.data;
        if (typeof data.cursor === 'string' && data.cursor) { cursors[account] = data.cursor; }
        deliver({ok: true, result: result, events: Array.isArray(data.events) ? data.events : [], resynced: resynced});
      });
    }
    attempt();
  }

  return {
    poll: poll,
    primed: function(account) { return Boolean(cursors[account]); },
    reset: function() {
      cursors = {};
      waiting = {};
    }
  };
}

module.exports = {create: create, TYPES: TYPES};
