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
  settings[locale] = readJson('localization/' + locale + '/companion.json');
});
var keys = Object.keys(watch.en);
function requireSequence(prefix, group, first, last) {
  var names = Object.keys(protocol[group]).filter(function(name) { return protocol[group][name] >= protocol[group][first] && protocol[group][name] <= protocol[group][last]; });
  var start = keys.indexOf(prefix + first);
  names.forEach(function(name, offset) {
    if (keys[start + offset] !== prefix + name) { throw new Error('watch strings must list ' + prefix + '* in protocol order at ' + name); }
  });
}
requireSequence('kind_', 'kind', 'photo', 'unsupported');
requireSequence('action_', 'action', 'members_added', 'topic_created');
locales.forEach(function(locale) {
  if (JSON.stringify(Object.keys(watch[locale])) !== JSON.stringify(keys)) {
    throw new Error('watch localization keys differ for ' + locale);
  }
  if (JSON.stringify(Object.keys(settings[locale])) !== JSON.stringify(Object.keys(settings.en))) {
    throw new Error('companion localization keys differ for ' + locale);
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

var iconBlocks = fs.readFileSync(path.join(root, 'resources/icons.txt'), 'utf8').split(/\n\s*\n/).filter(function(block) { return block.trim(); });
if (iconBlocks.length > 255) { throw new Error('too many icons'); }
var iconHeader = [Buffer.from([iconBlocks.length])];
var iconData = [];
var iconOffset = 1 + iconBlocks.length * 4;
var iconNames = iconBlocks.map(function(block, index) {
  var lines = block.split('\n').filter(function(line) { return line.length; });
  var match = /^([a-z0-9_]+) (\d+)x(\d+)$/.exec(lines[0]);
  if (!match) { throw new Error('bad icon header: ' + lines[0]); }
  var width = Number(match[2]);
  var height = Number(match[3]);
  var rows = lines.slice(1);
  if (width < 1 || width > 16 || height < 1 || height > 16 || rows.length !== height) { throw new Error('icon ' + match[1] + ' does not match ' + width + 'x' + height); }
  var stride = Math.ceil(width / 4);
  var pixels = Buffer.alloc(stride * height);
  rows.forEach(function(row, y) {
    if (row.length !== width + 2 || row[0] !== '|' || row[row.length - 1] !== '|') { throw new Error('icon ' + match[1] + ' row ' + y + ' is not ' + width + ' wide'); }
    for (var x = 0; x < width; ++x) {
      var alpha = ' .+#'.indexOf(row[x + 1]);
      if (alpha < 0) { throw new Error('icon ' + match[1] + ' has an unknown pixel'); }
      pixels[y * stride + (x >> 2)] |= alpha << (6 - 2 * (x & 3));
    }
  });
  var entry = Buffer.alloc(4);
  entry.writeUInt8(width, 0);
  entry.writeUInt8(height, 1);
  entry.writeUInt16LE(iconOffset, 2);
  iconHeader.push(entry);
  iconData.push(pixels);
  iconOffset += pixels.length;
  return '#define TB_ICON_' + match[1].toUpperCase() + ' ' + index;
});
var iconBlob = Buffer.concat(iconHeader.concat(iconData));
var iconFile = path.join(root, 'resources/generated/icons.bin');
if (!fs.existsSync(iconFile) || !fs.readFileSync(iconFile).equals(iconBlob)) {
  fs.mkdirSync(path.dirname(iconFile), {recursive: true});
  fs.writeFileSync(iconFile, iconBlob);
}
write('src/c/generated/icons.h', ['#pragma once', ''].concat(iconNames, ['']).join('\n'));

var mediaRoot = path.join(root, '..', 'tests', 'contracts', 'media');
var mediaManifests = fs.existsSync(mediaRoot) ? fs.readdirSync(mediaRoot).filter(function(name) { return /^manifest.*\.json$/.test(name); }).sort() : [];
if (mediaManifests.length) {
  var media = [].concat.apply([], mediaManifests.map(function(name) { return JSON.parse(fs.readFileSync(path.join(mediaRoot, name), 'utf8')); }));
  write('tests/generated/image_vectors.h', ['#pragma once', '#include <stdint.h>', '',
    'typedef struct { const char *name; const uint8_t *data; uint32_t length; uint8_t bits; uint8_t shape; uint16_t width; uint16_t height;',
    '  uint16_t canvas_width; uint16_t canvas_height; uint32_t tag; uint32_t size; uint32_t crc; } TbImageVector;', ''
  ].concat(media.map(function(vector) {
    var bytes = Array.prototype.slice.call(fs.readFileSync(path.join(mediaRoot, vector.name + '.tbi'))).map(function(value) { return '0x' + value.toString(16); });
    return 'static const uint8_t tb_image_' + vector.name + '[] = {' + bytes.join(', ') + '};';
  }), ['', 'static const TbImageVector tb_image_vectors[] = {'], media.map(function(vector) {
    return '  {"' + vector.name + '", tb_image_' + vector.name + ', sizeof(tb_image_' + vector.name + '), ' + [vector.bits, vector.shape, vector.width,
      vector.height, vector.canvasWidth, vector.canvasHeight, vector.tag + 'u', vector.size, vector.crc + 'u'].join(', ') + '},';
  }), ['};', '']).join('\n'));
}
