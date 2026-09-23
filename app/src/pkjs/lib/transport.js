'use strict';
var DEFAULT_RETRIES = 2;

function create(Pebble) {
  var queue = [];
  var busy = false;

  function pump() {
    if (busy || queue.length === 0) { return; }
    var item = queue[0];
    busy = true;
    Pebble.sendAppMessage(item.message, function() {
      busy = false;
      if (queue[0] === item) { queue.shift(); }
      pump();
    }, function() {
      busy = false;
      if (queue[0] === item) {
        if (item.retries > 0) {
          --item.retries;
        } else {
          drop(item.stream);
          if (queue[0] === item) { queue.shift(); }
        }
      }
      pump();
    });
  }

  function drop(stream) {
    if (stream === null || stream === undefined) { return; }
    queue = queue.filter(function(item, index) { return (index === 0 && busy) || item.stream !== stream; });
  }

  function send(message, stream) {
    queue.push({message: message, stream: stream === undefined ? null : stream, retries: DEFAULT_RETRIES});
    pump();
  }

  return {
    send: send,
    cancel: drop,
    pending: function() { return queue.length; }
  };
}

module.exports = {create: create};
