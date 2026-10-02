'use strict';
var TYPES = 'send,connection';

function create(options) {
  var cursors = {};
  var waiting = {};
  var generation = 0;

  function poll(value, account, callback) {
    var batch = waiting[account];
    if (batch) {
      batch.callbacks.push(callback);
      return;
    }
    batch = waiting[account] = {callbacks: [callback], handle: null};
    var started = generation;
    var resynced = !cursors[account];
    function live() { return started === generation && waiting[account] === batch; }
    function deliver(outcome) {
      delete waiting[account];
      batch.callbacks.forEach(function(done) { done(outcome); });
    }
    function attempt() {
      var cursor = cursors[account] || null;
      batch.handle = options.api.updates(value, account, {cursor: cursor, types: TYPES}, function(result) {
        if (!live()) { return; }
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
      ++generation;
      Object.keys(waiting).forEach(function(account) {
        var handle = waiting[account].handle;
        if (handle && typeof handle.abort === 'function') { handle.abort(); }
      });
      cursors = {};
      waiting = {};
    }
  };
}

module.exports = {create: create, TYPES: TYPES};
