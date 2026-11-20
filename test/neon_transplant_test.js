// node test/neon_transplant_test.js NEON.bin BASE.bin [LAND_WATER_OR_SKY.bin]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const C = require('../src/core.js'), E = require('../src/emu.js'), F = require('../src/feld.js');
const S = require('../src/six.js'), L = require('../six/experimental-lz4/patch.js');
const N = require('../six/experimental-neon/transplant.js');
const SV = require('../src/saves.js'), FL = require('../src/flash.js');
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

function itemRows(ctx) {
  const pack = Buffer.from(ctx.files[0]);
  const at = i => 2 * pack.readUInt32LE(4 * i);
  const bytes = pack.subarray(at(10), at(11));
  return new Map(Array.from({ length: bytes.length / 46 }, (_, i) => {
    const row = bytes.subarray(46 * i, 46 * (i + 1));
    return [row.readUInt16LE(0), row];
  }));
}

function itemSettings() {
  const page = Buffer.alloc(4096);
  for (const [i, id] of [5086, 6052, 5050].entries()) page.writeUInt16LE(id, 0x270 + 2 * i);
  page[0x770] = 0xFF;
  page[0x771] = 9;
  const crc = SV.crc16arc(page, 4, page.length);
  page.writeUInt16LE(crc, 0);
  page.writeUInt16LE(~crc & 0xFFFF, 2);
  return page;
}

async function checkItems() {
  const base = F.feldStrip(rom, menu, E), settings = itemSettings();
  const testBase = new Uint8Array(base);
  for (const at of [0xEFE000, 0xEFF000]) testBase.set(settings, at);
  const result = N.transplantItems(testBase, donor);
  assert(N.itemsInstalled(result.rom));
  assert.equal(result.ids.length, 59);
  assert(result.ids.includes(5086) && result.ids.includes(6052));
  const src = C.openRom(donor), dest = C.openRom(result.rom);
  const from = itemRows(src), to = itemRows(dest);
  const itemText = C.textTablesOf(result.rom).tabs;
  assert.equal(to.size, 400);
  assert.throws(() => N.transplantItems(result.rom, donor), /manifest/);
  for (const id of result.ids) {
    const a = from.get(id), b = to.get(id);
    assert.deepEqual(b.subarray(20), a.subarray(20));
    for (const [i, language] of itemText.entries()) {
      assert.deepEqual(C.txtWords(language, b.readUInt16LE(2) + 1), C.txtWords(source[i], a.readUInt16LE(2) + 1));
    }
    for (const at of [4, 6]) {
      const index = a.readUInt16LE(at), mapped = b.readUInt16LE(at);
      if (index) assert.deepEqual(C.spriteDecode(dest.entries[mapped], 0, true).cells, C.spriteDecode(src.entries[index], 0, true).cells);
      else assert.equal(mapped, 0);
    }
  }
  const archive = { ...F.slotBuild(new Map([[0xEFE000, settings]]), 0, 0, 0x1000000), index: 0 };
  const updated = N.clearItemSaveSlots([archive]);
  const cleared = (page) => {
    assert(SV.settingsOk(page));
    assert.equal(page[0x770], 0);
    assert.deepEqual(Buffer.from(page.subarray(0x771)), settings.subarray(0x771));
  };
  cleared(result.rom.subarray(0xEFE000, 0xEFF000));
  cleared(result.rom.subarray(0xEFF000, 0xF00000));
  cleared(F.slotPages(updated[0]).get(0xEFE000));
  assert.equal(settings[0x770], 0xFF);
  const output = F.feldApply(result.rom, { fields: true, bag: true, tama: true }, updated, menu, E);
  const oldDevice = F.feldApply(testBase, { fields: true, bag: true, tama: true }, [archive], menu, E);
  const changed = new Set();
  for (let at = 0; at < output.length; at += 4096) {
    if (!Buffer.from(output.subarray(at, at + 4096)).equals(oldDevice.subarray(at, at + 4096))) changed.add(at);
  }
  const kept = await FL.keepPlan(F, E, { stub: menu.stub })({
    info: { edition: C.editionIdOf(oldDevice) }, read: async (at, n) => oldDevice.slice(at, at + n),
  }, output, changed);
  cleared(kept.image.subarray(0xEFE000, 0xEFF000));
  cleared(F.slotPages(F.feldSlots(kept.image, menu.stub)[0]).get(0xEFE000));
  assert(changed.has(0xEFE000) && !kept.protectedSectors.includes(0xEFE000));
  const catalog = new Map(F.itemCatalog(output).map(row => [row.id, row]));
  assert(result.ids.every(id => catalog.get(id)?.at && catalog.get(id)?.name));
  assert.equal(F.fieldNames(output).length, 6);
  C.verifyRom(output, []);
  console.log('Verified 59 items, native sprite pixels and nine languages, and migration of active and archived inventories.');
}

checkItems().catch(err => { console.error(err); process.exitCode = 1; });
