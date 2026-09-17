'use strict';
var Clay = require('@rebble/clay');
var protocol = require('./lib/protocol.generated');
var settings = require('./lib/settings');
var api = require('./lib/api').create(XMLHttpRequest, settings, protocol);
require('./lib/runtime').create({Pebble: Pebble, Clay: Clay, storage: localStorage, protocol: protocol, settings: settings, api: api, configPage: require('./lib/config-page'), localization: require('./lib/localization'), locales: require('./lib/settings-locales.generated'), getLocale: function() { return typeof navigator !== 'undefined' && navigator.language ? navigator.language : 'en'; }}).register();
