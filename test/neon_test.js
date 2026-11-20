// 1.7.6 NEON_ED: Neon Planets is edition 8 (its ID page 0xD85000 holds ID 7 with its own footer), not Tropical/Glacier.
// node test/neon_test.js <neon.bin> [glacier.bin] [land.bin]
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const SV = require('../src/saves.js'), F = require('../src/feld.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const n = new Uint8Array(fs.readFileSync(process.argv[2])), o = C.openRom(n);
ok(o.editionId === 8 && o.edition === 'Neon Planets', 'Neon Planets: ' + o.edition);
ok(SV.family(8) === 'neon' && SV.idPage(8) === 0xD85000, 'own save family and ID page');
const fe = F.edition(n); ok(fe && fe.id === 8 && fe.profile.ring && F.canFields(n), 'menu supports the settings ring and fork save slots');
// 1.7.6 GFX_MAP: its own planets for the figures, each figure once
const fsx = C.fieldSets(o).filter(f => f.type >= 7);
ok(fsx.map(f => f.chars.length).join(',') === '24,38,24', 'native Neon rosters');
ok(fsx.every(f => f.chars.every(c => o.groups.includes(c.group))), 'native Neon characters have sprite groups');
// 1.7.6 NEON_SAVE: the save's settings block is the newest page of the ring
const sv = { stamp: 0, counter: 0, alarm: 0, status: 0, regs: [], pages: SV.paraFromImage(n, 8), edition: 8 }, pet = SV.paraPet(sv, 8);
ok(pet && pet.id === 1001, 'Tama in the save: ' + (pet && pet.id));
const nw = SV.paraPetSet(sv, 8, { id: 4018, stage: 5, care: true, level: 7 }), p2 = SV.paraPet(SV.paraDecode(SV.paraWrite(nw, 8)), 8);
ok(p2.id === 4018 && p2.level === 7 && p2.hunger === 6 && p2.happy === 20, 'Tama changed in the save');
ok([...nw.pages].filter(([a, d]) => Buffer.compare(Buffer.from(d), Buffer.from(sv.pages.get(a)))).every(([a]) => a < 0xD8B000 || a >= 0xE00000), 'only the ring page and the Tama file changed');
ok(!SV.paraCheck(SV.paraDecode(SV.paraWrite(nw, 8)), 8).errors.length, 'save fits a Neon device');
// Check the native LZ4 entries through the fork's bounded sprite decoder.
let cells = 0;
for (const entry of o.entries) {
  const header = C.spriteHeader(entry);
  if (header.flags & 8) cells += C.spriteDecode(entry).cells.length;
}
ok(cells > 10000, 'native LZ4 sprite cells: ' + cells);
// 1.7.6 NEON_APPS: programs between the pictures and 0xD80000
const E = require('../src/emu.js'); ok(E.neonRom(n), 'programs: Neon recognised');
const t = C.textApply(n, new Map([[899, 'TESTTCHI']])); ok(C.textGet(t.rom || t, 899) === 'TESTTCHI', 'names can be changed');
for (const p of process.argv.slice(3)) { const r = new Uint8Array(fs.readFileSync(p)), e = C.openRom(r).editionId; ok(e >= 1 && e <= 7, p.split('/').pop() + ' stays edition ' + e); }
console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
