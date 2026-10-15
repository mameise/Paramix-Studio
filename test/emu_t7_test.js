// T7Emu in the ROM: layout, figures, upgrade, swap with TamaEmu, removal, keeping the pet's save on the device.
// node test/emu_t7_test.js <land.bin> <v7_rom.bin> <idl_firmware.bin> <figure.bin...>
const fs = require('fs');
const E = require('../src/emu.js'), F = require('../src/feld.js'), FL = require('../src/flash.js');
const [land, v7, idl, ...figf] = process.argv.slice(2);
const L = new Uint8Array(fs.readFileSync(land)), R = new Uint8Array(fs.readFileSync(v7)), I = new Uint8Array(fs.readFileSync(idl));
const figs = figf.map((f) => ({ name: f, data: new Uint8Array(fs.readFileSync(f)) }));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../t7/t7.bin')), emuApp = new Uint8Array(fs.readFileSync(__dirname + '/../emu/emu.bin')), stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const ff = (b, a, e) => b.subarray(a, e).every((v) => v === 0xFF);
(async () => {
  ok(E.t7Detect(R) && figs.every((f) => E.t7FigDetect(f.data)) && !E.t7Detect(I), 'detection');
  const a = E.t7Patch(L, R, figs, app, stub, { lang: 'en' });
  const c = E.t7Cfg(a);
  ok(F.appKind(a) === 2 && F.emuType(a) === 't7', 'kind: emulator, type t7');
  ok(c.romOff === 0x940000 && c.flags === 1 && c.figs.length === figs.length, 'settings block');
  ok(Buffer.from(a.subarray(0x940000, 0x940000 + 0xA0000)).equals(Buffer.from(R)), 'V7 ROM in place');
  ok(c.figs.every((f, i) => Buffer.from(a.subarray(f.off, f.off + f.len)).equals(Buffer.from(figs[i].data.subarray(0, f.len))) && figs[i].data.subarray(f.len).every((v) => v === 0)), 'figures in place (only zero bytes cut)');
  ok(a.subarray(0xD49000).every((v, i) => v === L[0xD49000 + i]), 'Paradise save untouched');
  const back = E.t7FromRom(a);
  ok(Buffer.from(back.rom).equals(Buffer.from(R)) && back.figs.length === figs.length, 'read back from the ROM');
  // pet save stays when only the figures change
  const withEe = new Uint8Array(a); withEe.fill(0x5A, 0xC1000, 0xC5000); withEe.fill(0x6B, 0xC0000, 0xC1000);
  const b = E.t7Patch(withEe, R, figs.slice(1), app, stub, {});
  ok(b.subarray(0xC1000, 0xC5000).every((v) => v === 0x5A) && ff(b, 0xC0000, 0xC1000), 'other figures: EEPROM kept, session dropped');
  const b2 = E.t7Patch(withEe, R, figs, app, stub, {});
  ok(b2.subarray(0xC0000, 0xC5000).every((v, i) => v === withEe[0xC0000 + i]), 'same ROM and figures: session and EEPROM kept');
  // upgrade
  const app2 = new Uint8Array(app); app2[app2.length - 1] ^= 1;
  const u = E.t7Upgrade(a, app2);
  ok(u && E.t7Cfg(u).figs.length === figs.length && u[0xA0000 + app2.length - 1] === app2[app2.length - 1], 'newer program keeps the settings');
  ok(E.t7Upgrade(a, app) === null, 'same program: nothing to do');
  // swap to TamaEmu and back
  const s = E.emuPatch(a, I, emuApp, stub).rom;
  ok(F.emuType(s) === 'tama' && ff(s, 0x940000 + E.packGuest(I, 'idl').blocks * 0x10000, 0xD40000), 'swap to TamaEmu');
  const s2 = E.t7Patch(s, R, [], app, stub, {});
  ok(F.emuType(s2) === 't7' && ff(s2, 0xC0000, 0x110000), 'back to T7Emu: TamaEmu save area cleared');
  const rm = E.emuRemove(a);
  ok(F.appKind(rm) === 0 && ff(rm, 0x940000, 0xD48000) && ff(rm, 0xC0000, 0x110000), 'removal');
  // menu: the fields still fit next to T7Emu (limit 0x940000)
  let fe = true; try { F.feldApply(a, { fields: true, bag: false }, [], { feld: new Uint8Array(fs.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) }, E); } catch (e) { fe = false; console.log(e.message); }
  ok(fe, 'menu with fields next to T7Emu');
  // keepPlan: device has T7Emu with a pet, the new image the same ROM with other figures
  const device = new Uint8Array(a); device.fill(0x33, 0xC1000, 0xC5000); device.fill(0x44, 0xC0000, 0xC1000);
  const img = E.t7Patch(a, R, figs.slice(1), app, stub, {});
  const changed = new Set(); for (let s3 = 0; s3 < 0x1000000; s3 += 0x1000) if (!Buffer.from(device.subarray(s3, s3 + 0x1000)).equals(Buffer.from(img.subarray(s3, s3 + 0x1000)))) changed.add(s3);
  const dev = { info: { edition: F.edition(img).id }, read: async (ad, n) => device.slice(ad, ad + n) };
  const plan = await FL.keepPlan(F, E, null, {})(dev, img, changed);
  ok(plan.image.subarray(0xC1000, 0xC5000).every((v) => v === 0x33) && plan.image.subarray(0xC0000, 0xC1000).every((v) => v !== 0x44), 'write with "keep saves": EEPROM from the device, session not (other figures)');
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
