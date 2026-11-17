// Optional integration test with a user-supplied ROM; no game data is committed.
// node test/feld_language_compat_test.js <paradise.bin> [paradise-cli]
const fs = require('fs'), os = require('os'), path = require('path'), assert = require('assert');
const { execFileSync } = require('child_process');
const H = require('../src/home_menu.js');
const C = require('../src/core.js'), F = require('../src/feld.js'), E = require('../src/emu.js');
const romPath = process.argv[2];
if (!romPath) {
  console.log('Usage: node test/feld_language_compat_test.js <user-supplied paradise.bin> [paradise-cli]');
  process.exit(0);
}
const cli = path.resolve(process.argv[3] || process.env.PARADISE_CLI ||
  path.join(__dirname, '../../tamagotchi-paradise-zh-patcher/target/release/paradise-cli'));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'paramix-language-'));
const emb = { feld: new Uint8Array(fs.readFileSync(path.join(__dirname, '../feld/feld.bin'))),
  stub: new Uint8Array(fs.readFileSync(path.join(__dirname, '../feld/stub_feld_tpl.bin'))) };
const original = new Uint8Array(fs.readFileSync(romPath));
const oldSlots = F.feldSlots(original, emb.stub);
const live = [0xEFE000, 0xEFF000].map(a => Buffer.from(original.slice(a, a + 4096)));
const opts = { fields: true, bag: true, tama: true, saves: true };
const sum16 = bytes => { let n = 0; for (const b of bytes) n += b; return (n & 65535) || 1; };
let step = 0;
function patch(rom) {
  const input = path.join(tmp, `input-${step}.bin`), output = path.join(tmp, `output-${step++}.bin`);
  fs.writeFileSync(input, rom);
  execFileSync(cli, ['patch', input, '--variant', 'tw', '-o', output], { timeout: 60000 });
  execFileSync(cli, ['verify', input, output], { timeout: 60000 });
  return new Uint8Array(fs.readFileSync(output));
}
function rebuild(rom, saves = F.feldSlots(rom, emb.stub), shortcut = true) {
  return F.feldApply(F.feldStrip(rom, emb, E), { ...opts, paramixShortcut: shortcut }, saves, emb, E);
}
function preservesSaves(rom) {
  [0xEFE000, 0xEFF000].forEach((a, i) => assert(Buffer.from(rom.slice(a, a + 4096)).equals(live[i]), `live settings copy ${i} changed`));
  const slots = F.feldSlots(rom, emb.stub);
  assert.equal(slots.length, oldSlots.length, 'stored slot count changed');
  for (const source of oldSlots) {
    const target = slots.find(s => s.index === source.index);
    assert(target && target.field === source.field, `slot ${source.index + 1} moved to another field/index`);
    assert(Buffer.from(target.stream).equals(Buffer.from(source.stream)), `slot ${source.index + 1} compressed save changed`);
  }
}
function preservesSpriteOffsets(before, after) {
  const a = C.openRom(before), b = C.openRom(after);
  assert.equal(a.root.ents[1].offset, b.root.ents[1].offset, 'sprite archive moved');
  assert.equal(a.entries.length, b.entries.length, 'sprite count changed');
  const tableBytes = a.entries.length * 4;
  assert(Buffer.from(a.files[1].slice(0, tableBytes)).equals(Buffer.from(b.files[1].slice(0, tableBytes))), 'sprite offsets changed');
  let changed = 0;
  a.entries.forEach((entry, i) => { if (!Buffer.from(entry).equals(Buffer.from(b.entries[i]))) changed++; });
  assert.equal(changed, 1, 'patch must change exactly one font sprite');
}
function language(rom) {
  const pages = F.languagePatchPages(rom);
  assert.equal(pages.length, 1, 'one language helper page must be reserved');
  const page = pages[0], f = F.feldFind(rom, emb.stub), b = Buffer.from(rom);
  assert(page >= 0x90000 && page < 0x10F000 && !(page & 4095), 'invalid language helper allocation');
  const home = H.inspect(rom);
  if (home && !home.original) assert.notEqual(page, home.page, 'language helper overlaps main menu helper');
  for (let i = 0; i < f.nslots; i++) {
    const a = F.slotAddr(f, i), z = a + f.slotSize;
    assert(page + 4096 <= a || page >= z, `helper overlaps reserved slot ${i + 1}`);
  }
  assert(page + 4096 <= f.off || page >= f.catOff + 8 + 40 * f.catN, 'helper overlaps Menu code/catalog');
  return { page, label: b.readUInt32LE(page + 0x90), bytes: b.subarray(page, page + 4096), counts: C.textTablesOf(rom).tabs.map(t => t.n) };
}
function references(rom) {
  const f = F.feldFind(rom, emb.stub), b = Buffer.from(rom);
  const home = H.inspect(rom);
  assert(home && ((f.flags & F.F_NO_SHORTCUT) ? home.original : !home.original), 'home shortcut preference not applied');
  const settings = require('../src/settings.js').inspect(rom);
  assert(settings && !(settings.flags & 2), 'PARAMIX must not be in Settings after rebuilding');
  const ext = f.catOff + 8 + 28 * f.catN, h = ext + 4 + 12 * f.catN;
  assert.equal(b.subarray(ext, ext + 4).toString(), 'PEXT');
  let itemRefs = 0;
  for (let i = 0; i < f.catN; i++) {
    const q = ext + 4 + i * 12, addr = b.readUInt32LE(q), len = b.readUInt16LE(q + 4);
    if (!addr || !len) continue;
    assert(addr >= 0x111000 && addr + len <= rom.length, 'item reference out of range');
    assert.equal(sum16(rom.subarray(addr, addr + len)), b.readUInt16LE(q + 6), `item checksum ${i}`); itemRefs++;
  }
  assert.equal(b.subarray(h, h + 4).toString(), 'TPRV', 'preview metadata missing (ARC2 padding regression)');
  assert.equal(b.readUInt32LE(h + 4), 1); assert.equal(b.readUInt32LE(h + 8), 72);
  const chars = b.readUInt32LE(h + 12);
  assert.equal(chars, F.characters(rom).length, 'preview must cover every selectable character');
  assert.equal(sum16(rom.subarray(b.readUInt32LE(h + 16), b.readUInt32LE(h + 16) + 512)), b.readUInt32LE(h + 20), 'shared color palette checksum');
  for (let i = 0; i < chars; i++) for (let j = 0; j < 3; j++) {
    const q = h + 24 + i * 72 + 4 + j * 20;
    for (const [addr, len, want] of [[b.readUInt32LE(q), b.readUInt16LE(q + 4), b.readUInt16LE(q + 16)],
      [b.readUInt32LE(q + 8), 32, b.readUInt16LE(q + 18)]]) {
      assert(addr >= 0x111000 && addr + len <= rom.length, 'preview reference out of range');
      assert.equal(sum16(rom.subarray(addr, addr + len)), want, `preview checksum ${i}/${j}`);
    }
  }
  return { itemRefs, previewChars: chars };
}
try {
  // Upgrade the previous standalone Settings shortcut to the end of the main list.
  const SLegacy = require('../src/settings.js'), oldMenu = F.feldFind(original, emb.stub);
  if (oldMenu && SLegacy.inspect(original).original) {
    const legacy = SLegacy.apply(original, oldMenu.stubOff);
    assert.equal(SLegacy.inspect(legacy).flags, 2);
    const migrated = rebuild(legacy);
    assert(SLegacy.inspect(migrated).original, 'old standalone Settings row was not removed');
    assert(!H.inspect(migrated).original, 'main shortcut was not installed during migration');
    preservesSaves(migrated); references(migrated);
  }
  // Existing Menu -> patch -> rebuild twice, preserving both live and stored saves.
  const patchedOriginal = patch(original); preservesSpriteOffsets(original, patchedOriginal); preservesSaves(patchedOriginal);
  const first = rebuild(patchedOriginal); preservesSaves(first); const stats = references(first), reservation = language(first);
  const second = rebuild(first); preservesSaves(second); references(second);
  assert(language(second).bytes.equals(reservation.bytes), 'Menu rebuild changed helper page');
  assert(Buffer.from(second).equals(Buffer.from(first)), 'rebuilding twice must be byte-identical');

  // Patch before installing Menu, starting with stripped code but retaining saves separately.
  const stripped = F.feldStrip(original, emb, E), patchedBare = patch(stripped);
  preservesSpriteOffsets(stripped, patchedBare);
  const installedAfter = F.feldApply(patchedBare, opts, oldSlots, emb, E);
  preservesSaves(installedAfter); references(installedAfter); language(installedAfter);

  // Menu with generated preview metadata -> patch: all absolute references stay valid.
  const rebuiltOriginal = rebuild(original), homeBefore = H.inspect(rebuiltOriginal), patchedAfter = patch(rebuiltOriginal);
  const homeAfter = H.inspect(patchedAfter);
  assert.deepEqual(homeAfter, homeBefore, 'language patch changed home shortcut metadata');
  assert.deepEqual(patchedAfter.slice(homeAfter.page, homeAfter.page + 4096), rebuiltOriginal.slice(homeBefore.page, homeBefore.page + 4096), 'language patch changed home helper');
  preservesSpriteOffsets(rebuiltOriginal, patchedAfter); preservesSaves(patchedAfter); references(patchedAfter);
  const beforeRepeat = language(patchedAfter), repeated = patch(patchedAfter), afterRepeat = language(repeated);
  preservesSaves(repeated); references(repeated);
  assert.equal(afterRepeat.page, beforeRepeat.page); assert.equal(afterRepeat.label, beforeRepeat.label);
  assert.deepEqual(afterRepeat.counts, beforeRepeat.counts, 'patching twice appended duplicate language labels');
  // The opt-out is persistent, including when the language patch is applied later.
  const S = require('../src/settings.js');
  const disabled = rebuild(original, oldSlots, false);
  assert(F.feldFind(disabled, emb.stub).flags & F.F_NO_SHORTCUT);
  assert(!(S.inspect(disabled).flags & 2)); preservesSaves(disabled); references(disabled);
  const translatedDisabled = patch(disabled);
  assert.equal(S.inspect(translatedDisabled).flags, 1, 'language patch re-enabled a disabled PARAMIX shortcut');
  const disabledAgain = rebuild(translatedDisabled, F.feldSlots(translatedDisabled, emb.stub), false);
  assert.equal(S.inspect(disabledAgain).flags, 1); preservesSaves(disabledAgain); references(disabledAgain);
  console.log(`PASS ${path.basename(romPath)}: patch/Menu ordering, repeat patch/rebuild, ${stats.itemRefs} PEXT refs, ${stats.previewChars} previews, fixed sprite offsets, helper reservation, live and ${oldSlots.length} stored saves`);
} finally { fs.rmSync(tmp, { recursive: true, force: true }); }
