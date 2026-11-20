// 1.7.6 NEON_ED: Neon Planets is edition 8 (its ID page 0xD85000 holds ID 7 with its own footer), not Tropical/Glacier.
// node test/neon_test.js <neon.bin> [glacier.bin] [land.bin]
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const SV = require('../src/saves.js'), F = require('../src/feld.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const n = new Uint8Array(fs.readFileSync(process.argv[2])), o = C.openRom(n);
ok(o.editionId === 8 && o.edition === 'Neon Planets', 'Neon Planets: ' + o.edition);
ok(SV.family(8) === 'neon' && SV.idPage(8) === 0xD85000, 'own save family and ID page');
const fe = F.edition(n); ok(fe && fe.id === 8 && fe.profile.ring && !F.canFields(n), 'menu functions with the settings ring, no field switch');
// 1.7.6 GFX_MAP: its own planets for the figures, each figure once
const fsx = C.fieldSets(o); ok(fsx.map((x) => x.name).join(',') === 'Cosmic,New Tama,Tama' && fsx.every((x) => x.chars.length && new Set(x.chars.map((c) => c.group)).size === x.chars.length && o.groups.includes(x.chars[0].group)), 'figures of its planets: ' + fsx.map((x) => x.name + ' ' + x.chars.length).join(', '));
// 1.7.6 NEON_SAVE: the save's settings block is the newest page of the ring
const sv = { stamp: 0, counter: 0, alarm: 0, status: 0, regs: [], pages: SV.paraFromImage(n, 8), edition: 8 }, pet = SV.paraPet(sv, 8);
ok(pet && pet.id === 1001, 'Tama in the save: ' + (pet && pet.id));
const nw = SV.paraPetSet(sv, 8, { id: 4018, stage: 5, care: true, level: 7 }), p2 = SV.paraPet(SV.paraDecode(SV.paraWrite(nw, 8)), 8);
ok(p2.id === 4018 && p2.level === 7 && p2.hunger === 6 && p2.happy === 20, 'Tama changed in the save');
ok([...nw.pages].filter(([a, d]) => Buffer.compare(Buffer.from(d), Buffer.from(sv.pages.get(a)))).every(([a]) => a < 0xD8B000 || a >= 0xE00000), 'only the ring page and the Tama file changed');
ok(!SV.paraCheck(SV.paraDecode(SV.paraWrite(nw, 8)), 8).errors.length, 'save fits a Neon device');
// 1.7.6 LZ4_SPR: pictures with flag 0x08 are LZ4 blocks; every cell has the size of the picture, ours read back the same
{ const BITS = { 0: 1, 1: 2, 2: 4, 3: 8 }; let cells = 0, bad = 0;
  for (const e of o.entries) { const h = C.spriteHeader(e); if (!(h.flags & 8) || (h.flags & 0x60) || h.bpp > 3) continue;
    const dd = C.spriteDecode(e); if (!dd) bad++;
    for (let s = 0; s < h.numSprites; s++) { const u = (q) => (e[q] | (e[q + 1] << 8) | (e[q + 2] << 16) | (e[q + 3] << 24)) >>> 0;
      const off = u(h.pixOffset + 8 * s) & 0x7fffffff, ln = u(h.pixOffset + 8 * s + 4), r = C.lz4Decode(e.slice(h.pixOffset + off, h.pixOffset + off + ln).map((x) => x ^ 0x53));
      cells++; if (r.length !== Math.ceil(h.sw * h.sh * BITS[h.bpp] / 8) || Buffer.compare(Buffer.from(C.lz4Decode(C.lz4Encode(r))), Buffer.from(r))) bad++; } }
  ok(cells > 10000 && !bad, 'LZ4 pictures: ' + cells + ' cells, ' + bad + ' wrong'); }
// 1.7.6 NEON_APPS: programs between the pictures and 0xD80000
const E = require('../src/emu.js'); ok(E.neonRom(n), 'programs: Neon recognised');
const t = C.textApply(n, new Map([[899, 'TESTTCHI']])); ok(C.textGet(t.rom || t, 899) === 'TESTTCHI', 'names can be changed');
for (const p of process.argv.slice(3)) { const r = new Uint8Array(fs.readFileSync(p)), e = C.openRom(r).editionId; ok(e >= 1 && e <= 7, p.split('/').pop() + ' stays edition ' + e); }
console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
