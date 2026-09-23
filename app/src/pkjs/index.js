'use strict';
var Clay = require('@rebble/clay');
var protocol = require('./lib/protocol.generated');
var settings = require('./lib/settings');
var text = require('./lib/text');
var codec = require('./lib/codec').create(protocol);
function log(line) { console.log(line); }
var api = require('./lib/api').create(XMLHttpRequest, settings, protocol, Date.now, log);
var transport = require('./lib/transport').create(Pebble);
var leases = require('./lib/leases').create(localStorage);
var reader = require('./lib/reader').create({api: api, settings: settings, storage: localStorage, protocol: protocol, codec: codec, text: text, transport: transport, leases: leases, log: log});
require('./lib/runtime').create({Pebble: Pebble, Clay: Clay, storage: localStorage, protocol: protocol, settings: settings, transport: transport, reader: reader, api: api, configPage: require('./lib/config-page'), localization: require('./lib/localization'), locales: require('./lib/settings-locales.generated'), getLocale: function() { return typeof navigator !== 'undefined' && navigator.language ? navigator.language : 'en'; }}).register();
