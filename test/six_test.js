// Six fields (local only, needs your own images).
// node test/six_test.js <tropical_or_glacier.bin> <land_water_or_sky.bin> [jade_forest.bin]
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const E = require('../src/emu.js'), F = require('../src/feld.js'), S = require('../src/six.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
let bad = 0;
function ok(c, m) { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; }
const trop = rd(process.argv[2]), land = rd(process.argv[3]);
const blob = rd(__dirname + '/../six/six.bin');
const emb = { feld: rd(__dirname + '/../feld/feld.bin'), stub: rd(__dirname + '/../feld/stub_feld_tpl.bin') };

ok(S.sixCan(trop), 'base ROM can get six fields');
ok(S.sixSource(land), 'second ROM gives Land, Water, Sky');
ok(!S.sixSource(trop) && !S.sixCan(land), 'the two are not mixed up');
let base = F.feldStrip(trop, emb, E);
const t0 = Date.now();
const six = S.sixApply(base, land, blob);
ok(true, 'built in ' + (Date.now() - t0) + ' ms');
const info = S.sixInfo(six), ctx = C.openRom(six);
ok(info && info.sprites === C.openRom(base).entries.length, 'helper page ' + JSON.stringify(info));
ok(ctx.assetEnd < 0xD29000, 'archive ends at 0x' + ctx.assetEnd.toString(16) + ' (before the ID page)');
ok(ctx.entries.length >= info.sprites + 12, (ctx.entries.length - info.sprites) + ' pictures added');
{ const st = S.sixStrip(six), cs = C.openRom(st), cb = C.openRom(base);
  ok(Buffer.compare(Buffer.from(st.subarray(0x11000, 0x111000)), Buffer.from(base.subarray(0x11000, 0x111000))) === 0, 'strip: firmware and helper page as before');
  ok(cs.entries.length === cb.entries.length && Buffer.compare(Buffer.from(cs.files[0]), Buffer.from(cb.files[0])) === 0, 'strip: data pack and picture count as before'); }
ok(S.sixApply(six, land, blob) === six, 'second run changes nothing');
// firmware patches point into the helper page
const tgt = (o) => { const h1 = six[o] | six[o + 1] << 8, h2 = six[o + 2] | six[o + 3] << 8; return h1 >> 11 === 0x1E && (h2 >> 14) >= 2; };
ok(tgt(0x1BEAC) && tgt(0x20ED2) && tgt(0x20FBC) && tgt(0x20D62), 'branches into the helper');
ok(info.fieldMenu && tgt(0x11000 + 0x265E2) && tgt(0x11000 + 0x41690) && tgt(0x11000 + 0x416B8) && tgt(0x11000 + 0x414EC) && six[0x11000 + 0x41496 + 2] === 0x53, 'game field menu hooked');
{ const nm = S.sixApply(six, land, blob, { fieldMenu: false }), ni = S.sixInfo(nm);
  ok(ni && !ni.fieldMenu && nm[0x11000 + 0x41690] === 0x90 && nm[0x11000 + 0x41496 + 2] === 0x4F, 'switch off: game field menu as before'); }
ok(info.games && tgt(0x11000 + 0x50B1E) && tgt(0x11000 + 0x2FFE4) && tgt(0x11000 + 0x83938), 'lab games per start field hooked');
{ const ng = S.sixApply(six, land, blob, { games: false }), gi = S.sixInfo(ng);
  ok(gi && !gi.games && Buffer.compare(Buffer.from(ng.subarray(0x11000 + 0x50B1E, 0x11000 + 0x50B22)), Buffer.from(base.subarray(0x11000 + 0x50B1E, 0x11000 + 0x50B22))) === 0 && ng[0x11000 + 0x2FFE4] === 0xB2 && ng[0x11000 + 0x2FFE5] === 0xF8, 'switch off: lab games as before'); }
ok(u32(six, 0xF00000) === 0x42584953, 'rain pictures in region B');
// DNA per field (1.7.2): on by default, off and on again without the Land ROM
if (S.DNA) {
  const fo = (x) => x - 0x10000000 + 0x11000;
  ok(info.dna && tgt(fo(0x10081C68)) && tgt(fo(0x1000AE44)) && tgt(fo(0x10013BBE)) && tgt(fo(0x10068B54)) && six[fo(0x1003BFB2)] === 6, 'DNA per field hooked');
  const off = S.sixDna(six, blob, false), oi = S.sixInfo(off);
  ok(oi && !oi.dna && off[fo(0x10081C68)] === 0x80 && off[fo(0x1003BFB2)] === 3 && tgt(0x20D62) && tgt(0x11000 + 0x416B8), 'DNA off: firmware as with six fields only');
  { const st = S.sixStrip(off), st2 = S.sixStrip(six);
    ok(Buffer.compare(Buffer.from(st.subarray(0x11000, 0x111000)), Buffer.from(st2.subarray(0x11000, 0x111000))) === 0, 'DNA off, then strip: same as strip'); }
  const on = S.sixDna(off, blob, true);
  ok(Buffer.compare(Buffer.from(on), Buffer.from(six)) === 0, 'DNA on again: same ROM');
  const nd = S.sixApply(base, land, blob, { dna: false });
  ok(!S.sixInfo(nd).dna && Buffer.compare(Buffer.from(nd.subarray(0x11000, 0x111000)), Buffer.from(off.subarray(0x11000, 0x111000))) === 0, 'six fields without DNA: same as DNA off');
  ok(Buffer.compare(Buffer.from(S.sixApply(nd, land, blob)), Buffer.from(six)) === 0, 'apply with DNA on a ROM without: DNA added');
}
{ const moved = C.buildRom(C.openRom(six), new Map(), null).rom; ok(Buffer.compare(Buffer.from(S.sixFix(moved)), Buffer.from(six)) === 0, 'sixFix after a rebuild gives the same ROM'); }
for (let i = 0; i < 12; i++) { const d = C.spriteDecode(ctx.entries[info.sprites + i]); if (d.subs[0].w !== 512) { ok(false, 'picture ' + i); break; } }
ok(true, 'pictures decode');
// menu with six fields
const m = F.feldApply(six, { fields: true, bag: true }, F.feldSlots(trop, emb.stub), emb, E);
const f = F.feldFind(m, emb.stub);
ok(F.fieldNames(m).join() === 'Tropical,Glacier,Forest,Land,Water,Sky', 'menu: ' + F.fieldNames(m).join(', '));
{ const cfg = m.subarray(0, 0x111000); let fam = 0; for (let o = 0; o + 4 <= cfg.length; o += 4) if (cfg[o] === 0x05 && cfg[o + 1] === 0x0F && cfg[o + 3] === 0 && (cfg[o + 2] === 1 || cfg[o + 2] === 2)) fam++;
  ok(fam === 4, 'menu: fresh Land, Water, Sky and Forest get their game family (' + fam + ')'); }
ok(f.nslots === 6 && f.version >= 4, 'six slots, ' + (f.slotSize >> 10) + ' KB each');
const addr = []; for (let i = 0; i < 6; i++) addr.push(f.slotOff2 && i >= f.slotN1 ? f.slotOff2 + (i - f.slotN1) * f.slotSize : f.slotOff + i * f.slotSize);
const clash = addr.some((a, i) => (a < 0x10F000 && a + f.slotSize > 0x10F000) || (a >= 0x111000 && a < ctx.assetEnd) || a + f.slotSize > 0xD29000 || addr.some((b, k) => k !== i && Math.abs(a - b) < f.slotSize));
ok(!clash, 'slots: ' + addr.map((a) => '0x' + a.toString(16)).join(' '));
const again = F.feldApply(F.feldStrip(m, emb, E), { fields: true, bag: true }, F.feldSlots(m, emb.stub), emb, E);
ok(Buffer.compare(Buffer.from(again), Buffer.from(m)) === 0, 'remove and add the menu gives the same ROM');
// Jade Forest: Sky as 4th field (optional third argument)
if (process.argv[4]) {
  const jade = F.feldStrip(rd(process.argv[4]), emb, E);
  ok(S.sixCan(jade) && !S.sixNeedsSource(jade), 'Jade Forest can get Sky without a second ROM');
  const j4 = S.sixApply(jade, null, blob);
  ok(Buffer.compare(Buffer.from(j4.subarray(0x111000, 0xD49000)), Buffer.from(jade.subarray(0x111000, 0xD49000))) === 0, 'Jade: archive untouched');
  const jm = F.feldApply(j4, { fields: true, bag: true }, [], emb, E);
  ok(F.fieldNames(jm).join() === 'Land,Water,Forest,Sky', 'Jade menu: ' + F.fieldNames(jm).join(', '));
  ok(S.sixInfo(j4).fieldMenu, 'Jade: field menu flag');
  { const jt = (o) => { const h1 = j4[o] | j4[o + 1] << 8, h2 = j4[o + 2] | j4[o + 3] << 8; return h1 >> 11 === 0x1E && (h2 >> 14) >= 2; };
    ok(jt(0x11000 + 0x2262C) && jt(0x11000 + 0x3C470) && jt(0x11000 + 0x3C498) && jt(0x11000 + 0x3C2E0) && j4[0x11000 + 0x3C29E + 2] === 0x4F, 'Jade: game field menu hooked'); }
  ok(Buffer.compare(Buffer.from(S.sixStrip(j4)), Buffer.from(jade)) === 0, 'Jade: strip gives the original');
}
console.log(bad ? bad + ' WRONG' : 'all good');
process.exit(bad ? 1 : 0);
