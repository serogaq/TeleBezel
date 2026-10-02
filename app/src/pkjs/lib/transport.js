'use strict';
var DEFAULT_RETRIES = 2;

function create(Pebble) {
  var interactive = [];
  var bulk = [];
  var current = null;

  function pump() {
    if (current || (interactive.length === 0 && bulk.length === 0)) { return; }
    var item = interactive.length ? interactive.shift() : bulk.shift();
    current = item;
    Pebble.sendAppMessage(item.message, function() {
      current = null;
      pump();
    }, function() {
      current = null;
      if (!item.dropped) {
        if (item.retries > 0) {
          --item.retries;
          (item.bulk ? bulk : interactive).unshift(item);
        } else {
          drop(item.stream);
        }
      }
      pump();
    });
  }

  function drop(stream) {
    if (stream === null || stream === undefined) { return; }
    function keep(item) { return item.stream !== stream; }
    interactive = interactive.filter(keep);
    bulk = bulk.filter(keep);
    if (current && current.stream === stream) { current.dropped = true; }
  }

  function send(message, stream, lane) {
    var item = {message: message, stream: stream === undefined ? null : stream, retries: DEFAULT_RETRIES, bulk: lane === 'bulk', dropped: false};
    (item.bulk ? bulk : interactive).push(item);
    pump();
  }

  return {
    send: send,
    cancel: drop,
    pending: function() { return interactive.length + bulk.length + (current ? 1 : 0); }
  };
}

module.exports = {create: create};
