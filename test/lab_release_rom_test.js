// Usage: node test/lab_release_rom_test.js <original-ROM-directory> [six-field-ROM]
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const C = require('../src/core');
const E = require('../src/emu');
const F = require('../src/feld');
const L = require('../src/lab_release');
const S6 = require('../src/six');

const [directory, six] = process.argv.slice(2);
assert(directory, 'provide the directory containing original editions 1–7');
const stock = new Map();
for (const name of fs.readdirSync(directory)) {
  const file = path.join(directory, name);
  if (!fs.statSync(file).isFile() || !/\.bin$/i.test(name)) continue;
  const rom = fs.readFileSync(file);
  const id = C.editionIdOf(rom);
  if (id) stock.set(id, rom);
}
const donor = stock.get(7);
assert(donor);
assert(!L.supported(donor));
const sourceText = C.textTablesOf(donor).tabs;

function check(rom, rebuild) {
  assert(L.supported(rom));
  assert(!L.installed(rom));
  const initial = C.textTablesOf(rom);
  const prepared = rebuild && L.prepare(rom, donor);
  const beforeInstall = rebuild ? rebuild(prepared.rom) : rom;
  const output = rebuild ? L.install(beforeInstall, prepared.label) : L.apply(rom, donor);
  assert(L.installed(output));
  assert(!L.installed(rom));
  assert.equal(L.apply(output, donor), output);
  assert.equal(output.length, rom.length);
  const builtText = C.textTablesOf(output);
  initial.tabs.forEach((table, lang) => {
    const result = builtText.tabs[lang], last = table.n;
    if (!rebuild) assert.equal(result.n, last + 3);
    for (let row = 1; row <= last; row++) {
      assert.deepEqual(C.txtWords(result, row), C.txtWords(table, row), 'existing text changes');
    }
    [83, 288, 277].forEach((source, index) => {
      assert.deepEqual(C.txtWords(result, last + 1 + index), C.txtWords(sourceText[lang], source));
    });
  });
  C.verifyRom(output, []);
  if (S6.sixInfo(rom)) assert.deepEqual(S6.sixFix(output), output, 'rain pointers must stay valid');
  const shiftedHook = output.slice();
  const patch = output.findIndex((byte, i) => i > 0x60000 && i < 0xa6530 && byte !== beforeInstall[i]);
  assert(patch > 0);
  shiftedHook[patch] = beforeInstall[patch];
  assert(!L.installed(shiftedHook), 'installed must reject a damaged hook');
}

for (let id = 1; id <= 6; id++) {
  assert(stock.has(id), 'missing edition ' + id);
  check(stock.get(id));
}
if (six) {
  const rom = Uint8Array.from(fs.readFileSync(six));
  const read = name => fs.readFileSync(path.join(__dirname, '../feld', name));
  const emb = { feld: read('feld.bin'), stub: read('stub_feld_tpl.bin') };
  const menu = F.feldFind(rom, emb.stub);
  if (menu) {
    const archived = F.feldSlots(rom, emb.stub);
    const stripped = F.feldStrip(rom, { ...emb, oldStub: fs.readFileSync(path.join(__dirname, '../emu/stub_tpl.bin')) }, E);
    check(stripped, prepared => {
      const rebuilt = F.feldApply(prepared, { fields: !!(menu.flags & F.F_FIELDS), bag: !!(menu.flags & F.F_BAG),
        tama: !!(menu.flags & F.F_TAMA), saves: !!(menu.flags & F.F_SAVES) }, archived, emb, E);
      const slots = F.feldSlots(rebuilt, emb.stub);
      assert.deepEqual(slots.map(s => s.index), archived.map(s => s.index));
      slots.forEach((s, i) => assert.deepEqual(F.slotPages(s, false), F.slotPages(archived[i], false)));
      assert.deepEqual(rebuilt.slice(0xEFE000, 0xF00000), rom.slice(0xEFE000, 0xF00000));
      return rebuilt;
    });
  } else check(rom);
}
assert.throws(() => L.apply(stock.get(1), stock.get(2)), /Neon Planets/);
console.log('PASS: six firmware editions, nine locale tables, existing text and ROM marker');
