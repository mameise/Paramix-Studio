// More emulators (1.6.0): a second T7Emu (own ROM, own save) started from the menu.
// node test/emu_apps_test.js <land.bin> <tama-go.bin> <friends.bin> [out.bin]
const fs = require('fs');
const E = require('../src/emu.js'), F = require('../src/feld.js'), FL = require('../src/flash.js'), SV = require('../src/saves.js');
const [land, go, fr, outf] = process.argv.slice(2);
const L = new Uint8Array(fs.readFileSync(land)), G = new Uint8Array(fs.readFileSync(go)), R = new Uint8Array(fs.readFileSync(fr));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../t7/t7.bin')), stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
const fe = { feld: new Uint8Array(fs.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
function compose(base, primary, extras, old) {
  let rom = F.feldStrip(base, { stub: fe.stub, oldStub: stub }, E);
  if (primary) rom = E.t7Patch(rom, primary, [], app, stub, {});
  if (old) rom = E.appsClear(rom, E.appsRead(old, F.feldApps(old, fe.stub).apps));
  const r = E.appsPlace(rom, extras.map((x) => ({ rom: x })), app, {});
  const name0 = F.emuType(r.rom) === 't7' ? E.t7XName(E.t7FromRom(r.rom).rom) : '';
  return { rom: F.feldApply(r.rom, { fields: true, bag: true, apps: r.list, name0, appsLow: r.low }, old ? F.feldSlots(old, fe.stub) : [], fe, E), list: r.list };
}
(async () => {
  const a = compose(L, R, [G]);
  const fa = F.feldApps(a.rom, fe.stub);
  ok(fa.name0 === 'FRIENDS' && fa.apps.length === 1 && fa.apps[0].name === 'TAMA-GO', 'menu lists ' + fa.name0 + ' and ' + fa.apps.map((x) => x.name).join(','));
  const back = E.appsRead(a.rom, fa.apps);
  ok(back.length === 1 && Buffer.from(back[0].rom).equals(Buffer.from(G)), 'second ROM read back');
  const off = back[0].off, c = E.t7Cfg ? null : null;
  ok(off >= 0x940000 && off + 0x20000 + 0xA0000 <= 0xD40000 && (off & 0xFFFF) === 0, 'placed at 0x' + off.toString(16));
  ok(a.rom[off + 0x140] === 0x50 && a.rom.subarray(off + 0x10000, off + 0x16000).every((v) => v === 0xFF), 'program and empty save');
  ok(Buffer.from(a.rom.subarray(0x940000, 0x940000 + R.length)).equals(Buffer.from(R)), 'Friends stays the program at 0x0A0000');
  ok(a.rom.subarray(0xB0000 + 0x0C, 0xB0000 + 0x10).every((v, i) => v === [0x72, 0xB6, 0x04, 0x46][i]), 'stub entry at +0x0C');
  // again from the ROM: same bytes
  const b = compose(a.rom, R, [G], a.rom);
  ok(Buffer.from(b.rom).equals(Buffer.from(a.rom)), 'built again from the ROM: identical');
  // remove the second one
  const c2 = compose(a.rom, R, [], a.rom);
  ok(F.feldApps(c2.rom, fe.stub).apps.length === 0 && c2.rom.subarray(off, off + 0xC0000).every((v) => v === 0xFF), 'removed: blocks free');
  // keep saves: the second T7Emu's save stays
  const device = new Uint8Array(a.rom); device.fill(0x5A, off + 0x10000, off + 0x16000);
  const changed = new Set(); for (let s = 0; s < 0x1000000; s += 0x1000) if (!Buffer.from(device.subarray(s, s + 0x1000)).equals(Buffer.from(a.rom.subarray(s, s + 0x1000)))) changed.add(s);
  const dev = { info: { edition: F.edition(a.rom).id }, read: async (x, n) => device.slice(x, x + n) };
  const plan = await FL.keepPlan(F, E, { stub: fe.stub }, {})(dev, a.rom, changed);
  ok(plan.image.subarray(off + 0x10000, off + 0x16000).every((v) => v === 0x5A), 'keep saves: save of the second emulator kept');
  // save.bin of the second one: in, out, and kept over "keep saves" when imported
  const sv = new Uint8Array(4098); sv[0] = 0xFF; sv[1] = 0xFF; for (let i = 2; i < 4098; i++) sv[i] = i & 255;
  const withSv = SV.t7SaveIn(a.rom, sv, off + 0x10000), back2 = SV.t7SaveOut(withSv, off + 0x10000);
  ok(back2 && Buffer.from(back2).equals(Buffer.from(sv)) && !SV.t7SaveOut(withSv), 'save.bin into the second one and out again (main one untouched)');
  const dev2 = new Uint8Array(a.rom); dev2.fill(0x5A, off + 0x10000, off + 0x16000);
  const ch2 = new Set(); for (let s = 0; s < 0x1000000; s += 0x1000) if (!Buffer.from(dev2.subarray(s, s + 0x1000)).equals(Buffer.from(withSv.subarray(s, s + 0x1000)))) ch2.add(s);
  const p2 = await FL.keepPlan(F, E, { stub: fe.stub }, { t7SaveX: 0 })({ info: dev.info, read: async (x, n) => dev2.slice(x, x + n) }, withSv, ch2);
  ok(Buffer.from(SV.t7SaveOut(p2.image, off + 0x10000)).equals(Buffer.from(sv)) && p2.image.subarray(off + 0x15000, off + 0x16000).every((v) => v === 0x5A), 'keep saves with an imported save: the new pet, the device settings');
  // two more (Tama-Go twice is fine for the layout)
  let room = true; try { compose(L, R, [G, R, G]); } catch (e) { room = false; console.log('  ', e.message); }
  ok(room, 'three more fit next to Friends');
  if (outf) fs.writeFileSync(outf, a.rom);
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
