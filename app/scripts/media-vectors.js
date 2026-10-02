'use strict';

var Buffer = require('buffer').Buffer;
var fs = require('fs');
var path = require('path');
var tbi = require('../tests/helpers/tbi');
var root = path.resolve(__dirname, '../../tests/contracts/media');

function photo(x, y) { return (((x * 7) ^ (y * 3)) >> 4) & 15; }
function stripes(x, y) { return (y >> 4) & 3; }

var vectors = [
  {name: 'rect_p4', bits: 4, shape: 0, width: 200, height: 228, canvasWidth: 200, canvasHeight: 228, tag: 0x11111111, pixel: photo},
  {name: 'round_p4_landscape', bits: 4, shape: 1, width: 260, height: 146, canvasWidth: 260, canvasHeight: 260, tag: 0x22222222, pixel: photo},
  {name: 'round_p4_square', bits: 4, shape: 1, width: 260, height: 260, canvasWidth: 260, canvasHeight: 260, tag: 0x33333333, pixel: photo},
  {name: 'round_p2_portrait', bits: 2, shape: 1, width: 180, height: 260, canvasWidth: 260, canvasHeight: 260, tag: 0x44444444, pixel: stripes,
   palette: [0xC0, 0xD5, 0xEA, 0xFF]},
  {name: 'rect_p1_small', bits: 1, shape: 0, width: 13, height: 7, canvasWidth: 200, canvasHeight: 228, tag: 0x55555555,
   pixel: function(x, y) { return (x + y) & 1; }, palette: [0xC0, 0xFF]}
];

fs.mkdirSync(root, {recursive: true});
var manifest = vectors.map(function(vector) {
  var encoded = tbi.sample(vector);
  fs.writeFileSync(path.join(root, vector.name + '.tbi'), Buffer.from(encoded.bytes));
  return {name: vector.name, bits: vector.bits, shape: vector.shape, width: vector.width, height: vector.height, canvasWidth: vector.canvasWidth,
    canvasHeight: vector.canvasHeight, tag: vector.tag, size: encoded.size, crc: encoded.crc, bytes: encoded.bytes.length};
});
fs.writeFileSync(path.join(root, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
