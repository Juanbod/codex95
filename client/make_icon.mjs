import fs from "node:fs";

const palette = [
  [0, 0, 0, 0],       // transparent
  [26, 31, 36, 255],   // dark shell
  [55, 183, 208, 255], // cyan screen
  [236, 240, 241, 255],// light outline
  [45, 55, 62, 255],   // keyboard
  [99, 222, 128, 255], // prompt green
  [16, 91, 112, 255],  // screen shadow
  [128, 138, 145, 255],// mid outline
];

function pixels(size) {
  const p = Array.from({ length: size }, () => Array(size).fill(0));
  const scale = size / 16;
  const rect = (x1, y1, x2, y2, c) => {
    for (let y = Math.floor(y1 * scale); y < Math.ceil(y2 * scale); y++)
      for (let x = Math.floor(x1 * scale); x < Math.ceil(x2 * scale); x++) p[y][x] = c;
  };
  rect(2, 2, 14, 11, 3);
  rect(3, 3, 13, 10, 1);
  rect(4, 4, 12, 9, 6);
  rect(5, 5, 11, 8, 2);
  rect(5, 5, 7, 6, 5);
  rect(6, 6, 7, 7, 5);
  rect(5, 7, 7, 8, 5);
  rect(8, 7, 11, 8, 3);
  rect(1, 11, 15, 13, 3);
  rect(2, 11, 14, 12, 4);
  rect(4, 13, 12, 14, 7);
  return p;
}

function dib(size) {
  const p = pixels(size);
  const xorStride = Math.ceil(size / 4) * 4;
  const maskStride = Math.ceil(size / 32) * 4;
  const paletteBytes = 256 * 4;
  const out = Buffer.alloc(40 + paletteBytes + xorStride * size + maskStride * size);
  out.writeUInt32LE(40, 0);
  out.writeInt32LE(size, 4);
  out.writeInt32LE(size * 2, 8);
  out.writeUInt16LE(1, 12);
  out.writeUInt16LE(8, 14);
  out.writeUInt32LE(xorStride * size, 20);
  out.writeUInt32LE(256, 32);
  palette.forEach((color, i) => {
    const o = 40 + i * 4;
    out[o] = color[2]; out[o + 1] = color[1]; out[o + 2] = color[0];
  });
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      out[40 + paletteBytes + y * xorStride + x] = p[size - 1 - y][x];
    }
  }
  const mask = 40 + paletteBytes + xorStride * size;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      if (p[size - 1 - y][x] === 0)
        out[mask + y * maskStride + Math.floor(x / 8)] |= 0x80 >> (x % 8);
    }
  }
  return out;
}

const images = [dib(16), dib(32)];
const header = Buffer.alloc(6 + images.length * 16);
header.writeUInt16LE(0, 0);
header.writeUInt16LE(1, 2);
header.writeUInt16LE(images.length, 4);
let offset = header.length;
images.forEach((image, i) => {
  const o = 6 + i * 16;
  const size = i ? 32 : 16;
  header[o] = size;
  header[o + 1] = size;
  header.writeUInt16LE(1, o + 4);
  header.writeUInt16LE(8, o + 6);
  header.writeUInt32LE(image.length, o + 8);
  header.writeUInt32LE(offset, o + 12);
  offset += image.length;
});
fs.writeFileSync(new URL("CODEX95.ICO", import.meta.url), Buffer.concat([header, ...images]));
