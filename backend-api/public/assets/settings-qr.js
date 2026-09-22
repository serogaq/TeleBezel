(function (root, factory) {
  'use strict';
  const qr = factory();
  if (typeof module === 'object' && module.exports) module.exports = qr;
  if (root) root.TeleBezelQr = qr;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';

  // Byte-mode QR, error correction level M, versions 1 to 10. Login links are
  // rendered here rather than sent to a third-party image service, because the
  // link authorises a Telegram session.
  const VERSIONS = [
    {ec: 10, blocks: [[1, 16]]},
    {ec: 16, blocks: [[1, 28]]},
    {ec: 26, blocks: [[1, 44]]},
    {ec: 18, blocks: [[2, 32]]},
    {ec: 24, blocks: [[2, 43]]},
    {ec: 16, blocks: [[4, 27]]},
    {ec: 18, blocks: [[4, 31]]},
    {ec: 22, blocks: [[2, 38], [2, 39]]},
    {ec: 22, blocks: [[3, 36], [2, 37]]},
    {ec: 26, blocks: [[4, 43], [1, 44]]}
  ];
  const ALIGNMENT = [
    [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34],
    [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50]
  ];

  const EXP = new Uint8Array(512);
  const LOG = new Uint8Array(256);
  for (let index = 0, value = 1; index < 255; index += 1) {
    EXP[index] = value;
    LOG[value] = index;
    value <<= 1;
    if (value & 0x100) value ^= 0x11d;
  }
  for (let index = 255; index < 512; index += 1) EXP[index] = EXP[index - 255];

  const multiply = (left, right) => (left === 0 || right === 0 ? 0 : EXP[LOG[left] + LOG[right]]);

  const generator = degree => {
    let poly = [1];
    for (let index = 0; index < degree; index += 1) {
      const next = new Array(poly.length + 1).fill(0);
      for (let position = 0; position < poly.length; position += 1) {
        next[position] ^= poly[position];
        next[position + 1] ^= multiply(poly[position], EXP[index]);
      }
      poly = next;
    }
    return poly;
  };

  const remainder = (data, degree) => {
    const poly = generator(degree);
    const result = new Array(degree).fill(0);
    for (const byte of data) {
      const factor = byte ^ result[0];
      result.shift();
      result.push(0);
      for (let index = 0; index < degree; index += 1) {
        result[index] ^= multiply(poly[index + 1], factor);
      }
    }
    return result;
  };

  const bch = (value, generatorBits, length) => {
    let result = value << length;
    const width = 32 - Math.clz32(generatorBits);
    while (32 - Math.clz32(result) >= width) {
      result ^= generatorBits << (32 - Math.clz32(result) - width);
    }
    return (value << length) | result;
  };

  const blocksFor = version => {
    const spec = VERSIONS[version - 1];
    const sizes = [];
    for (const [count, size] of spec.blocks) {
      for (let index = 0; index < count; index += 1) sizes.push(size);
    }
    return {ec: spec.ec, sizes};
  };

  const capacity = version => blocksFor(version).sizes.reduce((total, size) => total + size, 0);

  const encodeData = (bytes, version) => {
    const total = capacity(version);
    const bits = [];
    const push = (value, length) => {
      for (let index = length - 1; index >= 0; index -= 1) bits.push((value >> index) & 1);
    };
    push(0b0100, 4);
    push(bytes.length, version < 10 ? 8 : 16);
    for (const byte of bytes) push(byte, 8);
    push(0, Math.min(4, total * 8 - bits.length));
    while (bits.length % 8 !== 0) bits.push(0);
    const codewords = [];
    for (let index = 0; index < bits.length; index += 8) {
      let value = 0;
      for (let offset = 0; offset < 8; offset += 1) value = (value << 1) | bits[index + offset];
      codewords.push(value);
    }
    const padding = [0xec, 0x11];
    while (codewords.length < total) codewords.push(padding[(codewords.length - bits.length / 8) % 2]);
    return codewords;
  };

  const interleave = (codewords, version) => {
    const {ec, sizes} = blocksFor(version);
    const data = [];
    const parity = [];
    let offset = 0;
    for (const size of sizes) {
      const block = codewords.slice(offset, offset + size);
      offset += size;
      data.push(block);
      parity.push(remainder(block, ec));
    }
    const result = [];
    for (let index = 0; index < Math.max(...sizes); index += 1) {
      for (const block of data) if (index < block.length) result.push(block[index]);
    }
    for (let index = 0; index < ec; index += 1) {
      for (const block of parity) result.push(block[index]);
    }
    return result;
  };

  const emptyGrid = size => {
    const modules = [];
    const reserved = [];
    for (let row = 0; row < size; row += 1) {
      modules.push(new Array(size).fill(false));
      reserved.push(new Array(size).fill(false));
    }
    return {modules, reserved};
  };

  const placeFunctionPatterns = (grid, version, size) => {
    const set = (row, column, value) => {
      grid.modules[row][column] = value;
      grid.reserved[row][column] = true;
    };
    const finder = (top, left) => {
      for (let row = -1; row <= 7; row += 1) {
        for (let column = -1; column <= 7; column += 1) {
          const y = top + row;
          const x = left + column;
          if (y < 0 || y >= size || x < 0 || x >= size) continue;
          const edge = Math.max(Math.abs(row - 3), Math.abs(column - 3));
          set(y, x, edge !== 2 && edge <= 3);
        }
      }
    };
    finder(0, 0);
    finder(0, size - 7);
    finder(size - 7, 0);
    for (let index = 8; index < size - 8; index += 1) {
      set(6, index, index % 2 === 0);
      set(index, 6, index % 2 === 0);
    }
    const centres = ALIGNMENT[version - 1];
    const last = centres.length - 1;
    for (let vertical = 0; vertical <= last; vertical += 1) {
      for (let horizontal = 0; horizontal <= last; horizontal += 1) {
        // The three corners are taken by the finder patterns.
        const corner = (vertical === 0 && horizontal === 0) || (vertical === 0 && horizontal === last)
          || (vertical === last && horizontal === 0);
        if (corner) continue;
        for (let dy = -2; dy <= 2; dy += 1) {
          for (let dx = -2; dx <= 2; dx += 1) {
            set(centres[vertical] + dy, centres[horizontal] + dx, Math.max(Math.abs(dy), Math.abs(dx)) !== 1);
          }
        }
      }
    }
    set(size - 8, 8, true);
    for (let index = 0; index < 9; index += 1) {
      if (!grid.reserved[8][index]) set(8, index, false);
      if (!grid.reserved[index][8]) set(index, 8, false);
    }
    for (let index = 0; index < 8; index += 1) {
      if (!grid.reserved[8][size - 1 - index]) set(8, size - 1 - index, false);
      if (!grid.reserved[size - 1 - index][8]) set(size - 1 - index, 8, false);
    }
    if (version >= 7) {
      const bits = bch(version, 0x1f25, 12);
      for (let index = 0; index < 18; index += 1) {
        const value = ((bits >> index) & 1) === 1;
        set(Math.floor(index / 3), size - 11 + (index % 3), value);
        set(size - 11 + (index % 3), Math.floor(index / 3), value);
      }
    }
  };

  const placeData = (grid, stream, size) => {
    let bit = 0;
    let upward = true;
    for (let right = size - 1; right >= 1; right -= 2) {
      if (right === 6) right = 5;
      for (let step = 0; step < size; step += 1) {
        const row = upward ? size - 1 - step : step;
        for (const column of [right, right - 1]) {
          if (grid.reserved[row][column]) continue;
          const byte = stream[bit >> 3];
          grid.modules[row][column] = byte !== undefined && ((byte >> (7 - (bit & 7))) & 1) === 1;
          bit += 1;
        }
      }
      upward = !upward;
    }
  };

  const MASKS = [
    (row, column) => (row + column) % 2 === 0,
    row => row % 2 === 0,
    (row, column) => column % 3 === 0,
    (row, column) => (row + column) % 3 === 0,
    (row, column) => (Math.floor(row / 2) + Math.floor(column / 3)) % 2 === 0,
    (row, column) => ((row * column) % 2) + ((row * column) % 3) === 0,
    (row, column) => (((row * column) % 2) + ((row * column) % 3)) % 2 === 0,
    (row, column) => (((row + column) % 2) + ((row * column) % 3)) % 2 === 0
  ];

  const placeFormat = (grid, mask, size) => {
    const bits = bch((0b00 << 3) | mask, 0x537, 10) ^ 0x5412;
    const value = index => ((bits >> index) & 1) === 1;
    for (let index = 0; index <= 5; index += 1) grid.modules[index][8] = value(index);
    grid.modules[7][8] = value(6);
    grid.modules[8][8] = value(7);
    for (let index = 8; index <= 14; index += 1) grid.modules[size - 15 + index][8] = value(index);
    for (let index = 0; index <= 7; index += 1) grid.modules[8][size - 1 - index] = value(index);
    grid.modules[8][7] = value(8);
    for (let index = 9; index <= 14; index += 1) grid.modules[8][14 - index] = value(index);
    grid.modules[size - 8][8] = true;
  };

  const penalty = (modules, size) => {
    let score = 0;
    const lines = [];
    for (let row = 0; row < size; row += 1) lines.push(modules[row]);
    for (let column = 0; column < size; column += 1) lines.push(modules.map(row => row[column]));
    for (const line of lines) {
      let run = 1;
      for (let index = 1; index < size; index += 1) {
        if (line[index] === line[index - 1]) {
          run += 1;
          continue;
        }
        if (run >= 5) score += run - 2;
        run = 1;
      }
      if (run >= 5) score += run - 2;
      for (let index = 0; index + 7 <= size; index += 1) {
        const window = line.slice(index, index + 7).map(value => (value ? 1 : 0)).join('');
        if (window === '1011101') score += 40;
      }
    }
    for (let row = 0; row + 1 < size; row += 1) {
      for (let column = 0; column + 1 < size; column += 1) {
        const first = modules[row][column];
        if (first === modules[row][column + 1] && first === modules[row + 1][column]
          && first === modules[row + 1][column + 1]) score += 3;
      }
    }
    let dark = 0;
    for (let row = 0; row < size; row += 1) {
      for (let column = 0; column < size; column += 1) if (modules[row][column]) dark += 1;
    }
    score += Math.floor(Math.abs((dark * 100) / (size * size) - 50) / 5) * 10;
    return score;
  };

  const matrix = text => {
    const bytes = Array.from(new TextEncoder().encode(String(text)));
    const version = VERSIONS.findIndex((_, index) => {
      const header = 2 + (index + 1 < 10 ? 1 : 2);
      return bytes.length + header <= capacity(index + 1);
    }) + 1;
    if (version === 0) throw new Error('qr.too_long');
    const size = 17 + 4 * version;
    const stream = interleave(encodeData(bytes, version), version);
    const base = emptyGrid(size);
    placeFunctionPatterns(base, version, size);
    placeData(base, stream, size);
    let best = null;
    for (let mask = 0; mask < 8; mask += 1) {
      const grid = {
        modules: base.modules.map(row => row.slice()),
        reserved: base.reserved
      };
      for (let row = 0; row < size; row += 1) {
        for (let column = 0; column < size; column += 1) {
          if (!grid.reserved[row][column] && MASKS[mask](row, column)) {
            grid.modules[row][column] = !grid.modules[row][column];
          }
        }
      }
      placeFormat(grid, mask, size);
      const score = penalty(grid.modules, size);
      if (best === null || score < best.score) best = {score, modules: grid.modules};
    }
    return best.modules;
  };

  const svg = (text, moduleSize = 4, quiet = 4) => {
    const modules = matrix(text);
    const size = modules.length;
    const extent = (size + quiet * 2) * moduleSize;
    let path = '';
    for (let row = 0; row < size; row += 1) {
      for (let column = 0; column < size; column += 1) {
        if (!modules[row][column]) continue;
        path += `M${(column + quiet) * moduleSize} ${(row + quiet) * moduleSize}h${moduleSize}v${moduleSize}h-${moduleSize}z`;
      }
    }
    return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${extent} ${extent}" width="${extent}" height="${extent}" role="img">`
      + `<rect width="${extent}" height="${extent}" fill="#ffffff"/>`
      + `<path d="${path}" fill="#000000"/></svg>`;
  };

  return Object.freeze({matrix, svg});
});
