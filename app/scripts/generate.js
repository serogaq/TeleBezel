'use strict';

var fs = require('fs');
var path = require('path');
var root = path.resolve(__dirname, '..');
var check = process.argv.indexOf('--check') !== -1;

function readJson(relative) {
  return JSON.parse(fs.readFileSync(path.join(root, relative), 'utf8'));
}

function write(relative, content) {
  var filename = path.join(root, relative);
  if (check) {
    if (!fs.existsSync(filename) || fs.readFileSync(filename, 'utf8') !== content) {
      throw new Error(relative + ' is stale; run npm run generate');
    }
    return;
  }
  fs.mkdirSync(path.dirname(filename), {recursive: true});
  fs.writeFileSync(filename, content);
}

var protocol = readJson('protocol/appmessage.json');
write('src/c/generated/protocol.h', [
  '#ifndef TELEBEZEL_PROTOCOL_H', '#define TELEBEZEL_PROTOCOL_H', '',
  '#define TB_REQUEST_STATUS ' + protocol.request.status,
  '#define TB_REQUEST_HELLO ' + protocol.request.hello,
  '#define TB_RESPONSE_STATUS ' + protocol.response.status,
  '#define TB_RESPONSE_READY ' + protocol.response.ready,
  '#define TB_RESPONSE_REFRESH ' + protocol.response.refresh,
  '#define TB_RESULT_OK ' + protocol.result.ok,
  '#define TB_RESULT_CONFIG_MISSING ' + protocol.result.config_missing,
  '#define TB_RESULT_CONFIG_INVALID ' + protocol.result.config_invalid,
  '#define TB_RESULT_BACKEND_UNAVAILABLE ' + protocol.result.backend_unavailable,
  '#define TB_RESULT_API_UNAUTHORIZED ' + protocol.result.api_unauthorized,
  '#define TB_RESULT_BACKEND_NOT_READY ' + protocol.result.backend_not_ready,
  '#define TB_RESULT_PROTOCOL_ERROR ' + protocol.result.protocol_error,
  '', '#endif', ''
].join('\n'));
write('src/pkjs/lib/protocol.generated.js', "'use strict';\n\nmodule.exports = " + JSON.stringify(protocol, null, 2) + ';\n');

var locales = ['en', 'ru'];
var watch = {};
var settings = {};
locales.forEach(function(locale) {
  watch[locale] = readJson('localization/' + locale + '/watch.json');
  settings[locale] = readJson('localization/' + locale + '/settings.json');
});
var keys = Object.keys(watch.en);
locales.forEach(function(locale) {
  if (JSON.stringify(Object.keys(watch[locale])) !== JSON.stringify(keys)) {
    throw new Error('watch localization keys differ for ' + locale);
  }
  if (JSON.stringify(Object.keys(settings[locale])) !== JSON.stringify(Object.keys(settings.en))) {
    throw new Error('settings localization keys differ for ' + locale);
  }
});
write('src/c/generated/localization.h', [
  '#ifndef TELEBEZEL_LOCALIZATION_H', '#define TELEBEZEL_LOCALIZATION_H', '',
  'typedef struct {', keys.map(function(key) { return '  const char *' + key + ';'; }).join('\n'),
  '} TbStrings;', '', 'const TbStrings *tb_localization_current(void);', '', '#endif', ''
].join('\n'));
function cLocale(locale) {
  return 'static const TbStrings s_' + locale + ' = {\n' + keys.map(function(key) {
    return '  .' + key + ' = ' + JSON.stringify(String(watch[locale][key])) + ',';
  }).join('\n') + '\n};';
}
write('src/c/generated/localization.c', [
  '#include <pebble.h>', '#include <string.h>', '#include "localization.h"', '',
  cLocale('en'), '', cLocale('ru'), '',
  'const TbStrings *tb_localization_current(void) {',
  '  const char *locale = i18n_get_system_locale();',
  '  return locale && strncmp(locale, "ru", 2) == 0 ? &s_ru : &s_en;',
  '}', ''
].join('\n'));
write('src/pkjs/lib/settings-locales.generated.js', "'use strict';\n\nmodule.exports = " + JSON.stringify(settings, null, 2) + ';\n');
