// 1.7.6 GEN3_ID4: Tropical/Glacier with Forest or Sky active write the ID 4 (own footer); read back as edition 7, same family
const F = require('../src/feld.js'), C = require('../src/core.js'), SV = require('../src/saves.js');
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const w32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
function rom(at, id, c) { const r = new Uint8Array(0x1000000).fill(0xFF); w32(r, at, id); const k = (c - id) >>> 0; w32(r, at + 0xFF8, k); w32(r, at + 0xFFC, ~k >>> 0); return r; }
const G3 = 0x96CB83B8, JD = 0x849633DB;
for (const at of [0xD29000, 0xEFD000]) {
  const r = rom(at, 4, G3), e = F.edition(r);
  ok(e && e.id === 7 && e.profile.name === 'gen3', 'gen3 page with ID 4 at ' + at.toString(16) + ' -> edition 7 gen3');
  ok(C.editionIdOf ? C.editionIdOf(r) === 7 : true, 'core reads 7');
}
{ const r = rom(0xEFD000, 4, JD), e = F.edition(r); ok(e && e.id === 4 && e.profile.name === 'jade', 'Jade Forest stays 4'); }
{ const r = rom(0xD29000, 6, G3), e = F.edition(r); ok(e && e.id === 6, 'Glacier stays 6'); }
ok(F.familyOf(7) === 'gen3' && F.familyOf(4) === 'jade', 'families');
ok(SV.family(7) === 'gen3', 'saves family 7');
const fake = { edition: 6, pages: new Map() };
const chk = SV.paraCheck(fake, 7); ok(!chk.errors.some((x) => x.code === 'saveOtherEd'), 'Glacier save fits a device with Forest active');
const chk2 = SV.paraCheck({ edition: 4, pages: new Map() }, 7); ok(chk2.errors.some((x) => x.code === 'saveOtherEd'), 'Jade save does not');
console.log(bad ? bad + ' failed' : 'all good'); process.exit(bad ? 1 : 0);
