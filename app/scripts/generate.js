'use strict';

var Buffer = require('buffer').Buffer;
var fs = require('fs');
var path = require('path');
var root = path.resolve(__dirname, '..');

function readJson(relative) {
  return JSON.parse(fs.readFileSync(path.join(root, relative), 'utf8'));
}

function write(relative, content) {
  var filename = path.join(root, relative);
  if (fs.existsSync(filename) && fs.readFileSync(filename, 'utf8') === content) { return; }
  fs.mkdirSync(path.dirname(filename), {recursive: true});
  fs.writeFileSync(filename, content);
}

var protocol = readJson('protocol/appmessage.json');
var defines = [];
Object.keys(protocol).forEach(function(group) {
  if (group === 'errors') { return; }
  Object.keys(protocol[group]).forEach(function(name) {
    var value = protocol[group][name];
    if (!Number.isInteger(value)) { throw new Error('non-integer protocol value ' + group + '.' + name); }
    defines.push('#define TB_' + group.toUpperCase() + '_' + name.toUpperCase().replace(/[^A-Z0-9]/g, '_') + ' ' + value);
  });
});
write('src/c/generated/protocol.h', [
  '#ifndef TELEBEZEL_PROTOCOL_H', '#define TELEBEZEL_PROTOCOL_H', ''
].concat(defines, ['', '#endif', '']).join('\n'));
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
  '} TbStrings;', '', 'const TbStrings *tb_localization_current(void);', 'void tb_localization_release(void);', '', '#endif', ''
].join('\n'));
locales.forEach(function(locale) {
  var blob = Buffer.concat(keys.map(function(key) {
    var value = String(watch[locale][key]);
    if (value.indexOf('\u0000') !== -1) { throw new Error('NUL in watch string ' + locale + '.' + key); }
    return Buffer.concat([Buffer.from(value, 'utf8'), Buffer.from([0])]);
  }));
  var filename = path.join(root, 'resources/generated/strings_' + locale + '.bin');
  if (!fs.existsSync(filename) || !fs.readFileSync(filename).equals(blob)) {
    fs.mkdirSync(path.dirname(filename), {recursive: true});
    fs.writeFileSync(filename, blob);
  }
});
write('src/c/generated/localization.c', [
  '#include <pebble.h>', '#include <string.h>', '#include "localization.h"', '',
  '#define TB_STRING_COUNT ' + keys.length, '',
  'static TbStrings *s_strings;', '',
  'const TbStrings *tb_localization_current(void) {',
  '  if (s_strings) { return s_strings; }',
  '  const char *locale = i18n_get_system_locale();',
  '  const uint32_t id = locale && strncmp(locale, "ru", 2) == 0 ? RESOURCE_ID_STRINGS_RU : RESOURCE_ID_STRINGS_EN;',
  '  ResHandle handle = resource_get_handle(id);',
  '  const size_t size = resource_size(handle);',
  '  s_strings = malloc(sizeof(TbStrings) + size + 1);',
  '  if (!s_strings) { return NULL; }',
  '  char *text = (char *)(s_strings + 1);',
  '  const size_t length = resource_load(handle, (uint8_t *)text, size);',
  '  text[length] = 0;',
  '  const char **slot = (const char **)s_strings;',
  '  size_t offset = 0;',
  '  for (int index = 0; index < TB_STRING_COUNT; ++index) {',
  '    slot[index] = offset < length ? text + offset : text + length;',
  '    while (offset < length && text[offset]) { ++offset; }',
  '    ++offset;',
  '  }',
  '  return s_strings;',
  '}', '',
  'void tb_localization_release(void) {',
  '  free(s_strings);',
  '  s_strings = NULL;',
  '}', ''
].join('\n'));
write('src/pkjs/lib/settings-locales.generated.js', "'use strict';\n\nmodule.exports = " + JSON.stringify(settings, null, 2) + ';\n');

var vectors = readJson('tests/fixtures/codec.json');
write('tests/generated/codec_vectors.h', ['#pragma once', '#include <stdint.h>', ''].concat(vectors.map(function(vector) {
  var bytes = vector.hex.match(/../g).map(function(pair) { return '0x' + pair; });
  return 'static const uint8_t tb_vector_' + vector.name + '[] = {' + bytes.join(', ') + '};';
}), ['']).join('\n'));
