// Check the field switch in the studio (local only, needs your own Paradise images).
// node test/feld_test.js <land.bin> [compare_feld_rom.bin] [glacier.bin]
const fs = require('fs');
const E = require('../src/emu.js'), D = require('../src/doom.js'), F = require('../src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const emb = { feld: rd(__dirname + '/../feld/feld.bin'), stub: rd(__dirname + '/../feld/stub_feld_tpl.bin'), oldStub: rd(__dirname + '/../emu/stub_tpl.bin') };
const eq = (a, b) => a.length === b.length && Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
let bad = 0;
function ok(c, m) { console.log((c ? 'ok   ' : 'FALSCH ') + m); if (!c) bad++; }
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
function w32(b, o, v) { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; }

const land = rd(process.argv[2]);
ok(F.feldId(land) >= 1, 'Feld erkannt: ' + F.NAMES[F.feldId(land)]);
ok(!F.feldFind(land, emb.stub), 'Original ohne Feldwahl');

// 1. field switch only
const a = F.feldApply(land, { fields: true, bag: false }, [], emb, E);
const fa = F.feldFind(a, emb.stub);
ok(fa && fa.off === 0xB1000 && fa.slotOff === 0x830000 && fa.slotSize === 0x58000, 'Lage ohne Programm ' + JSON.stringify(fa));

ok(eq(F.feldStrip(a, emb, E), land), 'Ausbauen gibt das Original zurueck');

// 2. save in a slot, then add the emulator, then DOOM: the slot moves along
const b = new Uint8Array(a);
const fake = new Uint8Array(0x3000); fake.fill(0x5A); w32(fake, 0, 0x544C5346); w32(fake, 4, 2); w32(fake, 8, 3); w32(fake, 12, 0x2000);
b.set(fake, fa.slotOff + 1 * fa.slotSize);
const saved = F.feldSlots(b, emb.stub);
ok(saved.length === 1 && saved[0].field === 2 && saved[0].data.length === 0x3000, 'Fach gelesen');
// Emulator: we check the program without a real guest via installApp (same stub path)
const embE = E.embedded() || { app: rd(__dirname + '/../emu/emu.bin'), stub: emb.oldStub };
let c = F.feldStrip(b, emb, E);
c = E.installApp(c, embE.app, emb.oldStub);
c = F.feldApply(c, { fields: true, bag: true }, saved, emb, E);
const fc = F.feldFind(c, emb.stub);
ok(F.appKind(c) === 2 && fc.off === 0xB1000, 'mit Emulator: Programm erkannt, Feldwahl bei 0xB1000');
ok(u32(c, 0xB0008) === 0x1008F000, 'Starter kennt das Programm');
ok(eq(c.slice(fc.slotOff + fc.slotSize, fc.slotOff + fc.slotSize + 0x3000), fake), 'Fach mit Emulator erhalten');
// DOOM (small fake WAD, the program is real)
const dapp = rd(__dirname + '/../doom/doom_app.bin'), dxip = rd(__dirname + '/../doom/doom_xip.bin');
let d = F.feldStrip(b, emb, E);
d = D.doomPatch(d, new Uint8Array(1000), { app: dapp, xip: dxip, stub: emb.oldStub }, E).rom;
d = F.feldApply(d, { fields: true, bag: true }, saved, emb, E);
const fd = F.feldFind(d, emb.stub);
ok(F.appKind(d) === 1, 'DOOM erkannt');
ok(fd.off === ((0xB1000 + dxip.length + 0xFFF) & ~0xFFF) && fd.slotOff + 3 * fd.slotSize <= 0x10E000, 'Lage mit DOOM ' + JSON.stringify(fd) + ' je Fach ' + (fd.slotSize >> 10) + ' KB');
ok(eq(d.slice(fd.slotOff + fd.slotSize, fd.slotOff + fd.slotSize + 0x3000), fake), 'Fach zieht zu DOOM um');
ok(eq(d.slice(0xB1000, 0xB1000 + dxip.length), dxip), 'DOOM-Programm unberuehrt');
ok(u32(d, 0x830000) === 1000, 'WAD unberuehrt');
ok(F.itemCatalog(d).pack === F.itemCatalog(land).pack, 'item pictures found in the asset archive, not in DOOM');
// without field switch again: DOOM stub as before
const e = F.feldStrip(d, emb, E);
ok(!F.feldFind(e, emb.stub) && F.appKind(e) === 1 && u32(e, 0xB0000 + 0x94) === u32(land, E.sceneTable(land).PSID_HOME + 12), 'Ausbau mit DOOM laesst DOOM stehen');

// 3. another edition
if (process.argv[4]) {
  const g = rd(process.argv[4]);
  let err = ''; try { F.feldApply(g, { fields: true, bag: false }, [], emb, E); } catch (x) { err = x.message; }
  ok(F.feldId(g) === 6 && F.canFields(g), 'Glacier erkannt, Felder: ' + F.fieldNames(g).join(' ') + (err ? ' FEHLER ' + err : ''));
}
// 4. more editions from the environment: install, find, remove = original
for (const p of (process.env.WEITERE || '').split(',').filter(Boolean)) {
  const r = rd(p); const ed = F.edition(r);
  try {
    const x = F.feldApply(r, { fields: true, bag: true }, [], emb, E), fx = F.feldFind(x, emb.stub);
    ok(fx && eq(F.feldStrip(x, emb, E), r), p.split('/').pop() + ': ' + F.NAMES[ed.id] + ' ' + JSON.stringify(fx));
  } catch (x) { ok(false, p.split('/').pop() + ': ' + x.message); }
}
console.log(bad ? bad + ' Fehler' : 'alles gut');
process.exit(bad ? 1 : 0);
