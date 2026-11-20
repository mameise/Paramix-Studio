// Usage: node test/lab_release_ghost_test.js <six-field-ROM> [native-ghost.raw]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const C = require('../src/core');
const V = require('../src/visit');

const [romPath, capturePath] = process.argv.slice(2);
assert(romPath, 'provide the six-field ROM that produced the Lab Invite packet');
const rom = C.openRom(fs.readFileSync(romPath));
const visitor = V.collectRomVisitors(rom, C).find(v => v.character.id === 4066);
assert(visitor, 'missing Chodracotchi');
const before = capturePath ? fs.readFileSync(capturePath) : Buffer.from(V.editGhostAppearance(
  V.buildRomGhost(visitor, C), { color: 14, palette: C.characterPaletteVariants(rom.entries).bytes },
)).subarray(0, 0x1b000);
assert.equal(before.length, 0x1b000);
assert.equal(before.readUInt16LE(0x10a), 4066);
assert.equal(before[0x10], 14);

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'pmx-lab-ghost-'));
try {
  const runner = path.join(tmp, 'ghost');
  execFileSync(process.env.CC || 'cc', ['-std=c11', '-O2', '-I', path.join(__dirname, '../six/my-lab'),
    '-x', 'c', '-', '-o', runner], { input: `
#include <stdio.h>
#include <assert.h>
#include "ghost_codec.h"
int main(void) {
    uint8_t input[LAB_GHOST_SIZE], chunk[4096], pages = 0;
    assert(fread(input, 1, sizeof(input), stdin) == sizeof(input));
    for (unsigned at = 0; at < sizeof(input); at += sizeof(chunk)) {
        assert(ghost_convert(input, chunk, at, sizeof(chunk), 0, &pages) == GHOST_CONVERTED);
        assert(fwrite(chunk, 1, sizeof(chunk), stdout) == sizeof(chunk));
    }
}
` });
  const after = execFileSync(runner, { input: before });
  assert.equal(after.length, before.length);
  assert.deepEqual(after.subarray(8, 0x10c), before.subarray(8, 0x10c));
  assert.deepEqual(after.subarray(0x140, 0x600), before.subarray(0x140, 0x600));

  const sumWords = data => {
    let sum = 0;
    for (let at = 0; at < data.length; at += 4) sum = (sum + data.readUInt32LE(at)) >>> 0;
    return sum;
  };
  const sprite = (ghost, i) => {
    const at = 0x110 + i * 8;
    const offset = ghost.readUInt32LE(at);
    return ghost.subarray(offset, offset + ghost.readUInt32LE(at + 4));
  };
  let remapped = 0;
  for (let at = 0x600; at < 0x1d34; at += 22) {
    const id = before.readUInt16LE(at);
    const type = before.readUInt16LE(at + 20);
    const expected = id >= 6 && before.readUInt16LE(at + 6) ?
      type === 16 ? 222 : type === 17 ? (at < 0x600 + 110 * 30 ? 224 : 223) : id : id;
    if (expected !== id) remapped++;
    assert.equal(after.readUInt16LE(at), expected);
    assert.deepEqual(after.subarray(at + 2, at + 22), before.subarray(at + 2, at + 22));
  }
  assert(remapped > 0);
  let checksum = (sumWords(after.subarray(8, 0x180)) + sumWords(after.subarray(0x600, 0x1d34))) >>> 0;
  let tagged = 0;
  for (let i = 0; i < 6; i++) {
    const source = sprite(before, i), result = sprite(after, i);
    const a = C.spriteDecode(source, 0, true), b = C.spriteDecode(result, 0, true);
    assert.deepEqual(b.cells, a.cells);
    assert.deepEqual(b.palettes, a.palettes);
    const isTagged = bytes => {
      const h = C.spriteHeader(bytes);
      if ((h.flags & 0xe8) !== 0xa0) return false;
      for (let j = 0; j < h.numSprites; j++) {
        const pointer = bytes.readUInt32LE(h.pixOffset + 8 * j);
        if (pointer >>> 31) continue;
        if (C.LEGACY_LZ4_TAG.every((value, k) => (bytes[h.pixOffset + pointer + k] ^ 0x53) === value)) return true;
      }
      return false;
    };
    if (isTagged(source)) tagged++;
    assert(!isTagged(result));
    checksum = (checksum + sumWords(result)) >>> 0;
  }
  assert(tagged > 0);
  assert.equal(after.readUInt32LE(0), checksum);
  assert.equal(after.readUInt32LE(4), (-checksum) >>> 0);

  const studio = Buffer.from(V.buildLabRelease(Buffer.concat([before, Buffer.alloc(0x5000)]), C));
  assert.deepEqual(studio.subarray(0x600, 0x1d34), after.subarray(0x600, 0x1d34));
  console.log('PASS: white Chodracotchi retains all six pixel layers, palettes and poses with native Lab Invite resources');
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
