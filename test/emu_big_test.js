// Big guest firmware (P's, 4U+): stored from 0x840000, menu fields refused, upgrade, swap and removal clean up.
// node test/emu_big_test.js <land.bin> <ps_or_4u_firmware.bin> <idl_firmware.bin>
const fs = require('fs');
const E = require('../src/emu.js'), F = require('../src/feld.js'), SV = require('../src/saves.js');
const [land, big, idl] = process.argv.slice(2).map((f) => new Uint8Array(fs.readFileSync(f)));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../emu/emu.bin')), stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
const feldEmb = { feld: new Uint8Array(fs.readFileSync(__dirname + '/../feld/feld.bin')), stub: new Uint8Array(fs.readFileSync(__dirname + '/../feld/stub_feld_tpl.bin')) };
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const ff = (b, a, e) => b.subarray(a, e).every((v) => v === 0xFF);
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const dev = E.emuDetect(big);
ok(['ps', '4u', '4u-plain'].includes(dev), 'detected ' + dev);
const r = E.emuPatch(land, big, app, stub);
ok(r.big && u32(r.rom, 0xA0148) === 0x840000 && u32(r.rom, 0xA0140 + 152) === 0xD48000, 'big layout 0x840000..0xD48000');
ok(u32(r.rom, 0xA0154) === E.DEVICES[dev].kind, 'device kind ' + E.DEVICES[dev].kind);
ok(ff(r.rom, 0xD48000, 0xD49000) && r.rom.subarray(0xD49000, 0xD4A000).every((v, i) => v === land[0xD49000 + i]), 'ID page untouched');
ok(r.rom.subarray(0xD49000).every((v, i) => v === land[0xD49000 + i]), 'save area of the Paradise unchanged');
// guest back out equals the firmware
(async () => {
  const cfg = SV.emuCfg(r.rom.subarray(0xA0000, 0xA1000));
  const g = await SV.emuGuestAsync((a, n) => r.rom.subarray(a, a + n), cfg);
  ok(Buffer.from(g).equals(Buffer.from(big.subarray(0, 0x800000))) && cfg.app === SV.KINDS[E.DEVICES[dev].kind].app, '.sav out equals the firmware (' + cfg.app + ')');
  // newer program keeps the layout
  const app2 = new Uint8Array(app); app2[app2.length - 1] ^= 1;
  const up = E.emuUpgrade(r.rom, app2);
  ok(up && u32(up, 0xA0148) === 0x840000 && up.subarray(0x840000, 0xD48000).every((v, i) => v === r.rom[0x840000 + i]), 'upgrade keeps the big layout');
  // menu with fields refused, menu with the bag only works
  let msg = ''; try { F.feldApply(r.rom, { fields: true, bag: false }, [], feldEmb, E); } catch (e) { msg = e.message; }
  ok(/no room for the field saves next to this emulator firmware/.test(msg), 'menu fields refused: ' + msg);
  let bagOk = true; try { F.feldApply(r.rom, { fields: false, bag: true }, [], feldEmb, E); } catch (e) { bagOk = false; console.log(e.message); }
  ok(bagOk, 'menu with items only works');
  // swap to the iD L: big area cleared, normal layout
  const s = E.emuPatch(r.rom, idl, app, stub);
  ok(!s.big && u32(s.rom, 0xA0148) === 0x940000 && ff(s.rom, 0x840000, 0x940000) && ff(s.rom, 0x940000 + s.blocks * 0x10000, 0xD48000), 'swap to iD L clears the big area');
  const back = E.emuPatch(s.rom, big, app, stub);
  ok(back.rom.every((v, i) => v === r.rom[i] || (i >= 0xA0000 && i < 0xC0000)), 'swap back gives the same layout');
  const rm = E.emuRemove(r.rom);
  ok(ff(rm, 0x830000, 0xD48000) && ff(rm, 0xA0000, 0xB0000), 'removal clears everything');
  console.log(bad ? 'FAIL ' + bad : 'all good');
})();
