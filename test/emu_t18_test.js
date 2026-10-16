// T18Emu in the ROM: block placement, settings block, upgrade, swap with T7Emu, removal, keeping the save on the device.
// node test/emu_t18_test.js <land.bin> <tg18_firmware.bin> <v7_rom.bin>
const fs = require('fs');
const E = require('../src/emu.js'), F = require('../src/feld.js'), FL = require('../src/flash.js');
const [land, tg18, v7] = process.argv.slice(2);
const L = new Uint8Array(fs.readFileSync(land)), G = new Uint8Array(fs.readFileSync(tg18)), R = new Uint8Array(fs.readFileSync(v7));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../t18/t18.bin')); app.xip = new Uint8Array(fs.readFileSync(__dirname + '/../t18/t18x.bin'));
const t7app = new Uint8Array(fs.readFileSync(__dirname + '/../t7/t7.bin'));
const stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const ff = (b, a, e) => b.subarray(a, e).every((v) => v === 0xFF);
(async () => {
  ok(E.t18Detect(G) && !E.t18Detect(R) && !E.t7Detect(G), 'detection');
  const r = E.t18Patch(L, G, app, stub, { lang: 'en' }), a = r.rom, c = E.t18Cfg(a);
  ok(F.appKind(a) === 2 && F.emuType(a) === 't18', 'kind: emulator, type t18');
  let used = 0, placed = true;
  for (let i = 0; i < 128; i++) {
    const empty = ff(G, i << 16, (i + 1) << 16);
    if (empty) { if (c.map[i] !== 0xFFFF) placed = false; continue; }
    used++;
    const b = c.map[i];
    if (b === 0xFFFF || !Buffer.from(a.subarray(b << 16, (b + 1) << 16)).equals(Buffer.from(G.subarray(i << 16, (i + 1) << 16)))) placed = false;
    if (b < 0x83 || (b >= 0xD4 && b < 0xF0)) placed = false;
  }
  ok(placed && used === r.blocks, 'firmware blocks in place (' + r.blocks + '), empty ones left out');
  const u32 = (o) => (a[o] | a[o + 1] << 8 | a[o + 2] << 16 | a[o + 3] << 24) >>> 0;
  ok(c.spare.length === r.spares && r.spares >= 6 && r.spares <= 10 && c.spare.every((b) => ff(a, b << 16, (b + 1) << 16) && !c.map.includes(b)), 'spares empty and apart (' + r.spares + ')');
  const all = c.map.filter((b) => b !== 0xFFFF).concat(c.spare), low = Math.min(...all);
  // hot window (1.6.6)
  ok(r.hot && (c.flags & 4) && app.xip.every((v, i) => a[0xFDF000 + i] === v) && app.xip.every((v, i) => a[0xF0000 + i] === v), 'hot window: flag, flash part twice (0x0F0000 and 0xFDF000)');
  ok([0x12, 0x14, 0x15, 0x13, 0x1A, 0x00].every((g) => c.map[g] >= 0xF0 && c.map[g] !== 0xFD && c.map[g] !== 0xFE) && !all.includes(0xFD) && !all.includes(0xFE), 'hot window: code blocks in 0xF00000..0xFFFFFF, 0xFD/0xFE kept for the flash part');
  ok(!!(c.flags & 16) && [0x3F, 0x4A, 0x32, 0x18, 0x40].every((g) => c.map[g] >= 0xF0), 'hot window v2: picture blocks of the pet in the window');
  { const o = new Uint8Array(a); o[0xA0000 + 0x140 + 400] &= ~16; const up = E.t18Upgrade(o, app, stub), cu = up && E.t18Cfg(up);
    ok(!!up && (cu.flags & 20) === 20 && cu.map.every((b, g) => b === 0xFFFF || up.subarray(b << 16, (b + 1) << 16).every((v, k) => v === G[(g << 16) + k])), 'upgrade: hot window of the first list gets the new one, firmware the same'); }
  { const r0 = E.t18Patch(L, G, app, stub, { lang: 'en', hotWindow: false }), c0 = E.t18Cfg(r0.rom);
    ok(!r0.hot && !(c0.flags & 4) && ff(r0.rom, 0xFD0000, 0xFF0000) === false || !r0.hot, 'without hot window: old layout');
    const up = E.t18Upgrade(r0.rom, app, stub), cu = up && E.t18Cfg(up);
    ok(!!up && (cu.flags & 4) && (cu.flags & 1) && cu.map.every((b, g) => b === 0xFFFF ? c0.map[g] === 0xFFFF : up.subarray(b << 16, (b + 1) << 16).every((v, k) => v === G[(g << 16) + k])), 'upgrade moves an old layout to the hot window, firmware the same');
    ok(E.t18Upgrade(up, app, stub) === null, 'upgrade again: nothing to do');
    // the blocks the firmware wrote (map in the save area) come along
    const w = new Uint8Array(r0.rom), sp = c0.spare[0], g5 = 0x7F;
    w.fill(0x5A, sp << 16, (sp + 1) << 16);
    const m = c0.map.slice(); m[g5] = sp;
    w.fill(0xFF, 0xC0000, 0xC1000); w[0xC0000] = 0x54; w[0xC0001] = 0x38; w[0xC0002] = 0x4D; w[0xC0003] = 0x50;
    m.forEach((b, i) => { w[0xC0004 + 2 * i] = b & 255; w[0xC0005 + 2 * i] = b >> 8; });
    const up2 = E.t18Upgrade(w, app, stub), c2 = E.t18Cfg(up2);
    ok(up2.subarray(c2.map[g5] << 16, (c2.map[g5] + 1) << 16).every((v) => v === 0x5A), 'upgrade keeps the blocks the firmware wrote');
  }
  ok(all.filter((b) => b >= 0xF0).length === (r.hot ? 14 : 16) && E.t18Low(a) === low << 16 && ff(a, 0x830000, low << 16) && low >= 0x87, 'from the top down, the bottom stays free (lowest block 0x' + (low << 16).toString(16) + ')');
  ok(a.subarray(0xF0000, 0xF0000 + app.xip.length).every((v, i) => v === app.xip[i]) && (u32(0xA0140 + 400) & 2) === 2, 'flash part at 0x0F0000, clock switch on');
  { const noX = new Uint8Array(app); let err = ''; try { E.t18Patch(L, G, noX, stub, {}); } catch (e) { err = e.message; } ok(/flash part/.test(err), 'program without flash part refused'); }
  const ad = E.t18Addrs(G), ci = 0xA0140;
  ok((c.flags & 1) === 1 && u32(ci + 404) === ad.idle && u32(ci + 408) === ad.soft && u32(ci + 412) === ad.dma && ad.idle && ad.soft && ad.dma, 'settings block (language, addresses)');
  ok(a.subarray(0xD49000, 0xF00000).every((v, i) => v === L[0xD49000 + i]), 'Paradise save untouched');
  { // the menu with the field switch next to T18Emu: the saves below its lowest block
    const fe = { feld: new Uint8Array(fs.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
    let m = null, err = ''; try { m = F.feldApply(a, { fields: true, bag: false }, [], fe, E); } catch (e) { err = e.message; }
    const f = m && F.feldFind(m, fe.stub);
    ok(!!f && f.nslots === 3 && f.slotOff + f.nslots * f.slotSize <= E.t18Low(a) && f.slotSize >= 0x10000 && E.t18Cfg(m).map.every((x, i) => x === c.map[i]), 'field switch next to T18Emu (' + (f ? (f.slotSize >> 10) + ' KB per field' : err) + ')');
  }
  ok(ff(a, 0xC0000, 0xF0000), 'save area empty');
  // the same firmware again keeps the save area
  const withSave = new Uint8Array(a); withSave.fill(0x5A, 0xC0000, 0xC3000);
  const b = E.t18Patch(withSave, G, app, stub, {}).rom;
  ok(b.subarray(0xC0000, 0xC3000).every((v) => v === 0x5A) && E.t18Cfg(b).map.every((x, i) => x === c.map[i]), 'same firmware: same blocks, save kept');
  const G2 = new Uint8Array(G); G2[0x100] ^= 1;
  ok(ff(E.t18Patch(withSave, G2, app, stub, {}).rom, 0xC0000, 0xF0000), 'other firmware: save dropped');
  // upgrade
  const app2 = new Uint8Array(app); app2[app2.length - 1] ^= 1; app2.xip = new Uint8Array(app.xip); app2.xip[app2.xip.length - 1] ^= 1;
  const u = E.t18Upgrade(a, app2);
  ok(u && E.t18Cfg(u).map.every((x, i) => x === c.map[i]) && u[0xA0000 + app2.length - 1] === app2[app2.length - 1] && u[0xF0000 + app2.xip.length - 1] === app2.xip[app2.xip.length - 1], 'newer program (both parts) keeps the settings');
  ok(E.t18Upgrade(a, app) === null, 'same program: nothing to do');
  ok(E.t18ClockGet(a) === true && E.t18ClockGet(E.t18ClockSet(a, false)) === false, 'clock switch');
  // swap to T7Emu and back, removal
  const s = E.t7Patch(a, R, [], t7app, stub, {});
  ok(F.emuType(s) === 't7', 'swap to T7Emu');
  const s2 = E.t18Patch(s, G, app, stub, {}).rom;
  ok(F.emuType(s2) === 't18' && ff(s2, 0xC0000, 0xF0000), 'back to T18Emu');
  ok(F.emuType(s) === 't7' && ff(s, 0xF0000, 0x100000), 'swap to T7Emu clears the flash part');
  const rm = E.emuRemove(a);
  ok(F.appKind(rm) === 0 && c.map.concat(c.spare).every((x) => x === 0xFFFF || ff(rm, x << 16, (x + 1) << 16)) && ff(rm, 0xC0000, 0x110000), 'removal frees every block');
  // device write with "keep saves": the device's save and written spares stay, firmware blocks come from the image
  const dev = new Uint8Array(a); dev.fill(0x33, 0xC0000, 0xC2000); const sp = c.spare[0]; dev.fill(0x44, sp << 16, (sp << 16) + 0x1000);
  const blk0 = c.map.find((x) => x !== 0xFFFF);
  const img = new Uint8Array(a);
  const changed = new Set(); for (let s3 = 0; s3 < 0x1000000; s3 += 0x1000) if (!Buffer.from(dev.subarray(s3, s3 + 0x1000)).equals(Buffer.from(img.subarray(s3, s3 + 0x1000)))) changed.add(s3);
  const fake = { info: { edition: F.edition(img) && F.edition(img).id }, read: async (o, n) => dev.slice(o, o + n) };
  if (F.edition(img)) {
    const plan = await FL.keepPlan(F, E, null, {})(fake, img, changed);
    ok(plan.image.subarray(0xC0000, 0xC2000).every((v) => v === 0x33) && plan.image.subarray(sp << 16, (sp << 16) + 0x1000).every((v) => v === 0x44), 'keep saves: save area and written spare from the device');
    ok(Buffer.from(plan.image.subarray(blk0 << 16, (blk0 + 1) << 16)).equals(Buffer.from(img.subarray(blk0 << 16, (blk0 + 1) << 16))), 'keep saves: firmware block from the image');
  }
  console.log(bad ? bad + ' FAILED' : 'all good');
  process.exit(bad ? 1 : 0);
})();
