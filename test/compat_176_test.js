const assert = require('node:assert/strict');
const C = require('../src/core.js');
const F = require('../src/feld.js');
const FL = require('../src/flash.js');
const SV = require('../src/saves.js');

const word = (bytes, at, value) => new DataView(bytes.buffer, bytes.byteOffset).setUint32(at, value, true);
const rom = new Uint8Array(0x1000000).fill(255);
function idPage(bytes, at, id, checksum) {
  word(bytes, at, id);
  word(bytes, at + 0xFF8, checksum - id);
  word(bytes, at + 0xFFC, ~(checksum - id));
}
async function detectedEdition(bytes) {
  const d = Object.create(FL.Paradise.prototype);
  d.link = { request: async () => ({ payload: new Uint8Array(44) }) };
  d.read = async (at, length) => bytes.slice(at, at + length);
  return (await d.hello()).edition;
}

(async () => {
  idPage(rom, 0xD29000, 4, 0x96CB83B8);
  assert.equal(C.editionIdOf(rom), 7);
  assert.equal(F.feldId(rom), 7);
  assert.equal(await detectedEdition(rom), 7);
  idPage(rom, 0xD85000, 7, 0x99431749);
  assert.equal(C.editionIdOf(rom), 8);
  assert.equal(F.feldId(rom), 8);
  assert.equal(await detectedEdition(rom), 8);

  const legacyNeon = SV.paraEncode(SV.paraFromImage(rom, 8), 8, { counter: 44, ctrl: 0xE0 }, 1650000000);
  word(legacyNeon, legacyNeon.length - 12, 7);
  const restored = SV.paraDecode(legacyNeon);
  assert.equal(restored.edition, 8);
  assert.equal(restored.counter, 44);
  assert.equal(SV.paraDecode(SV.paraWrite(restored, 8)).edition, 8);
  assert.equal(SV.paraDecode(SV.paraEncode(SV.paraFromImage(rom, 7), 7, null)).edition, 7);
  const gen3 = SV.paraFromImage(rom, 7);
  const settings = new Uint8Array(0x1000);
  word(settings, 12, 0x2AC); // A real older Glacier backup's day count.
  const crc = SV.crc16arc(settings, 4, settings.length);
  word(settings, 0, crc | ((~crc & 0xFFFF) << 16));
  gen3.set(0xEFE000, settings);
  const glacier = SV.paraDecode(SV.paraEncode(gen3, 6, null));
  for (const edition of [5, 6, 7]) assert.deepEqual(SV.paraCheck(glacier, edition).errors, []);
  assert.deepEqual(SV.paraCheck({ ...glacier, edition: 0 }, 7).errors, []);
  assert(SV.paraCheck(glacier, 8).errors.some(e => e.code === 'saveOtherEd'));
  word(legacyNeon, legacyNeon.length - 8, 0xD49000);
  assert.throws(() => SV.paraDecode(legacyNeon), /save layout/);

  const stub = 0xB0000, program = 0xB1000, config = program + 0x140;
  const tpl = new Uint8Array(4);
  word(tpl, 0, 0x6789EF01);
  word(rom, stub, 0x10000000 + program - 0x11000);
  rom.fill(0, config, config + 620);
  word(rom, config, 0x444C4546);
  word(rom, config + 4, 11);
  word(rom, config + 28, 0xD29000);
  word(rom, config + 36, 0x80);
  assert.equal(F.feldFind(rom, tpl).flags, F.F_NO_SHORTCUT);
  word(rom, config + 28, 0xD85000);
  assert.equal(F.feldFind(rom, tpl).flags, F.F_RING);
  word(rom, config + 36, 0x80 | F.F_APPS | F.F_NEON_EDIT);
  rom.set(Buffer.from('PMXVER:1.7.5-plus.45\0'), program + 0x500);
  let flags = F.feldFind(rom, tpl).flags;
  assert(flags & F.F_NO_SHORTCUT);
  assert(!(flags & F.F_RING));
  rom.set(Buffer.from('PMXVER:1.7.6-plus.1\0'), program + 0x500);
  flags = F.feldFind(rom, tpl).flags;
  assert(flags & F.F_RING);
  assert(!(flags & F.F_NO_SHORTCUT));
  word(rom, config + 36, flags | F.F_NO_SHORTCUT);
  assert(F.feldFind(rom, tpl).flags & F.F_NO_SHORTCUT);
  console.log('PASS: native and Studio editions, legacy Neon backup, and legacy/official/current menu flags');
})().catch(error => { console.error(error); process.exitCode = 1; });
