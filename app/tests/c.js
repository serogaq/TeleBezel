'use strict';
var childProcess = require('child_process');
var fs = require('fs');
var path = require('path');
var root = path.resolve(__dirname, '..');
var suites = {
  request_layer_test: ['src/c/request_layer.c'],
  codec_test: ['src/c/codec.c', 'src/c/text.c'],
  session_test: ['src/c/session.c', 'src/c/request_layer.c', 'src/c/codec.c', 'src/c/text.c', 'src/c/errors.c'],
  chats_test: ['src/c/chats.c', 'src/c/request_layer.c', 'src/c/codec.c', 'src/c/text.c', 'src/c/errors.c'],
  history_test: ['src/c/history.c', 'src/c/request_layer.c', 'src/c/codec.c', 'src/c/text.c', 'src/c/errors.c'],
  message_text_test: ['src/c/message_text.c', 'src/c/request_layer.c', 'src/c/codec.c', 'src/c/text.c', 'src/c/errors.c'],
  format_test: ['src/c/format.c', 'src/c/errors.c', 'src/c/request_layer.c']
};
var only = process.argv.slice(2);
fs.mkdirSync(path.join(root, 'build'), {recursive: true});
Object.keys(suites).filter(function(name) { return only.length === 0 || only.indexOf(name) !== -1; }).forEach(function(name) {
  var output = path.join('build', name);
  var args = ['-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-D_POSIX_C_SOURCE=200809L', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
    '-Isrc/c', '-Itests', 'tests/' + name + '.c'].concat(suites[name], ['-o', output]);
  childProcess.execFileSync(process.env.CC || 'cc', args, {cwd: root, stdio: 'inherit'});
  childProcess.execFileSync(path.join(root, output), [], {cwd: root, stdio: 'inherit', env: Object.assign({}, process.env, {TZ: 'UTC'})});
  process.stdout.write(name + ' passed\n');
});
