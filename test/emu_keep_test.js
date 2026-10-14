// TamaEmu update keeps the save: old emulator ROM on the "device" with a played save,
// new program from the Studio, write with "Keep saves".
// node test/emu_keep_test.js <paradise.bin> <color_tama_firmware.bin>
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const E = require('../src/emu.js'), F = require('../src/feld.js'), FL = require('../src/flash.js');
const rom0 = new Uint8Array(fs.readFileSync(process.argv[2])), guest = new Uint8Array(fs.readFileSync(process.argv[3]));
const app = new Uint8Array(fs.readFileSync(__dirname + '/../emu/emu.bin'));
const stub = new Uint8Array(fs.readFileSync(__dirname + '/../emu/stub_tpl.bin'));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
// old program: same settings block, other code
const old = new Uint8Array(app); old[0x40] ^= 0xFF; old[0x2000] ^= 0x55;
const built = E.emuPatch(rom0, guest, old, stub).rom;
// the device played: guest save sectors, a spare page, a session
const dev = new Uint8Array(built);
const gOff = u32(built, 0xA0148), gLen = u32(built, 0xA014C);
dev.fill(0x11, gOff + 0x3000, gOff + 0x4000);
const spare = (gOff + gLen + 0x1FFF) & ~0x1FFF; dev.fill(0x22, spare, spare + 0x1000); dev.fill(0x23, spare + 0x4000, spare + 0x5000);
dev.fill(0x33, 0xC0000, 0xC2000);
// Studio: open the old ROM file and save
const up = E.emuUpgrade(built, app);
ok(!!up, 'new program recognised');
ok(E.emuUpgrade(up, app) === null, 'second time: nothing to do');
ok(up.subarray(0xA0148, 0xA0140 + 152).every((v, i) => v === built[0xA0148 + i]), 'firmware layout taken over');
ok(u32(up, 0xA0144) === 3 && u32(up, 0xA0140 + 152) === 0xD40000, 'settings version 3, spare end');
ok(up.subarray(0xA0000, 0xA0140).every((v, i) => v === app[i]), 'program is the new one');
ok(up.subarray(gOff, 0xD40000).every((v, i) => v === built[gOff + i]), 'packed firmware untouched');
// keep plan against the device
const SEC = 0x1000, changed = new Set();
for (let s = 0; s < up.length; s += SEC) { for (let i = s; i < s + SEC; i++) if (up[i] !== dev[i]) { changed.add(s); break; } }
const ed = F.edition(up);
const plan = FL.keepPlan(F, E, null, {});
plan({ info: { edition: ed && ed.id, name: 'x' }, read: async (a, n) => dev.slice(a, a + n) }, up, changed).then((r) => {
  const img = r.image;
  ok(img.subarray(gOff + 0x3000, gOff + 0x4000).every((v) => v === 0x11), 'guest save kept');
  ok(img.subarray(spare, spare + 0x1000).every((v) => v === 0x22) && img.subarray(spare + 0x4000, spare + 0x5000).every((v) => v === 0x23), 'spare pages kept');
  ok(img.subarray(0xC0000, 0xC2000).every((v) => v === 0x33), 'session kept');
  ok(img.subarray(0xA0000, 0xA0140).every((v, i) => v === app[i]), 'new program written');
  ok(!r.notes.length, 'no notes: ' + JSON.stringify(r.notes));
  // firmware chosen again in the Studio: starts fresh (as before)
  return FL.keepPlan(F, E, null, { emuChanged: true })({ info: { edition: ed && ed.id, name: 'x' }, read: async (a, n) => dev.slice(a, a + n) }, up, new Set(changed));
}).then((r) => {
  ok(r.notes.length === 1, 'new firmware: note, fresh start');
  console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
});
