// 1.6.12 test2: the second part of T7Emu (t7x.bin, the JIT) lies in the save area at +0x8000, for the main program
// and for a T7Emu in the menu; an upgrade writes it again. node test/emu_t7x_test.js <land.bin> <tama-go.bin> <friends.bin>
const fs = require('fs');
const E = require('../src/emu.js'), F = require('../src/feld.js');
const [land, go, fr] = process.argv.slice(2);
const L = new Uint8Array(fs.readFileSync(land)), G = new Uint8Array(fs.readFileSync(go)), R = new Uint8Array(fs.readFileSync(fr));
const rd = (p) => new Uint8Array(fs.readFileSync(__dirname + '/../' + p));
const app = rd('t7/t7.bin'); app.x = rd('t7/t7x.bin');
const stub = rd('emu/stub_tpl.bin'), fe = { feld: rd('feld/feld.bin'), stub: rd('feld/stub_feld_tpl.bin') };
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) bad++; };
const has = (rom, at, x) => x.every((v, i) => rom[at + i] === v);
const u32 = (a, o) => (a[o] | (a[o + 1] << 8) | (a[o + 2] << 16) | (a[o + 3] << 24)) >>> 0;
ok(u32(app.x, 0) === 0x31583754 && app.x.length <= 0x8000, 't7x.bin: magic, ' + app.x.length + ' bytes');
let rom = F.feldStrip(L, { stub: fe.stub, oldStub: stub }, E);
rom = E.t7Patch(rom, R, [], app, stub, {});
const so = u32(rom, 0xA0150);
ok(has(rom, so + 0x8000, app.x), 'main program: second part at 0x' + (so + 0x8000).toString(16));
// a program without the second part (older tests, PC builds): nothing is written there
const plain = E.t7Patch(F.feldStrip(L, { stub: fe.stub, oldStub: stub }, E), R, [], rd('t7/t7.bin'), stub, {});
ok(plain.subarray(so + 0x8000, so + 0x10000).every((v) => v === 0xFF), 'without it: the place stays empty');
// upgrade: an other second part is written, the save stays
const app2 = new Uint8Array(app); app2.x = new Uint8Array(app.x); app2.x[20] ^= 0xFF;
const up = E.t7Upgrade(rom, app2);
ok(!!up && has(up, so + 0x8000, app2.x), 'upgrade writes the new second part');
ok(E.t7Upgrade(up, app2) === null, 'nothing to do the second time');
// a T7Emu in the menu
const r = E.appsPlace(rom, [{ rom: G }], app, {});
const off = r.list[0].off, so2 = u32(r.rom, off + 0x150);
ok(so2 === off + 0x10000 && has(r.rom, so2 + 0x8000, app.x), 'menu T7Emu: second part at 0x' + (so2 + 0x8000).toString(16));
ok(has(r.rom, so + 0x8000, app.x), 'main program keeps its own');
console.log(bad ? bad + ' FAILED' : 'all good');
process.exit(bad ? 1 : 0);
