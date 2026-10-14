// Autostart switch of the emulator in the stub (emulator alone and with the menu).
// node test/emu_auto_test.js <paradise.bin> <color_tama_firmware.bin> [old_menu_stub_tpl.bin]
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const E = require('../src/emu.js'), F = require('../src/feld.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
const land = rd(process.argv[2]), guest = rd(process.argv[3]);
const app = rd(__dirname + '/../emu/emu.bin'), es = rd(__dirname + '/../emu/stub_tpl.bin');
const emb = { feld: rd(__dirname + '/../feld/feld.bin'), stub: rd(__dirname + '/../feld/stub_feld_tpl.bin'), oldStub: es };
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const list = (rom) => { const l = [[0xB0000, es], [0xB0000, emb.stub]]; const f = F.feldFind(rom, emb.stub); if (f && f.stubOff !== 0xB0000) l.push([f.stubOff, emb.stub]); return l; };
const diff = (a, b) => { let n = 0; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) n++; return n; };
// emulator alone
const r = E.emuPatch(land, guest, app, es).rom;
ok(E.autoGet(r, list(r)) === false, 'emulator: autostart off after install');
const r1 = E.autoSet(r, list(r), true);
ok(r1 && E.autoGet(r1, list(r1)) === true && diff(r, r1) === 1, 'emulator: switched on, one byte differs');
ok(E.autoGet(E.autoSet(r1, list(r1), false), list(r1)) === false, 'emulator: switched off again');
ok(E.installedOrigs(r1, E.sceneTable(r1)).every((v, k) => v === E.installedOrigs(r, E.sceneTable(r))[k]), 'original handlers unchanged');
// with the menu
const m = F.feldApply(r, { fields: true, bag: false }, [], emb, E);
ok(E.autoGet(m, list(m)) === false, 'menu: autostart off');
const m1 = E.autoSet(m, list(m), true);
ok(m1 && E.autoGet(m1, list(m1)) === true && F.feldFind(m1, emb.stub), 'menu: switched on, menu still found');
const s = F.feldStrip(m1, emb, E);
ok(!F.feldFind(s, emb.stub) && F.appKind(s) === 2 && E.autoGet(s, list(s)) === false, 'menu removed: emulator stub back, autostart off (the Studio sets it again)');
// menu built by an older Studio (menu address at 0xF0 in the stub)
if (process.argv[4]) {
  const old = { feld: emb.feld, stub: rd(process.argv[4]), oldStub: es };
  const o = F.feldApply(r, { fields: true, bag: false }, [], old, E);
  const f = F.feldFind(o, emb.stub);
  ok(f && f.off === F.feldFind(o, old.stub).off, 'older menu still recognised with the new template');
  ok(E.autoGet(o, list(o)) === null, 'older stub: no switch (null)');
}
console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
