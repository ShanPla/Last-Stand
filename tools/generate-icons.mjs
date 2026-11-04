// Draws the app icons in code and writes them as PNG files. No image is ever
// committed: the build runs this script and writes into the build folder only.
//
// Usage: node tools/generate-icons.mjs <output folder>
//
// The pixel grid is the zombie head from the title screen (kZombie in
// src/components/Ui/Icons/Icons.cpp) and the colors are from src/data/Theme.h.
// It has no dependencies; node:zlib does the PNG compression.
import { deflateSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const outDir = process.argv[2];
if (!outDir) {
  console.error('Usage: node tools/generate-icons.mjs <output folder>');
  process.exit(1);
}

// Palette. Each letter in the grid picks one of these.
const COLORS = {
  '#': [132, 164, 100], // kZombieSkin
  'o': [58, 82, 50],    // kZombieDark
  'e': [206, 70, 52],   // kUiHealth (eyes)
  'm': [14, 12, 16],    // kUiBorderDark (mouth)
  'b': [240, 226, 196], // kUiText (teeth)
};
const BACKGROUND = [20, 19, 26]; // #14131a, the page shell color
const SHADOW = [112, 36, 26];    // kLogoShadow, the same drop shadow the title logo has

const GRID = [
  '...######...',
  '..########..',
  '.##########.',
  '.#ee####ee#.',
  '.#ee####ee#.',
  '.##########.',
  '.####oo####.',
  '.##########.',
  '.#mbmbmbmb#.',
  '..#mmmmmm#..',
  '..oo####oo..',
  '...oo##oo...',
];
const CELLS = GRID.length;

// Pixel art must stay crisp, so the head is always an exact whole number of
// screen pixels per grid cell. `fill` is how much of the icon the head covers.
// A maskable icon gets cropped to a circle by some phones, so its head is
// smaller and the script checks that every drawn pixel stays inside the circle.
function drawIcon(size, { fill, maskable = false, shadow = true }) {
  const cell = Math.max(1, Math.floor((size * fill) / CELLS));
  const span = cell * CELLS;
  const left = Math.floor((size - span) / 2);
  const top = Math.floor((size - span) / 2);
  const pixels = Buffer.alloc(size * size * 4);
  for (let i = 0; i < size * size; i++) {
    pixels.set([...BACKGROUND, 255], i * 4);
  }
  const put = (x, y, rgb) => {
    for (let dy = 0; dy < cell; dy++) {
      for (let dx = 0; dx < cell; dx++) {
        const px = left + x * cell + dx;
        const py = top + y * cell + dy;
        if (px < 0 || py < 0 || px >= size || py >= size) continue;
        pixels.set([...rgb, 255], (py * size + px) * 4);
        if (maskable) {
          const r = Math.hypot(px + 0.5 - size / 2, py + 0.5 - size / 2);
          if (r > size * 0.4) throw new Error(`maskable icon ${size}: pixel outside the safe circle`);
        }
      }
    }
  };
  // Shadow first (one cell down and right), then the head over it.
  for (const pass of shadow ? ['shadow', 'head'] : ['head']) {
    for (let y = 0; y < CELLS; y++) {
      for (let x = 0; x < CELLS; x++) {
        const ch = GRID[y][x];
        if (ch === '.') continue;
        if (pass === 'shadow') put(x + 1, y + 1, SHADOW);
        else put(x, y, COLORS[ch]);
      }
    }
  }
  return pixels;
}

// ---- Minimal PNG writer ------------------------------------------------------

const crcTable = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});

function crc32(buffer) {
  let c = 0xffffffff;
  for (const byte of buffer) c = crcTable[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([length, body, crc]);
}

function encodePng(size, rgba) {
  const header = Buffer.alloc(13);
  header.writeUInt32BE(size, 0);
  header.writeUInt32BE(size, 4);
  header[8] = 8; // bit depth
  header[9] = 6; // color type: RGBA
  // Each row starts with a filter byte; 0 means "no filter".
  const rows = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; y++) {
    rgba.copy(rows, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(rows, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---- The icon set ------------------------------------------------------------

const ICONS = [
  ['icon-192.png', 192, { fill: 0.75 }],
  ['icon-512.png', 512, { fill: 0.75 }],
  ['icon-maskable-192.png', 192, { fill: 0.55, maskable: true }],
  ['icon-maskable-512.png', 512, { fill: 0.55, maskable: true }],
  ['apple-touch-icon.png', 180, { fill: 0.7 }], // iOS rounds the corners itself
  ['favicon-48.png', 48, { fill: 0.75, shadow: false }],
  ['favicon-32.png', 32, { fill: 0.75, shadow: false }],
];

mkdirSync(outDir, { recursive: true });
for (const [name, size, options] of ICONS) {
  writeFileSync(path.join(outDir, name), encodePng(size, drawIcon(size, options)));
}
console.log(`Wrote ${ICONS.length} icons to ${outDir}`);
