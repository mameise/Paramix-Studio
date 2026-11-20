// node test/neon_transplant_test.js NEON.bin BASE.bin [LAND_WATER_OR_SKY.bin]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const C = require('../src/core.js'), E = require('../src/emu.js'), F = require('../src/feld.js');
const S = require('../src/six.js'), L = require('../six/experimental-lz4/patch.js');
const N = require('../six/experimental-neon/transplant.js');
const [donorPath, basePath, fieldPath] = process.argv.slice(2);
if (!donorPath || !basePath) throw new Error('Provide a Neon ROM and a Tropical or Glacier ROM.');

const read = file => new Uint8Array(fs.readFileSync(file));
const binary = file => read(path.join(__dirname, '..', file));
const menu = { feld: binary('feld/feld.bin'), stub: binary('feld/stub_feld_tpl.bin') };
const original = read(basePath);
const saved = F.feldSlots(original, menu.stub);
let rom = F.feldStrip(original, menu, E);
if (!S.sixInfo(rom)) {
  assert(fieldPath, 'A three-field base needs a Land, Water or Sky ROM.');
  rom = S.sixApply(rom, read(fieldPath), binary('six/six.bin'), {});
}
if (!L.isInstalled(rom)) rom = L.patch(rom, binary('six/experimental-lz4/helper.bin'), null, true, menu.stub).rom;
const donor = read(donorPath);
const added = N.transplant(rom, donor);
rom = N.installGrowth(added.rom, added.rows);
rom = F.feldApply(rom, { fields: true, tama: true }, saved, menu, E);

const config = F.feldFind(rom, menu.stub);
assert(N.installed(rom));
assert(config.flags & 0x100);
assert.equal(rom[config.stubOff + 0x304], 3);
assert.equal(F.fieldNames(rom).length, 6);
assert.equal(F.feldSlots(rom, menu.stub).length, saved.length);
assert.equal(C.editionIdOf(rom), C.editionIdOf(original));
const c = new DataView(rom.buffer);
for (const at of [0x34B26, 0x34B44]) assert.equal(c.getUint32(at, true), 0x3000F8B3);
assert.equal(c.getUint32(0x107FE0, true), 0x314E5A4E);
assert.equal(c.getUint32(0x107FEC, true), 0);
const counts = (chars, first) => [0, 1, 2].map(i => chars.filter(x => x.field === first + i).length);
const chars = C.charTable(C.openRom(rom).files[0]).filter(x => x.id > 1000 && x.id < 9000);
assert.deepEqual(counts(chars, 7), [24, 38, 24]);
const catalog = Array.from({ length: config.catN }, (_, i) => ({ field: rom[config.catOff + 8 + 28 * i + 7] }));
assert.deepEqual(counts(catalog, 7), [24, 38, 24]);
const previews = config.catOff + 12 + 40 * config.catN;
assert.equal(c.getUint32(previews, true), 0x56525054);
const previewIds = new Set(Array.from({ length: c.getUint32(previews + 12, true) }, (_, i) => c.getUint16(previews + 24 + 72 * i, true)));
assert.ok(added.rows.every(row => previewIds.has(row.id)));
const layers = C.paraLayers(C.openRom(rom).files[0]);
assert.ok(layers.layers.length > 32767);
for (const id of [4716, 4717, 4718, 4719, 4720, 4721]) {
  const row = chars.find(c => c.id === id);
  assert.ok(layers.layers.indexOf(layers.frameAt(54 * row.animation + 30)[0]) > 32767);
}
const source = C.textTablesOf(donor).tabs, localized = C.textTablesOf(rom).tabs;
for (let lang = 0; lang < source.length; lang++) {
  added.planetTextIds.forEach((id, i) => {
    assert.deepEqual(C.txtWords(localized[lang], id + 1), C.txtWords(source[lang], 14 + i));
  });
}
C.verifyRom(rom, []);
console.log('Verified six maps, nine native Tama Files groups, nine languages, and 86 imported names and previews.');
