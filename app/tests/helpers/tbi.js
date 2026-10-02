'use strict';

function isqrt(value) {
  var root = 0;
  var bit = 1 << 30;
  while (bit > value) { bit = bit >>> 2; }
  while (bit) {
    if (value >= root + bit) {
      value -= root + bit;
      root = (root >>> 1) + bit;
    } else {
      root = root >>> 1;
    }
    bit = bit >>> 2;
  }
  return root;
}

function row(image, y) {
  var left = Math.trunc((image.canvasWidth - image.width) / 2);
  var top = Math.trunc((image.canvasHeight - image.height) / 2);
  var screenY = top + y;
  var from = left;
  var to = left + image.width;
  if (image.shape === 1) {
    var diameter = image.canvasWidth;
    var dy = 2 * screenY + 1 - image.canvasHeight;
    from = 0;
    to = 0;
    if (dy > -diameter && dy < diameter) {
      var half = isqrt(diameter * diameter - dy * dy);
      from = Math.trunc((diameter - half - 1) / 2) - 1;
      to = Math.trunc((diameter + half - 1) / 2) + 2;
    }
    if (from < left) { from = left; }
    if (to > left + image.width) { to = left + image.width; }
  }
  var count = to > from ? to - from : 0;
  return {x: from - left, count: count, bytes: Math.ceil(count * image.bits / 8)};
}

function pack(image) {
  var out = [];
  for (var y = 0; y < image.height; ++y) {
    var geometry = row(image, y);
    var bytes = new Array(geometry.bytes).fill(0);
    for (var index = 0; index < geometry.count; ++index) {
      var value = image.pixel(geometry.x + index, y) & ((1 << image.bits) - 1);
      var bit = index * image.bits;
      bytes[bit >> 3] |= value << (8 - image.bits - (bit & 7));
    }
    out = out.concat(bytes);
  }
  return out;
}

function crc32(bytes) {
  var crc = 0xFFFFFFFF;
  for (var index = 0; index < bytes.length; ++index) {
    crc ^= bytes[index];
    for (var bit = 0; bit < 8; ++bit) { crc = (crc >>> 1) ^ (0xEDB88320 & (-(crc & 1))); }
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function compress(data) {
  var out = [];
  var literal = [];
  function flush() {
    while (literal.length) {
      var part = literal.splice(0, 128);
      out.push(part.length - 1);
      out = out.concat(part);
    }
  }
  var position = 0;
  while (position < data.length) {
    var run = 1;
    while (position + run < data.length && data[position + run] === data[position] && run < 66) { ++run; }
    var best = 0;
    var bestDistance = 0;
    for (var distance = 1; distance <= Math.min(position, 4096); ++distance) {
      var length = 0;
      while (position + length < data.length && length < 66 && data[position + length] === data[position + length - distance]) { ++length; }
      if (length > best) { best = length; bestDistance = distance; }
    }
    if (run >= 3 && run >= best) {
      flush();
      out.push(0x80 | (run - 3), data[position]);
      position += run;
    } else if (best >= 4) {
      flush();
      out.push(0xC0 | (best - 3), bestDistance & 0xFF, bestDistance >> 8);
      position += best;
    } else {
      literal.push(data[position]);
      ++position;
    }
  }
  flush();
  return out;
}

function encode(image) {
  var pixels = pack(image);
  var header = [0x54, 0x42, 1, image.bits, image.shape, image.palette.length];
  function u16(value) { header.push(value & 0xFF, (value >> 8) & 0xFF); }
  function u32(value) { header.push(value & 0xFF, (value >>> 8) & 0xFF, (value >>> 16) & 0xFF, (value >>> 24) & 0xFF); }
  u16(image.width);
  u16(image.height);
  u16(image.canvasWidth);
  u16(image.canvasHeight);
  u32(image.tag >>> 0);
  u32(crc32(pixels));
  header = header.concat(image.palette);
  return {bytes: header.concat(compress(pixels)), size: pixels.length, crc: crc32(pixels)};
}

function sample(options) {
  var width = options.width;
  var height = options.height;
  return encode({
    bits: options.bits || 4,
    shape: options.shape || 0,
    width: width,
    height: height,
    canvasWidth: options.canvasWidth || width,
    canvasHeight: options.canvasHeight || height,
    tag: options.tag || 1,
    palette: options.palette || [0xC0, 0xFF, 0xC3, 0xF0, 0xCC, 0xFC, 0xCF, 0xF3, 0xD5, 0xEA, 0xC1, 0xC4, 0xD0, 0xE0, 0xC8, 0xE5],
    pixel: options.pixel || function(x, y) { return ((x >> 3) + (y >> 3)) & 15; }
  });
}

module.exports = {encode: encode, sample: sample, row: row, crc32: crc32, isqrt: isqrt};
