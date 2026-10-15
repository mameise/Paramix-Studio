// Save games out of and into the Paradise: .tamasave (TamaParadise app) and .sav (TamaEmu app).
// node test/saves_test.js <paradise_water.bin> <paradise_glacier.bin> <idl_firmware.bin> [out_dir]
const fs = require('fs');
const C = require('../src/core.js'); globalThis.ParamixCore = C;
const S = require('../src/saves.js'), E = require('../src/emu.js');
const rd = (p) => new Uint8Array(fs.readFileSync(p));
let bad = 0; const ok = (c, m) => { console.log((c ? 'ok   ' : 'WRONG ') + m); if (!c) bad++; };
const [waterF, glacF, idlF, out] = process.argv.slice(2);
const water = rd(waterF), glac = rd(glacF);
(async () => {
  // Paradise
  const pages = S.paraFromImage(water, 2);
  const file = S.paraEncode(pages, 2, { counter: 123456, ctrl: 0xE0 }, 1700000000);
  const sv = S.paraDecode(file);
  ok(sv.pages.size === pages.size && [...pages].every(([a, d]) => Buffer.compare(Buffer.from(d), Buffer.from(sv.pages.get(a))) === 0), 'Paradise: pages back (' + pages.size + ')');
  ok(sv.counter === 123456 && sv.stamp === 1700000000 && sv.edition === 2 && sv.alarm === 123455 && sv.status === 0x100, 'Paradise: clock and edition');
  const c1 = S.paraCheck(sv, 2);
  ok(!c1.errors.length && c1.complete, 'Paradise: fits Water ' + JSON.stringify(c1.errors));
  ok(S.paraCheck(sv, 1).errors.some((e) => e.code === 'saveOtherEd'), 'Paradise: refused on Land');
  ok(S.paraCheck(Object.assign({}, sv, { edition: 0 }), 6).errors.some((e) => e.code === 'saveFamily'), 'Paradise: without trailer, Glacier device refuses a Water save');
  const g = S.paraDecode(S.paraEncode(S.paraFromImage(glac, 6), 6, null));
  ok(!S.paraCheck(Object.assign({}, g, { edition: 0 }), 6).errors.length && S.paraCheck(Object.assign({}, g, { edition: 0 }), 2).errors.some((e) => e.code === 'saveFamily'), 'Paradise: Glacier save fits Glacier, not Water');
  ok(S.paraClock(sv, 1700000100) === 123556, 'Paradise: clock moves on with the time since');
  const part = Object.assign({}, sv, { pages: new Map([...sv.pages].filter(([a]) => a >= 0xEF0000)) });
  const c2 = S.paraCheck(part, 2); ok(!c2.errors.length && !c2.complete && c2.warnings.some((w) => w.code === 'saveIncomplete'), 'Paradise: partial save gives a warning');
  if (out) fs.writeFileSync(out + '/water.tamasave', file);
  // TamaEmu
  const guest = rd(idlF), app = rd(__dirname + '/../emu/emu.bin'), stub = rd(__dirname + '/../emu/stub_tpl.bin');
  const rom = E.emuPatch(rd(process.argv[2]), guest, app, stub).rom;
  const cfg = S.emuCfg(rom.subarray(0xA0000, 0xA1000));
  ok(cfg && cfg.dev === 'idl' && cfg.size === 0x800000, 'TamaEmu: settings read');
  let g1 = await S.emuGuestAsync((a, n) => rom.subarray(a, a + n), cfg);
  const fwPad = new Uint8Array(0x800000).fill(0xFF); fwPad.set(guest.subarray(0, 0x800000));
  ok(Buffer.compare(Buffer.from(g1), Buffer.from(fwPad)) === 0, 'TamaEmu: unchanged ROM gives the firmware back');
  // the device played: a packed block changed, a spare page for an empty block
  const dev = new Uint8Array(rom);
  const b0 = [...cfg.map].findIndex((k) => k !== 0xFF && k !== 0xFE); const k0 = cfg.map[b0];
  dev.fill(0x42, cfg.guestOff + (k0 << 16) + 0x1000, cfg.guestOff + (k0 << 16) + 0x2000);
  const empty = [...cfg.map.subarray(0, 128)].findIndex((k, i) => k === 0xFF && i > 4);
  const lo = (cfg.guestOff + cfg.guestSize + 0x1FFF) & ~0x1FFF, data = lo + 0x2000, n = Math.min(64, ((cfg.spareEnd & ~0x1FFF) - data) >> 13);
  const t = new Uint8Array(16 + 128).fill(0xFF); const vp = (empty << 16) >> 13;
  const w32 = (b, o, v) => { b[o] = v & 255; b[o + 1] = (v >>> 8) & 255; b[o + 2] = (v >>> 16) & 255; b[o + 3] = (v >>> 24) & 255; };
  t[16] = vp & 255; t[17] = vp >> 8;
  let x = (0x50584D50 ^ Math.imul(n, 0x9E3779B1)) >>> 0; for (let i = 0; i < 64; i++) x = (((x << 5) | (x >>> 27)) ^ (t[16 + 2 * i] | (t[17 + 2 * i] << 8))) >>> 0;
  w32(t, 0, 0x50584D50); w32(t, 4, n); w32(t, 8, x); w32(t, 12, 0);
  dev.fill(0xFF, lo, lo + 0x1000); dev.set(t, lo); dev.fill(0x77, data, data + 0x2000);
  const g2 = await S.emuGuestAsync((a, n2) => dev.subarray(a, a + n2), cfg);
  ok(g2[(b0 << 16) + 0x1800] === 0x42 && g2[(empty << 16) + 0x100] === 0x77 && g2[(empty << 16) + 0x2100] === 0xFF, 'TamaEmu: written block and spare page are in the .sav');
  ok(E.emuDetect(g2) === 'idl', 'TamaEmu: the .sav is recognised as a firmware again');
  const back = E.emuPatch(rd(process.argv[2]), g2, app, stub).rom;
  const g3 = await S.emuGuestAsync((a, n2) => back.subarray(a, a + n2), S.emuCfg(back.subarray(0xA0000, 0xA1000)));
  ok(Buffer.compare(Buffer.from(g3), Buffer.from(g2)) === 0, 'TamaEmu: .sav into the ROM and out again is the same');
  if (out) fs.writeFileSync(out + '/idl.sav', g2);
  console.log(bad ? bad + ' WRONG' : 'all good'); process.exit(bad ? 1 : 0);
})();
