'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');
const qr = require('../../public/assets/settings-qr');

// Read a matrix back the way a scanner does, so the test proves the modules
// carry the link rather than merely that some modules were produced.
const decode = modules => {
  const size = modules.length;
  const version = (size - 17) / 4;
  let format = 0;
  for (let index = 0; index <= 5; index += 1) if (modules[index][8]) format |= 1 << index;
  if (modules[7][8]) format |= 1 << 6;
  if (modules[8][8]) format |= 1 << 7;
  for (let index = 8; index <= 14; index += 1) if (modules[size - 15 + index][8]) format |= 1 << index;
  const raw = format ^ 0x5412;
  const mask = (raw >> 10) & 7;
  assert.equal((raw >> 13) & 3, 0, 'error correction level M');
  const masks = [
    (row, column) => (row + column) % 2 === 0,
    row => row % 2 === 0,
    (row, column) => column % 3 === 0,
    (row, column) => (row + column) % 3 === 0,
    (row, column) => (Math.floor(row / 2) + Math.floor(column / 3)) % 2 === 0,
    (row, column) => ((row * column) % 2) + ((row * column) % 3) === 0,
    (row, column) => (((row * column) % 2) + ((row * column) % 3)) % 2 === 0,
    (row, column) => (((row + column) % 2) + ((row * column) % 3)) % 2 === 0
  ];
  const reserved = [];
  for (let row = 0; row < size; row += 1) reserved.push(new Array(size).fill(false));
  const reserve = (row, column) => {
    if (row >= 0 && row < size && column >= 0 && column < size) reserved[row][column] = true;
  };
  for (const [top, left] of [[0, 0], [0, size - 7], [size - 7, 0]]) {
    for (let row = -1; row <= 7; row += 1) for (let column = -1; column <= 7; column += 1) reserve(top + row, left + column);
  }
  for (let index = 0; index < size; index += 1) {
    reserve(6, index);
    reserve(index, 6);
  }
  for (let index = 0; index <= 8; index += 1) {
    reserve(8, index);
    reserve(index, 8);
  }
  for (let index = 0; index < 8; index += 1) {
    reserve(8, size - 1 - index);
    reserve(size - 1 - index, 8);
  }
  const centres = [[], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34], [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50]][version - 1];
  const last = centres.length - 1;
  for (let vertical = 0; vertical <= last; vertical += 1) {
    for (let horizontal = 0; horizontal <= last; horizontal += 1) {
      if ((vertical === 0 && horizontal === 0) || (vertical === 0 && horizontal === last)
        || (vertical === last && horizontal === 0)) continue;
      for (let dy = -2; dy <= 2; dy += 1) for (let dx = -2; dx <= 2; dx += 1) reserve(centres[vertical] + dy, centres[horizontal] + dx);
    }
  }
  if (version >= 7) {
    for (let index = 0; index < 18; index += 1) {
      reserve(Math.floor(index / 3), size - 11 + (index % 3));
      reserve(size - 11 + (index % 3), Math.floor(index / 3));
    }
  }
  const bits = [];
  let upward = true;
  for (let right = size - 1; right >= 1; right -= 2) {
    if (right === 6) right = 5;
    for (let step = 0; step < size; step += 1) {
      const row = upward ? size - 1 - step : step;
      for (const column of [right, right - 1]) {
        if (reserved[row][column]) continue;
        bits.push(modules[row][column] !== masks[mask](row, column) ? 1 : 0);
      }
    }
    upward = !upward;
  }
  const codewords = [];
  for (let index = 0; index + 8 <= bits.length; index += 8) {
    let value = 0;
    for (let offset = 0; offset < 8; offset += 1) value = (value << 1) | bits[index + offset];
    codewords.push(value);
  }
  const blocks = [[1, 16], [1, 28], [1, 44], [2, 32], [2, 43], [4, 27], [4, 31], [2, 38, 2, 39], [3, 36, 2, 37], [4, 43, 1, 44]][version - 1];
  const sizes = [];
  for (let index = 0; index < blocks.length; index += 2) {
    for (let count = 0; count < blocks[index]; count += 1) sizes.push(blocks[index + 1]);
  }
  const data = sizes.map(() => []);
  let cursor = 0;
  for (let index = 0; index < Math.max(...sizes); index += 1) {
    for (let block = 0; block < sizes.length; block += 1) {
      if (index < sizes[block]) data[block].push(codewords[cursor++]);
    }
  }
  const stream = data.flat();
  let bit = 0;
  const take = length => {
    let value = 0;
    for (let index = 0; index < length; index += 1, bit += 1) {
      value = (value << 1) | ((stream[bit >> 3] >> (7 - (bit & 7))) & 1);
    }
    return value;
  };
  assert.equal(take(4), 0b0100, 'byte mode');
  const count = take(version < 10 ? 8 : 16);
  const bytes = [];
  for (let index = 0; index < count; index += 1) bytes.push(take(8));
  return Buffer.from(bytes).toString('utf8');
};

test('a login link round-trips through the generated matrix', () => {
  for (const link of ['tg://login?token=AbCdEf0123456789-_xyz==', 'tg://login?token=' + 'Zz9-_'.repeat(14), 'x']) {
    assert.equal(decode(qr.matrix(link)), link);
  }
});

test('the matrix grows with the payload and stays square', () => {
  const small = qr.matrix('x');
  const large = qr.matrix('y'.repeat(200));
  assert.equal(small.length, 21);
  assert.equal(large.length, 57);
  assert.ok(small.every(row => row.length === 21));
  assert.ok(large.every(row => row.length === 57));
});

test('finder patterns sit in the three corners', () => {
  const modules = qr.matrix('tg://login?token=abc');
  const size = modules.length;
  for (const [top, left] of [[0, 0], [0, size - 7], [size - 7, 0]]) {
    assert.ok(modules[top][left] && modules[top + 6][left + 6] && !modules[top + 1][left + 1]);
  }
});

test('an oversized payload is refused rather than silently truncated', () => {
  assert.throws(() => qr.matrix('z'.repeat(1000)), /qr.too_long/);
});

test('the rendered svg is self-contained', () => {
  const svg = qr.svg('tg://login?token=abc');
  assert.match(svg, /^<svg xmlns="http:\/\/www\.w3\.org\/2000\/svg"/);
  assert.ok(!svg.includes('http') || svg.indexOf('http') === svg.indexOf('http://www.w3.org/2000/svg'));
});
