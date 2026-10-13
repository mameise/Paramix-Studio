// Self test without a real image (runs on GitHub, where no ROMs may be stored).
// Builds a synthetic Paradise image and checks archive, sprites, eye rule,
// character palette, sheets and ZIP. Usage: node test/unit_test.js
const C = require('../src/core.js');
const zlib = require('zlib');
let fails = 0;
const ok = (cond, msg) => { console.log((cond ? 'ok   ' : 'FAIL ') + msg); if (!cond) fails++; };
const img = (w, h, fn) => { const d = new Uint8ClampedArray(w * h * 4); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) d.set(fn(x, y), (y * w + x) * 4); return { w, h, data: d }; };
// a fake "original" sprite in the character format: 4 bpp, palette, transparency
function fakeSprite(w, h, n, seed) {
  const head = new Uint8Array(24); head[5] = 2; head[6] = n; head[8] = w; head[9] = h; head[12] = 1; head[13] = 1; head[14] = 0x11; head[15] = 1; head[4] = 0xa7;
  const tmpl = new Uint8Array([...head]);
  const subs = []; for (let i = 0; i < n; i++) subs.push(img(w, h, (x, y) => ((x + y + i + seed) % 7 === 0) ? [0, 0, 0, 0] : [(x * 9 + seed * 40) & 255, (y * 7) & 255, (i * 30) & 255, 255]));
  // encode through the generic path using a minimal original
  const orig = new Uint8Array(24 + 32 + 8 * n); orig.set(tmpl); orig[18] = 24; orig[20] = 56;
  return C.spriteEncode(orig, subs, {});
}
// 1) archive + pack
const parts = [];
for (let i = 0; i < 6; i++) parts.push(fakeSprite(16, 16, 2, i));
const shapes = [[64, 64, 20], [64, 32, 20], [64, 32, 18], [32, 32, 24], [32, 16, 14], [32, 16, 14]];
const group = shapes.map(([w, h, n], k) => fakeSprite(w, h, n, 10 + k));
const egg = [fakeSprite(32, 32, 5, 1), fakeSprite(16, 16, 1, 2), fakeSprite(16, 16, 1, 3), fakeSprite(64, 64, 5, 4), fakeSprite(16, 16, 1, 5), fakeSprite(16, 16, 1, 6)];
const entries = [...parts, ...egg, ...group];
const pack = C.packJoin(entries);
ok(C.packSplit(pack).every((e, i) => e.length >= entries[i].length), 'pack split/join');
const root = C.arc2Build([new Uint8Array(64).fill(7), pack, C.arc2Build([new Uint8Array(10)])]);
const rom = new Uint8Array(0x1000000).fill(0xff);
rom.set([...'SONIXDEV'].map(c => c.charCodeAt(0)), 0);
rom.set(root, C.ASSET_BASE);
const verPage = (C.ASSET_BASE + root.length + 0x200000) & ~0xfff;
rom.set([1, 0, 0, 0], verPage);
const ctx = C.openRom(rom);
ok(ctx.edition === 'Land', 'edition from version page');
ok(ctx.limit === verPage, 'free space ends at version page');
ok(ctx.groups.length === 1 && ctx.groups[0] === 12, 'character group found');
ok(ctx.egg === 6, 'egg found');
const same = C.buildRom(ctx, new Map());
ok(Buffer.compare(Buffer.from(same.rom), Buffer.from(rom)) === 0, 'rebuild without changes is byte identical');
// 2) roundtrip of every sprite
let rt = true;
for (const e of ctx.entries) { const d = C.spriteDecode(e); const d2 = C.spriteDecode(C.spriteEncode(e, d.subs, {})); rt = rt && d2.subs.every((s, k) => C.sameImage(s, d.subs[k])); }
ok(rt, 'decode/encode roundtrip is pixel exact');
// 3) character rules
const g = ctx.groups[0];
const red = img(64, 64, (x, y) => (x > 10 && x < 50 && y > 10) ? [250, 30, 40, 255] : (x === 10 ? [0, 0, 0, 255] : [0, 0, 0, 0]));
const body = C.spriteEncode(ctx.entries[g], Array(20).fill(red), C.partOptions(0));
const h = C.spriteHeader(body), dec = C.spriteDecode(body);
const used = new Set(); dec.cells.forEach(c => c.forEach(v => used.add(v)));
ok(![2, 3, 4, 5, 6, 7].some(v => used.has(v)), 'new colours avoid the recoloured slots 2-7');
ok(used.has(1) && dec.palettes[0][1] === 0x000c, 'outline on slot 1 = 0x000C');
ok(dec.palettes[0][0] === 0x07e0 && h.bpp === 2, 'slot 0 colour key, 4 bpp');
const eyesOrig = ctx.entries[g + 1];
const eyesBlank = C.spriteBlank(eyesOrig, C.partOptions(1));
ok(eyesBlank.length === eyesOrig.length && C.spriteHeader(eyesBlank).dataLength === eyesOrig.length, 'blank eyes keep the original byte length');
let threw = false; try { C.spriteEncode(eyesOrig, Array(20).fill(img(64, 32, (x, y) => [(x * 37 + y * 91) & 255, (x * 13) & 255, (y * 71) & 255, 255])), C.partOptions(1)); } catch (e) { threw = true; }
ok(threw, 'too detailed eyes are rejected');
// 4) sheets
const parts6 = [0, 1, 2, 3, 4, 5].map(k => C.spriteDecode(ctx.entries[g + k]).subs);
const sheet = C.groupSheet(parts6);
ok(sheet.w === 1280 && sheet.h === 192, 'character sheet 1280x192');
ok(C.groupFromSheet(sheet).every((p, k) => p.every((s, i) => C.sameImage(s, parts6[k][i]))), 'character sheet splits back');
const ss = C.sheetFromSubs(parts6[3]);
ok(C.subsFromSheet(ss, 32, 32, 24).every((s, i) => C.sameImage(s, parts6[3][i])), 'sprite sheet splits back');
// 5) changed ROM
const changed = new Map([[g, body], [g + 1, eyesBlank]]);
const out = C.buildRom(ctx, changed);
const ctx2 = C.openRom(out.rom);
ok(C.sameImage(C.spriteDecode(ctx2.entries[g]).subs[0], C.spriteDecode(body).subs[0]), 'changed ROM reads back');
// 6) zip
const z = C.zipBuild([{ name: 'figur_0012.png', data: new Uint8Array([1, 2, 3]) }, { name: 'a/b.txt', data: new TextEncoder().encode('hallo') }]);
C.zipRead(z, async (b) => new Uint8Array(zlib.inflateRawSync(b))).then(files => {
  ok(files.length === 2 && files[1].name === 'a/b.txt' && new TextDecoder().decode(files[1].data) === 'hallo', 'zip write/read');
  // 7) card pose placement
  const ch = { baby: false, frames: { idle1: img(64, 56, () => [200, 100, 50, 255]) } };
  const p = C.dimCharParts(ch);
  ok(p[0].length === 20 && p[3].length === 24 && p[0][0].w === 64, 'card character parts');
  // 8) hide eye layer: body 10 at (5,-20), eye 11 follows; eye moves far away, rest unchanged
  const dp = new Uint8Array(new Uint16Array([0, 10, 1027, 5, 65516, 1, 11, 1539, 0, 65520, 15, 12, 1031, 0, 0, 0, 0, 0]).buffer);
  const hd = C.hideEyes(dp, 10, 11);
  const hw = new Uint16Array(hd.dataPack.buffer.slice(0));
  ok(hd.count === 1 && hw[8] === 2005 && hw[9] === ((-2020) & 0xffff) && hw[3] === 5 && hw[13] === 0, 'eye layer hidden');
  // 9) DOOM without WAD: read/write WAD, find episodes, converter runs (synthetic mini WAD without game data)
  const D = require('../src/doom.js'), fs2 = require('fs');
  const mini = D.writeWad([['E2M1', new Uint8Array(0)], ['THINGS', new Uint8Array(10)], ['PLAYPAL', new Uint8Array(768)]], 'IWAD');
  const back = D.readWad(mini);
  ok(back.length === 3 && back[0][0] === 'E2M1' && back[2][1].length === 768, 'doom wad write/read');
  ok(JSON.stringify(D.wadEpisodes(mini)) === '["2"]', 'doom episodes');
  return WebAssembly.instantiate(fs2.readFileSync(__dirname + '/../doom/mcuwad.wasm'), {}).then(({ instance }) => {
    ok(typeof instance.exports.convert === 'function' && typeof instance.exports.alloc === 'function', 'doom converter loads');
  });
}).then(() => {
  // 10) field switch on a synthetic ROM (scene table, ID Land)
  const E = require('../src/emu.js'), F = require('../src/feld.js'), fs3 = require('fs');
  const r = new Uint8Array(0x1000000); r.fill(0xFF);
  const put32 = (o, v) => { r[o] = v & 255; r[o + 1] = (v >>> 8) & 255; r[o + 2] = (v >>> 16) & 255; r[o + 3] = (v >>> 24) & 255; };
  const xp = (o) => 0x10000000 + o - 0x11000;
  ['PSID_TEST', 'PSID_HOME', 'PSID_MORE'].forEach((n, k) => { const o = 0x200000 + k * 16; for (let i = 0; i < n.length; i++) r[o + i] = n.charCodeAt(i); r[o + n.length] = 0; put32(0x300004 + k * 28, xp(o)); });
  for (let k = 0; k < 3; k++) for (let h = 0; h < 4; h++) put32(0x300000 + k * 28 + 12 + 4 * h, 0x10001001 + k * 0x100 + h * 0x10);
  put32(0x111008, 0x1000); put32(0xD49000, 1);
  const fe = { feld: new Uint8Array(fs3.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs3.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')), oldStub: new Uint8Array(fs3.readFileSync(__dirname + '/../emu/stub_tpl.bin')) };
  const a = F.feldApply(r, { fields: true, bag: true }, [], fe, E), fa = F.feldFind(a, fe.stub);
  ok(fa && fa.off === 0xB1000 && fa.slotOff === 0x830000 && fa.nslots === 3, 'field switch built in');
  const home = E.sceneTable(a).PSID_HOME;
  ok(((a[home + 12] | a[home + 13] << 8 | a[home + 14] << 16 | a[home + 15] << 24) >>> 0) === (xp(0xB0040) | 1), 'home screen hooked');
  ok(Buffer.compare(Buffer.from(F.feldStrip(a, fe, E)), Buffer.from(r)) === 0, 'field switch removed again');
  const g = new Uint8Array(r); g[0xD49000] = 5; let err = ''; try { F.feldApply(g, { fields: true, bag: true }, [], fe, E); } catch (e) { err = e.message; }
  ok(F.feldId(g) === 0 && err, 'other editions refused');
}).then(async () => {
  // 11) flasher: CRC, frames, write with "only changes" and "keep saves" against a fake device
  const FL = require('../src/flash.js'), F = require('../src/feld.js'), E = require('../src/emu.js');
  ok(FL.crc32(Buffer.from('123456789')) === 0xCBF43926, 'flash crc32');
  const order = FL.sectorOrder();
  ok(order.length === 4096 && order[0] === 0x111000 && order[4095] === 0, 'flash sector order');
  const pd = FL.buildPdlo(new Uint8Array([1, 2, 3, 4]));
  ok(pd.length === 0x10010 && (((pd[0] | pd[1] << 8 | pd[2] << 16 | pd[3] << 24) ^ 0xE5F04076) >>> 0) === 0x4F4C4450, 'flash PDLO header');
  // fake device: 16 MB flash, answers PAU1 like the flash app
  function fakeDevice(mem) {
    const devSide = new FL.Link({ write: async () => {} });
    const sec = new Uint8Array(0x1000), io = { onData: null };
    io.write = async (d) => {
      devSide.feed(d);
      for (const f of devSide.frames.splice(0)) {
        const p = f.payload, u = (o) => (p[o] | p[o + 1] << 8 | p[o + 2] << 16 | p[o + 3] << 24) >>> 0;
        let out = new Uint8Array(0);
        if (f.type === 1) { out = new Uint8Array(44); out[8] = mem[0xD49000]; out[10] = 0; out[11] = 16; out.set([0, 0, 0, 1], 32); }
        else if (f.type === 5) out = mem.slice(f.off, f.off + u(0));
        else if (f.type === 6) { const c = FL.crc32(mem, f.off, u(0)); out = new Uint8Array([c & 255, c >>> 8 & 255, c >>> 16 & 255, c >>> 24]); }
        else if (f.type === 13) { out = new Uint8Array(16); out[12] = 1; }
        else if (f.type === 9) out = new Uint8Array(4);
        else if (f.type === 10) sec.set(p, f.off & 0xFFF);
        else if (f.type === 11) { mem.set(sec, f.off & ~0xFFF); out = new Uint8Array(8); }
        setImmediate(() => io.onData(devSide.frame(f.type, f.seq, f.off, out)));
      }
    };
    return io;
  }
  const mem = new Uint8Array(0x1000000); mem.fill(0xFF);
  mem.set(Buffer.from('SONIXDEV')); mem[0xD49000] = 1; mem[0xD49001] = 0; mem[0xD49002] = 0; mem[0xD49003] = 0;
  for (let i = 0; i < 0x1000; i++) mem[0xEFE000 + i] = i & 255;          // save data on the device
  const io = fakeDevice(mem);
  const { dev } = await FL.connect(io, new Uint8Array(16), {});
  ok(dev.info.edition === 1 && dev.info.capacity === 0x1000000, 'flash hello');
  const img = new Uint8Array(mem); img.fill(0, 0xEFE000, 0xEFF000); img[0x200000] = 0x42;
  const keep = FL.keepPlan(F, E, null, {});
  let r = await dev.write(img, { onlyChanged: true, keep }, null);
  ok(r.written === 1 && r.kept === 1 && mem[0x200000] === 0x42 && mem[0xEFE001] === 1, 'flash: only changes, save kept');
  r = await dev.write(img, { onlyChanged: true, keep: null }, null);
  ok(r.written === 1 && mem[0xEFE001] === 0, 'flash: without keep the save is written');
  const other = new Uint8Array(img); other[0xD49000] = 2; let e2 = '';
  try { await dev.write(other, { onlyChanged: true, keep }, null); } catch (e) { e2 = e.code || e.message; console.log('   (' + e2 + ')'); }
  ok(e2 === 'otherEd', 'flash: other edition refused with keep');
  // field slots: same layout is kept, another layout gets the stored fields moved over
  {
    const fs4 = require('fs');
    const r = new Uint8Array(0x1000000); r.fill(0xFF); r.set(Buffer.from('SONIXDEV'));
    const put32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
    const xp = (o) => 0x10000000 + o - 0x11000;
    ['PSID_TEST', 'PSID_HOME', 'PSID_MORE'].forEach((n, k) => { const o = 0x200000 + k * 16; for (let i = 0; i < n.length; i++) r[o + i] = n.charCodeAt(i); r[o + n.length] = 0; put32(r, 0x300004 + k * 28, xp(o)); });
    for (let k = 0; k < 3; k++) for (let h = 0; h < 4; h++) put32(r, 0x300000 + k * 28 + 12 + 4 * h, 0x10001001 + k * 0x100 + h * 0x10);
    put32(r, 0x111008, 0x1000); put32(r, 0xD49000, 1);
    const menu = { feld: new Uint8Array(fs4.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs4.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
    const rom = F.feldApply(r, { fields: true, bag: false }, [], menu, E), fi = F.feldFind(rom, menu.stub);
    const m2 = new Uint8Array(rom), slot = fi.slotOff + fi.slotSize;          // device: Water stored in slot 2
    put32(m2, slot, 0x544C5346); put32(m2, slot + 4, 2); put32(m2, slot + 8, 3); put32(m2, slot + 12, 0x2000); m2.fill(0x77, slot + 0x1000, slot + 0x3000);
    const io2 = fakeDevice(m2), d2 = (await FL.connect(io2, new Uint8Array(16), {})).dev;
    const k2 = FL.keepPlan(F, E, menu, {});
    let q = await d2.write(rom, { onlyChanged: true, keep: k2 }, null);
    ok(q.written === 0 && m2[slot + 0x2000] === 0x77, 'flash: stored field kept (same layout)');
    // new ROM with the slots somewhere else
    const moved = new Uint8Array(rom); let ci = -1;
    for (let i = fi.off; i < fi.off + 0x8000; i += 4) if (moved[i] === 0x46 && moved[i + 1] === 0x45 && moved[i + 2] === 0x4C && moved[i + 3] === 0x44) { ci = i; break; }
    put32(moved, ci + 8, 0x900000);
    q = await d2.write(moved, { onlyChanged: true, keep: k2 }, null);
    ok(m2[0x900000 + fi.slotSize + 0x2000] === 0x77 && m2[0x900000 + fi.slotSize + 4] === 2 && q.notes.some((n) => n.code === 'noteMoved'), 'flash: stored field moved to the new layout');
  }
}).then(() => {
  // 12) TamaEmu download content: route, add, list, remove (synthetic record)
  const E = require('../src/emu.js');
  const g = new Uint8Array(0x800000); g.fill(0xFF);
  const rec = new Uint8Array(0x100); rec.set(Buffer.from('TAMAGO'));
  const nm = 'DL_X_cake.jpg'; for (let i = 0; i < nm.length; i++) rec[7 + 2 * i] = nm.charCodeAt(i);
  rec[0x4E] = 0x81; rec[0x50] = 0x02; rec[0x51] = 0x01; rec[0x4B] = 0x00; rec[0x4A] = 0x01;
  let sum = 0; for (let i = 0; i < 0xFE; i++) sum += rec[i]; rec[0xFE] = sum >> 8 & 255; rec[0xFF] = sum & 255;
  const r = E.dlcRecord(rec);
  ok(E.dlcRoute(r).label === 'Meals', 'dlc routed to meals');
  const a = E.dlcAdd(g, r);
  ok(a.slot === 0 && E.dlcStores(a.guest)[0].slots[0].name === 'cake', 'dlc added');
  let dup = ''; try { E.dlcAdd(a.guest, r); } catch (e) { dup = e.message; }
  ok(dup, 'dlc duplicate refused');
  ok(E.dlcStores(E.dlcFree(a.guest, 0, 0))[0].slots[0].name === null, 'dlc removed');
}).then(() => {
  console.log(fails ? fails + ' failed' : 'all good');
  process.exit(fails ? 1 : 0);
});
